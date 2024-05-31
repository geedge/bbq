/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-31 10:52:23
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#include "bcm_queue.h"

int test_queue_init_dpdk(test_cfg *cfg, test_queue_s *q) {
    char *argv[1] = {"bcm_dpdk"};

    // eal环境初始化，如接口、内存等
    if (rte_eal_init(1, argv) < 0) {
        rte_exit(EXIT_FAILURE, "Error with EAL init\n");
    }

    q->ring_type = TEST_RING_TYPE_DPDK;
    unsigned int flags = 0;
    if (cfg->ring.producer_cnt <= 1) {
        flags |= RING_F_SP_ENQ;
    } else {
        flags |= RING_F_MP_RTS_ENQ;
    }

    if (cfg->ring.consumer_cnt <= 1) {
        flags |= RING_F_SC_DEQ;
    } else {
        flags |= RING_F_MC_RTS_DEQ;
    }

    q->ring = (void *)rte_ring_create("dpdk_ring", cfg->ring.entries_cnt, rte_socket_id(), RING_F_MP_RTS_ENQ | RING_F_MC_RTS_DEQ);
    if (q->ring == NULL) {
        return BBQ_NULL_PTR;
    }

    q->ring_free_f = (test_ring_free_f)rte_ring_free;
    q->enqueue_f = (test_ring_enqueue_f)rte_ring_enqueue;
    q->dequeue_f = (test_ring_dequeue_f)rte_ring_dequeue;

    return BBQ_OK;
}

int bcm_queue_init(test_cfg *cfg, test_queue_s *q) {
    if (cfg == NULL || q == NULL) {
        return BBQ_NULL_PTR;
    }

    int ret = -1;
    q->ring_type = cfg->ring.ring_type;
    switch (q->ring_type) {
    case TEST_RING_TYPE_DPDK:
        ret = test_queue_init_dpdk(cfg, q);
        break;
    case TEST_RING_TYPE_BBQ:
        ret = test_queue_init_bbq(cfg, q);
        break;
    default:
        return BBQ_UNKNOWN_TYPE;
    }

    return ret;
}
