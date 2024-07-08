/*
 * @Author: liuyu
 * @LastEditTime: 2024-07-07 21:59:54
 * @Email: liuyu@geedgenetworks.com
 * @Describe: 简单的测试用例，测试基本功能
 */

#include "gtest/gtest.h"
extern "C" {
#include "ut_bbq_func.h"
#include <math.h>
extern bool bbq_debug_check_array_bounds(struct bbq *q);
extern void bbq_struct_print(struct bbq *q);
extern uint32_t bbq_enqueue_burst_elem_2d_array(struct bbq *q, void *const *obj_table, uint32_t n, uint32_t *wait_consumed);
extern bool bbq_check_power_of_two(int n);
extern unsigned bbq_ceil_log2(uint64_t x);
extern uint64_t bbq_fetch_max(union bbq_atomic64 *atomic, uint64_t upd, bool single);
extern bool ut_malloc_free_equal();
extern int bbq_bnbs_calc(uint32_t entries, uint32_t *bn, uint32_t *bs);
extern void bbq_atomic64_store(union bbq_atomic64 *atomic, uint64_t value, bool single);
extern uint64_t bbq_atomic64_load(union bbq_atomic64 *atomic, bool single);
extern bool bbq_debug_check_array_bounds(struct bbq *q);
extern struct bbq *bbq_create_elem_with_bnbs(const char *name, uint32_t bn, uint32_t bs,
                                             size_t obj_size, int socket_id, uint32_t flags,
                                             bbq_malloc_f malloc_f, bbq_free_f free_f);
extern uint64_t bbq_atomic64_load(union bbq_atomic64 *atomic, bool single);
}

#define BUF_CNT 4096

class ut_bbq : public testing::Test {
  protected:
    virtual void SetUp() override {
        // 1.清空内存malloc/free统计
        ut_memory_counter_clear();

        // 2.入队空间初始化
        UT_DOUBLE_PTR_DATA_INIT(enq_table1, uint16_t, BUF_CNT);
        UT_PTR_ARRAY_DATA_INIT(enq_table2, uint16_t, BUF_CNT);
        UT_ARRAY_DATA_INIT(enq_table3, BUF_CNT);
    }

    virtual void TearDown() override {
        // 1.释放测试数据
        UT_DOUBLE_PTR_DATA_DESTORY(enq_table1, BUF_CNT);
        UT_PTR_ARRAY_DATA_DESTORY(enq_table2, BUF_CNT);

        // 2.内存泄漏检测
        EXPECT_TRUE(ut_malloc_free_equal());
    }

    // 入队数据
    uint16_t **enq_table1;
    uint16_t *enq_table2[BUF_CNT];
    uint16_t enq_table3[BUF_CNT];
};

TEST_F(ut_bbq, single_retry_new_cp_ptr) {
    int ret = 0;
    uint64_t cnt = 0;
    uint16_t *deq_data = NULL;

    // 创建队列
    struct bbq *q = bbq_create("ut_bbq", BUF_CNT, BBQ_SOCKET_ID_ANY,
                               BBQ_F_RETRY_NEW, ut_malloc_def_callback, ut_free_def_callback);
    ASSERT_NE(q, nullptr);

    // 空队出队失败
    EXPECT_EQ(bbq_dequeue(q, (void **)&deq_data), BBQ_ERR_EMPTY);

    // 全部入队成功
    for (uint32_t i = 0; i < 4000; i++) {
        if (bbq_enqueue(q, (void **)&enq_table1[i]) == 0) {
            cnt++;
        }
    }

    // 部分入队成功
    for (uint32_t i = 0; i < 4000; i++) {
        if (bbq_enqueue(q, (void **)&enq_table2[i]) == 0) {
            cnt++;
        }
    }

    // 入队成功个数等于队列总数
    EXPECT_EQ(cnt, BUF_CNT);

    cnt = 0;
    for (uint32_t i = 0; i < BUF_CNT; i++) {
        ret = bbq_dequeue(q, (void **)&deq_data);
        if (ret == 0) {
            EXPECT_EQ(*deq_data, UT_DATA_MAGIC);
            cnt++;
        }
    }
    // 全部出队成功
    EXPECT_EQ(cnt, BUF_CNT);

    // 空队出队失败
    EXPECT_EQ(bbq_dequeue(q, (void **)&deq_data), BBQ_ERR_EMPTY);
    bbq_destory(q);
}

TEST_F(ut_bbq, single_retry_new_cp_value) {
    int ret = 0;
    uint64_t cnt = 0;
    uint16_t deq_data;

    // 创建队列
    struct bbq *q = bbq_create_elem("ut_bbq", BUF_CNT, sizeof(uint16_t),
                                    BBQ_SOCKET_ID_ANY, BBQ_F_RETRY_NEW,
                                    ut_malloc_def_callback, ut_free_def_callback);
    ASSERT_NE(q, nullptr);

    // 空队出队失败
    EXPECT_EQ(bbq_dequeue(q, (void **)&deq_data), BBQ_ERR_EMPTY);

    // 全部入队成功
    for (uint32_t i = 0; i < 4000; i++) {
        if (bbq_enqueue(q, (void **)enq_table1[i]) == 0) {
            cnt++;
        }
    }

    // 部分入队成功
    for (uint32_t i = 0; i < 4000; i++) {
        if (bbq_enqueue_elem(q, enq_table2[i]) == 0) {
            cnt++;
        }
    }

    // 入队成功个数等于队列总数
    EXPECT_EQ(cnt, BUF_CNT);

    cnt = 0;
    for (uint32_t i = 0; i < BUF_CNT; i++) {
        ret = bbq_dequeue_elem(q, &deq_data);
        if (ret == 0) {
            EXPECT_EQ(deq_data, UT_DATA_MAGIC);
            cnt++;
        }
    }
    // 全部出队成功
    EXPECT_EQ(cnt, BUF_CNT);

    // 空队出队失败
    EXPECT_EQ(bbq_dequeue_elem(q, &deq_data), BBQ_ERR_EMPTY);
    bbq_destory(q);
}

