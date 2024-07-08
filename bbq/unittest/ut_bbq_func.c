/*
 * @Author: liuyu
 * @LastEditTime: 2024-07-07 22:34:03
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */

#include "ut_bbq_func.h"
#include "bbq.h"
#include <pthread.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/time.h>
#include <unistd.h>

struct ut_memory {
    aotmic_uint64 malloc_cnt;
    aotmic_uint64 free_cnt;
};

struct ut_memory ut_memory_g[UT_MODULE_MAX] = {0};

char *ut_ring_type_map[UT_RING_TYPE_MAX] = {
    [UT_RING_TYPE_BBQ] = UT_RING_TYPE_BBQ_STR,
    [UT_RING_TYPE_DPDK] = UT_RING_TYPE_DPDK_STR,
    [UT_RING_TYPE_RMIND] = UT_RING_TYPE_RMIND_STR,
};

void *ut_malloc(enum ut_module module, size_t size) {
    void *ptr = malloc(size);
    if (ptr != NULL) {
        atomic_fetch_add(&ut_memory_g[module].malloc_cnt, 1);
    }

    return ptr;
}

void ut_free(enum ut_module module, void *ptr) {
    if (ptr != NULL) {
        atomic_fetch_add(&ut_memory_g[module].free_cnt, 1);
    }
    free(ptr);
}

bool ut_malloc_free_equal() {
    bool ret = true;
    for (int i = 0; i < UT_MODULE_MAX; i++) {
        uint64_t malloc_cnt = atomic_load(&ut_memory_g[i].malloc_cnt);
        uint64_t free_cnt = atomic_load(&ut_memory_g[i].free_cnt);
        if (malloc_cnt != free_cnt) {
            UT_ERR_LOG("[module:%d] malloc:%lu free:%lu, test malloc-free not equal\n", i, malloc_cnt, free_cnt);
            ret = false;
        }
    }

    return ret;
}

void ut_memory_counter_clear() {
    memset(ut_memory_g, 0, sizeof(ut_memory_g));
}

void ut_memory_counter_print() {
    for (int i = 0; i < UT_MODULE_MAX; i++) {
        uint64_t malloc_cnt = atomic_load(&ut_memory_g[i].malloc_cnt);
        uint64_t free_cnt = atomic_load(&ut_memory_g[i].free_cnt);
        if (malloc_cnt == 0 && free_cnt == 0) {
            continue;
        }

        UT_INFO_LOG("[%d]test malloc:%lu free:%lu", i,
                    atomic_load(&ut_memory_g[i].malloc_cnt),
                    atomic_load(&ut_memory_g[i].free_cnt));
    }

    if (ut_malloc_free_equal()) {
        UT_INFO_LOG("all memory free");
    } else {
        UT_ERR_LOG("memory not all free");
    }
}

struct ut_metric ut_clock_time_get() {
    struct ut_metric metric = {0};
    clock_gettime(CLOCK_REALTIME, &metric.timestamp); // 系统实时时间,随系统实时时间改变而改变
    return metric;
}

uint64_t ut_clock_time_to_ns(struct ut_metric *metric) {
    return metric->timestamp.tv_nsec + metric->timestamp.tv_sec * 1000 * 1000 * 1000;
}

double ut_clock_time_to_double(struct ut_metric *metric) {
    return metric->timestamp.tv_sec +
           metric->timestamp.tv_nsec * 1.0 / 1000 / 1000 / 1000;
}

bool ut_clock_time_is_zero(struct ut_metric *metric) {
    return metric->timestamp.tv_sec == 0 && metric->timestamp.tv_nsec == 0;
}

bool ut_timespec_is_after(const struct timespec *a, const struct timespec *b) {
    if (a->tv_sec > b->tv_sec) {
        // a的秒数大于b的秒数，所以a在b之后
        return true;
    } else if (a->tv_sec == b->tv_sec && a->tv_nsec > b->tv_nsec) {
        // a和b的秒数相同，但a的纳秒数大于b的纳秒数，所以a在b之后
        return true;
    }
    // 否则，a不在b之后
    return false;
}

