/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-26 11:06:56
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#ifndef _BCM_QUEUE_H_
#define _BCM_QUEUE_H_
#include "bbq.h"
#include "rte_ring.h"
#include "test_common.h"

typedef void (*bcm_ring_free_f)(void* ring);
typedef int (*bcm_ring_enqueue_f)(void* ring, void* obj);
typedef int (*bcm_ring_dequeue_f)(void* ring, void** obj);
typedef int (*bcm_ring_full_f)(void* ring);
typedef int (*bcm_ring_empty_f)(void* ring);

typedef struct {
    test_ring_type ring_type;
    bcm_ring_free_f ring_free_f;
    bcm_ring_full_f ring_full_f;
    bcm_ring_empty_f ring_empty_f;
    bcm_ring_enqueue_f enqueue_f;
    bcm_ring_dequeue_f dequeue_f;

    union {
        void* ring;
        struct rte_ring* dpdk_ring;
        bbq_queue_s* bbq_ring;
    };
} bcm_queue_t;

extern int bcm_queue_init(test_cfg* cfg, bcm_queue_t* q);
extern int bcm_queue_init_dpdk(test_cfg* cfg, bcm_queue_t* q);
extern int bcm_queue_init_bbq(test_cfg* cfg, bcm_queue_t* q);
extern void bcm_queue_destory(bcm_queue_t* q);
#endif