TEST_F(ut_bbq, single_drop_old_cp_pointer) {
    int ret = 0;
    uint64_t cnt = 0;
    uint16_t *deq_data = NULL;
    uint64_t first_cnt = BUF_CNT;
    uint64_t second_cnt = 1000;

    // 创建队列
    struct bbq *q = bbq_create("ut_bbq", BUF_CNT, BBQ_SOCKET_ID_ANY,
                               BBQ_F_DROP_OLD, ut_malloc_def_callback,
                               ut_free_def_callback);
    ASSERT_NE(q, nullptr);
    EXPECT_LT(second_cnt, q->bs * q->bn);

    // 空队出队失败
    EXPECT_EQ(bbq_dequeue(q, (void **)&deq_data), BBQ_ERR_EMPTY);

    // 全部入队成功，入队个数是BUF_CNT的整数倍，因此到了一个边界，刚好与消费者位置一致（套了loop圈）
    uint32_t loop = 3;
    for (uint32_t n = 0; n < loop; n++) {
        for (uint32_t i = 0; i < first_cnt; i++) {
            ret = bbq_enqueue(q, (void **)&enq_table1[i]);
            if (ret == 0) {
                cnt++;
            }
        }
    }
    EXPECT_EQ(cnt, first_cnt * loop);

    // 全部入队成功
    cnt = 0;
    for (uint32_t i = 0; i < second_cnt; i++) {
        if (bbq_enqueue(q, (void **)&enq_table2[i]) == 0) {
            cnt++;
        }
    }
    EXPECT_EQ(cnt, second_cnt);

    cnt = 0;
    for (uint32_t i = 0; i < BUF_CNT; i++) {
        ret = bbq_dequeue(q, (void **)&deq_data);
        if (ret == 0) {
            EXPECT_EQ(*deq_data, UT_DATA_MAGIC);
            cnt++;
        }
    }

    // 一旦生产者追上了消费者，之前未消费的，以及当前块的数据全都作废了。
    EXPECT_EQ(cnt, second_cnt - q->bs);

    // 空队出队失败
    EXPECT_EQ(bbq_dequeue(q, (void **)&deq_data), BBQ_ERR_EMPTY);

    bbq_destory(q);
}

TEST_F(ut_bbq, single_drop_old_cp_value) {
    int ret = 0;
    uint64_t cnt = 0;
    uint16_t deq_data;
    uint64_t first_cnt = BUF_CNT;
    uint64_t second_cnt = 1000;

    // 创建队列
    struct bbq *q = bbq_create_elem("ut_bbq", BUF_CNT, sizeof(uint16_t),
                                    BBQ_SOCKET_ID_ANY, BBQ_F_DROP_OLD,
                                    ut_malloc_def_callback, ut_free_def_callback);
    ASSERT_NE(q, nullptr);
    EXPECT_LT(second_cnt, q->bs * q->bn);

    // 空队出队失败
    EXPECT_EQ(bbq_dequeue_elem(q, &deq_data), BBQ_ERR_EMPTY);

    // 全部入队成功，入队个数是BUF_CNT的整数倍，因此到了一个边界，刚好与消费者位置一致（套了loop圈）
    uint32_t loop = 3;
    for (uint32_t n = 0; n < loop; n++) {
        for (uint32_t i = 0; i < first_cnt; i++) {
            ret = bbq_enqueue_elem(q, enq_table1[i]);
            if (ret == 0) {
                cnt++;
            }
        }
    }
    EXPECT_EQ(cnt, first_cnt * loop);

    // 全部入队成功
    cnt = 0;
    for (uint32_t i = 0; i < second_cnt; i++) {
        if (bbq_enqueue_elem(q, enq_table2[i]) == 0) {
            cnt++;
        }
    }
    EXPECT_EQ(cnt, second_cnt);

    cnt = 0;
    for (uint32_t i = 0; i < BUF_CNT; i++) {
        ret = bbq_dequeue_elem(q, &deq_data);
        if (ret == 0) {
            EXPECT_EQ(deq_data, UT_DATA_MAGIC);
            cnt++;
        }
    }

    // 一旦生产者追上了消费者，之前未消费的，以及当前块的数据全都作废了。
    EXPECT_EQ(cnt, second_cnt - q->bs);
    // 空队出队失败
    EXPECT_EQ(bbq_dequeue_elem(q, &deq_data), BBQ_ERR_EMPTY);

    bbq_destory(q);
}

TEST_F(ut_bbq, burst_retry_new_cp_value) {
    struct bbq *q;
    uint32_t ret1 = 0;
    uint32_t ret2 = 0;
    uint64_t first_cnt = 4000;
    uint64_t second_cnt = 1000;
    uint16_t deq_table1[BUF_CNT] = {0};
    uint16_t *deq_table2 = (uint16_t *)ut_malloc(UT_MODULE_DATA, sizeof(uint16_t) * BUF_CNT);
    uint32_t wait_consumed = 0;

    // 创建队列
    q = bbq_create_elem("ut_bbq", BUF_CNT, sizeof(uint16_t),
                        BBQ_SOCKET_ID_ANY, BBQ_F_RETRY_NEW | BBQ_F_ENABLE_STAT,
                        ut_malloc_def_callback, ut_free_def_callback);
    ASSERT_NE(q, nullptr);
    EXPECT_LT(first_cnt, q->bn * q->bs);

    // 批量入队(全部成功)
    ret1 = bbq_enqueue_burst_elem(q, (void const *)enq_table3, first_cnt, &wait_consumed);
    EXPECT_EQ(ret1, first_cnt);
    EXPECT_EQ(wait_consumed, ret1);

    // 批量入队(部分成功)
    // 由于需要将最终的值入队列，二维数组里的值不连续，需要循环赋值。不推荐这个函数，但可用于特殊场景。
    ret2 = bbq_enqueue_burst_elem_2d_array(q, (void *const *)enq_table2, second_cnt, &wait_consumed);
    EXPECT_EQ(ret2, BUF_CNT - ret1);
    EXPECT_EQ(wait_consumed, ret1 + ret2);

    // 出队列（全部成功）
    ret1 = bbq_dequeue_burst_elem(q, (void *)deq_table1, first_cnt, &wait_consumed);
    EXPECT_EQ(ret1, first_cnt);
    EXPECT_EQ(wait_consumed, ret2);

    // 出队列（部分成功）
    ret2 = bbq_dequeue_burst_elem(q, (void *)deq_table2, second_cnt, &wait_consumed);
    EXPECT_EQ(ret2, BUF_CNT - ret1);
    EXPECT_EQ(wait_consumed, 0);

    // 验证数据
    for (uint32_t i = 0; i < ret1; i++) {
        EXPECT_EQ(deq_table1[i], UT_DATA_MAGIC) << "i :" << i;
    }

    for (uint32_t i = 0; i < ret2; i++) {
        EXPECT_EQ(deq_table2[i], UT_DATA_MAGIC) << "i :" << i;
    }

    EXPECT_TRUE(bbq_debug_check_array_bounds(q));
    ut_free(UT_MODULE_DATA, deq_table2);
    bbq_destory(q);
}