struct ut_metric ut_clock_time_sub(struct ut_metric now, struct ut_metric last) {
    struct ut_metric diff = {
        .timestamp.tv_sec = now.timestamp.tv_sec - last.timestamp.tv_sec,
        .timestamp.tv_nsec = now.timestamp.tv_nsec - last.timestamp.tv_nsec,
    };

    if (now.timestamp.tv_nsec > last.timestamp.tv_nsec) {
        diff.timestamp.tv_nsec = now.timestamp.tv_nsec - last.timestamp.tv_nsec;
    } else {
        // 从秒借位
        diff.timestamp.tv_sec--;
        diff.timestamp.tv_nsec = 1000 * 1000 * 1000 + now.timestamp.tv_nsec - last.timestamp.tv_nsec;
    }

    return diff;
}

enum ut_workload ut_workload_str2enum(const char *workload) {
    if (strcmp(workload, "simple") == 0) {
        return UT_WORKLOAD_SIMPLE;
    } else if (strcmp(workload, "complex") == 0) {
        return UT_WORKLOAD_COMPLEX;
    }

    return UT_WORKLOAD_MAX;
}

enum ut_ring_type ut_ring_type_str2enum(const char *ring_type) {
    if (strcmp(ring_type, UT_RING_TYPE_BBQ_STR) == 0) {
        return UT_RING_TYPE_BBQ;
    } else if (strcmp(ring_type, UT_RING_TYPE_DPDK_STR) == 0) {
        return UT_RING_TYPE_DPDK;
    } else if (strcmp(ring_type, UT_RING_TYPE_RMIND_STR) == 0) {
        return UT_RING_TYPE_RMIND;
    }

    return UT_RING_TYPE_MAX;
}

char *ut_ring_type_enum2str(enum ut_ring_type ring_type) {
    if (ring_type >= UT_RING_TYPE_MAX) {
        return "unknown";
    } else {
        return ut_ring_type_map[ring_type];
    }
}

int ut_setaffinity(int core_id) {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(core_id, &mask);

    if (pthread_setaffinity_np(pthread_self(), sizeof(mask), &mask) == -1) {
        UT_ERR_LOG("pthread_setaffinity_np erro\n");
        return BBQ_ERR;
    }

    return BBQ_OK;
}

void *ut_malloc_def_callback(int32_t socket_id __attribute__((unused)), size_t size) {
    return malloc(size);
    // return aligned_alloc(BBQ_CACHE_LINE, size);
}

void ut_free_def_callback(void *ptr,
                          size_t size __attribute__((unused))) {
    free(ptr);
}

uint32_t ut_bbq_enqueue_burst(void *ring, void **obj_table, uint32_t n, uint16_t thread_idx, uint32_t *wait_consumed) {
    UT_AVOID_WARNING(thread_idx);
    return bbq_enqueue_burst(ring, obj_table, n, wait_consumed);
}

