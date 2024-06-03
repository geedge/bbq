/*
 * @Author: liuyu
 * @LastEditTime: 2024-06-03 09:47:08
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */

#include "test_queue.h"
#include "bbq.h"
#include "test_mix.h"
#include <sys/prctl.h>
#include <unistd.h>

void test_busy_loop(uint32_t busy_loop) {
    uint32_t loop = 0;
    while (loop <= busy_loop) {
        loop++;
    };
}

int test_queue_init_bbq(test_cfg *cfg, test_queue_s *q) {
    bbq_queue_s *ring;

    if (cfg->ring.block_count == 0) {
        q->ring = bbq_ring_create_with_socket("bbq_ring", cfg->ring.entries_cnt, 0, NULL);
    } else {
        q->ring = bbq_ring_create_bnbs_with_socket("bbq_ring", cfg->ring.block_count,
                                                   cfg->ring.entries_cnt / cfg->ring.block_count,
                                                   0, NULL);
    }

    if (q->ring == NULL) {
        BBQ_ERR_LOG("bbq_ring_create_with_socket failed");
        return BBQ_NULL_PTR;
    }

    bbq_queue_s *bbq_ring = (bbq_queue_s *)q->ring;
    BBQ_DBG_LOG("block number:%lu size:%lu", bbq_ring->bn, bbq_ring->bs);

    q->ring_free_f = (test_ring_free_f)bbq_ring_free;
    q->enqueue_f = (test_ring_enqueue_f)bbq_enqueue;
    q->dequeue_f = (test_ring_dequeue_f)bbq_dequeue;

    return 0;
}

void test_queue_destory(test_queue_s *q) {
    if (q != NULL && q->ring_free_f != NULL) {
        q->ring_free_f(q->ring);
    }
}

bool test_all_producer_exit(test_cfg *cfg) {
    return atomic_load(&cfg->run.producer_exit) == cfg->ring.producer_cnt;
}

void test_wait_all_threads_ready(test_cfg *cfg) {
    uint32_t thread_cnt_max = cfg->ring.consumer_cnt +
                              cfg->ring.producer_cnt;

    while (atomic_load(&cfg->run.thread_start) != thread_cnt_max) {
        // 忙等待其他线程准备好
        // BBQ_DBG_LOG("wait all thread init done!");
        // sleep(1);
    }
    BBQ_DBG_LOG("thread init done!");
}

test_exit_data *test_exit_data_create(test_thread_arg_s *t_arg) {
    test_exit_data *exit_data = (test_exit_data *)test_malloc(TEST_MODULE_COMMON, sizeof(test_exit_data));
    if (exit_data == NULL) {
        BBQ_ERR_LOG("malloc failed");
        exit(-1);
    }

    exit_data->arg = t_arg;
    exit_data->thread_id = pthread_self();
    exit_data->simple_data.data = TEST_DATA_MAGIC;
    if (t_arg->cfg->run.enq_per_producer > 0) {
        exit_data->complex_data.data = test_malloc(TEST_MODULE_COMMON, t_arg->cfg->run.enq_per_producer * sizeof(test_data));
    } else {
        exit_data->complex_data.data = NULL;
    }
    exit_data->complex_data.data_cnt = 0;
    exit_data->latency_ns = 0;
    exit_data->data_error_cnt = 0;

    return exit_data;
}

test_exit_data *test_exit_data_destory(test_exit_data *data) {
    test_free(TEST_MODULE_COMMON, data->complex_data.data);
    test_free(TEST_MODULE_COMMON, data->arg);
    test_free(TEST_MODULE_COMMON, data);
}

int test_exec_enqueue(test_queue_s *q, void *obj, test_time_metric *op_use_diff) {
    int ret = 0;

    test_time_metric op_use_start = test_clock_time_get();
    ret = q->enqueue_f(q->ring, obj);
    *op_use_diff = test_clock_time_sub(test_clock_time_get(), op_use_start);

    return ret;
}

int test_exec_dequeue(test_queue_s *q, void **obj, test_time_metric *op_use_diff) {
    int ret = 0;

    test_time_metric op_use_start = test_clock_time_get();
    ret = q->dequeue_f(q->ring, obj);
    *op_use_diff = test_clock_time_sub(test_clock_time_get(), op_use_start);

    return ret;
}

