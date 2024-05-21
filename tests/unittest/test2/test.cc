/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-21 11:21:47
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */

#include "sample1.h"
#include "gtest/gtest.h"
#include <limits.h>

// extern "C" {
// #include "bbq.h"
// }
namespace {

// Tests positive input.
TEST(IsPrimeTest, Positive) {
    EXPECT_FALSE(IsPrime(4));
    EXPECT_TRUE(IsPrime(5));
    EXPECT_FALSE(IsPrime(6));
    EXPECT_TRUE(IsPrime(23));
}
} // namespace