int ut_queue_init_bbq(struct ut_cfg *cfg, struct ut_queue *q) {
#if 0
    // 开启了BBQ_F_ENABLE_STAT 会导致性能下降
    unsigned int flags = BBQ_F_RETRY_NEW | BBQ_F_ENABLE_STAT;
#else
    unsigned int flags = BBQ_F_RETRY_NEW;
#endif

    if (cfg->ring.producer_cnt <= 1) {
        flags |= BBQ_F_SP_ENQ;
    }

    if (cfg->ring.consumer_cnt <= 1) {
        flags |= BBQ_F_SC_DEQ;
    }

    if (cfg->ring.block_count == 0) {
        q->ring = bbq_create("ut_bbq", cfg->ring.entries_cnt, BBQ_SOCKET_ID_ANY, flags,
                             ut_malloc_def_callback, ut_free_def_callback);
    } else {
        q->ring = bbq_create_with_bnbs("ut_bbq", cfg->ring.block_count,
                                       cfg->ring.entries_cnt / cfg->ring.block_count,
                                       BBQ_SOCKET_ID_ANY, flags, ut_malloc_def_callback, ut_free_def_callback);
    }

    if (q->ring == NULL) {
        UT_ERR_LOG("bbq create queue failed");
        return BBQ_ERR_INPUT_NULL;
    }

    q->ring_free_f = (ut_ring_free_f)bbq_destory;
    q->enqueue_f = (ut_ring_enqueue_f)bbq_enqueue;
    q->dequeue_f = (ut_ring_dequeue_f)bbq_dequeue;
    q->enqueue_burst_f = (ut_enqueue_burst_f)ut_bbq_enqueue_burst;
    q->dequeue_burst_f = (ut_dequeue_burst_f)bbq_dequeue_burst;
    return 0;
}

void ut_queue_destory(struct ut_queue *q) {
    if (q != NULL && q->ring_free_f != NULL) {
        q->ring_free_f(q->ring);
    }
}

bool ut_all_producer_exit(struct ut_info_s *ut_info) {
    return atomic_load(&ut_info->ctl.producer_exit) == ut_info->cfg.ring.producer_cnt;
}

void ut_wait_all_threads_ready(struct ut_ctl *ctl) {
    pthread_barrier_wait(&ctl->all_threads_start);
    UT_DBG_LOG("thread init done!");
}

struct ut_exit_data *ut_exit_data_create(struct ut_thread_arg *t_arg) {
    struct ut_exit_data *exit_data = (struct ut_exit_data *)ut_malloc(UT_MODULE_COMMON, sizeof(struct ut_exit_data));
    if (exit_data == NULL) {
        UT_ERR_LOG("malloc failed");
        exit(-1);
    }

    size_t size = t_arg->info->cfg.ring.entries_cnt;
    exit_data->simple_data_cnt = size;
    exit_data->simple_data = ut_data_create(size, UT_DATA_MAGIC_TYPE);

    if (exit_data->simple_data == NULL) {
        UT_ERR_LOG("malloc failed");
        exit(-1);
    }
    exit_data->arg = t_arg;
    exit_data->thread_id = pthread_self();
    exit_data->latency_ns = 0;
    exit_data->data_error_cnt = 0;

    return exit_data;
}

void ut_exit_data_destory(struct ut_exit_data *data) {
    ut_data_destory(data->simple_data, data->simple_data_cnt);
    ut_free(UT_MODULE_COMMON, data->arg);
    ut_free(UT_MODULE_COMMON, data);
}

struct ut_data **ut_data_create(size_t cnt, enum ut_data_type data_type) {
    struct ut_data **simple_data = ut_malloc(UT_MODULE_DATA, sizeof(*simple_data) * cnt);
    struct ut_metric enqueue_time = ut_clock_time_get();
    for (size_t i = 0; i < cnt; i++) {
        simple_data[i] = ut_malloc(UT_MODULE_DATA, sizeof(*simple_data[i]));
        if (data_type == UT_DATA_MAGIC_TYPE) {
            simple_data[i]->data = UT_DATA_MAGIC;
        } else {
            simple_data[i]->data = (uintptr_t)(simple_data[i]);
        }
        simple_data[i]->enqueue_time = enqueue_time;
    }

    return simple_data;
}

void ut_data_destory(struct ut_data **data, size_t cnt) {
    for (size_t i = 0; i < cnt; i++) {
        ut_free(UT_MODULE_DATA, data[i]);
    }
    ut_free(UT_MODULE_DATA, data);
}

