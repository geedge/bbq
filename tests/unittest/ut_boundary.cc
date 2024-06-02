/*
 * @Author: liuyu
 * @LastEditTime: 2024-06-02 09:19:01
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */

#include "gtest/gtest.h"
#include <limits.h>

extern "C" {
#include "test_queue.h"
#include "ut.h"
extern bool bbq_malloc_free_equal();
extern bool test_malloc_free_equal();
extern void bbq_memory_info();
}

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

TEST(boundary, full_empty) {
    test_memory_counter_clear();
    int ret = 0;
    uint32_t entries_cnt = 4096;
    uint32_t loop = 1000;
    bbq_queue_s *q;

    int *data = (int *)test_malloc(TEST_MODULE_UTEST, sizeof(*data) * entries_cnt);
    void *tmp_data = NULL;
    EXPECT_TRUE(data);

    q = bbq_ring_create("ring", entries_cnt, BBQ_RETRY_NEW);
    EXPECT_TRUE(q);

    for (uint32_t i = 0; i < loop; i++) {
        // 入满队
        for (uint32_t j = 0; j < entries_cnt; j++) {
            data[j] = (i + 1) * j;
            ret = bbq_enqueue(q, &data[j]);
            EXPECT_TRUE(ret == BBQ_OK) << "ret " << ret;
        }

        // 满队再入队
        for (uint32_t j = 0; j < entries_cnt / 3; j++) {
            ret = bbq_enqueue(q, &data[j]);
            EXPECT_TRUE(ret == BBQ_QUEUE_FULL);
        }

        if (i == 0) {
            EXPECT_EQ((q->phead.load() + 1) & q->idx_mask, q->chead.load() & q->idx_mask);
        } else {
            EXPECT_EQ((q->phead.load()) & q->idx_mask, q->chead.load() & q->idx_mask);
        }
        for (uint32_t i = 0; i < q->bn; i++) {
            EXPECT_EQ(q->blocks[i].committed.load() & q->off_mask, q->bs);
            EXPECT_GE(q->blocks[i].allocated.load() & q->off_mask, q->bs);
        }

        // 全出队
        for (uint32_t j = 0; j < entries_cnt; j++) {
            ret = bbq_dequeue(q, &tmp_data);
            EXPECT_TRUE(ret == BBQ_OK);
            EXPECT_EQ(*((int *)tmp_data), data[j]);
        }

        // 空出队再出队
        for (uint32_t j = 0; j < entries_cnt / 2; j++) {
            ret = bbq_dequeue(q, &tmp_data);
            EXPECT_TRUE(ret == BBQ_QUEUE_EMPTY);
        }

        EXPECT_EQ(q->phead.load() & q->idx_mask, q->chead.load() & q->idx_mask);
        for (uint32_t i = 0; i < q->bn; i++) {
            EXPECT_EQ(q->blocks[i].committed.load() & q->off_mask, q->bs);
            EXPECT_GE(q->blocks[i].allocated.load() & q->off_mask, q->bs);
            EXPECT_EQ(q->blocks[i].consumed.load() & q->off_mask, q->bs);
            EXPECT_GE(q->blocks[i].reserved.load() & q->off_mask, q->bs);
        }
    }

    test_free(TEST_MODULE_UTEST, data);
    bbq_ring_free(q);
    EXPECT_TRUE(bbq_malloc_free_equal());
    EXPECT_TRUE(test_malloc_free_equal());
}

TEST(boundary, mpsc_faa) {
    test_memory_counter_clear();

    test_cfg cfg = {
        .base = {
            .name = "mpsc_faa",
            .introduce = "mpsc_faa",
            .cores_cnt = 4,
        },
        .ring = {
            .ring_type = TEST_RING_TYPE_BBQ,
            .producer_cnt = 10,
            .consumer_cnt = 1,
            .workload = TEST_WORKLOAD_SIMPLE,
            .entries_cnt = 1,
            .block_count = 1,
        },
        .run = {
            .enq_per_producer = 1000000,
            .deq_per_consumer = 1000000,
            .running = true,
        },
    }; // namespace

    // 队列初始化
    int ret = -1;
    test_queue_s q;
    ret = test_queue_init_bbq(&cfg, &q);
    ASSERT_TRUE(ret == 0);

    // 创建线程
    pthread_t *threads = test_threads_create(&cfg, &q);
    ASSERT_TRUE(threads);

    // 等待所有线程完成，回收数据
    uint32_t thread_cnt = cfg.ring.producer_cnt + cfg.ring.consumer_cnt;
    test_exit_data **exit_data = (test_exit_data **)test_malloc(TEST_MODULE_UTEST, sizeof(test_exit_data **) * (thread_cnt));
    uint32_t i = 0;

    for (i = 0; i < thread_cnt; i++) {
        pthread_join(threads[i], (void **)(&exit_data[i])); // 等待每个线程结束
    }

    // 比较数据
    test_merge_s merge = {0};
    test_merge_all_data(exit_data, thread_cnt, &merge);
    EXPECT_EQ(merge.consumer.data_error_cnt, 0);
    EXPECT_EQ(merge.consumer.ok_cnt, merge.producer.ok_cnt);
    EXPECT_EQ(merge.consumer.run_times, cfg.run.deq_per_consumer * cfg.ring.consumer_cnt);
    EXPECT_EQ(merge.producer.run_times, cfg.run.enq_per_producer * cfg.ring.producer_cnt);

    // 释放数据
    for (i = 0; i < thread_cnt; i++) {
        test_exit_data_destory(exit_data[i]);
    }
    test_free(TEST_MODULE_UTEST, exit_data);
    test_threads_destory(threads);
    test_queue_destory(&q);
    EXPECT_TRUE(bbq_malloc_free_equal());
    EXPECT_TRUE(test_malloc_free_equal());
}
} // namespace