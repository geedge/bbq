/*
 * @Author: liuyu
 * @LastEditTime: 2024-06-04 15:21:19
 * @Email: liuyu@geedgenetworks.com
 * @Describe: 简单的测试用例，测试基本功能，同时让调用者快速上手
 */

#include "gtest/gtest.h"
extern "C" {
#include "test_mix.h"
#include "test_queue.h"
#include "ut.h"
extern bool bbq_malloc_free_equal();
extern void bbq_memory_info();
}

// 初始化状态
TEST(burst, init) {
    test_memory_counter_clear();

    uint32_t ret1 = 0;
    uint32_t ret2 = 0;
    uint32_t ret3 = 0;
    bbq_queue_s *q;
    uint32_t buf_cnt = 4096;
    uint32_t cnt1 = 2048;
    uint32_t cnt2 = 2048;
    uint32_t cnt3 = 128;

    // 创建测试数据
    void **enq_table1 = test_enqueue_table_create(cnt1);
    void **enq_table2 = test_enqueue_table_create(cnt2);
    void **enq_table3 = test_enqueue_table_create(cnt3);
    void **deq_table1 = test_table_create(cnt1);
    void **deq_table2 = test_table_create(cnt2);
    void **deq_table3 = test_table_create(cnt3);

    // 创建队列
    q = bbq_ring_create(buf_cnt, BBQ_RETRY_NEW);
    EXPECT_TRUE(q);

    // 批量入队
    ret1 = bbq_enqueue_burst(q, enq_table1, cnt1);
    EXPECT_EQ(ret1, cnt1);
    ret3 = bbq_enqueue_burst(q, enq_table3, cnt3);
    EXPECT_EQ(ret3, cnt3);
    // 队列已满，实际入队<cnt2
    ret2 = bbq_enqueue_burst(q, enq_table2, cnt2);
    EXPECT_EQ(ret2, cnt2 - cnt3);
    EXPECT_EQ(buf_cnt, ret1 + ret2 + ret3);

    ret3 = bbq_dequeue_burst(q, deq_table3, cnt3);
    EXPECT_EQ(ret3, cnt3);
    ret2 = bbq_dequeue_burst(q, deq_table2, cnt2);
    EXPECT_EQ(ret2, cnt2);
    // 队列已空，实际入队<cnt1
    ret1 = bbq_dequeue_burst(q, deq_table1, cnt1);
    EXPECT_EQ(ret1, cnt1 - cnt3);
    EXPECT_EQ(buf_cnt, ret1 + ret2 + ret3);

    // 验证数据
    for (uint32_t i = 0; i < ret1; i++) {
        EXPECT_TRUE(deq_table1[i]);
        EXPECT_EQ(*((int *)deq_table1[i]), TEST_TABLE_DATA_MAGIC);
    }
    for (uint32_t i = 0; i < ret2; i++) {
        EXPECT_TRUE(deq_table2[i]);
        EXPECT_EQ(*((int *)deq_table2[i]), TEST_TABLE_DATA_MAGIC);
    }
    for (uint32_t i = 0; i < ret3; i++) {
        EXPECT_TRUE(deq_table3[i]);
        EXPECT_EQ(*((int *)deq_table3[i]), TEST_TABLE_DATA_MAGIC);
    }

    bbq_ring_free(q);

    // 释放测试数据
    test_enqueue_table_destory(enq_table1, cnt1);
    test_enqueue_table_destory(enq_table2, cnt2);
    test_enqueue_table_destory(enq_table3, cnt3);
    test_table_destory(deq_table1);
    test_table_destory(deq_table2);
    test_table_destory(deq_table3);

    // 内存泄漏检测
    EXPECT_TRUE(bbq_malloc_free_equal());
    EXPECT_TRUE(test_malloc_free_equal());
}