TEST_F(ut_bbq, burst_retry_new_cp_pointer) {
    struct bbq *q;
    uint32_t ret1 = 0;
    uint32_t ret2 = 0;
    uint64_t first_cnt = 4000;
    uint64_t second_cnt = 1000;
    uint32_t wait_consumed = 0;
    uint16_t *deq_table1[BUF_CNT] = {0};
    uint16_t **deq_table2 = (uint16_t **)ut_malloc(UT_MODULE_DATA, sizeof(uint16_t *) * BUF_CNT);

    // 创建队列
    q = bbq_create("ut_bbq", BUF_CNT, BBQ_SOCKET_ID_ANY,
                   BBQ_F_RETRY_NEW | BBQ_F_ENABLE_STAT,
                   ut_malloc_def_callback, ut_free_def_callback);
    ASSERT_NE(q, nullptr);
    EXPECT_LT(first_cnt, q->bn * q->bs);

    // 批量入队(全部成功)
    ret1 = bbq_enqueue_burst(q, (void *const *)enq_table1, first_cnt, &wait_consumed);
    EXPECT_EQ(ret1, first_cnt);
    EXPECT_EQ(wait_consumed, ret1);

    // 批量入队(部分成功)
    ret2 = bbq_enqueue_burst(q, (void *const *)enq_table2, second_cnt, &wait_consumed);
    EXPECT_EQ(ret2, BUF_CNT - ret1);
    EXPECT_EQ(wait_consumed, ret1 + ret2);

    // 出队列（全部成功）
    ret1 = bbq_dequeue_burst(q, (void **)deq_table1, first_cnt, &wait_consumed);
    EXPECT_EQ(ret1, first_cnt);
    EXPECT_EQ(wait_consumed, ret2);

    // 出队列（部分成功）
    ret2 = bbq_dequeue_burst(q, (void **)deq_table2, second_cnt, &wait_consumed);
    EXPECT_EQ(ret2, BUF_CNT - ret1);
    EXPECT_EQ(wait_consumed, 0);

    // 验证数据
    for (uint32_t i = 0; i < ret1; i++) {
        EXPECT_EQ(*deq_table1[i], UT_DATA_MAGIC) << "i :" << i;
    }

    for (uint32_t i = 0; i < ret2; i++) {
        EXPECT_EQ(*deq_table2[i], UT_DATA_MAGIC) << "i :" << i;
    }

    EXPECT_TRUE(bbq_debug_check_array_bounds(q));
    ut_free(UT_MODULE_DATA, deq_table2);
    bbq_destory(q);
}

TEST_F(ut_bbq, burst_drop_old_cp_pointer) {
    struct bbq *q;
    uint32_t ret1 = 0;
    uint32_t ret2 = 0;
    uint64_t first_cnt = BUF_CNT;
    uint64_t second_cnt = 1000;
    uint32_t wait_consumed = 0;
    uint16_t *deq_table1[BUF_CNT] = {0};
    uint16_t **deq_table2 = (uint16_t **)ut_malloc(UT_MODULE_DATA, sizeof(uint16_t *) * BUF_CNT);

    // 创建队列
    q = bbq_create("ut_bbq", BUF_CNT, BBQ_SOCKET_ID_ANY, BBQ_F_DROP_OLD,
                   ut_malloc_def_callback, ut_free_def_callback);
    ASSERT_NE(q, nullptr);
    EXPECT_GT(second_cnt, q->bs);
    EXPECT_LT(second_cnt, q->bs * q->bn);

    // 批量入队(全部成功，入队个数等于队列总容量，未发生覆盖)
    ret1 = bbq_enqueue_burst(q, (void *const *)enq_table1, first_cnt, &wait_consumed);
    EXPECT_EQ(ret1, first_cnt);
    // EXPECT_EQ(wait_consumed, ret1);

    // 批量入队(全部成功)，覆盖了旧数据
    ret2 = bbq_enqueue_burst(q, (void *const *)enq_table2, second_cnt, &wait_consumed);
    EXPECT_EQ(ret2, second_cnt);
    // EXPECT_EQ(wait_consumed, second_cnt - q->bs);

    // 出队列（部分成功）
    // 一旦生产者追上了消费者，之前未消费的，以及当前块的数据全都作废了。本例中第一个完整块作废。
    ret1 = bbq_dequeue_burst(q, (void **)deq_table1, BUF_CNT, &wait_consumed);
    EXPECT_EQ(ret1, second_cnt - q->bs);
    // EXPECT_EQ(wait_consumed, 0);

    // 验证数据
    for (uint32_t i = 0; i < ret1; i++) {
        EXPECT_EQ(*deq_table1[i], UT_DATA_MAGIC) << "i :" << i;
    }

    // 此时生产者和消费者在同一块上，入队个数为队列容量的N倍，由于发生了覆盖，且依旧在同一块上，数据全作废
    for (uint32_t loop = 0; loop < 3; loop++) {
        ret1 = bbq_enqueue_burst(q, (void *const *)enq_table1, BUF_CNT, &wait_consumed);
        EXPECT_EQ(ret1, BUF_CNT);
        EXPECT_TRUE(bbq_empty(q));
        EXPECT_EQ(wait_consumed, 0);
    }

    EXPECT_TRUE(bbq_debug_check_array_bounds(q));
    ut_free(UT_MODULE_DATA, deq_table2);
    bbq_destory(q);
}

TEST_F(ut_bbq, burst_drop_old_cp_value) {
    struct bbq *q;
    uint32_t ret1 = 0;
    uint32_t ret2 = 0;
    uint64_t first_cnt = BUF_CNT;
    uint64_t second_cnt = 1000;
    uint32_t wait_consumed = 0;
    uint16_t deq_table1[BUF_CNT] = {0};

    // 创建队列
    q = bbq_create_elem("ut_bbq", BUF_CNT, sizeof(uint16_t),
                        BBQ_SOCKET_ID_ANY, BBQ_F_DROP_OLD,
                        ut_malloc_def_callback, ut_free_def_callback);
    ASSERT_NE(q, nullptr);
    EXPECT_GT(second_cnt, q->bs);
    EXPECT_LT(second_cnt, q->bs * q->bn);

    // 批量入队(全部成功)
    ret1 = bbq_enqueue_burst_elem(q, (void const *)enq_table3, first_cnt, &wait_consumed);
    EXPECT_EQ(ret1, first_cnt);
    // EXPECT_EQ(wait_consumed, ret1);

    // 批量入队(全部成功)，覆盖了旧数据
    // 由于需要将最终的值入队列，二维数组里的值不连续，需要循环赋值。不推荐这个函数，但可用于特殊场景。
    ret2 = bbq_enqueue_burst_elem_2d_array(q, (void *const *)enq_table1, second_cnt, &wait_consumed);
    EXPECT_EQ(ret2, second_cnt);
    // EXPECT_EQ(wait_consumed, second_cnt - q->bs);

    // 出队列（部分成功）
    // 一旦生产者追上了消费者，之前未消费的，以及当前块的数据全都作废了。
    ret1 = bbq_dequeue_burst_elem(q, (void *)deq_table1, BUF_CNT, &wait_consumed);
    EXPECT_EQ(ret1, second_cnt - q->bs);
    EXPECT_EQ(wait_consumed, 0);

    // 验证数据
    for (uint32_t i = 0; i < ret1; i++) {
        EXPECT_EQ(deq_table1[i], UT_DATA_MAGIC) << "i :" << i;
    }

    EXPECT_TRUE(bbq_debug_check_array_bounds(q));
    bbq_destory(q);
}

