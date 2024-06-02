/*
 * @Author: liuyu
 * @LastEditTime: 2024-06-01 01:17:15
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#include "gtest/gtest.h"
extern "C" {
#include "test_queue.h"
#include "ut.h"
extern bool bbq_malloc_free_equal();
extern bool test_malloc_free_equal();
extern void bbq_memory_info();
}

void expect_phead(bbq_queue_s *q, uint64_t idx, uint64_t vsn, int line) {
    EXPECT_EQ(bbq_idx(q, q->phead), idx) << "line: " << line;
    EXPECT_EQ(bbq_head_vsn(q, q->phead), vsn) << "line: " << line;
}

void expect_chead(bbq_queue_s *q, uint64_t idx, uint64_t vsn, int line) {
    EXPECT_EQ(bbq_idx(q, q->chead), idx) << "line: " << line;
    EXPECT_EQ(bbq_head_vsn(q, q->chead), vsn) << "line: " << line;
}

void expect_eq_allocated(bbq_queue_s *q, bbq_block_s *block, uint64_t off, uint64_t vsn, int line) {
    EXPECT_EQ(bbq_off(q, block->allocated), off) << "line: " << line;
    EXPECT_EQ(bbq_cur_vsn(q, block->allocated), vsn) << "line: " << line;
}

void expect_eq_committed(bbq_queue_s *q, bbq_block_s *block, uint64_t off, uint64_t vsn, int line) {
    EXPECT_EQ(bbq_off(q, block->committed), off) << "line: " << line;
    EXPECT_EQ(bbq_cur_vsn(q, block->committed), vsn) << "line: " << line;
}

void expect_eq_consumed(bbq_queue_s *q, bbq_block_s *block, uint64_t off, uint64_t vsn, int line) {
    EXPECT_EQ(bbq_off(q, block->consumed), off) << "line: " << line;
    EXPECT_EQ(bbq_cur_vsn(q, block->consumed), vsn) << "line: " << line;
}

void expect_eq_reserved(bbq_queue_s *q, bbq_block_s *block, uint64_t off, uint64_t vsn, int line) {
    EXPECT_EQ(bbq_off(q, block->reserved), off) << "line: " << line;
    EXPECT_EQ(bbq_cur_vsn(q, block->reserved), vsn) << "line: " << line;
}

// 初始化状态
TEST(head_cursor, init) {
    test_memory_counter_clear();

    int ret = 0;
    bbq_queue_s *q;
    uint32_t bn = 2;
    uint32_t bs = 4;
    int enqueue_data = TEST_DATA_MAGIC;

    q = bbq_ring_create_bnbs("ring", bn, bs, BBQ_RETRY_NEW);
    EXPECT_TRUE(q);

    // 1.初始化状态,除了第一个block外其他块的4个游标都指向最后一个条目
    EXPECT_EQ(q->phead, 0);
    EXPECT_EQ(q->chead, 0);

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

    bbq_ring_free(q);
    EXPECT_TRUE(bbq_malloc_free_equal());
    EXPECT_TRUE(test_malloc_free_equal());
}

void ut_produce_something(uint32_t produce_cnt) {
    int ret = 0;
    bbq_queue_s *q;
    uint32_t bn = 8;
    uint32_t bs = 4096;
    int enqueue_data = TEST_DATA_MAGIC;
    int *dequeue_data = NULL;

    EXPECT_GT(produce_cnt, 0);
    EXPECT_LE(produce_cnt, bs);

    q = bbq_ring_create_bnbs("ring", bn, bs, BBQ_RETRY_NEW);
    EXPECT_TRUE(q);

    // 生产produce_cnt
    for (uint32_t i = 0; i < produce_cnt; i++) {
        ret = bbq_enqueue(q, &enqueue_data);
        EXPECT_TRUE(ret == BBQ_OK);
    }

    EXPECT_EQ(q->phead, 0);
    EXPECT_EQ(q->chead, 0);
    expect_eq_allocated(q, &q->blocks[0], produce_cnt, 0, __LINE__);
    expect_eq_committed(q, &q->blocks[0], produce_cnt, 0, __LINE__);
    expect_eq_reserved(q, &q->blocks[0], 0, 0, __LINE__);
    expect_eq_consumed(q, &q->blocks[0], 0, 0, __LINE__);

    // 消费完
    for (uint32_t i = 0; i < produce_cnt; i++) {
        ret = bbq_dequeue(q, (void **)&dequeue_data);
        EXPECT_TRUE(ret == BBQ_OK);
        EXPECT_EQ(*dequeue_data, TEST_DATA_MAGIC);
    }

    EXPECT_EQ(q->phead, 0);
    EXPECT_EQ(q->chead, 0);
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

    bbq_ring_free(q);
}
// 在第一块内生产，然后被消费完
TEST(head_cursor, produce_something) {
    test_memory_counter_clear();

    ut_produce_something(1);
    ut_produce_something(567);
    ut_produce_something(789);
    ut_produce_something(4096);
    EXPECT_TRUE(bbq_malloc_free_equal());
    EXPECT_TRUE(test_malloc_free_equal());
}

void ut_produce_next_block(uint32_t over) {
    int ret = 0;
    bbq_queue_s *q;
    uint32_t bn = 8;
    uint32_t bs = 4096;
    uint32_t produce_cnt = bs + over;
    int enqueue_data = TEST_DATA_MAGIC;
    int *dequeue_data = NULL;

    EXPECT_GT(over, 0);
    EXPECT_LT(over, bs);

    q = bbq_ring_create_bnbs("ring", bn, bs, BBQ_RETRY_NEW);
    EXPECT_TRUE(q);

    // 生产至第二块的第一个entry
    for (uint32_t i = 0; i < produce_cnt; i++) {
        ret = bbq_enqueue(q, &enqueue_data);
        EXPECT_TRUE(ret == BBQ_OK);
    }

    EXPECT_EQ(q->chead, 0);
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
        ret = bbq_dequeue(q, (void **)&dequeue_data);
        EXPECT_TRUE(ret == BBQ_OK);
        EXPECT_EQ(*dequeue_data, TEST_DATA_MAGIC);
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

    bbq_ring_free(q);
}

// 第一块生产完毕，第二块生产了若干，然后被消费完
TEST(head_cursor, produce_next_block) {
    test_memory_counter_clear();

    ut_produce_next_block(1);
    ut_produce_next_block(123);
    ut_produce_next_block(456);
    ut_produce_next_block(4095);
    EXPECT_TRUE(bbq_malloc_free_equal());
    EXPECT_TRUE(test_malloc_free_equal());
}

void ut_produce_all_loop(uint32_t loop) {
    int ret = 0;
    bbq_queue_s *q;
    uint32_t bn = 8;
    uint32_t bs = 4096;
    uint32_t produce_cnt = bn * bs;
    int enqueue_data = TEST_DATA_MAGIC;
    int *dequeue_data = NULL;

    q = bbq_ring_create_bnbs("ring", bn, bs, BBQ_RETRY_NEW);
    EXPECT_TRUE(q);

    for (uint32_t cnt = 0; cnt < loop; cnt++) {
        // 所有entry生产完毕
        for (uint32_t i = 0; i < produce_cnt; i++) {
            ret = bbq_enqueue(q, &enqueue_data);
            EXPECT_TRUE(ret == BBQ_OK);
        }

        // 消费完
        for (uint32_t i = 0; i < produce_cnt; i++) {
            ret = bbq_dequeue(q, (void **)&dequeue_data);
            EXPECT_TRUE(ret == BBQ_OK);
            EXPECT_EQ(*dequeue_data, TEST_DATA_MAGIC);
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
}

// 完成多轮的满生产和满消费
TEST(head_cursor, produce_all_loop) {
    test_memory_counter_clear();

    ut_produce_all_loop(1);
    ut_produce_all_loop(10);
    ut_produce_all_loop(23);
    ut_produce_all_loop(1000);
    EXPECT_TRUE(bbq_malloc_free_equal());
    EXPECT_TRUE(test_malloc_free_equal());
}