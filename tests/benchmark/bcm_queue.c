/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-23 18:50:36
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */

#include "bcm_queue.h"
#include "bbq.h"

int bcm_queue_init(bcm_cfg *cfg, bcm_queue_t *q) {
    if (cfg == NULL || q == NULL) {
        return BBQ_NULL_PTR;
    }

    int ret = -1;
    switch (cfg->ring_type) {
    case BCM_RING_TYPE_DPDK:
        ret = bcm_queue_init_dpdk(cfg, q);
        break;
    case BCM_RING_TYPE_BBQ:
        ret = bcm_queue_init_bbq(cfg, q);
        break;
    default:
        return BBQ_UNKNOWN_TYPE;
    }

    return ret;
}

void bcm_queue_destory(bcm_queue_t *q) {
    if (likely(q != NULL && q->ring_free_f != NULL)) {
        q->ring_free_f(q->ring);
    }
}