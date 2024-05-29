/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-29 18:07:58
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

#include "ut.h"

namespace {
extern void *utest_producer_thread(void *arg);
extern void *utest_consumer_thread(void *arg);
extern pthread_t *utest_threads_create(bbq_queue_s *q, size_t exec_times, size_t pthread_cnt,
                                       uint32_t pthread_usleep, size_t cthread_cnt,
                                       uint32_t cthread_usleep);
extern void utest_thread_exit_data_destory(thread_data_s *data);
extern void *utest_consumer_thread(void *arg);
extern void utest_thread_create(pthread_t *threads, void *(*__start_routine)(void *),
                                bbq_queue_s *q, uint32_t thread_usleep,
                                bool until_end, uint64_t thread_exec_times);
extern void utest_threads_destory(pthread_t *threads);
extern bool utest_thread_exit_merge_data_compare(thread_data_s *merge_pdata, thread_data_s *deque_datas);
extern void utest_thread_exit_merge_counter(thread_data_s *data_arr, thread_data_s *data);
size_t bn = 4;
size_t bs = 1365;
size_t pthread_cnt = 1; // 生产者线程个数
size_t cthread_cnt = 1; // 消费者线程个数

// 空队满队的边界测试
TEST(boundary, full_empty) {
    int ret = 0;
    bbq_queue_s *q;
    // 1.初始队列
    q = bbq_ring_create_bnbs_with_socket("bbq_ring", bn, bs, 0, BBQ_RETRY_NEW);
    EXPECT_TRUE(q != NULL) << "FAILED-1";

    int cnt = bn * bs;
    testdata_s *data = (testdata_s *)malloc(sizeof(testdata_s) * cnt);

    // 2.入满队列，成功
    int en_status;
    for (int i = 0; i < cnt; i++) {
        data[i].v1 = i;
        data[i].v2 = i;
        en_status = bbq_enqueue(q, &data[i]);
        EXPECT_TRUE(en_status == BBQ_OK) << "FAILED-2 when enqueue,  i:" << i << " en_status:%d " << en_status;
    }

    // 3.再入队（满），失败
    en_status = bbq_enqueue(q, &data[0]);
    EXPECT_TRUE(en_status == BBQ_QUEUE_FULL) << "FAILED-3";

    int de_status;
    // 4.出满队列，成功
    for (int i = 0; i < cnt; i++) {
        bbq_data de_data = NULL;
        de_status = bbq_dequeue(q, &de_data);
        EXPECT_TRUE(de_status == BBQ_OK) << "FAILED-4 when dequeue,  i:" << i << " en_status:%d " << en_status;
        EXPECT_EQ(i, ((testdata_s *)de_data)->v1) << "FAILED 5";
        EXPECT_EQ(i, ((testdata_s *)de_data)->v2) << "FAILED 6";
    }

    // 5.再出队（空），失败
    bbq_data de_data = NULL;
    de_status = bbq_dequeue(q, &de_data);
    EXPECT_TRUE(de_status == BBQ_QUEUE_EMPTY) << "FAILED 7";

    bbq_ring_free(q);

    free(data);
#ifdef BBQ_DEBUG
    bbq_memory_info();
    EXPECT_EQ(true, bbq_malloc_free_equal()) << "FAILED 9";
#endif
}

// 多生产者多消费者参加，生产者更快
TEST(multi_pc, pquick) {
    int ret;
    uint64_t thread_exec_times = bs * 10;
    pthread_t *threads = NULL; // 存储所有线程ID的数组

    bbq_queue_s *q = bbq_ring_create_bnbs_with_socket("bbq_ring", bn, bs, 0, BBQ_RETRY_NEW);
    EXPECT_TRUE(q != NULL) << "FAILED-1";

    // 创建生产者消费者线程
    threads = utest_threads_create(q, bs * 10, pthread_cnt, 0, cthread_cnt, 10);
    size_t thread_cnt = pthread_cnt + cthread_cnt;
    thread_data_s **exit_data = (thread_data_s **)malloc(sizeof(thread_data_s **) * (thread_cnt + 1));

    // 等待所有线程完成
    size_t i = 0;
    for (i = 0; i < thread_cnt; i++) {
        pthread_join(threads[i], (void **)(&exit_data[i])); // 等待每个线程结束
        EXPECT_TRUE(exit_data[i] != NULL) << "FAILED-2";
    }

    // 创建消费者线程把队列中剩余的数据读完
    pthread_t last_thread;
    utest_thread_create(&last_thread, utest_consumer_thread, q, 0, true, thread_exec_times);
    pthread_join(last_thread, (void **)(&exit_data[i])); // 等待线程结束

    // 聚合所有数据
    thread_data_s merge_data[TEST_THREAD_TYPE_MAX];
    memset(merge_data, 0, sizeof(merge_data));
    merge_data[TEST_THREAD_PRODUCER].type = TEST_THREAD_PRODUCER;
    merge_data[TEST_THREAD_CONSUMER].type = TEST_THREAD_CONSUMER;

    for (i = 0; i < (thread_cnt + 1); i++) {
        utest_thread_exit_merge_counter(merge_data, exit_data[i]);
    }

    thread_data_s *merge_pdata = &merge_data[TEST_THREAD_PRODUCER];
    thread_data_s *merge_cdata = &merge_data[TEST_THREAD_CONSUMER];
    if (merge_pdata->succ_times) {
        merge_pdata->data = (testdata_s *)malloc(merge_pdata->succ_times * sizeof(testdata_s));
    }
    if (merge_cdata->succ_times) {
        merge_cdata->data = (testdata_s *)malloc(merge_cdata->succ_times * sizeof(testdata_s));
    }

    uint64_t merge_pdata_idx = 0;
    uint64_t merge_cdata_idx = 0;
    for (i = 0; i < (thread_cnt + 1); i++) {
        if (exit_data[i]->type == TEST_THREAD_PRODUCER && merge_pdata->data != NULL) {
            memcpy(&merge_pdata->data[merge_pdata_idx],
                   exit_data[i]->data,
                   sizeof(merge_pdata->data[merge_pdata_idx]));
            merge_pdata_idx++;
        } else if (exit_data[i]->type == TEST_THREAD_CONSUMER && merge_cdata->data != NULL) {
            memcpy(&merge_cdata->data[merge_cdata_idx],
                   exit_data[i]->data,
                   sizeof(merge_cdata->data[merge_cdata_idx]));
            merge_cdata_idx++;
        }
        utest_thread_exit_data_destory(exit_data[i]);
    }

    EXPECT_EQ(merge_pdata->succ_times, merge_cdata->succ_times); // 成功入队==成功出队个数相同

    // 生产者或消费者的 总执行次数统计符合预期
    EXPECT_NE(merge_pdata->exec_times, 0);
    EXPECT_NE(merge_pdata->succ_times, 0);
    EXPECT_EQ(merge_pdata->exec_times, pthread_cnt * thread_exec_times);
    EXPECT_EQ(merge_pdata->exec_times, merge_pdata->succ_times + merge_pdata->full_times + merge_pdata->busy_times);

    EXPECT_NE(merge_cdata->exec_times, 0);
    EXPECT_NE(merge_cdata->succ_times, 0);
    EXPECT_EQ(merge_cdata->exec_times, merge_cdata->succ_times + merge_cdata->empty_times + merge_cdata->busy_times);

    EXPECT_EQ(merge_pdata->empty_times, 0); // 生产者不会遇到空队列返回
    EXPECT_EQ(merge_cdata->full_times, 0);  // 消费者不会遇到空队列返回

    // EXPECT_TRUE(utest_thread_exit_merge_data_compare(merge_pdata, merge_cdata)); // 比较生产者消费者的数据是否一致

    utest_threads_destory(threads);
}

/*-----------------------------------------------------------------------------*/

thread_data_s *utest_thread_exit_data_init(thread_arg_s *arg, test_thread_type_e type) {
    int i;
    thread_data_s *exit_data = (thread_data_s *)malloc(sizeof(thread_data_s));
    if (exit_data == NULL) {
        BBQ_ERR_LOG("malloc failed");
        return NULL;
    }
    memset(exit_data, 0, sizeof(*exit_data));

    exit_data->thread_id = pthread_self();
    exit_data->type = type;
    exit_data->exec_times = 0;

    exit_data->arg = arg;
    size_t alloc_size = sizeof(testdata_s) * arg->thread_exec_times;
    exit_data->data = (testdata_s *)malloc(alloc_size);
    if (exit_data->data == NULL) {
        BBQ_ERR_LOG("malloc failed");
        exit(-1);
    }
    memset(exit_data->data, 0, alloc_size);

    return exit_data;
}

void utest_thread_exit_data_destory(thread_data_s *data) {
    free(data->data);
    free(data->arg);
    free(data);
}

// 比较函数，用于qsort
int utest_compare_data(const void *a, const void *b) {
    testdata_s *itemA = (testdata_s *)a;
    testdata_s *itemB = (testdata_s *)b;
    return itemA->v1 < itemB->v2;
}

bool utest_thread_exit_merge_data_compare(thread_data_s *merge_pdata, thread_data_s *merge_cdata) {
    if (merge_pdata->succ_times != merge_cdata->succ_times) {
        return false;
    }

    // 使用qsort进行排序
    qsort(merge_pdata, merge_pdata->succ_times, sizeof(testdata_s), utest_compare_data);
    qsort(merge_cdata, merge_cdata->succ_times, sizeof(testdata_s), utest_compare_data);

    for (uint64_t i = 0; i < merge_pdata->succ_times; i++) {
        if (merge_pdata->data[i].v1 != merge_cdata->data[i].v1) {
            return false;
        }

        if (merge_pdata->data[i].v2 != merge_cdata->data[i].v2) {
            return false;
        }
    }

    return true;
}

void utest_thread_exit_merge_counter(thread_data_s *data_arr, thread_data_s *data) {
    if (data_arr == NULL || data == NULL) {
        BBQ_ERR_LOG("NULL point");
        exit(-1);
    }

    data_arr[data->type].exec_times += data->exec_times;
    data_arr[data->type].succ_times += data->succ_times;
    data_arr[data->type].full_times += data->full_times;
    data_arr[data->type].busy_times += data->busy_times;
    data_arr[data->type].empty_times += data->empty_times;
}

void utest_thread_exit_data_counter(thread_data_s *exit_data, int status) {
    exit_data->exec_times++;
    switch (status) {
    case BBQ_OK:
        exit_data->succ_times++;
        break;
    case BBQ_QUEUE_BUSY:
        exit_data->busy_times++;
        break;
    case BBQ_QUEUE_EMPTY:
        exit_data->empty_times++;
        break;
    case BBQ_QUEUE_FULL:
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
    thread_data_s *exit_data = utest_thread_exit_data_init(tmp_arg, TEST_THREAD_PRODUCER);
    if (arg == NULL) {
        BBQ_ERR_LOG("thread_data init failed");
        exit(-1);
    }

    size_t i = 0;
    int en_status = BBQ_OK;

    for (size_t i = 0; i < tmp_arg->thread_exec_times; i++) {
        testdata_s *enqueue_data = &exit_data->data[exit_data->succ_times];
        enqueue_data->v1 = i + 1;
        enqueue_data->v2 = i + 1;
        en_status = bbq_enqueue(tmp_arg->q, enqueue_data);
        utest_thread_exit_data_counter(exit_data, en_status);

        if (tmp_arg->usleep > 0) {
            usleep(tmp_arg->usleep);
        }
    }
    pthread_exit(exit_data);
}

void *utest_consumer_thread(void *arg) {
    if (arg == NULL) {
        BBQ_ERR_LOG("arg is NULL");
        exit(-1);
    }

    thread_arg_s *tmp_arg = (thread_arg_s *)arg;
    thread_data_s *exit_data = utest_thread_exit_data_init(tmp_arg, TEST_THREAD_CONSUMER);
    if (arg == NULL) {
        BBQ_ERR_LOG("thread_data init failed");
        exit(-1);
    }

    size_t i = 0;
    int status = BBQ_OK;
    while (true) {
        // 如果==0，则直到队列空了才退出
        if (tmp_arg->until_end == true && status == BBQ_QUEUE_EMPTY) {
            break;
        }

        if (tmp_arg->until_end == false && i >= tmp_arg->thread_exec_times) {
            break;
        }

        bbq_data deq_data = NULL;
        status = bbq_dequeue(tmp_arg->q, &deq_data);
        if (status == BBQ_OK) {
            exit_data->data[exit_data->succ_times] = (*(testdata_s *)deq_data);
        }
        utest_thread_exit_data_counter(exit_data, status);

        if (tmp_arg->usleep > 0) {
            usleep(tmp_arg->usleep);
        }
        i++;
    }
    pthread_exit(exit_data);
}

void utest_thread_create(pthread_t *threads, void *(*thread_func)(void *),
                         bbq_queue_s *q, uint32_t thread_usleep,
                         bool until_end, uint64_t thread_exec_times) {
    thread_arg_s *arg = (thread_arg_s *)malloc(sizeof(thread_arg_s)); // 线程回收时free
    arg->q = q;
    arg->usleep = thread_usleep;
    arg->until_end = until_end;
    arg->thread_exec_times = thread_exec_times;
    pthread_create(threads, NULL, thread_func, arg);
}

pthread_t *utest_threads_create(bbq_queue_s *q, uint64_t thread_exec_times,
                                size_t pthread_cnt, uint32_t pthread_usleep,
                                size_t cthread_cnt, uint32_t cthread_usleep) {
    int ret;
    size_t thread_id = 0;
    size_t thread_cnt = pthread_cnt + cthread_cnt;
    pthread_t *threads = (pthread_t *)malloc(sizeof(pthread_t) * thread_cnt); // 存储所有线程ID的数组

    // 创建线程
    if (cthread_cnt > 0) {
        // 消费者线程（先启动，读取空队列的场景）
        for (size_t i = 0; i < cthread_cnt; i++, thread_id++) {
            utest_thread_create(&threads[thread_id], utest_consumer_thread, q, cthread_usleep, false, thread_exec_times);
        }
    }

    // 创建生产者线程
    if (pthread_cnt > 0) {
        for (size_t i = 0; i < pthread_cnt; i++, thread_id++) {
            utest_thread_create(&threads[thread_id], utest_producer_thread, q, pthread_usleep, false, thread_exec_times);
        }
    }

    return threads;
}

void utest_threads_destory(pthread_t *threads) {
    free(threads);
}

} // namespace