typedef struct {
    uint64_t thread_cnt;
    bbq_atomic64 data;
    aotmic_uint64 ready_thread_cnt;
} ut_fetch_arg;

void *fetch_max_thread_func(void *arg) {
    ut_fetch_arg *fetch_arg = (ut_fetch_arg *)arg;
    fetch_arg->ready_thread_cnt.fetch_add(1);

    while (fetch_arg->ready_thread_cnt.load() != fetch_arg->thread_cnt) {
    }

    uint64_t *ret = (uint64_t *)ut_malloc(UT_MODULE_UTEST, sizeof(*ret));
    // 不同线程写入不同的>3的数
    *ret = bbq_fetch_max(&fetch_arg->data, pthread_self() + 3, false);
    pthread_exit(ret);
}

TEST_F(ut_bbq, mpmc) {
    struct ut_info_s info {
        .cfg = {
            .base = {
                .name = {},
                .introduce = {},
                .core_begin = 0,
                .core_end = 3,
            },
            .ring = {
                .ring_type = UT_RING_TYPE_BBQ,
                .producer_cnt = 4,
                .consumer_cnt = 4,
                .workload = UT_WORKLOAD_SIMPLE,
                .entries_cnt = 4096,
                .block_count = 0,
                .burst_cnt = 4,
            },
            .run = {
                .run_ok_times = 9000000,
                .run_time = 0,
            },
        },
        .ctl = {
            .running = true,
            .all_threads_start = {},
            .producer_exit = {},
        },
    };

    // 队列初始化
    int ret = -1;
    struct ut_queue q;
    ret = ut_queue_init_bbq(&info.cfg, &q);
    ASSERT_TRUE(ret == 0);

    // 创建线程
    pthread_t *threads = ut_threads_create(&info, &q);
    ASSERT_TRUE(threads);

    // 等待所有线程完成，回收数据
    uint32_t thread_cnt = info.cfg.ring.producer_cnt + info.cfg.ring.consumer_cnt;
    struct ut_exit_data **exit_data = (struct ut_exit_data **)ut_malloc(UT_MODULE_UTEST, sizeof(struct ut_exit_data **) * (thread_cnt));
    ut_wait_all_threads_exit(&info, thread_cnt, threads, exit_data);

    // 比较数据
    struct ut_merge_s merge;
    memset(&merge, 0, sizeof(merge));
    ut_merge_all_data(exit_data, thread_cnt, &merge);
    EXPECT_EQ(merge.consumer.data_error_cnt, 0);
    EXPECT_EQ(merge.consumer.ok_cnt, merge.producer.ok_cnt);

    // 释放数据
    for (uint32_t i = 0; i < thread_cnt; i++) {
        ut_exit_data_destory(exit_data[i]);
    }
    ut_free(UT_MODULE_UTEST, exit_data);
    ut_threads_destory(&info, threads);
    EXPECT_TRUE(bbq_debug_check_array_bounds((struct bbq *)q.ring));
    ut_queue_destory(&q);
}

TEST_F(ut_bbq, bbq_fetch_max) {
    uint64_t ret = 0;
    ut_fetch_arg arg;

    bool single = false;
    arg.data.m.store(0);
    arg.thread_cnt = 0;
    arg.ready_thread_cnt.store(0);

    bbq_atomic64_store(&arg.data, 1, single); // 初始化1
    arg.thread_cnt = 50;

    ret = bbq_fetch_max(&arg.data, 2, single); // max比较后设置为2
    EXPECT_EQ(bbq_atomic64_load(&arg.data, single), 2);
    EXPECT_EQ(ret, 1);

    pthread_t *threads = (pthread_t *)ut_malloc(UT_MODULE_UTEST, sizeof(*threads) * arg.thread_cnt);
    for (uint64_t i = 0; i < arg.thread_cnt; i++) {
        // 多个线程同时fetch_max，输入 > 3的数据
        pthread_create(&threads[i], NULL, fetch_max_thread_func, (void *)&arg);
    }

    int eq_cnt = 0;
    for (uint64_t i = 0; i < arg.thread_cnt; i++) {
        uint64_t *tret;
        pthread_join(threads[i], (void **)&tret); // 等待每个线程结束
        if (*tret == 2) {
            eq_cnt++; // 统计返回2的个数
        }
        ut_free(UT_MODULE_UTEST, tret);
    }

    // EXPECT_EQ(eq_cnt, 1);
    ut_free(UT_MODULE_UTEST, threads);
}

TEST_F(ut_bbq, power_of_two) {
    uint32_t tmp = 0;
    uint32_t max = pow(2, 32) - 1;

    EXPECT_FALSE(bbq_check_power_of_two(0));

    tmp = 3;
    for (uint32_t val = 5; val < max; val *= tmp) {
        EXPECT_FALSE(bbq_check_power_of_two(val));
        if (val >= max / tmp) {
            break; // 即将越界
        }
    }

    tmp = 2;
    for (uint32_t val = 1; val < max; val *= tmp) {
        EXPECT_TRUE(bbq_check_power_of_two(val));
        if (val >= max / tmp) {
            break;
        }
    }
}

TEST_F(ut_bbq, bbq_block_number_calc) {
    uint32_t tmp = 2;
    uint32_t max = pow(2, 32) - 1;
    uint32_t bn = 0, bs = 0;
    int ret = 0;

    ret = bbq_bnbs_calc(1, &bn, &bs);
    EXPECT_EQ(ret, BBQ_ERR_OUT_OF_RANGE);

    for (uint32_t val = 2; val < max; val *= tmp) {
        ret = bbq_bnbs_calc(val, &bn, &bs);
        EXPECT_EQ(ret, BBQ_OK);
        if (val <= 128) {
            EXPECT_EQ(bn, 2);
        } else if (val <= 2048) {
            EXPECT_EQ(bn, 4);
        } else if (val <= 32768) {
            EXPECT_EQ(bn, 8);
        } else if (val <= 524288) {
            EXPECT_EQ(bn, 16);
        } else if (val <= 8388608) {
            EXPECT_EQ(bn, 32);
        } else if (val <= 134217728) {
            EXPECT_EQ(bn, 64);
        } else if (val <= 2147483648) {
            EXPECT_EQ(bn, 128);
        } else {
            EXPECT_TRUE(0); // 异常
        }

        if (val >= max / tmp) {
            break;
        }
    }
}