uint32_t ut_exec_enqueue(struct ut_queue *q, struct ut_data **data, size_t burst_cnt,
                         struct ut_metric *op_use_diff, uint16_t thread_idx) {
    uint32_t enqueue_cnt = 0;
    struct ut_metric op_use_start = ut_clock_time_get();
    uint32_t wait_consumed = 0;
    enqueue_cnt = q->enqueue_burst_f(q->ring, (void **)data, burst_cnt, thread_idx, &wait_consumed);
    *op_use_diff = ut_clock_time_sub(ut_clock_time_get(), op_use_start);

    return enqueue_cnt;
}

uint32_t ut_exec_dequeue(struct ut_queue *q, struct ut_data **data, size_t burst_cnt, struct ut_metric *op_use_diff) {
    uint32_t dequeue_cnt = 0;

    struct ut_metric op_use_start = ut_clock_time_get();
    dequeue_cnt = q->dequeue_burst_f(q->ring, (void **)data, burst_cnt, NULL);
    *op_use_diff = ut_clock_time_sub(ut_clock_time_get(), op_use_start);

    return dequeue_cnt;
}

void *ut_thread_producer_start(void *arg) {
    uint32_t enqueue_cnt = 0;
    uint64_t ok_cnt = 0;
    uint64_t run_times = 0;
    struct ut_thread_arg *t_arg = (struct ut_thread_arg *)arg;
    struct ut_info_s *info = t_arg->info;
    struct ut_cfg *cfg = &info->cfg;
    struct ut_queue *q = t_arg->q;
    struct ut_exit_data *exit_data = ut_exit_data_create(t_arg);

    char thread_name[128] = {0};
    uint64_t op_ok_latency_ns = 0;
    uint64_t op_err_latency_ns = 0;
    uint64_t run_ok_times = cfg->run.run_ok_times / cfg->ring.producer_cnt;
    struct ut_metric op_latency = {0};
    snprintf(thread_name, sizeof(thread_name), "producer:%lu", exit_data->thread_id);
    prctl(PR_SET_NAME, thread_name);
    if (ut_setaffinity(t_arg->core) != BBQ_OK) {
        UT_ERR_LOG("ut_setaffinity error");
        exit(-1);
    }

    ut_wait_all_threads_ready(&info->ctl);
    UT_INFO_LOG("producer thread:%lx, core:%d", exit_data->thread_id, t_arg->core);

    exit_data->metric_start = ut_clock_time_get();
    while (true) {
        if ((run_ok_times > 0 && ok_cnt >= run_ok_times) || (!info->ctl.running)) {
            // 控制次数的循环或运行时间到了
            break;
        }

        if (cfg->ring.workload == UT_WORKLOAD_SIMPLE) {
            enqueue_cnt = ut_exec_enqueue(q, exit_data->simple_data, cfg->ring.burst_cnt, &op_latency, t_arg->thread_idx);
        } else {
            struct ut_data **data = ut_data_create(cfg->ring.burst_cnt, UT_DATA_UINTPTR_TYPE);
            if (data == NULL) {
                UT_ERR_LOG("malloc falied");
                exit(-1);
            }

            enqueue_cnt = ut_exec_enqueue(q, data, cfg->ring.burst_cnt, &op_latency, t_arg->thread_idx);
            // 释放未入队的内存
            for (uint32_t i = enqueue_cnt; i < cfg->ring.burst_cnt; i++) {
                ut_free(UT_MODULE_DATA, data[i]);
            }

            ut_free(UT_MODULE_DATA, data);
        }

        if (enqueue_cnt > 0) {
            ok_cnt += enqueue_cnt;
            op_ok_latency_ns += ut_clock_time_to_ns(&op_latency);
        } else {
            op_err_latency_ns += ut_clock_time_to_ns(&op_latency);
        }

        run_times++;
    }

    exit_data->metric_end = ut_clock_time_get();
    exit_data->run_times = run_times;
    exit_data->ok_cnt = ok_cnt;

    exit_data->op_ok_latency_ns = op_ok_latency_ns;
    exit_data->op_err_latency_ns = op_err_latency_ns;
    atomic_fetch_add(&info->ctl.producer_exit, 1);

    UT_DBG_LOG("producer-----> en_ok:%lu", ok_cnt);
    pthread_exit(exit_data);
}

