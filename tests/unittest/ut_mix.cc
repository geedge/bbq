/*
 * @Author: liuyu
 * @LastEditTime: 2024-06-01 01:15:34
 * @Email: liuyu@geedgenetworks.com
 * @Describe: bbq除了队列操作外，其他函数的测试
 */
#include "gtest/gtest.h"
extern "C" {
#include "bbq.h"
#include "test_mix.h"
#include "ut.h"
#include <math.h>
extern bool bbq_check_power_of_two(int n);
extern uint32_t bbq_blocks_calc(uint32_t entries);
extern unsigned ceil_log2(uint64_t x);
extern uint64_t fetch_max(aotmic_uint64 *atom, uint64_t upd);
extern bool bbq_malloc_free_equal();
extern bool test_malloc_free_equal();
}
namespace {

typedef struct {
    uint64_t thread_cnt;
    aotmic_uint64 data;
    aotmic_uint64 ready_thread_cnt;
} ut_fetch_arg;

void *fetch_max_thread_func(void *arg) {
    ut_fetch_arg *fetch_arg = (ut_fetch_arg *)arg;
    fetch_arg->ready_thread_cnt.fetch_add(1);

    while (fetch_arg->ready_thread_cnt.load() != fetch_arg->thread_cnt) {
    }

    uint64_t *ret = (uint64_t *)test_malloc(TEST_MODULE_UTEST, sizeof(*ret));
    // 不同线程写入不同的>3的数
    *ret = fetch_max(&fetch_arg->data, pthread_self() + 3);
    pthread_exit(ret);
}

TEST(bbq_mix, fetch_max) {
    test_memory_counter_clear();

    uint64_t ret = 0;
    ut_fetch_arg arg = {0};
    arg.data.store(1); // 初始化1
    arg.thread_cnt = 50;

    ret = fetch_max(&arg.data, 2); // max比较后设置为2
    EXPECT_EQ(arg.data.load(), 2);
    EXPECT_EQ(ret, 1);

    pthread_t *threads = (pthread_t *)test_malloc(TEST_MODULE_UTEST, sizeof(*threads) * arg.thread_cnt);
    for (uint64_t i = 0; i < arg.thread_cnt; i++) {
        // 多个线程同时fetch_max，输入 > 3的数据
        pthread_create(&threads[i], NULL, fetch_max_thread_func, (void *)&arg);
    }

    int eq_cnt = 0;
    for (int i = 0; i < arg.thread_cnt; i++) {
        uint64_t *tret;
        pthread_join(threads[i], (void **)&tret); // 等待每个线程结束
        if (*tret == 2) {
            eq_cnt++; // 统计返回2的个数
        }
        test_free(TEST_MODULE_UTEST, tret);
    }

    // EXPECT_EQ(eq_cnt, 1);
    test_free(TEST_MODULE_UTEST, threads);
    EXPECT_TRUE(bbq_malloc_free_equal());
    EXPECT_TRUE(test_malloc_free_equal());
}

TEST(bbq_mix, power_of_two) {
    test_memory_counter_clear();

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
    EXPECT_TRUE(bbq_malloc_free_equal());
    EXPECT_TRUE(test_malloc_free_equal());
}

TEST(bbq_mix, bbq_blocks_calc) {
    test_memory_counter_clear();

    uint32_t tmp = 0;
    uint32_t max = pow(2, 32) - 1;

    tmp = 2;
    for (uint32_t val = 1; val < max; val *= tmp) {
        if (val <= 128) {
            EXPECT_TRUE(bbq_blocks_calc(val) == 2);
        } else if (val <= 2048) {
            EXPECT_TRUE(bbq_blocks_calc(val) == 4);
        } else if (val <= 32768) {
            EXPECT_TRUE(bbq_blocks_calc(val) == 8);
        } else if (val <= 524288) {
            EXPECT_TRUE(bbq_blocks_calc(val) == 16);
        } else if (val <= 8388608) {
            EXPECT_TRUE(bbq_blocks_calc(val) == 32);
        } else if (val <= 134217728) {
            EXPECT_TRUE(bbq_blocks_calc(val) == 64);
        } else if (val <= 2147483648) {
            EXPECT_TRUE(bbq_blocks_calc(val) == 128);
        } else {
            EXPECT_TRUE(0); // 异常
        }

        if (val >= max / tmp) {
            break;
        }
    }
    EXPECT_TRUE(bbq_malloc_free_equal());
    EXPECT_TRUE(test_malloc_free_equal());
}

TEST(bbq_mix, ceil_log2) {
    test_memory_counter_clear();

    uint32_t tmp = 0;
    uint32_t max = pow(2, 32) - 1;

    tmp = 2;
    for (uint32_t val = 1, bbq_idx = 0; val < max; val *= tmp, bbq_idx++) {
        EXPECT_TRUE(bbq_idx == ceil_log2(val));
        if (val >= max / tmp) {
            break;
        }
        EXPECT_TRUE(log2(val) == ceil_log2(val));
    }
    EXPECT_TRUE(bbq_malloc_free_equal());
    EXPECT_TRUE(test_malloc_free_equal());
}
} // namespace