/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-24 16:27:39
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#include "bbq.h"
#include "bcm_common.h"
#include "test_common.h"

#include "bcm_queue.h"
#include <math.h>

int bcm_queue_init_bbq(bcm_cfg *cfg, bcm_queue_t *q) {
    bbq_queue_s *ring;

    q->ring = (void *)bbq_ring_create("bbq_ring", cfg->entries_cnt, 0, BBQ_RETRY_NEW);
    if (q->ring == NULL) {
        BBQ_ERR_LOG("bbq_ring_create failed");
        return BBQ_NULL_PTR;
    }

    q->ring_free_f = (bcm_ring_free_f)bbq_ring_free;
    q->enqueue_f = (bcm_ring_enqueue_f)bbq_enqueue;
    q->dequeue_f = (bcm_ring_dequeue_f)bbq_dequeue;

    return 0;
}