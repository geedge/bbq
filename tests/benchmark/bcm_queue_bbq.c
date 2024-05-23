/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-23 18:03:51
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#include "bbq.h"
#include "bcm_queue.h"
#include "common.h"
#include <math.h>

int bcm_queue_init_bbq(bcm_cfg *cfg, bcm_queue_t *q) {
    BlockBoundedQueue_s *ring;

    q->ring = (void *)bbq_ring_create("bbq_ring", cfg->entries_cnt, 0, RETRY_NEW);
    if (q->ring == NULL) {
        BBQ_ERR_LOG("bbq_ring_create failed");
        return BBQ_NULL_PTR;
    }

    q->ring_free_f = (ring_free_func)bbq_ring_free;
    q->enqueue_f = (ring_enqueue_func)bbq_enqueue;
    q->dequeue_f = (ring_dequeue_func)bbq_dequeue;

    return 0;
}