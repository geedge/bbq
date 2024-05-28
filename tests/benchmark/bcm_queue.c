/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-28 18:03:23
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */

#include "bcm_queue.h"
#include "bbq.h"

int bcm_queue_init_dpdk(test_cfg *cfg, bcm_queue_t *q) {
    char *argv[1] = {"bcm_dpdk"};

    // eal环境初始化，如接口、内存等
    if (rte_eal_init(1, argv) < 0) {
        rte_exit(EXIT_FAILURE, "Error with EAL init\n");
    }

    q->ring_type = TEST_RING_TYPE_DPDK;
    q->ring = (void *)rte_ring_create("dpdk_ring", cfg->ring.entries_cnt, rte_socket_id(), RING_F_MP_RTS_ENQ | RING_F_MC_RTS_DEQ);
    if (q->dpdk_ring == NULL) {
        return BBQ_NULL_PTR;
    }

    q->ring_free_f = (bcm_ring_free_f)rte_ring_free;
    q->enqueue_f = (bcm_ring_enqueue_f)rte_ring_enqueue;
    q->dequeue_f = (bcm_ring_dequeue_f)rte_ring_dequeue;

    return BBQ_OK;
}

int bbq_rint_ret_full(int ret) {
    return ret == BBQ_QUEUE_FULL;
}

int bcm_queue_init_bbq(test_cfg *cfg, bcm_queue_t *q) {
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

    q->ring_free_f = (bcm_ring_free_f)bbq_ring_free;
    q->enqueue_f = (bcm_ring_enqueue_f)bbq_enqueue;
    q->dequeue_f = (bcm_ring_dequeue_f)bbq_dequeue;

    return 0;
}

int bcm_queue_init(test_cfg *cfg, bcm_queue_t *q) {
    if (cfg == NULL || q == NULL) {
        return BBQ_NULL_PTR;
    }

    int ret = -1;
    q->ring_type = cfg->ring.ring_type;
    switch (q->ring_type) {
    case TEST_RING_TYPE_DPDK:
        ret = bcm_queue_init_dpdk(cfg, q);
        break;
    case TEST_RING_TYPE_BBQ:
        ret = bcm_queue_init_bbq(cfg, q);
        break;
    default:
        return BBQ_UNKNOWN_TYPE;
    }

    return ret;
}

void bcm_queue_destory(bcm_queue_t *q) {
    if (likely(q != NULL && q->ring_free_f != NULL)) {
        q->ring_free_f(q->ring);
    }
}