void *test_thread_producer_start(void *arg) {
    int ret = 0;
    uint64_t ok_cnt = 0;
    uint64_t run_times = 0;
    test_thread_arg_s *t_arg = (test_thread_arg_s *)arg;
    test_cfg *cfg = t_arg->cfg;
    test_queue_s *q = t_arg->q;
    test_exit_data *exit_data = test_exit_data_create(t_arg);
    char thread_name[128] = {0};
    uint64_t op_ok_latency_ns = 0;
    uint64_t op_err_latency_ns = 0;

    test_time_metric op_latency = {0};

    snprintf(thread_name, sizeof(thread_name), "producer:%x", exit_data->thread_id);
    prctl(PR_SET_NAME, thread_name);

    atomic_fetch_add(&cfg->run.thread_start, 1);
    test_wait_all_threads_ready(cfg);

    exit_data->metric_start = test_clock_time_get();
    while (true) {
        if ((cfg->run.enq_per_producer > 0 && run_times >= cfg->run.enq_per_producer) || (!cfg->run.running)) {
            // 控制次数的循环或运行时间到了
            break;
        }

        if (cfg->ring.workload == TEST_WORKLOAD_SIMPLE) {
            ret = test_exec_enqueue(q, &(exit_data->simple_data), &op_latency);
        } else {
            test_data *data = test_malloc(TEST_MODULE_COMMON_EXIT_DATA, sizeof(test_data));
            if (data == NULL) {
                BBQ_ERR_LOG("malloc falied");
                exit(-1);
            }
            data->enqueue_time = test_clock_time_get();
            data->data = TEST_DATA_MAGIC;
            if (exit_data->complex_data.data != NULL) {
                exit_data->complex_data.data[exit_data->complex_data.data_cnt] = *data;
            }
            ret = test_exec_enqueue(q, data, &op_latency);
            if (ret == 0) {
                exit_data->complex_data.data_cnt++;
            } else {
                test_free(TEST_MODULE_COMMON_EXIT_DATA, data);
            }
            test_busy_loop(t_arg->busy_loop);
        }

        if (ret == 0) {
            ok_cnt++;
            op_ok_latency_ns += test_clock_time_to_ns(&op_latency);
        } else {
            op_err_latency_ns += test_clock_time_to_ns(&op_latency);
        }

        run_times++;
    }

    exit_data->metric_end = test_clock_time_get();
    exit_data->run_times = run_times;
    exit_data->ok_cnt = ok_cnt;

    exit_data->op_ok_latency_ns = op_ok_latency_ns;
    exit_data->op_err_latency_ns = op_err_latency_ns;
    atomic_fetch_add(&cfg->run.producer_exit, 1);

    BBQ_DBG_LOG("producer-----> en_ok:%lu", ok_cnt);
    pthread_exit(exit_data);
}

