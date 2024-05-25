/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-25 11:14:47
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */

#include "bcm_queue.h"
#include "bbq.h"

int bcm_queue_init_dpdk(test_cfg* cfg, bcm_queue_t* q) {
    char* argv[1] = {"bcm_dpdk"};

    // eal环境初始化，如接口、内存等
    if (rte_eal_init(1, argv) < 0) {
        rte_exit(EXIT_FAILURE, "Error with EAL init\n");
    }

    q->ring_type = TEST_RING_TYPE_DPDK;
    q->dpdk_ring = rte_ring_create("dpdk_ring", cfg->ring.entries_cnt, rte_socket_id(), RING_F_MP_RTS_ENQ | RING_F_MC_RTS_DEQ);
    if (q->dpdk_ring == NULL) {
        return BBQ_NULL_PTR;
    }

    q->ring_free_f = (bcm_ring_free_f)rte_ring_free;
    q->enqueue_f = (bcm_ring_enqueue_f)rte_ring_enqueue;
    q->dequeue_f = (bcm_ring_dequeue_f)rte_ring_dequeue;
    // q->ring_full_f = (bcm_ring_dequeue_f)rte_ring_full;
    // q->ring_empty_f = (bcm_ring_dequeue_f)rte_ring_empty;

    return BBQ_OK;
}

int bcm_queue_init_bbq(test_cfg* cfg, bcm_queue_t* q) {
    bbq_queue_s* ring;

    q->ring = (void*)bbq_ring_create("bbq_ring", cfg->ring.entries_cnt, 0, BBQ_RETRY_NEW);
    if (q->ring == NULL) {
        BBQ_ERR_LOG("bbq_ring_create failed");
        return BBQ_NULL_PTR;
    }

    q->ring_free_f = (bcm_ring_free_f)bbq_ring_free;
    q->enqueue_f = (bcm_ring_enqueue_f)bbq_enqueue;
    q->dequeue_f = (bcm_ring_dequeue_f)bbq_dequeue;

    return 0;
}

int bcm_queue_init(test_cfg* cfg, bcm_queue_t* q) {
    if (cfg == NULL || q == NULL) {
        return BBQ_NULL_PTR;
    }

    int ret = -1;
    switch (cfg->ring.ring_type) {
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

void bcm_queue_destory(bcm_queue_t* q) {
    if (likely(q != NULL && q->ring_free_f != NULL)) {
        q->ring_free_f(q->ring);
    }
}