#define OFFSETOF(type, member) ((size_t) & ((type *)0)->member)
#define PRINT_OFFSETOF(type, member) printf("Offset of '%s' in '%s' is %zu\n", #member, #type, OFFSETOF(type, member))
#define PTR_ALIGNED_64(ptr) (((uintptr_t)ptr & (64 - 1)) == 0)
#define SIZE_ALIGNED_64(v) (((sizeof(v)) & (64 - 1)) == 0)

TEST_F(ut_bbq, bbq_cache_line) {

    // 创建队列
    struct bbq *q = bbq_create("ut_bbq", 4096, BBQ_SOCKET_ID_ANY, BBQ_F_RETRY_NEW,
                               ut_malloc_def_callback, ut_free_def_callback);
    ASSERT_NE(q, nullptr);

    // 首地址64字节对齐
    EXPECT_EQ(PTR_ALIGNED_64(q), true);

    // 关键成员64字节对齐
    EXPECT_EQ(PTR_ALIGNED_64(&q->name), true);
    EXPECT_EQ(PTR_ALIGNED_64(&q->socket_id), true);
    EXPECT_EQ(PTR_ALIGNED_64(&q->phead.value), true);
    EXPECT_EQ(PTR_ALIGNED_64(&q->chead.value), true);
    EXPECT_EQ(PTR_ALIGNED_64(&q->blocks), true);
    EXPECT_EQ(PTR_ALIGNED_64(&q->blocks[0].committed), true);
    EXPECT_EQ(PTR_ALIGNED_64(&q->blocks[0].entries), true);

    EXPECT_EQ(SIZE_ALIGNED_64(struct bbq_head), true);
    EXPECT_EQ(SIZE_ALIGNED_64(struct bbq_block), true);

    // PRINT_OFFSETOF(struct bbq, name);
    // PRINT_OFFSETOF(struct bbq, socket_id);
    // PRINT_OFFSETOF(struct bbq, phead);
    // PRINT_OFFSETOF(struct bbq, chead);
    // PRINT_OFFSETOF(struct bbq, blocks);

    bbq_destory(q);
}

void expect_phead(struct bbq *q, uint64_t idx, uint64_t vsn, int line) {
    uint64_t ph = bbq_atomic64_load(&q->phead.value, q->prod_single);
    EXPECT_EQ(bbq_head_idx(q, ph), idx) << "line: " << line;
    EXPECT_EQ(bbq_head_vsn(q, ph), vsn) << "line: " << line;
}

void expect_chead(struct bbq *q, uint64_t idx, uint64_t vsn, int line) {
    uint64_t ch = bbq_atomic64_load(&q->chead.value, q->cons_single);
    EXPECT_EQ(bbq_head_idx(q, ch), idx) << "line: " << line;
    EXPECT_EQ(bbq_head_vsn(q, ch), vsn) << "line: " << line;
}

void expect_eq_allocated(struct bbq *q, bbq_block *block, uint64_t off, uint64_t vsn, int line) {
    uint64_t allocated = bbq_atomic64_load(&block->allocated, q->prod_single);
    EXPECT_EQ(bbq_cur_off(q, allocated), off) << "line: " << line;
    EXPECT_EQ(bbq_cur_vsn(q, allocated), vsn) << "line: " << line;
}

void expect_eq_committed(struct bbq *q, bbq_block *block, uint64_t off, uint64_t vsn, int line) {
    uint64_t committed = bbq_atomic64_load(&block->committed, q->prod_single);
    EXPECT_EQ(bbq_cur_off(q, committed), off) << "line: " << line;
    EXPECT_EQ(bbq_cur_vsn(q, committed), vsn) << "line: " << line;
}

void expect_eq_consumed(struct bbq *q, bbq_block *block, uint64_t off, uint64_t vsn, int line) {
    uint64_t consumed = bbq_atomic64_load(&block->consumed, q->cons_single);
    EXPECT_EQ(bbq_cur_off(q, consumed), off) << "line: " << line;
    EXPECT_EQ(bbq_cur_vsn(q, consumed), vsn) << "line: " << line;
}

void expect_eq_reserved(struct bbq *q, bbq_block *block, uint64_t off, uint64_t vsn, int line) {
    uint64_t reserved = bbq_atomic64_load(&block->reserved, q->cons_single);
    EXPECT_EQ(bbq_cur_off(q, reserved), off) << "line: " << line;
    EXPECT_EQ(bbq_cur_vsn(q, reserved), vsn) << "line: " << line;
}

// 初始化状态
TEST_F(ut_bbq, head_cursor_init) {
    struct bbq *q;
    uint32_t bn = 2;
    uint32_t bs = 4;
    q = bbq_create_elem_with_bnbs("ut_bbq", bn, bs, sizeof(int),
                                  BBQ_SOCKET_ID_ANY, BBQ_F_RETRY_NEW,
                                  ut_malloc_def_callback, ut_free_def_callback);
    ASSERT_NE(q, nullptr);

    // 1.初始化状态,除了第一个block外其他块的4个游标都指向最后一个条目

    EXPECT_EQ(bbq_atomic64_load(&q->phead.value, q->prod_single), 0);
    EXPECT_EQ(bbq_atomic64_load(&q->chead.value, q->cons_single), 0);

    expect_eq_allocated(q, &q->blocks[0], 0, 0, __LINE__);
    expect_eq_committed(q, &q->blocks[0], 0, 0, __LINE__);
    expect_eq_reserved(q, &q->blocks[0], 0, 0, __LINE__);
    expect_eq_consumed(q, &q->blocks[0], 0, 0, __LINE__);
    for (uint32_t i = 1; i < bn; i++) {
        expect_eq_allocated(q, &q->blocks[i], bs, 0, __LINE__);
        expect_eq_committed(q, &q->blocks[i], bs, 0, __LINE__);
        expect_eq_reserved(q, &q->blocks[i], bs, 0, __LINE__);
        expect_eq_consumed(q, &q->blocks[i], bs, 0, __LINE__);
    }
    EXPECT_TRUE(bbq_debug_check_array_bounds(q));
    bbq_destory(q);
}

