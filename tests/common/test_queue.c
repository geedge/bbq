/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-31 10:17:55
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */

#include "test_queue.h"
#include "bbq.h"
#include "test_mix.h"

int test_queue_init_bbq(test_cfg *cfg, test_queue_s *q) {
    bbq_queue_s *ring;

    if (cfg->ring.block_count == 0) {
        q->bbq_ring = bbq_ring_create_with_socket("bbq_ring", cfg->ring.entries_cnt, 0, BBQ_RETRY_NEW);
    } else {
        q->bbq_ring = bbq_ring_create_bnbs_with_socket("bbq_ring", cfg->ring.block_count,
                                                       cfg->ring.entries_cnt / cfg->ring.block_count,
                                                       0, BBQ_RETRY_NEW);
    }

    if (q->ring == NULL) {
        BBQ_ERR_LOG("bbq_ring_create_with_socket failed");
        return BBQ_NULL_PTR;
    }

    BBQ_INFO_LOG("block number:%lu size:%lu", q->bbq_ring->bn, q->bbq_ring->bs);

    q->ring_free_f = (test_ring_free_f)bbq_ring_free;
    q->enqueue_f = (test_ring_enqueue_f)bbq_enqueue;
    q->dequeue_f = (test_ring_dequeue_f)bbq_dequeue;

    return 0;
}

void test_queue_destory(test_queue_s *q) {
    if (q != NULL && q->ring_free_f != NULL) {
        q->ring_free_f(q->ring);
    }
}