void *ut_thread_consumer_start(void *arg) {
    uint32_t deq_cnt = -1;
    uint64_t ok_cnt = 0;
    uint64_t run_times = 0;
    struct ut_thread_arg *t_arg = (struct ut_thread_arg *)arg;
    struct ut_info_s *info = t_arg->info;
    struct ut_cfg *cfg = &info->cfg;
    struct ut_queue *q = t_arg->q;
    struct ut_exit_data *exit_data = ut_exit_data_create(t_arg);
    uint64_t latency_ns = 0;
    struct ut_metric op_latency = {0};
    uint64_t op_ok_latency_ns = 0;
    uint64_t op_err_latency_ns = 0;
    uint64_t data_error_cnt = 0;
    char thread_name[128] = {0};
    struct ut_data **deq_data = ut_malloc(UT_MODULE_DATA, sizeof(*deq_data) * cfg->ring.entries_cnt);

    snprintf(thread_name, sizeof(thread_name), "consumer:%lu", exit_data->thread_id);
    prctl(PR_SET_NAME, thread_name);
    if (ut_setaffinity(t_arg->core) != BBQ_OK) {
        UT_ERR_LOG("ut_setaffinity error");
        exit(-1);
    }

    ut_wait_all_threads_ready(&info->ctl);
    UT_INFO_LOG("consumer thread:%lx, core:%d", exit_data->thread_id, t_arg->core);

    exit_data->metric_start = ut_clock_time_get();

    while (true) {
        if (ut_all_producer_exit(info) && deq_cnt == 0) {
            // 运行时间到了或是所有生产者退出了,检查生产者是否全部退出，且队列被消费完了
            break;
        }

        deq_cnt = ut_exec_dequeue(q, deq_data, cfg->ring.burst_cnt, &op_latency);
        if (deq_cnt > 0) {
            for (uint32_t i = 0; i < deq_cnt; i++) {
                struct ut_data *data = deq_data[i];
                if (cfg->ring.workload == UT_WORKLOAD_SIMPLE) {
                    if (data->data != UT_DATA_MAGIC) {
                        UT_ERR_LOG("the obtained data is not consistent with the expectation, expect:%u actual:%lu", UT_DATA_MAGIC, data->data);
                        exit_data->data_error_cnt += 1;
                    }
                } else {
                    struct ut_metric latency = ut_clock_time_sub(ut_clock_time_get(), data->enqueue_time);
                    if (ut_clock_time_is_zero(&data->enqueue_time)) {
                        UT_ERR_LOG("enqueue_time is 0");
                        exit(-1);
                    }

                    if (data->data != (uintptr_t)data) {
                        UT_ERR_LOG("the obtained data is not consistent with the expectation, expect:%lu actual:%lu", (uintptr_t)data, data->data);
                        data_error_cnt += 1;
                    }

                    latency_ns += ut_clock_time_to_ns(&latency);
                    ut_free(UT_MODULE_DATA, data);
                }
            }
            ok_cnt += deq_cnt;
            op_ok_latency_ns += ut_clock_time_to_ns(&op_latency);
        } else {
            op_err_latency_ns += ut_clock_time_to_ns(&op_latency);
        }

        run_times++;
    }

    exit_data->metric_end = ut_clock_time_get();
    exit_data->run_times = run_times;
    exit_data->ok_cnt = ok_cnt;
    exit_data->latency_ns = latency_ns;
    exit_data->op_ok_latency_ns = op_ok_latency_ns;
    exit_data->op_err_latency_ns = op_err_latency_ns;
    exit_data->data_error_cnt = data_error_cnt;

    ut_free(UT_MODULE_DATA, deq_data);
    UT_DBG_LOG("consumer-----> de_ok:%lu", ok_cnt);
    pthread_exit(exit_data);
}

