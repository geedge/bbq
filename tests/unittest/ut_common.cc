/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-29 20:06:23
 * @Email: liuyu@geedgenetworks.com
 * @Describe: bbq除了队列操作外，其他函数的测试
 */
#include "gtest/gtest.h"
extern "C" {
#include "ut.h"
#include <math.h>
extern bool bbq_is_power_of_two(int n);
extern uint32_t bbq_blocks_calc(uint32_t entries);
extern unsigned ceil_log2(uint64_t x);
}
namespace {

TEST(bbq_mix, power_of_two) {
    uint32_t tmp = 0;
    uint32_t max = pow(2, 32) - 1;

    EXPECT_FALSE(bbq_is_power_of_two(0));

    tmp = 3;
    for (uint32_t val = 5; val < max; val *= tmp) {
        EXPECT_FALSE(bbq_is_power_of_two(val));
        if (val >= max / tmp) {
            break; // 即将越界
        }
    }

    tmp = 2;
    for (uint32_t val = 1; val < max; val *= tmp) {
        EXPECT_TRUE(bbq_is_power_of_two(val));
        if (val >= max / tmp) {
            break;
        }
    }
}

TEST(bbq_mix, bbq_blocks_calc) {
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
}

TEST(bbq_mix, ceil_log2) {
    uint32_t tmp = 0;
    uint32_t max = pow(2, 32) - 1;

    tmp = 2;
    for (uint32_t val = 1, idx = 0; val < max; val *= tmp, idx++) {
        EXPECT_TRUE(idx == ceil_log2(val));
        if (val >= max / tmp) {
            break;
        }
        EXPECT_TRUE(log2(val) == ceil_log2(val));
    }
}
} // namespace