void ut_produce_something(uint32_t produce_cnt) {
    int ret = 0;
    struct bbq *q;
    uint32_t bn = 8;
    uint32_t bs = 4096;
    int enqueue_data = UT_DATA_MAGIC;
    int dequeue_data = 0;

    EXPECT_GT(produce_cnt, 0);
    EXPECT_LE(produce_cnt, bs);

    q = bbq_create_elem_with_bnbs("ut_bbq", bn, bs, sizeof(int),
                                  BBQ_SOCKET_ID_ANY, BBQ_F_RETRY_NEW,
                                  ut_malloc_def_callback, ut_free_def_callback);
    ASSERT_NE(q, nullptr);

    // 生产produce_cnt
    for (uint32_t i = 0; i < produce_cnt; i++) {
        ret = bbq_enqueue_elem(q, &enqueue_data);
        EXPECT_TRUE(ret == BBQ_OK);
    }

    EXPECT_EQ(bbq_atomic64_load(&q->phead.value, q->prod_single), 0);
    EXPECT_EQ(bbq_atomic64_load(&q->chead.value, q->cons_single), 0);
    expect_eq_allocated(q, &q->blocks[0], produce_cnt, 0, __LINE__);
    expect_eq_committed(q, &q->blocks[0], produce_cnt, 0, __LINE__);
    expect_eq_reserved(q, &q->blocks[0], 0, 0, __LINE__);
    expect_eq_consumed(q, &q->blocks[0], 0, 0, __LINE__);

    // 消费完
    for (uint32_t i = 0; i < produce_cnt; i++) {
        ret = bbq_dequeue_elem(q, &dequeue_data);
        EXPECT_TRUE(ret == BBQ_OK);
        EXPECT_EQ(dequeue_data, UT_DATA_MAGIC);
    }

    EXPECT_EQ(bbq_atomic64_load(&q->phead.value, q->prod_single), 0);
    EXPECT_EQ(bbq_atomic64_load(&q->chead.value, q->cons_single), 0);
    expect_eq_allocated(q, &q->blocks[0], produce_cnt, 0, __LINE__);
    expect_eq_committed(q, &q->blocks[0], produce_cnt, 0, __LINE__);
    expect_eq_reserved(q, &q->blocks[0], produce_cnt, 0, __LINE__);
    expect_eq_consumed(q, &q->blocks[0], produce_cnt, 0, __LINE__);

    for (uint32_t i = 1; i < bn; i++) {
        expect_eq_allocated(q, &q->blocks[i], bs, 0, __LINE__);
        expect_eq_committed(q, &q->blocks[i], bs, 0, __LINE__);
        expect_eq_reserved(q, &q->blocks[i], bs, 0, __LINE__);
        expect_eq_consumed(q, &q->blocks[i], bs, 0, __LINE__);
    }
    EXPECT_TRUE(bbq_debug_check_array_bounds(q));
    bbq_destory(q);
}
// 在第一块内生产，然后被消费完
TEST_F(ut_bbq, head_cursor_produce_something) {
    ut_produce_something(1);
    ut_produce_something(567);
    ut_produce_something(789);
    ut_produce_something(4096);
}

void ut_produce_next_block(uint32_t over) {
    int ret = 0;
    struct bbq *q;
    uint32_t bn = 8;
    uint32_t bs = 4096;
    uint32_t produce_cnt = bs + over;
    int enqueue_data = UT_DATA_MAGIC;
    int dequeue_data = 0;

    EXPECT_GT(over, 0);
    EXPECT_LT(over, bs);

    q = bbq_create_elem_with_bnbs("ut_bbq", bn, bs, sizeof(int),
                                  BBQ_SOCKET_ID_ANY, BBQ_F_RETRY_NEW,
                                  ut_malloc_def_callback, ut_free_def_callback);
    ASSERT_NE(q, nullptr);

    // 生产至第二块的第一个entry
    for (uint32_t i = 0; i < produce_cnt; i++) {
        ret = bbq_enqueue_elem(q, &enqueue_data);
        EXPECT_TRUE(ret == BBQ_OK);
    }

    EXPECT_EQ(bbq_atomic64_load(&q->chead.value, q->cons_single), 0);
    expect_phead(q, 1, 0, __LINE__);
    expect_eq_allocated(q, &q->blocks[0], bs, 0, __LINE__);
    expect_eq_committed(q, &q->blocks[0], bs, 0, __LINE__);
    expect_eq_reserved(q, &q->blocks[0], 0, 0, __LINE__);
    expect_eq_consumed(q, &q->blocks[0], 0, 0, __LINE__);

    expect_eq_allocated(q, &q->blocks[1], over, 1, __LINE__);
    expect_eq_committed(q, &q->blocks[1], over, 1, __LINE__);
    expect_eq_reserved(q, &q->blocks[1], bs, 0, __LINE__);
    expect_eq_consumed(q, &q->blocks[1], bs, 0, __LINE__);

    // 消费完
    for (uint32_t i = 0; i < produce_cnt; i++) {
        ret = bbq_dequeue_elem(q, &dequeue_data);
        EXPECT_TRUE(ret == BBQ_OK);
        EXPECT_EQ(dequeue_data, UT_DATA_MAGIC);
    }

    expect_phead(q, 1, 0, __LINE__);
    expect_chead(q, 1, 0, __LINE__);
    expect_eq_allocated(q, &q->blocks[0], bs, 0, __LINE__);
    expect_eq_committed(q, &q->blocks[0], bs, 0, __LINE__);
    expect_eq_reserved(q, &q->blocks[0], bs, 0, __LINE__);
    expect_eq_consumed(q, &q->blocks[0], bs, 0, __LINE__);

    expect_eq_allocated(q, &q->blocks[1], over, 1, __LINE__);
    expect_eq_committed(q, &q->blocks[1], over, 1, __LINE__);
    expect_eq_reserved(q, &q->blocks[1], over, 1, __LINE__);
    expect_eq_consumed(q, &q->blocks[1], over, 1, __LINE__);

    EXPECT_TRUE(bbq_debug_check_array_bounds(q));
    bbq_destory(q);
}

// 第一块生产完毕，第二块生产了若干，然后被消费完
TEST_F(ut_bbq, head_cursor_produce_next_block) {
    ut_produce_next_block(1);
    ut_produce_next_block(123);
    ut_produce_next_block(456);
    ut_produce_next_block(4095);
}

void ut_produce_all_loop(uint32_t loop) {
    int ret = 0;
    struct bbq *q;
    uint32_t bn = 8;
    uint32_t bs = 4096;
    uint32_t produce_cnt = bn * bs;
    int enqueue_data = UT_DATA_MAGIC;
    int dequeue_data = 0;

    q = bbq_create_elem_with_bnbs("ut_bbq", bn, bs, sizeof(int),
                                  BBQ_SOCKET_ID_ANY, BBQ_F_RETRY_NEW,
                                  ut_malloc_def_callback, ut_free_def_callback);
    ASSERT_NE(q, nullptr);

    for (uint32_t cnt = 0; cnt < loop; cnt++) {
        // 所有entry生产完毕
        for (uint32_t i = 0; i < produce_cnt; i++) {
            ret = bbq_enqueue_elem(q, &enqueue_data);
            EXPECT_TRUE(ret == BBQ_OK);
        }

        // 消费完
        for (uint32_t i = 0; i < produce_cnt; i++) {
            ret = bbq_dequeue_elem(q, &dequeue_data);
            EXPECT_TRUE(ret == BBQ_OK);
            EXPECT_EQ(dequeue_data, UT_DATA_MAGIC);
        }
    }

    expect_phead(q, bn - 1, loop - 1, __LINE__);
    expect_chead(q, bn - 1, loop - 1, __LINE__);

    expect_eq_allocated(q, &q->blocks[0], bs, loop - 1, __LINE__);
    expect_eq_committed(q, &q->blocks[0], bs, loop - 1, __LINE__);
    expect_eq_reserved(q, &q->blocks[0], bs, loop - 1, __LINE__);
    expect_eq_consumed(q, &q->blocks[0], bs, loop - 1, __LINE__);

    for (uint32_t i = 1; i < bn; i++) {
        expect_eq_allocated(q, &q->blocks[i], bs, loop, __LINE__);
        expect_eq_committed(q, &q->blocks[i], bs, loop, __LINE__);
        expect_eq_reserved(q, &q->blocks[i], bs, loop, __LINE__);
        expect_eq_consumed(q, &q->blocks[i], bs, loop, __LINE__);
    }

    EXPECT_TRUE(bbq_debug_check_array_bounds(q));
    bbq_destory(q);
}

