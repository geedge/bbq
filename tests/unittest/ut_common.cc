/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-29 18:54:21
 * @Email: liuyu@geedgenetworks.com
 * @Describe: bbq除了队列操作外，其他函数的测试
 */
#include "ut.h"
#include "gtest/gtest.h"
extern "C" {
#include "bbq.h"
#include <math.h>
extern bool bbq_is_power_of_two(int n);
extern uint32_t bbq_blocks_calc(uint32_t entries);
}
namespace {

TEST(bbq_mix, power_of_two) {
    uint32_t i = 0;
    EXPECT_FALSE(bbq_is_power_of_two(0));

    for (i = 5; i <= pow(2, 32); i * 3) {
        EXPECT_FALSE(bbq_is_power_of_two(i));
    }

    for (i = 1; i <= pow(2, 32); i * 2) {
        EXPECT_TRUE(bbq_is_power_of_two(i));
    }
}

TEST(bbq_mix, bbq_blocks_calc) {
    uint32_t i = 0;
    for (i = 1; i <= pow(2, 32); i * 2) {
        printf("i:%d   :%d\n", i, bbq_blocks_calc(i));
    }
}
} // namespace