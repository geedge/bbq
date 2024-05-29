/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-29 19:44:49
 * @Email: liuyu@geedgenetworks.com
 * @Describe: bbq除了队列操作外，其他函数的测试
 */
#include "gtest/gtest.h"
extern "C" {
#include "ut.h"
#include <math.h>
extern bool bbq_is_power_of_two(int n);
extern uint32_t bbq_blocks_calc(uint32_t entries);
}
namespace {

TEST(bbq_mix, power_of_two) {
    uint32_t i = 0;
    uint32_t tmp = 0;
    uint32_t max = pow(2, 32) - 1;

    EXPECT_FALSE(bbq_is_power_of_two(0));

    tmp = 3;
    for (i = 5; i < max; i *= tmp) {
        EXPECT_FALSE(bbq_is_power_of_two(i));
        if (i >= max / tmp) {
            break; // 即将越界
        }
    }

    tmp = 2;
    for (i = 1; i < max; i *= tmp) {
        EXPECT_TRUE(bbq_is_power_of_two(i));
        if (i >= max / tmp) {
            break;
        }
    }
}

TEST(bbq_mix, bbq_blocks_calc) {
    uint32_t i = 0;
    uint32_t tmp = 0;
    uint32_t max = pow(2, 32) - 1;

    tmp = 2;
    for (i = 1; i < max; i *= tmp) {
        printf("i: %u   :%u\n", i, bbq_blocks_calc(i));

        if (i <= 128) {
            EXPECT_TRUE(bbq_blocks_calc(i) == 2);
        } else if (i <= 2048) {
            EXPECT_TRUE(bbq_blocks_calc(i) == 4);
        } else if (i <= 32768) {
            EXPECT_TRUE(bbq_blocks_calc(i) == 8);
        } else if (i <= 524288) {
            EXPECT_TRUE(bbq_blocks_calc(i) == 16);
        } else if (i <= 8388608) {
            EXPECT_TRUE(bbq_blocks_calc(i) == 32);
        } else if (i <= 134217728) {
            EXPECT_TRUE(bbq_blocks_calc(i) == 64);
        } else if (i <= 2147483648) {
            EXPECT_TRUE(bbq_blocks_calc(i) == 128);
        } else {
            EXPECT_TRUE(0); // 异常
        }

        if (i >= max / tmp) {
            break;
        }
    }
}
} // namespace