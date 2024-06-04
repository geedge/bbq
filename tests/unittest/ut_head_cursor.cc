/*
 * @Author: liuyu
 * @LastEditTime: 2024-06-04 22:08:27
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#include "gtest/gtest.h"
extern "C" {
#include "test_queue.h"
#include "ut.h"
extern bool bbq_malloc_free_equal();
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

    q = bbq_ring_create_bnbs(bn, bs, sizeof(int), BBQ_RETRY_NEW);
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
    int dequeue_data = 0;

    EXPECT_GT(produce_cnt, 0);
    EXPECT_LE(produce_cnt, bs);

    q = bbq_ring_create_bnbs(bn, bs, sizeof(int), BBQ_RETRY_NEW);
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
        ret = bbq_dequeue(q, &dequeue_data);
        EXPECT_TRUE(ret == BBQ_OK);
        EXPECT_EQ(dequeue_data, TEST_DATA_MAGIC);
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
    int dequeue_data = 0;

    EXPECT_GT(over, 0);
    EXPECT_LT(over, bs);

    q = bbq_ring_create_bnbs(bn, bs, sizeof(int), BBQ_RETRY_NEW);
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
        ret = bbq_dequeue(q, &dequeue_data);
        EXPECT_TRUE(ret == BBQ_OK);
        EXPECT_EQ(dequeue_data, TEST_DATA_MAGIC);
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
    int dequeue_data = 0;

    q = bbq_ring_create_bnbs(bn, bs, sizeof(int), BBQ_RETRY_NEW);
    EXPECT_TRUE(q);

    for (uint32_t cnt = 0; cnt < loop; cnt++) {
        // 所有entry生产完毕
        for (uint32_t i = 0; i < produce_cnt; i++) {
            ret = bbq_enqueue(q, &enqueue_data);
            EXPECT_TRUE(ret == BBQ_OK);
        }

        // 消费完
        for (uint32_t i = 0; i < produce_cnt; i++) {
            ret = bbq_dequeue(q, &dequeue_data);
            EXPECT_TRUE(ret == BBQ_OK);
            EXPECT_EQ(dequeue_data, TEST_DATA_MAGIC);
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

TEST(boundary, retry_new_full_empty) {
    test_memory_counter_clear();
    int ret = 0;
    uint32_t entries_cnt = 4096;
    uint32_t loop = 1000;
    bbq_queue_s *q;

    int *data = (int *)test_malloc(TEST_MODULE_UTEST, sizeof(*data) * entries_cnt);
    int tmp_data = 0;
    EXPECT_TRUE(data);

    q = bbq_ring_create(entries_cnt, sizeof(int), BBQ_RETRY_NEW);
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
            EXPECT_EQ(tmp_data, data[j]);
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
    EXPECT_GE(merge.consumer.run_times, cfg.run.deq_per_consumer * cfg.ring.consumer_cnt);
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

void debug_head_print(bbq_queue_s *q) {
    printf("phead vsn:%d idx:%d\n", bbq_head_vsn(q, q->phead), bbq_idx(q, q->phead));
    printf("chead vsn:%d idx:%d\n", bbq_head_vsn(q, q->chead), bbq_idx(q, q->chead));
}

void debug_block_print(bbq_queue_s *q) {
    for (int i = 0; i < q->bn; i++) {
        printf("[%d]zzzz allocated:vsn:%d off:%d\n", i, bbq_cur_vsn(q, q->blocks[i].allocated.load()), bbq_off(q, q->blocks[i].allocated.load()));
        printf("[%d]zzzz committed:%d off:%d\n", i, bbq_cur_vsn(q, q->blocks[i].committed.load()), bbq_off(q, q->blocks[i].committed.load()));
        printf("[%d]zzzz reserved:%d off:%d\n", i, bbq_cur_vsn(q, q->blocks[i].reserved.load()), bbq_off(q, q->blocks[i].reserved.load()));
        printf("[%d]zzzz consumed:%d off:%d\n\n", i, bbq_cur_vsn(q, q->blocks[i].consumed.load()), bbq_off(q, q->blocks[i].consumed.load()));
    }
}

TEST(boundary, drop_old_full_empty1) {
    test_memory_counter_clear();
    int ret = 0;
    uint32_t bn = 2;
    uint32_t bs = 4;
    uint32_t over_cnt = 3;
    uint32_t loop = 1000;
    bbq_queue_s *q;

    int tmp_data = 0;
    q = bbq_ring_create_bnbs(bn, bs, sizeof(int), BBQ_DROP_OLD);
    EXPECT_TRUE(q);

    for (uint32_t j = 0; j < loop; j++) {
        // 入满队列
        for (uint32_t i = 0; i < bn * bs; i++) {
            ret = bbq_enqueue(q, &i);
            EXPECT_TRUE(ret == BBQ_OK) << "ret " << ret;
        }

        // 全出队
        for (uint32_t i = 0; i < bn * bs; i++) {
            ret = bbq_dequeue(q, &tmp_data);
            EXPECT_TRUE(ret == BBQ_OK) << "ret " << ret;
            EXPECT_EQ(tmp_data, i);
        }

        // 空队再出队，失败
        for (uint32_t i = 0; i < bn * bs; i++) {
            ret = bbq_dequeue(q, &tmp_data);
            EXPECT_TRUE(ret == BBQ_QUEUE_EMPTY) << "ret " << ret;
        }

        expect_phead(q, bn - 1, j, __LINE__);
        expect_chead(q, bn - 1, j, __LINE__);
        for (uint32_t i = 0; i < q->bn; i++) {
            expect_eq_committed(q, &q->blocks[i], q->bs, i == 0 ? j : j + 1, __LINE__);
            expect_eq_allocated(q, &q->blocks[i], q->bs, i == 0 ? j : j + 1, __LINE__);
            expect_eq_reserved(q, &q->blocks[i], q->bs, i == 0 ? j : j + 1, __LINE__);
            EXPECT_EQ(q->blocks[i].consumed.load(), 0);
        }
    }

    bbq_ring_free(q);

    EXPECT_TRUE(bbq_malloc_free_equal());
    EXPECT_TRUE(test_malloc_free_equal());
}

TEST(boundary, drop_old_full_empty2) {
    test_memory_counter_clear();
    int ret = 0;
    uint32_t bn = 2;
    uint32_t bs = 4;
    uint32_t loop = 1000;
    uint32_t over_cnt = bs + 2;
    bbq_queue_s *q;

    EXPECT_EQ(over_cnt / bs, 1);

    int tmp_data = 0;
    q = bbq_ring_create_bnbs(bn, bs, sizeof(int), BBQ_DROP_OLD);
    EXPECT_TRUE(q);

    // 入满队列，再入over_cnt
    for (uint32_t i = 0; i < bn * bs * loop + over_cnt; i++) {
        ret = bbq_enqueue(q, &i);
        EXPECT_TRUE(ret == BBQ_OK) << "ret " << ret;
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
        EXPECT_EQ(q->blocks[i].consumed.load(), 0);
    }

    // 队列中的数据全出队
    for (uint32_t i = 0; i < over_cnt - bs; i++) {
        ret = bbq_dequeue(q, &tmp_data);
        EXPECT_TRUE(ret == BBQ_OK) << "ret " << ret;
    }

    for (uint32_t i = 0; i < bn * bs; i++) {
        ret = bbq_dequeue(q, &tmp_data);
        EXPECT_TRUE(ret == BBQ_QUEUE_EMPTY) << "ret " << ret;
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
        EXPECT_EQ(q->blocks[i].consumed.load(), 0);
    }

    bbq_ring_free(q);
    EXPECT_TRUE(bbq_malloc_free_equal());
    EXPECT_TRUE(test_malloc_free_equal());
}