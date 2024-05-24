/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-24 16:53:30
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#ifndef _BCM_QUEUE_H_
#define _BCM_QUEUE_H_
#include "rte_ring.h"
#include "test_common.h"

typedef void (*bcm_ring_free_f)(void *ring);
typedef int (*bcm_ring_enqueue_f)(void *ring, void *obj);
typedef int (*bcm_ring_dequeue_f)(void *ring, void **obj);

typedef struct {
    bcm_ring_type ring_type;
    bcm_ring_free_f ring_free_f;
    bcm_ring_enqueue_f enqueue_f;
    bcm_ring_dequeue_f dequeue_f;

    union {
        void *ring;
        struct rte_ring *dpdk_ring;
    };
} bcm_queue_t;

extern int bcm_queue_init(bcm_cfg *cfg, bcm_queue_t *q);
extern int bcm_queue_init_dpdk(bcm_cfg *cfg, bcm_queue_t *q);
extern int bcm_queue_init_bbq(bcm_cfg *cfg, bcm_queue_t *q);

#endif