void ut_wait_all_threads_exit(struct ut_info_s *info, uint32_t thread_cnt, pthread_t *threads, struct ut_exit_data **exit_data) {
    if (info->cfg.run.run_time > 0) {
        UT_DBG_LOG("sleep %lus, and notify all threads to exit...", info->cfg.run.run_time);
        sleep(info->cfg.run.run_time);
        info->ctl.running = false;
    }

    for (uint32_t i = 0; i < thread_cnt; i++) {
        pthread_join(threads[i], (void **)(&exit_data[i])); // 等待每个线程结束
    }
}
void ut_one_thread_create(struct ut_info_s *info, struct ut_queue *q, enum ut_thread_type ttype, int core, uint16_t thread_id, pthread_t *thread) {
    UT_DBG_LOG("thread type:%d core:%d", ttype, core);
    struct ut_thread_arg *arg = (struct ut_thread_arg *)ut_malloc(UT_MODULE_COMMON, sizeof(struct ut_thread_arg)); // 线程回收时free
    arg->info = info;
    arg->q = q;
    arg->ttype = ttype;
    arg->core = core;
    arg->thread_idx = thread_id;

    if (ttype == UT_THREAD_PRODUCER) {
        pthread_create(thread, NULL, ut_thread_producer_start, arg);
    } else {
        pthread_create(thread, NULL, ut_thread_consumer_start, arg);
    }
}

#define CORE_ID_CHK_SET(core_id, max_id)                             \
    do {                                                             \
        core_id = (core_id + 1) <= max_id ? (core_id + 1) : core_id; \
    } while (0)

pthread_t *ut_threads_create(struct ut_info_s *info, struct ut_queue *q) {
    // 创建生产者消费者线程
    uint16_t thread_id = 0;
    struct ut_cfg *cfg = &info->cfg;
    int core_id = cfg->base.core_begin;
    size_t thread_cnt = cfg->ring.producer_cnt + cfg->ring.consumer_cnt;
    pthread_t *threads = (pthread_t *)ut_malloc(UT_MODULE_COMMON, sizeof(pthread_t) * thread_cnt); // 存储所有线程ID的数组

    pthread_barrier_init(&info->ctl.all_threads_start, NULL, thread_cnt);
    info->ctl.running = true;
    info->ctl.producer_exit = ATOMIC_VAR_INIT(0);

    // MPSC 或 SPMC 场景在第一个核心/超线程上分配单个生产者或消费者，然后将其他线程按顺序分配给核心/超线程。
    // MPMC，我们将生产者和消费者一一交错分配
    // 如果数量不同，则在最后分配剩余部分。
    if (cfg->ring.producer_cnt == 1 && cfg->ring.consumer_cnt >= 1) {
        // SPMC，第一个核心给生产者，其他分配给消费者
        ut_one_thread_create(info, q, UT_THREAD_PRODUCER, core_id, thread_id, &(threads[thread_id]));
        thread_id++;
        for (uint32_t i = 0; i < cfg->ring.consumer_cnt; i++) {
            CORE_ID_CHK_SET(core_id, cfg->base.core_end);
            ut_one_thread_create(info, q, UT_THREAD_CONSUMER, core_id, thread_id, &(threads[thread_id]));
            thread_id++;
        }
    } else if (cfg->ring.consumer_cnt == 1 && cfg->ring.producer_cnt >= 1) {
        // MPSC，第一个核心给消费者，其他分配给生产者
        ut_one_thread_create(info, q, UT_THREAD_CONSUMER, core_id, thread_id, &(threads[thread_id]));
        thread_id++;
        for (uint32_t i = 0; i < cfg->ring.producer_cnt; i++) {
            CORE_ID_CHK_SET(core_id, cfg->base.core_end);
            ut_one_thread_create(info, q, UT_THREAD_PRODUCER, core_id, thread_id, &(threads[thread_id]));
            thread_id++;
        }
    } else {
        // MPMC 或 只有生产者 或这有消费者，核心交错分配
        uint32_t pcnt = cfg->ring.producer_cnt; // 生产者个数
        uint32_t ccnt = cfg->ring.consumer_cnt; // 消费者个数
        for (core_id = cfg->base.core_begin; core_id < cfg->base.core_end && pcnt > 0 && ccnt > 0;) {
            if ((core_id & 1) == 0) {
                // 偶数
                ut_one_thread_create(info, q, UT_THREAD_PRODUCER, core_id, thread_id, &(threads[thread_id]));
                thread_id++;
                pcnt--;
            } else {
                ut_one_thread_create(info, q, UT_THREAD_CONSUMER, core_id, thread_id, &(threads[thread_id]));
                thread_id++;
                ccnt--;
            }
            CORE_ID_CHK_SET(core_id, cfg->base.core_end);
        }

        for (uint32_t i = 0; i < pcnt; i++) {
            ut_one_thread_create(info, q, UT_THREAD_PRODUCER, core_id, thread_id, &(threads[thread_id]));
            thread_id++;
            CORE_ID_CHK_SET(core_id, cfg->base.core_end);
        }

        for (uint32_t i = 0; i < ccnt; i++) {
            ut_one_thread_create(info, q, UT_THREAD_CONSUMER, core_id, thread_id, &(threads[thread_id]));
            thread_id++;
            CORE_ID_CHK_SET(core_id, cfg->base.core_end);
        }
    }

    return threads;
}

