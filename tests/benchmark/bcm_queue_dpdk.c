/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-23 16:30:47
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */

#include "bcm_queue.h"
#include "common.h"

int bcm_queue_init_dpdk(bcm_cfg *cfg, bcm_queue_t *q) {
    char *argv[1] = {"bcm_dpdk"};

    // eal环境初始化，如接口、内存等
    if (rte_eal_init(1, argv) < 0) {
        rte_exit(EXIT_FAILURE, "Error with EAL init\n");
    }

    q->ring_type = BCM_RING_TYPE_DPDK;
    q->dpdk_ring = rte_ring_create("dpdk_ring", cfg->entries_cnt, rte_socket_id(), 0);

    q->ring_free_f = (ring_free_func)rte_ring_free;
    q->enqueue_f = (ring_enqueue_func)rte_ring_enqueue;
    q->dequeue_f = (ring_dequeue_func)rte_ring_dequeue;
}