// 完成多轮的满生产和满消费
TEST_F(ut_bbq, head_cursor_produce_all_loop) {
    ut_produce_all_loop(1);
    ut_produce_all_loop(10);
    ut_produce_all_loop(23);
    ut_produce_all_loop(79);
}

TEST_F(ut_bbq, head_cursor_retry_new_full_empty) {
    int ret = 0;
    uint32_t entries_cnt = 4096;
    uint32_t loop = 1000;
    struct bbq *q;
    uint64_t ph = 0;
    uint64_t ch = 0;
    int *data = (int *)ut_malloc(UT_MODULE_UTEST, sizeof(*data) * entries_cnt);
    int tmp_data = 0;
    EXPECT_TRUE(data);

    q = bbq_create_elem("ut_bbq", entries_cnt, sizeof(int), BBQ_SOCKET_ID_ANY,
                        BBQ_F_RETRY_NEW | BBQ_F_ENABLE_STAT,
                        ut_malloc_def_callback, ut_free_def_callback);
    ASSERT_NE(q, nullptr);
    EXPECT_TRUE(bbq_empty(q));

    for (uint32_t i = 0; i < loop; i++) {
        // 入满队
        for (uint32_t j = 0; j < entries_cnt; j++) {
            data[j] = (i + 1) * j;
            ret = bbq_enqueue_elem(q, &data[j]);
            EXPECT_TRUE(ret == BBQ_OK) << "ret " << ret;
            EXPECT_FALSE(bbq_empty(q));
        }

        // 满队再入队
        for (uint32_t j = 0; j < entries_cnt / 3; j++) {
            ret = bbq_enqueue_elem(q, &data[j]);
            EXPECT_TRUE(ret == BBQ_ERR_FULL);
        }

        ph = bbq_atomic64_load(&q->phead.value, q->prod_single);
        ch = bbq_atomic64_load(&q->chead.value, q->cons_single);
        if (i == 0) {
            EXPECT_EQ((ph + 1) & q->idx_mask, ch & q->idx_mask);
        } else {
            EXPECT_EQ((ph)&q->idx_mask, ch & q->idx_mask);
        }

        for (uint32_t i = 0; i < q->bn; i++) {
            EXPECT_EQ(bbq_atomic64_load(&q->blocks[i].committed, q->prod_single) & q->off_mask, q->bs);
            EXPECT_GE(bbq_atomic64_load(&q->blocks[i].allocated, q->prod_single) & q->off_mask, q->bs);
        }

        // 全出队
        for (uint32_t j = 0; j < entries_cnt; j++) {
            EXPECT_FALSE(bbq_empty(q));
            ret = bbq_dequeue_elem(q, &tmp_data);
            EXPECT_TRUE(ret == BBQ_OK);
            EXPECT_EQ(tmp_data, data[j]);
        }

        EXPECT_TRUE(bbq_empty(q));
        // 空出队再出队
        for (uint32_t j = 0; j < entries_cnt / 2; j++) {
            ret = bbq_dequeue_elem(q, &tmp_data);
            EXPECT_TRUE(ret == BBQ_ERR_EMPTY);
        }

        ph = bbq_atomic64_load(&q->phead.value, q->prod_single);
        ch = bbq_atomic64_load(&q->chead.value, q->cons_single);
        EXPECT_EQ(ph & q->idx_mask, ch & q->idx_mask);
        for (uint32_t i = 0; i < q->bn; i++) {
            EXPECT_EQ(bbq_atomic64_load(&q->blocks[i].committed, q->prod_single) & q->off_mask, q->bs);
            EXPECT_GE(bbq_atomic64_load(&q->blocks[i].allocated, q->prod_single) & q->off_mask, q->bs);
            EXPECT_EQ(bbq_atomic64_load(&q->blocks[i].consumed, q->cons_single) & q->off_mask, q->bs);
            EXPECT_GE(bbq_atomic64_load(&q->blocks[i].reserved, q->cons_single) & q->off_mask, q->bs);
        }
    }

    ut_free(UT_MODULE_UTEST, data);
    EXPECT_TRUE(bbq_debug_check_array_bounds(q));
    bbq_destory(q);
}

TEST_F(ut_bbq, head_cursor_mpsc_faa) {
    struct ut_info_s info = {
        .cfg = {
            .base = {
                .name = {},
                .introduce = {},
                .core_begin = 0,
                .core_end = 3,
            },
            .ring = {
                .ring_type = UT_RING_TYPE_BBQ,
                .producer_cnt = 10,
                .consumer_cnt = 1,
                .workload = UT_WORKLOAD_SIMPLE,
                .entries_cnt = 4,
                .block_count = 1,
                .burst_cnt = 1,
            },
            .run = {
                .run_ok_times = 9000000,
                .run_time = 0,
            },
        },
        .ctl = {
            .running = true,
            .all_threads_start = {},
            .producer_exit = {},
        },
    };

    // 队列初始化
    int ret = -1;
    struct ut_queue q;
    ret = ut_queue_init_bbq(&info.cfg, &q);
    ASSERT_TRUE(ret == 0);

    // 创建线程
    pthread_t *threads = ut_threads_create(&info, &q);
    ASSERT_TRUE(threads);

    // 等待所有线程完成，回收数据
    uint32_t thread_cnt = info.cfg.ring.producer_cnt + info.cfg.ring.consumer_cnt;
    struct ut_exit_data **exit_data = (struct ut_exit_data **)ut_malloc(UT_MODULE_UTEST, sizeof(struct ut_exit_data **) * (thread_cnt));
    uint32_t i = 0;

    ut_wait_all_threads_exit(&info, thread_cnt, threads, exit_data);

    // 比较数据
    struct ut_merge_s merge;
    memset(&merge, 0, sizeof(merge));

    ut_merge_all_data(exit_data, thread_cnt, &merge);
    EXPECT_EQ(merge.consumer.data_error_cnt, 0);
    EXPECT_EQ(merge.consumer.ok_cnt, merge.producer.ok_cnt);

    // 释放数据
    for (i = 0; i < thread_cnt; i++) {
        ut_exit_data_destory(exit_data[i]);
    }
    ut_free(UT_MODULE_UTEST, exit_data);
    ut_threads_destory(&info, threads);
    EXPECT_TRUE(bbq_debug_check_array_bounds((struct bbq *)q.ring));
    ut_queue_destory(&q);
}

