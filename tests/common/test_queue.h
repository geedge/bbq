/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-31 10:23:08
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#ifndef _TEST_QUEUE_H_
#define _TEST_QUEUE_H_
#include "bbq.h"
#include "test_mix.h"

typedef void (*test_ring_free_f)(void *ring);
typedef int (*test_ring_enqueue_f)(void *ring, void *obj);
typedef int (*test_ring_dequeue_f)(void *ring, void **obj);

typedef struct {
    test_ring_type ring_type;
    test_ring_free_f ring_free_f;
    test_ring_enqueue_f enqueue_f;
    test_ring_dequeue_f dequeue_f;

    union {
        void *ring;
        struct rte_ring *dpdk_ring;
        bbq_queue_s *bbq_ring;
    };
} test_queue_s;

extern int test_queue_init_bbq(test_cfg *cfg, test_queue_s *q);
extern void test_queue_destory(test_queue_s *q);
#endif