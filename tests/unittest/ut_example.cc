/*
 * @Author: liuyu
 * @LastEditTime: 2024-06-05 16:27:51
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
/*
 * @Author: liuyu
 * @LastEditTime: 2024-06-04 18:16:51
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

TEST(burst, table_cp_value) {
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
    uint64_t enq_table1[cnt1];
    uint64_t enq_table2[cnt2];
    uint64_t enq_table3[cnt3];
    memset(enq_table1, 1, sizeof(enq_table1));
    memset(enq_table2, 1, sizeof(enq_table2));
    memset(enq_table3, 1, sizeof(enq_table3));

    uint64_t deq_table1[cnt1];
    uint64_t deq_table2[cnt2];
    uint64_t deq_table3[cnt3];

    // 创建队列
    q = bbq_ring_create(buf_cnt, sizeof(uint64_t), BBQ_F_COPY_VALUE | BBQ_F_POLICY_RETRY_NEW);
    EXPECT_TRUE(q);

    // 批量入队
    ret1 = bbq_enqueue_burst(q, (void *)enq_table1, cnt1, BBQ_F_ARRAY_1D);
    EXPECT_EQ(ret1, cnt1);
    ret3 = bbq_enqueue_burst(q, (void *)enq_table3, cnt3, BBQ_F_ARRAY_1D);
    EXPECT_EQ(ret3, cnt3);
    // 队列已满，实际入队<cnt2
    ret2 = bbq_enqueue_burst(q, (void *)enq_table2, cnt2, BBQ_F_ARRAY_1D);
    EXPECT_EQ(ret2, cnt2 - cnt3);
    EXPECT_EQ(buf_cnt, ret1 + ret2 + ret3);

    ret3 = bbq_dequeue_burst(q, (void *)deq_table3, cnt3);
    EXPECT_EQ(ret3, cnt3);
    ret2 = bbq_dequeue_burst(q, (void *)deq_table2, cnt2);
    EXPECT_EQ(ret2, cnt2);
    // 队列已空，实际入队<cnt1
    ret1 = bbq_dequeue_burst(q, (void *)deq_table1, cnt1);
    EXPECT_EQ(ret1, cnt1 - cnt3);
    EXPECT_EQ(buf_cnt, ret1 + ret2 + ret3);

    // 验证数据
    uint64_t tmp = 0;
    memset(&tmp, 1, sizeof(tmp));

    for (uint32_t i = 0; i < ret1; i++) {
        EXPECT_TRUE(deq_table1[i]);
        EXPECT_EQ(deq_table1[i], tmp);
    }
    for (uint32_t i = 0; i < ret2; i++) {
        EXPECT_TRUE(deq_table2[i]);
        EXPECT_EQ(deq_table2[i], tmp);
    }
    for (uint32_t i = 0; i < ret3; i++) {
        EXPECT_TRUE(deq_table3[i]);
        EXPECT_EQ(deq_table3[i], tmp);
    }

    bbq_ring_free(q);

    // 内存泄漏检测
    EXPECT_TRUE(bbq_malloc_free_equal());
    EXPECT_TRUE(test_malloc_free_equal());
}

#define BUF_CNT 4096
#define ENQ_TABLE1_CNT 4000
#define ENQ_TABLE2_CNT 90
#define ENQ_TABLE3_CNT 10

TEST(burst, ptr_table_cp_value) {
    test_memory_counter_clear();
    uint32_t ret1 = 0;
    uint32_t ret2 = 0;
    uint32_t ret3 = 0;
    bbq_queue_s *q;

    // 创建测试数据，3种数据类型，均将数据值入队列
    uint16_t **enq_table1 = test_enqueue_table_create(ENQ_TABLE1_CNT);
    uint16_t *enq_table2[ENQ_TABLE2_CNT] = {0};
    for (int i = 0; i < ENQ_TABLE2_CNT; i++) {
        enq_table2[i] = (uint16_t *)test_malloc(TEST_MODULE_DATA, sizeof(uint16_t));
        *enq_table2[i] = TEST_TABLE_DATA_MAGIC;
    }
    uint16_t enq_table3[ENQ_TABLE3_CNT];
    for (int i = 0; i < ENQ_TABLE3_CNT; i++) {
        enq_table3[i] = TEST_TABLE_DATA_MAGIC;
    }

    uint16_t deq_table1[ENQ_TABLE1_CNT] = {0};
    uint16_t *deq_table2 = (uint16_t *)test_malloc(TEST_MODULE_DATA, sizeof(uint16_t) * BUF_CNT);

    // 创建队列
    q = bbq_ring_create(BUF_CNT, sizeof(uint16_t), BBQ_F_POLICY_RETRY_NEW | BBQ_F_COPY_VALUE);
    EXPECT_TRUE(q);

    // 批量入队(全成功)
    ret1 = bbq_enqueue_burst(q, (void *)enq_table1, ENQ_TABLE1_CNT, BBQ_F_ARRAY_2D);
    EXPECT_EQ(ret1, ENQ_TABLE1_CNT);

    // 批量入队(全成功)
    ret2 = bbq_enqueue_burst(q, (void *)enq_table2, ENQ_TABLE2_CNT, BBQ_F_ARRAY_2D);
    EXPECT_EQ(ret2, ENQ_TABLE2_CNT);

    // 批量入队(部分成功)
    ret3 = bbq_enqueue_burst(q, (void *)enq_table3, ENQ_TABLE3_CNT, BBQ_F_ARRAY_1D);
    EXPECT_EQ(ret3, BUF_CNT - ret1 - ret2);

    /*------------------------------------------------------------------*/
    // 出队列（全成功）
    ret1 = bbq_dequeue_burst(q, (void *)deq_table1, ENQ_TABLE1_CNT);
    EXPECT_EQ(ret1, ENQ_TABLE1_CNT);

    // 出队列（部分成功）
    ret2 = bbq_dequeue_burst(q, (void *)deq_table2, BUF_CNT);
    EXPECT_EQ(ret2, BUF_CNT - ret1);

    // 验证数据
    for (uint32_t i = 0; i < ret1; i++) {
        EXPECT_TRUE(deq_table1[i]);
        EXPECT_EQ(deq_table1[i], TEST_TABLE_DATA_MAGIC) << "i :" << i;
    }

    for (uint32_t i = 0; i < ret2; i++) {
        EXPECT_TRUE(deq_table2[i]);
        EXPECT_EQ(deq_table2[i], TEST_TABLE_DATA_MAGIC) << "i :" << i;
    }

    bbq_ring_free(q);

    // 释放测试数据
    test_enqueue_table_destory(enq_table1, ENQ_TABLE1_CNT);
    for (int i = 0; i < ENQ_TABLE2_CNT; i++) {
        test_free(TEST_MODULE_DATA, enq_table2[i]);
    }
    test_free(TEST_MODULE_DATA, deq_table2);
    // 内存泄漏检测
    EXPECT_TRUE(bbq_malloc_free_equal());
    EXPECT_TRUE(test_malloc_free_equal());
}

