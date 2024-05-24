/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-24 14:13:49
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */

#include "bbq_errno.h"
#include "test_common.h"

#include "bcm_queue.h"

int bcm_queue_init_dpdk(bcm_cfg *cfg, bcm_queue_t *q) {
    char *argv[1] = {"bcm_dpdk"};

    // eal环境初始化，如接口、内存等
    if (rte_eal_init(1, argv) < 0) {
        rte_exit(EXIT_FAILURE, "Error with EAL init\n");
    }

    q->ring_type = BCM_RING_TYPE_DPDK;
    q->dpdk_ring = rte_ring_create("dpdk_ring", cfg->entries_cnt, rte_socket_id(), 0);
    if (q->dpdk_ring == NULL) {
        return BBQ_NULL_PTR;
    }

    q->ring_free_f = (bcm_ring_free_f)rte_ring_free;
    q->enqueue_f = (bcm_ring_enqueue_f)rte_ring_enqueue;
    q->dequeue_f = (bcm_ring_dequeue_f)rte_ring_dequeue;

    return BBQ_OK;
}