/*
 * @Author: liuyu
 * @LastEditTime: 2024-07-07 20:52:05
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#include "bcm_queue.h"
#include "ringbuf.h"

static __rte_always_inline unsigned int
bcm_dpdk_ring_enqueue_burst(struct rte_ring *r, void **obj_table, uint32_t n, uint16_t thread_idx, uint32_t *wait_consumed) {
    UT_AVOID_WARNING(thread_idx);
    int ret = 0;

    if (wait_consumed) {
        unsigned int free_space = 0;
        ret = rte_ring_enqueue_burst(r, (void *const *)obj_table, n, &free_space);
        *wait_consumed = r->size - free_space - 1;
    } else {
        ret = rte_ring_enqueue_burst(r, (void *const *)obj_table, n, NULL);
    }

    return ret;
}

int ut_queue_init_dpdk(struct ut_cfg *cfg, struct ut_queue *q) {
    /* generate eal parameters */
    const char *eal_args[] = {"bcm_dpdk", "-n", "4", "--proc-type", "auto", "--no-huge", "-m", "2048"};
    if (rte_eal_init(RTE_DIM(eal_args), (char **)eal_args) < 0) {
        return -1;
    }

    q->ring_type = UT_RING_TYPE_DPDK;
    unsigned int flags = 0;
    if (cfg->ring.producer_cnt <= 1) {
        flags |= RING_F_SP_ENQ;
    } else {
        flags |= RING_F_MP_RTS_ENQ;
    }

    if (cfg->ring.consumer_cnt <= 1) {
        flags |= RING_F_SC_DEQ;
    } else {
        flags |= RING_F_MC_RTS_DEQ;
    }

    q->ring = (void *)rte_ring_create("dpdk_ring", cfg->ring.entries_cnt, rte_socket_id(), flags);
    if (q->ring == NULL) {
        return BBQ_ERR_INPUT_NULL;
    }

    q->ring_free_f = (ut_ring_free_f)rte_ring_free;
    q->enqueue_f = (ut_ring_enqueue_f)rte_ring_enqueue;
    q->dequeue_f = (ut_ring_dequeue_f)rte_ring_dequeue;
    q->enqueue_burst_f = (ut_enqueue_burst_f)bcm_dpdk_ring_enqueue_burst;
    q->dequeue_burst_f = (ut_dequeue_burst_f)rte_ring_dequeue_burst;

    return BBQ_OK;
}

unsigned char *rmind_buf;
uint16_t worker_cnt;
ringbuf_worker_t **rmind_workers;

void ut_queue_free_rmind(void *ring) {
    for (uint16_t i = 0; i < worker_cnt; i++) {
        ringbuf_unregister((ringbuf_t *)ring, rmind_workers[i]);
    }

    ut_free(UT_MODULE_RMIND, rmind_workers);
    ut_free(UT_MODULE_RMIND, rmind_buf);
    ut_free(UT_MODULE_RMIND, ring);
}

uint32_t ut_enqueue_burst_rmind(void *ring, void **obj_table, uint32_t n, uint16_t thread_idx, uint32_t *wait_consumed) {
    UT_AVOID_WARNING(wait_consumed);
    uint32_t cnt = 0;
    int ret = 0;
    size_t off = 0;
    void *obj = NULL;
    ringbuf_worker_t *w = (ringbuf_worker_t *)rmind_workers[thread_idx];
    size_t len = sizeof(uintptr_t);

    for (cnt = 0; cnt < n; cnt++) {
        obj = obj_table[cnt];
        uintptr_t uptr = (uintptr_t)obj;

        if ((ret = ringbuf_acquire(ring, w, len)) != -1) {
            off = (size_t)ret;
            memcpy(&rmind_buf[off], &uptr, len);
            ringbuf_produce(ring, w);
        } else {
            break;
        }
    }

    return cnt;
}

uint32_t ut_dequeue_burst_rmind(void *ring, void *obj_table, uint32_t n, uint32_t *wait_consumed) {
    UT_AVOID_WARNING(n);
    UT_AVOID_WARNING(wait_consumed);
    size_t len = 0;
    size_t off = 0;
    size_t per_size = sizeof(void *);
    void **table = (void **)obj_table;

    if ((len = ringbuf_consume(ring, &off)) != 0) {
        size_t rem = len;
        size_t i = 0;

        while (rem) {
            uintptr_t *data = (uintptr_t *)(&rmind_buf[off]);
            table[i] = (void *)(*data);
            i++;
            off += per_size;
            rem -= sizeof(void *);
        }
        ringbuf_release(ring, len);
        return i;
    }

    return 0;
}

int ut_queue_init_rmind(struct ut_cfg *cfg, struct ut_queue *q) {
    static size_t ringbuf_obj_size;
    worker_cnt = cfg->ring.producer_cnt + cfg->ring.consumer_cnt;

    ringbuf_get_sizes(worker_cnt, &ringbuf_obj_size, NULL);
    ringbuf_t *r = ut_malloc(UT_MODULE_RMIND, ringbuf_obj_size);
    if (r == NULL) {
        exit(-1);
    }

    size_t buf_size = sizeof(void *) * cfg->ring.entries_cnt;
    rmind_buf = ut_malloc(UT_MODULE_RMIND, buf_size);
    if (rmind_buf == NULL) {
        exit(-1);
    }
    ringbuf_setup(r, worker_cnt, buf_size);

    rmind_workers = ut_malloc(UT_MODULE_RMIND, sizeof(*rmind_workers) * worker_cnt);
    if (rmind_workers == NULL) {
        exit(-1);
    }
    for (uint32_t i = 0; i < worker_cnt; i++) {
        rmind_workers[i] = ringbuf_register(r, i);
        if (rmind_workers[i] == NULL) {
            exit(-1);
        }
    }

    q->ring = r;
    q->ring_free_f = (ut_ring_free_f)ut_queue_free_rmind;
    q->enqueue_f = NULL;
    q->dequeue_f = NULL;
    q->enqueue_burst_f = (ut_enqueue_burst_f)ut_enqueue_burst_rmind;
    q->dequeue_burst_f = (ut_dequeue_burst_f)ut_dequeue_burst_rmind;

    return 0;
}

int bcm_queue_init(struct ut_cfg *cfg, struct ut_queue *q) {
    if (cfg == NULL || q == NULL) {
        return BBQ_ERR_INPUT_NULL;
    }

    memset(q, 0, sizeof(*q));
    int ret = -1;
    q->ring_type = cfg->ring.ring_type;
    switch (q->ring_type) {
    case UT_RING_TYPE_DPDK:
        ret = ut_queue_init_dpdk(cfg, q);
        break;
    case UT_RING_TYPE_BBQ:
        ret = ut_queue_init_bbq(cfg, q);
        break;
    case UT_RING_TYPE_RMIND:
        ret = ut_queue_init_rmind(cfg, q);
        break;
    default:
        return BBQ_ERR;
    }

    return ret;
}