// TEST(burst, ptr_table_cp_pointer) {
//     test_memory_counter_clear();
//     uint32_t ret1 = 0;
//     uint32_t ret2 = 0;
//     uint32_t ret3 = 0;
//     bbq_queue_s *q;

//     // 创建测试数据，3种数据类型，均将数据值入队列
//     uint16_t **enq_table1 = test_enqueue_table_create(ENQ_TABLE1_CNT);
//     uint16_t *enq_table2[ENQ_TABLE2_CNT] = {0};
//     for (int i = 0; i < ENQ_TABLE2_CNT; i++) {
//         enq_table2[i] = (uint16_t *)test_malloc(TEST_MODULE_DATA, sizeof(uint16_t));
//         *enq_table2[i] = TEST_TABLE_DATA_MAGIC;
//     }
//     uint16_t enq_table3[ENQ_TABLE3_CNT];
//     for (int i = 0; i < ENQ_TABLE3_CNT; i++) {
//         enq_table3[i] = TEST_TABLE_DATA_MAGIC;
//     }

//     uint16_t *deq_table1[BUF_CNT] = {0};
//     uint16_t *deq_table2[BUF_CNT] = {0};
//     uint16_t *deq_table3[BUF_CNT] = {0};

//     // 创建队列
//     q = bbq_ring_create(BUF_CNT, sizeof(uint16_t), BBQ_F_POLICY_RETRY_NEW | BBQ_F_COPY_POINTER);
//     EXPECT_TRUE(q);

//     // 批量入队(全成功)
//     ret1 = bbq_enqueue_burst(q, (void *)enq_table1, ENQ_TABLE1_CNT, BBQ_F_ARRAY_2D);
//     EXPECT_EQ(ret1, ENQ_TABLE1_CNT);

//     // 批量入队(全成功)
//     ret2 = bbq_enqueue_burst(q, (void *)enq_table2, ENQ_TABLE2_CNT, BBQ_F_ARRAY_2D);
//     EXPECT_EQ(ret2, ENQ_TABLE2_CNT);

//     // 批量入队(部分成功)
//     ret3 = bbq_enqueue_burst(q, (void *)enq_table3, ENQ_TABLE3_CNT, BBQ_F_ARRAY_1D);
//     EXPECT_EQ(ret3, BUF_CNT - ret1 - ret2);

//     /*------------------------------------------------------------------*/
//     // 出队列（全成功）
//     ret1 = bbq_dequeue_burst(q, (void *)deq_table1, ENQ_TABLE1_CNT);
//     EXPECT_EQ(ret1, ENQ_TABLE1_CNT);

//     // 出队列（全成功）
//     ret2 = bbq_dequeue_burst(q, (void *)deq_table2, ENQ_TABLE2_CNT);
//     EXPECT_EQ(ret2, ENQ_TABLE2_CNT);

//     // 出队列（部分成功）
//     ret3 = bbq_dequeue_burst(q, (void *)deq_table2, ENQ_TABLE3_CNT);
//     EXPECT_EQ(ret3, BUF_CNT - ret1 - ret2);

//     // 验证数据
//     for (uint32_t i = 0; i < ret1; i++) {
//         EXPECT_TRUE(deq_table1[i]);
//         EXPECT_EQ(*deq_table1[i], TEST_TABLE_DATA_MAGIC) << "i :" << i;
//     }

//     for (uint32_t i = 0; i < ret2; i++) {
//         EXPECT_TRUE(deq_table2[i]);
//         EXPECT_EQ(*deq_table2[i], TEST_TABLE_DATA_MAGIC) << "i :" << i;
//     }

//     for (uint32_t i = 0; i < ret3; i++) {
//         EXPECT_TRUE(deq_table3[i]);
//         EXPECT_EQ(*deq_table3[i], TEST_TABLE_DATA_MAGIC) << "i :" << i;
//     }

//     bbq_ring_free(q);

//     // 释放测试数据
//     test_enqueue_table_destory(enq_table1, ENQ_TABLE1_CNT);
//     for (int i = 0; i < ENQ_TABLE2_CNT; i++) {
//         test_free(TEST_MODULE_DATA, enq_table2[i]);
//     }
//     // 内存泄漏检测
//     EXPECT_TRUE(bbq_malloc_free_equal());
//     EXPECT_TRUE(test_malloc_free_equal());
// }