void *test_thread_consumer_start(void *arg) {
    int ret = 0;
    uint64_t ok_cnt = 0;
    uint64_t run_times = 0;
    test_thread_arg_s *t_arg = (test_thread_arg_s *)arg;
    test_cfg *cfg = t_arg->cfg;
    test_queue_s *q = t_arg->q;
    test_exit_data *exit_data = test_exit_data_create(t_arg);
    uint64_t latency_ns = 0;
    test_time_metric op_latency = {0};
    uint64_t op_ok_latency_ns;
    uint64_t op_err_latency_ns = 0;
    uint64_t data_error_cnt = 0;
    char thread_name[128] = {0};

    snprintf(thread_name, sizeof(thread_name), "consumer:%x", exit_data->thread_id);
    prctl(PR_SET_NAME, thread_name);

    atomic_fetch_add(&cfg->run.thread_start, 1);
    test_wait_all_threads_ready(cfg);

    usleep(100);

    exit_data->metric_start = test_clock_time_get();
    while (true) {
        if ((cfg->run.deq_per_consumer > 0 && run_times >= cfg->run.deq_per_consumer) || (!cfg->run.running)) {
            // 控制次数的循环或运行时间到了,检查生产者是否全部退出，且队列被消费完了
            if (ret && test_all_producer_exit(cfg)) {
                break;
            }
        }

        test_data *data = NULL;

        ret = test_exec_dequeue(q, (void **)(&data), &op_latency);
        if (ret == 0) {
            if (t_arg->cfg->ring.workload == TEST_WORKLOAD_SIMPLE) {
                if (data->data != TEST_DATA_MAGIC) {
                    BBQ_ERR_LOG("the obtained data is not consistent with the expectation, expect:%u actual:%u", TEST_DATA_MAGIC, *data);
                    exit_data->data_error_cnt += 1;
                }
            } else {
                test_time_metric latency = test_clock_time_sub(test_clock_time_get(), data->enqueue_time);
                if (test_clock_time_is_zero(&data->enqueue_time)) {
                    BBQ_ERR_LOG("enqueue_time is 0");
                    exit(-1);
                }

                if (data->data != TEST_DATA_MAGIC) {
                    BBQ_ERR_LOG("the obtained data is not consistent with the expectation, expect:%u actual:%u", TEST_DATA_MAGIC, *data);
                    data_error_cnt += 1;
                }

                latency_ns += test_clock_time_to_ns(&latency);
                if (exit_data->complex_data.data != NULL) {
                    exit_data->complex_data.data[exit_data->complex_data.data_cnt] = *data;
                }
                exit_data->complex_data.data_cnt++;
                test_free(TEST_MODULE_COMMON_EXIT_DATA, data);
                test_busy_loop(t_arg->busy_loop);
            }

            ok_cnt++;
            op_ok_latency_ns += test_clock_time_to_ns(&op_latency);
        } else {
            op_err_latency_ns += test_clock_time_to_ns(&op_latency);
        }

        run_times++;
    }

    exit_data->metric_end = test_clock_time_get();
    exit_data->run_times = run_times;
    exit_data->ok_cnt = ok_cnt;
    exit_data->latency_ns = latency_ns;
    exit_data->op_ok_latency_ns = op_ok_latency_ns;
    exit_data->op_err_latency_ns = op_err_latency_ns;
    exit_data->data_error_cnt = data_error_cnt;
    BBQ_DBG_LOG("consumer-----> de_ok:%lu", ok_cnt);
    pthread_exit(exit_data);
}

void test_check_run_time(test_cfg *cfg) {
    if (cfg->run.run_time > 0) {
        BBQ_DBG_LOG("sleep %lus, and notify all threads to exit...", cfg->run.run_time);
        sleep(cfg->run.run_time);
        cfg->run.running = false;
    }
}

pthread_t *
test_one_thread_create(test_cfg *cfg, test_queue_s *q, test_thread_type_e ttype, int core, pthread_t *thread) {
    BBQ_DBG_LOG("thread type:%d core:%d", ttype, core);
    test_thread_arg_s *arg = (test_thread_arg_s *)test_malloc(TEST_MODULE_COMMON, sizeof(test_thread_arg_s)); // 线程回收时free
    arg->cfg = cfg;
    arg->q = q;
    arg->ttype = ttype;
    arg->core = core;
    arg->busy_loop = (core + 1) * 7 % 100;

    if (ttype == TEST_THREAD_PRODUCER) {
        pthread_create(thread, NULL, test_thread_producer_start, arg);
    } else {
        pthread_create(thread, NULL, test_thread_consumer_start, arg);
    }
}

#define CORE_ID_CHK_SET(core_id, max_id)                            \
    do {                                                            \
        core_id = (core_id + 1) < max_id ? (core_id + 1) : core_id; \
    } while (0)