TEST_F(ut_bbq, head_cursor_drop_old_full_empty) {
    int ret = 0;
    uint32_t bn = 2;
    uint32_t bs = 4;
    uint32_t loop = 1000;
    struct bbq *q;

    int tmp_data = 0;
    q = bbq_create_elem_with_bnbs("ut_bbq", bn, bs, sizeof(int),
                                  BBQ_SOCKET_ID_ANY, BBQ_F_DROP_OLD,
                                  ut_malloc_def_callback, ut_free_def_callback);
    ASSERT_NE(q, nullptr);
    // EXPECT_TRUE(bbq_empty(q));

    for (uint32_t j = 0; j < loop; j++) {
        // 入满队列
        for (uint32_t i = 0; i < bn * bs; i++) {
            ret = bbq_enqueue_elem(q, &i);
            EXPECT_TRUE(ret == BBQ_OK) << "ret " << ret;
            // EXPECT_FALSE(bbq_empty(q));
        }

        // 全出队
        for (uint32_t i = 0; i < bn * bs; i++) {
            // EXPECT_FALSE(bbq_empty(q));
            ret = bbq_dequeue_elem(q, &tmp_data);
            EXPECT_TRUE(ret == BBQ_OK) << "ret " << ret;
            EXPECT_EQ(tmp_data, i);
        }

        // EXPECT_TRUE(bbq_empty(q));
        // 空队再出队，失败
        for (uint32_t i = 0; i < bn * bs; i++) {
            ret = bbq_dequeue_elem(q, &tmp_data);
            EXPECT_TRUE(ret == BBQ_ERR_EMPTY) << "ret " << ret;
        }

        expect_phead(q, bn - 1, j, __LINE__);
        expect_chead(q, bn - 1, j, __LINE__);
        for (uint32_t i = 0; i < q->bn; i++) {
            expect_eq_committed(q, &q->blocks[i], q->bs, i == 0 ? j : j + 1, __LINE__);
            expect_eq_allocated(q, &q->blocks[i], q->bs, i == 0 ? j : j + 1, __LINE__);
            expect_eq_reserved(q, &q->blocks[i], q->bs, i == 0 ? j : j + 1, __LINE__);
            EXPECT_EQ(bbq_atomic64_load(&q->blocks[i].consumed, q->cons_single), 0);
        }
    }
    EXPECT_TRUE(bbq_debug_check_array_bounds(q));
    bbq_destory(q);
}

TEST_F(ut_bbq, head_cursor_drop_old_full_empty_cover) {
    int ret = 0;
    uint32_t bn = 2;
    uint32_t bs = 4;
    uint32_t loop = 1000;
    uint32_t over_cnt = bs + 2;
    struct bbq *q;

    EXPECT_EQ(over_cnt / bs, 1);

    int tmp_data = 0;
    q = bbq_create_elem_with_bnbs("ut_bbq", bn, bs, sizeof(int),
                                  BBQ_SOCKET_ID_ANY, BBQ_F_DROP_OLD,
                                  ut_malloc_def_callback, ut_free_def_callback);
    ASSERT_NE(q, nullptr);

    // EXPECT_TRUE(bbq_empty(q));
    // 入满队列，再入over_cnt
    for (uint32_t i = 0; i < bn * bs * loop + over_cnt; i++) {
        ret = bbq_enqueue_elem(q, &i);
        EXPECT_TRUE(ret == BBQ_OK) << "ret " << ret;

        // uint32_t tmpA = i / (bn * bs);
        // uint32_t tmpB = i % (bn * bs);
        // if (tmpA > 0 && (tmpB < bs)) {
        //     // 覆盖第一个块时，整个块被作废，因此都是empty，从第二个块开始可读取
        //     EXPECT_TRUE(bbq_empty(q)) << "i " << i << "tmpA " << tmpA << "tmpB " << tmpB;
        // } else {
        //     EXPECT_FALSE(bbq_empty(q));
        // }
    }

    expect_phead(q, 1, loop, __LINE__);
    expect_chead(q, 0, 0, __LINE__);
    // 检查每一个block上游标的正确性
    for (uint32_t i = 0; i < bn; i++) {
        expect_eq_committed(q, &q->blocks[i],
                            i == bn - 1 ? over_cnt - bs : bs,
                            i == 0 ? loop : loop + 1,
                            __LINE__);
        expect_eq_allocated(q, &q->blocks[i],
                            i == bn - 1 ? over_cnt - bs : bs,
                            i == 0 ? loop : loop + 1,
                            __LINE__);
        expect_eq_reserved(q, &q->blocks[i],
                           i == 0 ? 0 : bs, 0,
                           __LINE__);
        EXPECT_EQ(bbq_atomic64_load(&q->blocks[i].consumed, false), 0);
    }

    // 队列中的数据全出队
    for (uint32_t i = 0; i < over_cnt - bs; i++) {
        ret = bbq_dequeue_elem(q, &tmp_data);
        EXPECT_TRUE(ret == BBQ_OK) << "ret " << ret;
    }

    for (uint32_t i = 0; i < bn * bs; i++) {
        // EXPECT_TRUE(bbq_empty(q));
        ret = bbq_dequeue_elem(q, &tmp_data);
        EXPECT_TRUE(ret == BBQ_ERR_EMPTY) << "ret " << ret;
    }

    expect_chead(q, 1, 0, __LINE__);
    for (uint32_t i = 0; i < bn; i++) {
        expect_eq_committed(q, &q->blocks[i],
                            i == bn - 1 ? over_cnt - bs : bs,
                            i == 0 ? loop : loop + 1,
                            __LINE__);
        expect_eq_allocated(q, &q->blocks[i],
                            i == bn - 1 ? over_cnt - bs : bs,
                            i == 0 ? loop : loop + 1,
                            __LINE__);
        expect_eq_reserved(q, &q->blocks[i],
                           i == bn - 1 ? over_cnt - bs : bs,
                           i == 1 ? loop + 1 : 0, __LINE__);
        EXPECT_EQ(bbq_atomic64_load(&q->blocks[i].consumed, false), 0);
    }

    EXPECT_TRUE(bbq_debug_check_array_bounds(q));
    bbq_destory(q);
}