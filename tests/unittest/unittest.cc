/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-21 19:47:48
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */

#include "gtest/gtest.h"
#include <limits.h>

extern "C" {
#include "bbq.h"
extern int bbq_malloc_free_equal();
extern void bbq_memory_info();
}

#include "unittest.h"

namespace {
extern void *utest_producer_thread(void *arg);
extern void *utest_consumer_thread(void *arg);
extern pthread_t *utest_threads_create(BlockBoundedQueue_s *q, size_t pthread_cnt,
                                       uint32_t pthread_usleep, size_t cthread_cnt,
                                       uint32_t cthread_usleep);
extern void utest_thread_exit_data_destory(thread_data_s *data);
extern void *utest_consumer_thread(void *arg);
extern void utest_thread_create(pthread_t *threads, void *(*__start_routine)(void *),
                                BlockBoundedQueue_s *q, uint32_t thread_usleep,
                                bool until_end);
extern void utest_threads_destory(pthread_t *threads);

size_t bn = 100;
size_t bs = 4096;
size_t pthread_cnt = 10;
size_t cthread_cnt = 10;

// 空队满队的边界测试
TEST(boundary, full_empty) {
    int ret = 0;
    BlockBoundedQueue_s *q;
    // 1.初始队列为空，入队失败
    q = BlockBoundedQueue_create(bn, bs, RETRY_NEW);
    EXPECT_TRUE(q != NULL) << "FAILED-1";

    int cnt = bn * bs;
    testdata_s *data = (testdata_s *)malloc(sizeof(testdata_s) * cnt);

    // 2.入满队列，成功
    QueueStatus_e en_status;
    for (int i = 0; i < cnt; i++) {
        data[i].v1 = i;
        data[i].v2 = i;
        en_status = bbq_enqueue(q, &data[i]);
        EXPECT_TRUE(en_status == OK) << "FAILED-2 when enqueue,  i:" << i << " en_status:%d " << en_status;
    }

    // 3.再入队（满），失败
    en_status = bbq_enqueue(q, &data[0]);
    EXPECT_TRUE(en_status == FULL) << "FAILED-3";

    QueueStatus_s de_status;
    // 4.出满队列，成功
    for (int i = 0; i < cnt; i++) {
        de_status = bbq_dequeue(q);
        EXPECT_TRUE(de_status.status == OK) << "FAILED-4 when dequeue,  i:" << i << " en_status:%d " << en_status;
        EXPECT_EQ(i, ((testdata_s *)de_status.data)->v1) << "FAILED 5";
        EXPECT_EQ(i, ((testdata_s *)de_status.data)->v2) << "FAILED 6";
    }

    // 5.再出队（空），失败
    de_status = bbq_dequeue(q);
    EXPECT_TRUE(de_status.status == EMPTY) << "FAILED 7";

    ret = BlockBoundedQueue_destory(q);
    EXPECT_TRUE(ret == BBQ_OK) << "FAILED 8";

    free(data);
#ifdef BBQ_DEBUG
    bbq_memory_info();
    EXPECT_EQ(true, bbq_malloc_free_equal()) << "FAILED 9";
#endif
}

// 多生产者多消费者参加，生产者更快
TEST(multi_pc, pquick) {
    int ret;
    pthread_t *threads = NULL; // 存储所有线程ID的数组

    BlockBoundedQueue_s *q = BlockBoundedQueue_create(bn, bs, RETRY_NEW);
    EXPECT_TRUE(q != NULL) << "FAILED-1";

    // 创建生产者消费者线程
    threads = utest_threads_create(q, pthread_cnt, 0, cthread_cnt, 10);
    size_t thread_cnt = pthread_cnt + cthread_cnt;
    thread_data_s **exit_data = (thread_data_s **)malloc(sizeof(thread_data_s **) * (thread_cnt + 1));

    // 等待所有线程完成
    size_t i = 0;
    for (i = 0; i < thread_cnt; i++) {
        pthread_join(threads[i], (void **)(&exit_data[i])); // 等待每个线程结束
        EXPECT_TRUE(exit_data[i] != NULL) << "FAILED-2";
        BBQ_INFO_LOG("thread_id:%d\n", exit_data[i]->thread_id);
    }

    // 创建消费者线程把队列中剩余的数据读完
    pthread_t last_thread;
    utest_thread_create(&last_thread, utest_consumer_thread, q, 0, true);
    pthread_join(last_thread, (void **)(&exit_data[i])); // 等待线程结束

    for (i = 0; i < (thread_cnt + 1); i++) {
        utest_thread_exit_data_destory(exit_data[i]);
    }

    utest_threads_destory(threads);
}

/*-----------------------------------------------------------------------------*/

size_t times = 1000; // 生产/消费次数

thread_data_s *utest_thread_exit_data_init(thread_arg_s *arg, thread_type type) {
    int i;
    thread_data_s *exit_data = (thread_data_s *)malloc(sizeof(thread_data_s));
    if (exit_data == NULL) {
        BBQ_ERR_LOG("malloc failed");
        return NULL;
    }
    memset(exit_data, 0, sizeof(*exit_data));

    exit_data->thread_id = pthread_self();
    exit_data->type = THREAD_CONSUMER;
    if (arg->until_end) {
        exit_data->exec_times = 0;
    } else {
        exit_data->exec_times = times;
    }
    exit_data->arg = arg;
    exit_data->data = (testdata_s *)malloc(sizeof(testdata_s) * times);
    if (exit_data->data == NULL) {
        BBQ_ERR_LOG("malloc failed");
        exit(-1);
    }
    memset(exit_data->data, 0, sizeof(*exit_data->data));

    return exit_data;
}

void utest_thread_exit_data_destory(thread_data_s *data) {
    free(data->data);
    free(data->arg);
    free(data);
}

void utest_thread_exit_data_counter(thread_data_s *exit_data, QueueStatus_e status) {
    switch (status) {
    case OK:
        exit_data->succ_times++;
        break;
    case BUSY:
        exit_data->busy_times++;
        break;
    case EMPTY:
        exit_data->empty_times++;
        break;
    case FULL:
        exit_data->full_times++;
        break;
    default:
        BBQ_ERR_LOG("unknown status:%d", status);
        exit(-1);
    }
}

void *utest_producer_thread(void *arg) {
    if (arg == NULL) {
        BBQ_ERR_LOG("arg is NULL");
        exit(-1);
    }

    thread_arg_s *tmp_arg = (thread_arg_s *)arg;
    thread_data_s *exit_data = utest_thread_exit_data_init(tmp_arg, THREAD_PRODUCER);
    if (arg == NULL) {
        BBQ_ERR_LOG("thread_data init failed");
        exit(-1);
    }

    size_t i = 0;
    QueueStatus_e en_status = OK;

    while (true) {
        // exit_data->exec_times限制循环次数
        if (exit_data->exec_times > 0 && i >= exit_data->exec_times) {
            break;
        }

        // 如果==0，则直到队列满了才退出
        if (exit_data->exec_times == 0 && en_status == FULL) {
            break;
        }

        exit_data->data[exit_data->succ_times].v1 = exit_data->thread_id;
        exit_data->data[exit_data->succ_times].v2 = i;
        en_status = bbq_enqueue(tmp_arg->q, &exit_data->data);
        utest_thread_exit_data_counter(exit_data, en_status);

        if (tmp_arg->usleep > 0) {
            usleep(tmp_arg->usleep);
        }
        i++;
    }
    pthread_exit(exit_data);
}

void *utest_consumer_thread(void *arg) {
    if (arg == NULL) {
        BBQ_ERR_LOG("arg is NULL");
        exit(-1);
    }

    thread_arg_s *tmp_arg = (thread_arg_s *)arg;
    thread_data_s *exit_data = utest_thread_exit_data_init(tmp_arg, THREAD_CONSUMER);
    if (arg == NULL) {
        BBQ_ERR_LOG("thread_data init failed");
        exit(-1);
    }

    size_t i = 0;
    QueueStatus_s de_status = {.status = OK};
    while (true) {
        // exit_data->exec_times限制循环次数
        if (exit_data->exec_times > 0 && i >= exit_data->exec_times) {
            break;
        }

        // 如果==0，则直到队列空了才退出
        if (exit_data->exec_times == 0 && de_status.status == EMPTY) {
            break;
        }

        de_status = bbq_dequeue(tmp_arg->q);
        if (de_status.status == OK) {
            exit_data->data[exit_data->succ_times] = (*(testdata_s *)de_status.data);
        }
        utest_thread_exit_data_counter(exit_data, de_status.status);

        if (tmp_arg->usleep > 0) {
            usleep(tmp_arg->usleep);
        }
        i++;
    }
    pthread_exit(exit_data);
}

void utest_thread_create(pthread_t *threads, void *(*__start_routine)(void *),
                         BlockBoundedQueue_s *q, uint32_t thread_usleep,
                         bool until_end) {
    thread_arg_s *arg = (thread_arg_s *)malloc(sizeof(thread_arg_s)); // 线程回收时free
    arg->q = q;
    arg->usleep = thread_usleep;
    arg->until_end = until_end;
    pthread_create(threads, NULL, utest_consumer_thread, arg);
}

pthread_t *utest_threads_create(BlockBoundedQueue_s *q, size_t pthread_cnt,
                                uint32_t pthread_usleep, size_t cthread_cnt,
                                uint32_t cthread_usleep) {
    int ret;
    size_t thread_id = 0;
    size_t thread_cnt = pthread_cnt + cthread_cnt;
    pthread_t *threads = (pthread_t *)malloc(sizeof(pthread_t) * thread_cnt); // 存储所有线程ID的数组

    // 创建线程
    if (cthread_cnt > 0) {
        // 消费者线程（先启动，读取空队列的场景）
        for (size_t i = 0; i < cthread_cnt; i++, thread_id++) {
            utest_thread_create(&threads[thread_id], utest_consumer_thread, q, cthread_usleep, false);
        }
    }

    // 创建生产者线程
    if (pthread_cnt > 0) {
        for (size_t i = 0; i < pthread_cnt; i++, thread_id++) {
            utest_thread_create(&threads[thread_id], utest_producer_thread, q, pthread_usleep, false);
        }
    }

    return threads;
}

void utest_threads_destory(pthread_t *threads) {
    free(threads);
}

} // namespace