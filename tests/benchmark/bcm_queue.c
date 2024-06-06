/*
 * @Author: liuyu
 * @LastEditTime: 2024-06-06 17:36:40
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#include "bcm_queue.h"
#include "ringbuf.h"

static __rte_always_inline unsigned int
bcm_ring_enqueue_burst(struct rte_ring *r, void *obj_table, size_t n, unsigned int flags) {
    return rte_ring_enqueue_burst(r, (void *const *)obj_table, n, NULL);
}

static __rte_always_inline unsigned int
bcm_ring_dequeue_burst(struct rte_ring *r, void *obj_table, unsigned int n) {
    return rte_ring_dequeue_burst(r, (void **)obj_table, n, NULL);
}

int test_queue_init_dpdk(test_cfg *cfg, test_queue_s *q) {
    char *argv[1] = {"bcm_dpdk"};

    // eal环境初始化，如接口、内存等
    if (rte_eal_init(1, argv) < 0) {
        rte_exit(EXIT_FAILURE, "Error with EAL init\n");
    }

    q->ring_type = TEST_RING_TYPE_DPDK;
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

    q->ring = (void *)rte_ring_create("dpdk_ring", cfg->ring.entries_cnt, rte_socket_id(), RING_F_MP_RTS_ENQ | RING_F_MC_RTS_DEQ);
    if (q->ring == NULL) {
        return BBQ_NULL_PTR;
    }

    q->ring_free_f = (test_ring_free_f)rte_ring_free;
    q->enqueue_f = (test_ring_enqueue_f)rte_ring_enqueue;
    q->dequeue_f = (test_ring_dequeue_f)rte_ring_dequeue;
    q->enqueue_burst_f = (test_enqueue_burst_f)bcm_ring_enqueue_burst;
    q->dequeue_burst_f = (test_dequeue_burst_f)bcm_ring_dequeue_burst;

    return BBQ_OK;
}

unsigned char *rmind_buf;
uint16_t worker_cnt;
ringbuf_worker_t **rmind_workers;

void test_queue_free_rmind(void *ring) {
    for (uint16_t i = 0; i < worker_cnt; i++) {
        ringbuf_unregister((ringbuf_t *)ring, rmind_workers[i]);
    }

    test_free(TEST_MODULE_RMIND, rmind_workers);
    test_free(TEST_MODULE_RMIND, rmind_buf);
    test_free(TEST_MODULE_RMIND, ring);
}

uint32_t test_enqueue_burst_rmind(void *ring, void *obj_table, size_t n, unsigned int flags, uint16_t thread_idx) {
    uint32_t cnt = 0;
    int ret = 0;
    size_t off = 0;
    void *obj = NULL;
    ringbuf_worker_t *w = (ringbuf_worker_t *)rmind_workers[thread_idx];
    size_t len = sizeof(uintptr_t);

    for (cnt = 0; cnt < n; cnt++) {
        obj = ((void **)obj_table)[cnt];
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

uint32_t test_dequeue_burst_rmind(void *ring, void *obj_table, uint32_t n) {
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

int test_queue_init_rmind(test_cfg *cfg, test_queue_s *q) {
    static size_t ringbuf_obj_size;
    worker_cnt = cfg->ring.producer_cnt + cfg->ring.consumer_cnt;

    ringbuf_get_sizes(worker_cnt, &ringbuf_obj_size, NULL);
    ringbuf_t *r = test_malloc(TEST_MODULE_RMIND, ringbuf_obj_size);
    if (r == NULL) {
        exit(-1);
    }

    size_t buf_size = sizeof(void *) * cfg->ring.entries_cnt;
    rmind_buf = test_malloc(TEST_MODULE_RMIND, buf_size);
    if (rmind_buf == NULL) {
        exit(-1);
    }
    ringbuf_setup(r, worker_cnt, buf_size);

    rmind_workers = test_malloc(TEST_MODULE_RMIND, sizeof(*rmind_workers) * worker_cnt);
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
    q->ring_free_f = (test_ring_free_f)test_queue_free_rmind;
    q->enqueue_f = NULL;
    q->dequeue_f = NULL;
    q->enqueue_burst_f = (test_enqueue_burst_f)test_enqueue_burst_rmind;
    q->dequeue_burst_f = (test_dequeue_burst_f)test_dequeue_burst_rmind;

    return 0;
}

int bcm_queue_init(test_cfg *cfg, test_queue_s *q) {
    if (cfg == NULL || q == NULL) {
        return BBQ_NULL_PTR;
    }

    memset(q, 0, sizeof(*q));
    int ret = -1;
    q->ring_type = cfg->ring.ring_type;
    switch (q->ring_type) {
    case TEST_RING_TYPE_DPDK:
        ret = test_queue_init_dpdk(cfg, q);
        break;
    case TEST_RING_TYPE_BBQ:
        ret = test_queue_init_bbq(cfg, q);
        break;
    case TEST_RING_TYPE_RMIND:
        ret = test_queue_init_rmind(cfg, q);
        break;
    default:
        return BBQ_UNKNOWN_TYPE;
    }

    return ret;
}