pthread_t *test_threads_create(test_cfg *cfg, test_queue_s *q) {
    // 创建生产者消费者线程
    int ret;
    size_t thread_id = 0;
    int core_id = 0;
    size_t thread_cnt = cfg->ring.producer_cnt + cfg->ring.consumer_cnt;
    pthread_t *threads = (pthread_t *)test_malloc(TEST_MODULE_COMMON, sizeof(pthread_t) * thread_cnt); // 存储所有线程ID的数组

    // MPSC 或 SPMC 场景在第一个核心/超线程上分配单个生产者或消费者，然后将其他线程按顺序分配给核心/超线程。
    // MPMC，我们将生产者和消费者一一交错分配
    // 如果数量不同，则在最后分配剩余部分。
    if (cfg->ring.producer_cnt == 1 && cfg->ring.consumer_cnt >= 1) {
        // SPMC，第一个核心给生产者，其他分配给消费者
        test_one_thread_create(cfg, q, TEST_THREAD_PRODUCER, core_id, &(threads[thread_id++]));
        for (int i = 0; i < cfg->ring.consumer_cnt; i++) {
            CORE_ID_CHK_SET(core_id, cfg->base.cores_cnt);
            test_one_thread_create(cfg, q, TEST_THREAD_CONSUMER, core_id, &(threads[thread_id++]));
        }
    } else if (cfg->ring.consumer_cnt == 1 && cfg->ring.producer_cnt >= 1) {
        // MPSC，第一个核心给消费者，其他分配给生产者
        test_one_thread_create(cfg, q, TEST_THREAD_CONSUMER, core_id, &(threads[thread_id++]));
        for (int i = 0; i < cfg->ring.producer_cnt; i++) {
            CORE_ID_CHK_SET(core_id, cfg->base.cores_cnt);
            test_one_thread_create(cfg, q, TEST_THREAD_PRODUCER, core_id, &(threads[thread_id++]));
        }
    } else {
        // MPMC 或 只有生产者 或这有消费者，核心交错分配
        uint32_t pcnt = cfg->ring.producer_cnt; // 生产者个数
        uint32_t ccnt = cfg->ring.consumer_cnt; // 消费者个数
        for (core_id = 0; core_id < cfg->base.cores_cnt && pcnt > 0 && ccnt > 0;) {
            if ((core_id & 1) == 0) {
                // 偶数
                test_one_thread_create(cfg, q, TEST_THREAD_PRODUCER, core_id, &(threads[thread_id++]));
                pcnt--;
            } else {
                test_one_thread_create(cfg, q, TEST_THREAD_CONSUMER, core_id, &(threads[thread_id++]));
                ccnt--;
            }
            CORE_ID_CHK_SET(core_id, cfg->base.cores_cnt);
        }

        for (int i = 0; i < pcnt; i++) {
            test_one_thread_create(cfg, q, TEST_THREAD_PRODUCER, core_id, &(threads[thread_id++]));
            CORE_ID_CHK_SET(core_id, cfg->base.cores_cnt);
        }

        for (int i = 0; i < ccnt; i++) {
            test_one_thread_create(cfg, q, TEST_THREAD_CONSUMER, core_id, &(threads[thread_id++]));
            CORE_ID_CHK_SET(core_id, cfg->base.cores_cnt);
        }
    }

    return threads;
}

void test_threads_destory(pthread_t *threads) {
    test_free(TEST_MODULE_COMMON, threads);
}

void test_merge_data_detail(test_merge_data *merge, test_exit_data *exit_data) {
    merge->run_times += exit_data->run_times;
    merge->ok_cnt += exit_data->ok_cnt;
    merge->latency_ns += exit_data->latency_ns;
    merge->op_err_latency_ns = exit_data->op_err_latency_ns;
    merge->op_ok_latency_ns += exit_data->op_ok_latency_ns;
    merge->data_error_cnt += exit_data->data_error_cnt;
}

void test_merge_all_data(test_exit_data **exit_data, uint32_t thread_cnt, test_merge_s *merge) {
    test_time_metric p_start = {0};
    test_time_metric p_end = {0};
    test_time_metric c_start = {0};
    test_time_metric c_end = {0};

    for (uint32_t i = 0; i < thread_cnt; i++) {
        // 根据生产者/消费者 线程最早开始和最晚结束，记录时间
        if (exit_data[i]->arg->ttype == TEST_THREAD_PRODUCER) {
            if (test_clock_time_is_zero(&p_start) || test_timespec_is_after(&p_start.timestamp, &exit_data[i]->metric_start.timestamp)) {
                p_start = exit_data[i]->metric_start;
            }

            if (test_timespec_is_after(&exit_data[i]->metric_start.timestamp, &p_end.timestamp)) {
                p_end = exit_data[i]->metric_end;
            }

            test_merge_data_detail(&merge->producer, exit_data[i]);
        } else {
            if (test_clock_time_is_zero(&c_start) || test_timespec_is_after(&c_start.timestamp, &exit_data[i]->metric_start.timestamp)) {
                c_start = exit_data[i]->metric_start;
            }

            if (test_timespec_is_after(&exit_data[i]->metric_start.timestamp, &c_end.timestamp)) {
                c_end = exit_data[i]->metric_end;
            }

            test_merge_data_detail(&merge->consumer, exit_data[i]);
        }
    }

    merge->producer.use_time = test_clock_time_sub(p_end, p_start);
    merge->consumer.use_time = test_clock_time_sub(c_end, c_start);
}