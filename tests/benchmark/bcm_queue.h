/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-23 17:38:37
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#ifndef _BCM_QUEUE_H_
#define _BCM_QUEUE_H_
#include "common.h"
#include "rte_ring.h"

typedef void (*ring_free_func)(void *ring);
typedef int (*ring_enqueue_func)(void *ring, void *obj);
typedef int (*ring_dequeue_func)(void *ring, void **obj);

typedef struct {
    bcm_ring_type ring_type;
    ring_free_func ring_free_f;
    ring_enqueue_func enqueue_f;
    ring_dequeue_func dequeue_f;

    union {
        void *ring;
        struct rte_ring *dpdk_ring;
    };
} bcm_queue_t;

extern int bcm_queue_init(bcm_cfg *cfg, bcm_queue_t *q);
extern int bcm_queue_init_dpdk(bcm_cfg *cfg, bcm_queue_t *q);
extern int bcm_queue_init_bbq(bcm_cfg *cfg, bcm_queue_t *q);

#endif