void ut_threads_destory(struct ut_info_s *info, pthread_t *threads) {
    pthread_barrier_destroy(&info->ctl.all_threads_start);
    ut_free(UT_MODULE_COMMON, threads);
}

void ut_merge_data_detail(struct ut_merge_data *merge, struct ut_exit_data *exit_data) {
    merge->run_times += exit_data->run_times;
    merge->ok_cnt += exit_data->ok_cnt;
    merge->latency_ns += exit_data->latency_ns;
    merge->op_err_latency_ns = exit_data->op_err_latency_ns;
    merge->op_ok_latency_ns += exit_data->op_ok_latency_ns;
    merge->data_error_cnt += exit_data->data_error_cnt;
}

void ut_merge_all_data(struct ut_exit_data **exit_data, uint32_t thread_cnt, struct ut_merge_s *merge) {
    struct ut_metric p_start = {0};
    struct ut_metric p_end = {0};
    struct ut_metric c_start = {0};
    struct ut_metric c_end = {0};

    for (uint32_t i = 0; i < thread_cnt; i++) {
        // 根据生产者/消费者 线程最早开始和最晚结束，记录时间
        if (exit_data[i]->arg->ttype == UT_THREAD_PRODUCER) {
            if (ut_clock_time_is_zero(&p_start) || ut_timespec_is_after(&p_start.timestamp, &exit_data[i]->metric_start.timestamp)) {
                p_start = exit_data[i]->metric_start;
            }

            if (ut_timespec_is_after(&exit_data[i]->metric_start.timestamp, &p_end.timestamp)) {
                p_end = exit_data[i]->metric_end;
            }

            ut_merge_data_detail(&merge->producer, exit_data[i]);
        } else {
            if (ut_clock_time_is_zero(&c_start) || ut_timespec_is_after(&c_start.timestamp, &exit_data[i]->metric_start.timestamp)) {
                c_start = exit_data[i]->metric_start;
            }

            if (ut_timespec_is_after(&exit_data[i]->metric_start.timestamp, &c_end.timestamp)) {
                c_end = exit_data[i]->metric_end;
            }

            ut_merge_data_detail(&merge->consumer, exit_data[i]);
        }
    }

    merge->producer.use_time = ut_clock_time_sub(p_end, p_start);
    merge->consumer.use_time = ut_clock_time_sub(c_end, c_start);
}