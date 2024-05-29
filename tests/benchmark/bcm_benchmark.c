/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-29 17:48:41
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#include "bbq.h"
#include "bcm_queue.h"
#include "iniparser.h"
#include "test_common.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <unistd.h>

typedef struct {
    int core;
    uint32_t busy_loop; // 忙循环次数
    test_thread_type_e ttype;
    test_cfg *cfg;
    bcm_queue_t *q;
} bcm_thread_arg_s;

typedef struct bcm_benchmark {
    uint8_t data;                  // 数据
    test_time_metric enqueue_time; // 入队时间
} bcm_data;

typedef struct {
    bcm_data *data;
    uint64_t data_cnt;
} bcm_complex_data;

typedef struct {
    pthread_t thread_id;
    bcm_thread_arg_s *arg;
    bcm_data simple_data;
    bcm_complex_data complex_data;
    test_time_metric metric_start;
    test_time_metric metric_end;
    uint64_t run_times;
    uint64_t ok_cnt;
    uint64_t latency_ns;        // 仅消费者有效，数据停留的时延
    uint64_t op_ok_latency_ns;  // 成功操作的时延
    uint64_t op_err_latency_ns; // 操作失败的时延， 如满队入队，空队出队
} bcm_exit_data;

typedef struct {
    test_time_metric use_time;
    uint64_t run_times;
    uint64_t ok_cnt;
    uint64_t latency_ns;        // 仅消费者有效，数据停留的时延
    uint64_t op_ok_latency_ns;  // 成功操作的时延
    uint64_t op_err_latency_ns; // 操作失败的时延， 如满队入队，空队出队
} bcm_merge_data;

typedef struct {
    bcm_merge_data producer;
    bcm_merge_data consumer;
} bcm_merge;

#define BCM_SIMPLE_DATA_MAGIC 234

// 记录已启动的线程
atomic_uint bcm_thread_now = 0;
volatile bool bcm_threads_running = true;

extern void bbq_memory_info();
extern bool bbq_malloc_free_equal();

void bcmbusy_loop(uint32_t busy_loop) {
    uint32_t loop = 0;
    while (loop <= busy_loop) {
        loop++;
    };
}

void bcm_wait_all_threads_ready(test_cfg *cfg) {
    BBQ_INFO_LOG("wait other thread init....");
    uint32_t thread_cnt_max = cfg->ring.consumer_cnt +
                              cfg->ring.producer_cnt + 1; //  +1个运行时间控制线程

    atomic_fetch_add(&bcm_thread_now, 1);
    while (atomic_load(&bcm_thread_now) != thread_cnt_max) {
        // 忙等待其他线程准备好
        sleep(1);
    }
    BBQ_INFO_LOG("wait other thread init done!");
}

bcm_exit_data *bcm_exit_data_create(bcm_thread_arg_s *t_arg) {
    bcm_exit_data *exit_data = (bcm_exit_data *)test_malloc(TEST_MODULE_BCM, sizeof(bcm_exit_data));
    if (exit_data == NULL) {
        BBQ_ERR_LOG("malloc failed");
        exit(-1);
    }

    exit_data->arg = t_arg;
    exit_data->thread_id = pthread_self();
    exit_data->simple_data.data = BCM_SIMPLE_DATA_MAGIC;
    if (t_arg->cfg->run.enqueue_cnt > 0) {
        exit_data->complex_data.data = test_malloc(TEST_MODULE_BCM, t_arg->cfg->run.enqueue_cnt * sizeof(bcm_data));
    } else {
        exit_data->complex_data.data = NULL;
    }
    exit_data->complex_data.data_cnt = 0;
    exit_data->latency_ns = 0;

    return exit_data;
}

bcm_exit_data *bcm_exit_data_destory(bcm_exit_data *data) {
    test_free(TEST_MODULE_BCM, data->complex_data.data);
    test_free(TEST_MODULE_BCM, data->arg);
    test_free(TEST_MODULE_BCM, data);
}

int bcm_exec_enqueue(bcm_queue_t *q, void *obj, test_time_metric *op_use_diff) {
    int ret = 0;

    test_time_metric op_use_start = test_clock_time_get();
    ret = q->enqueue_f(q->ring, obj);
    *op_use_diff = test_clock_time_sub(test_clock_time_get(), op_use_start);

    return ret;
}

int bcm_exec_dequeue(bcm_queue_t *q, void **obj, test_time_metric *op_use_diff) {
    int ret = 0;

    test_time_metric op_use_start = test_clock_time_get();
    ret = q->dequeue_f(q->ring, obj);
    *op_use_diff = test_clock_time_sub(test_clock_time_get(), op_use_start);

    return ret;
}

void *bcm_thread_producer_start(void *arg) {
    int ret = 0;
    uint64_t ok_cnt = 0;
    uint64_t run_times = 0;
    bcm_thread_arg_s *t_arg = (bcm_thread_arg_s *)arg;
    test_cfg *cfg = t_arg->cfg;
    bcm_queue_t *q = t_arg->q;
    bcm_exit_data *exit_data = bcm_exit_data_create(t_arg);
    char thread_name[128] = {0};
    uint64_t op_ok_latency_ns = 0;
    uint64_t op_err_latency_ns = 0;

    test_time_metric op_latency = {0};

    snprintf(thread_name, sizeof(thread_name), "producer:%x", exit_data->thread_id);
    prctl(PR_SET_NAME, thread_name);

    bcm_wait_all_threads_ready(cfg);

    exit_data->metric_start = test_clock_time_get();
    while (true) {
        if (cfg->run.enqueue_cnt > 0 && run_times >= cfg->run.enqueue_cnt) {
            // 控制次数的循环
            break;
        }

        if (!bcm_threads_running) {
            // 控制时间的循环，时间一到生产者退出
            break;
        }
        if (cfg->ring.workload == TEST_WORKLOAD_SIMPLE) {
            ret = bcm_exec_enqueue(q, &(exit_data->simple_data), &op_latency);
        } else {
            bcm_data *data = test_malloc(TEST_MODULE_BCM_EXIT_DATA, sizeof(bcm_data));
            if (data == NULL) {
                BBQ_ERR_LOG("malloc falied");
                exit(-1);
            }
            data->enqueue_time = test_clock_time_get();
            data->data = BCM_SIMPLE_DATA_MAGIC;
            if (exit_data->complex_data.data != NULL) {
                exit_data->complex_data.data[exit_data->complex_data.data_cnt] = *data;
            }
            ret = bcm_exec_enqueue(q, data, &op_latency);
            if (ret == 0) {
                exit_data->complex_data.data_cnt++;
            } else {
                test_free(TEST_MODULE_BCM_EXIT_DATA, data);
            }
            bcmbusy_loop(t_arg->busy_loop);
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

    BBQ_INFO_LOG("producer-----> en_ok:%lu", ok_cnt);
    pthread_exit(exit_data);
}

void *bcm_thread_consumer_start(void *arg) {
    int ret = 0;
    uint64_t ok_cnt = 0;
    uint64_t run_times = 0;
    bcm_thread_arg_s *t_arg = (bcm_thread_arg_s *)arg;
    test_cfg *cfg = t_arg->cfg;
    bcm_queue_t *q = t_arg->q;
    bcm_exit_data *exit_data = bcm_exit_data_create(t_arg);
    uint64_t latency_ns = 0;
    test_time_metric op_latency = {0};
    uint64_t op_ok_latency_ns;
    uint64_t op_err_latency_ns = 0;

    char thread_name[128] = {0};
    snprintf(thread_name, sizeof(thread_name), "consumer:%x", exit_data->thread_id);
    prctl(PR_SET_NAME, thread_name);

    bcm_wait_all_threads_ready(cfg);

    usleep(100);

    exit_data->metric_start = test_clock_time_get();
    while (true) {
        if (cfg->run.dequeue_cnt > 0 && run_times >= cfg->run.dequeue_cnt) {
            // 控制次数的循环
            break;
        }

        bcm_data *data = NULL;

        ret = bcm_exec_dequeue(q, (void **)(&data), &op_latency);
        if (ret == 0) {
            if (t_arg->cfg->ring.workload == TEST_WORKLOAD_SIMPLE) {
                if (data->data != BCM_SIMPLE_DATA_MAGIC) {
                    BBQ_ERR_LOG("the obtained data is not consistent with the expectation, expect:%u actual:%u", BCM_SIMPLE_DATA_MAGIC, *data);
                    exit(-1);
                }
            } else {
                test_time_metric latency = test_clock_time_sub(test_clock_time_get(), data->enqueue_time);
                if (test_clock_time_is_zero(&data->enqueue_time)) {
                    BBQ_ERR_LOG("enqueue_time is 0");
                    exit(-1);
                }

                if (data->data != BCM_SIMPLE_DATA_MAGIC) {
                    BBQ_ERR_LOG("the obtained data is not consistent with the expectation, expect:%u actual:%u", BCM_SIMPLE_DATA_MAGIC, *data);
                    exit(-1);
                }

                latency_ns += test_clock_time_to_ns(&latency);
                if (exit_data->complex_data.data != NULL) {
                    exit_data->complex_data.data[exit_data->complex_data.data_cnt] = *data;
                }
                exit_data->complex_data.data_cnt++;
                test_free(TEST_MODULE_BCM_EXIT_DATA, data);
                bcmbusy_loop(t_arg->busy_loop);
            }

            ok_cnt++;
            op_ok_latency_ns += test_clock_time_to_ns(&op_latency);
        } else {
            op_err_latency_ns += test_clock_time_to_ns(&op_latency);
        }

        run_times++;

        if (!bcm_threads_running && ret != 0) {
            // 控制时间的循环，时间到，需要等队列里的数据都消费完再退出
            break;
        }
    }

    exit_data->metric_end = test_clock_time_get();
    exit_data->run_times = run_times;
    exit_data->ok_cnt = ok_cnt;
    exit_data->latency_ns = latency_ns;
    exit_data->op_ok_latency_ns = op_ok_latency_ns;
    exit_data->op_err_latency_ns = op_err_latency_ns;

    BBQ_INFO_LOG("consumer-----> de_ok:%lu", ok_cnt);
    pthread_exit(exit_data);
}

void *bcm_thread_run_time_set(void *arg) {
    test_cfg *cfg = (test_cfg *)arg;
    pthread_detach(pthread_self());

    bcm_wait_all_threads_ready(cfg);
    if (cfg->run.run_time) {
        sleep(cfg->run.run_time);
        bcm_threads_running = false;
    }
}

pthread_t *
bcm_one_thread_creat(test_cfg *cfg, bcm_queue_t *q, test_thread_type_e ttype, int core, pthread_t *thread) {
    BBQ_INFO_LOG("thread type:%d core:%d", ttype, core);

    bcm_thread_arg_s *arg = (bcm_thread_arg_s *)test_malloc(TEST_MODULE_BCM, sizeof(bcm_thread_arg_s)); // 线程回收时free
    arg->cfg = cfg;
    arg->q = q;
    arg->ttype = ttype;
    arg->core = core;
    arg->busy_loop = (core + 1) * 7 % 100;

    if (ttype == TEST_THREAD_PRODUCER) {
        pthread_create(thread, NULL, bcm_thread_producer_start, arg);
    } else {
        pthread_create(thread, NULL, bcm_thread_consumer_start, arg);
    }
}

#define CORE_ID_CHK_SET(core_id, max_id)                            \
    do {                                                            \
        core_id = (core_id + 1) < max_id ? (core_id + 1) : core_id; \
    } while (0)

pthread_t *bcm_threads_creat(test_cfg *cfg, bcm_queue_t *q) {
    // 创建生产者消费者线程
    int ret;
    size_t thread_id = 0;
    int core_id = 0;
    size_t thread_cnt = cfg->ring.producer_cnt + cfg->ring.consumer_cnt;
    pthread_t *threads = (pthread_t *)test_malloc(TEST_MODULE_BCM, sizeof(pthread_t) * thread_cnt); // 存储所有线程ID的数组

    // MPSC 或 SPMC 场景在第一个核心/超线程上分配单个生产者或消费者，然后将其他线程按顺序分配给核心/超线程。
    // MPMC，我们将生产者和消费者一一交错分配
    // 如果数量不同，则在最后分配剩余部分。
    if (cfg->ring.producer_cnt == 1 && cfg->ring.consumer_cnt >= 1) {
        // SPMC，第一个核心给生产者，其他分配给消费者
        bcm_one_thread_creat(cfg, q, TEST_THREAD_PRODUCER, core_id, &(threads[thread_id++]));
        for (int i = 0; i < cfg->ring.consumer_cnt; i++) {
            CORE_ID_CHK_SET(core_id, cfg->base.cores_cnt);
            bcm_one_thread_creat(cfg, q, TEST_THREAD_CONSUMER, core_id, &(threads[thread_id++]));
        }
    } else if (cfg->ring.consumer_cnt == 1 && cfg->ring.producer_cnt >= 1) {
        // MPSC，第一个核心给消费者，其他分配给生产者
        bcm_one_thread_creat(cfg, q, TEST_THREAD_CONSUMER, core_id, &(threads[thread_id++]));
        for (int i = 0; i < cfg->ring.producer_cnt; i++) {
            CORE_ID_CHK_SET(core_id, cfg->base.cores_cnt);
            bcm_one_thread_creat(cfg, q, TEST_THREAD_PRODUCER, core_id, &(threads[thread_id++]));
        }
    } else {
        // MPMC 或 只有生产者 或这有消费者，核心交错分配
        uint32_t pcnt = cfg->ring.producer_cnt; // 生产者个数
        uint32_t ccnt = cfg->ring.consumer_cnt; // 消费者个数
        for (core_id = 0; core_id < cfg->base.cores_cnt && pcnt > 0 && ccnt > 0;) {
            if ((core_id & 1) == 0) {
                // 偶数
                bcm_one_thread_creat(cfg, q, TEST_THREAD_PRODUCER, core_id, &(threads[thread_id++]));
                pcnt--;
            } else {
                bcm_one_thread_creat(cfg, q, TEST_THREAD_CONSUMER, core_id, &(threads[thread_id++]));
                ccnt--;
            }
            CORE_ID_CHK_SET(core_id, cfg->base.cores_cnt);
        }

        for (int i = 0; i < pcnt; i++) {
            bcm_one_thread_creat(cfg, q, TEST_THREAD_PRODUCER, core_id, &(threads[thread_id++]));
            CORE_ID_CHK_SET(core_id, cfg->base.cores_cnt);
        }

        for (int i = 0; i < ccnt; i++) {
            bcm_one_thread_creat(cfg, q, TEST_THREAD_CONSUMER, core_id, &(threads[thread_id++]));
            CORE_ID_CHK_SET(core_id, cfg->base.cores_cnt);
        }
    }

    return threads;
}

void bcm_threads_destory(pthread_t *threads) {
    test_free(TEST_MODULE_BCM, threads);
}

void bcm_merge_data_detail(bcm_merge_data *merge, bcm_exit_data *exit_data) {
    merge->run_times += exit_data->run_times;
    merge->ok_cnt += exit_data->ok_cnt;
    merge->latency_ns += exit_data->latency_ns;
    merge->op_err_latency_ns = exit_data->op_err_latency_ns;
    merge->op_ok_latency_ns += exit_data->op_ok_latency_ns;
}
void bcm_merge_all_data(bcm_exit_data **exit_data, uint32_t thread_cnt, bcm_merge *merge) {
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

            bcm_merge_data_detail(&merge->producer, exit_data[i]);
        } else {
            if (test_clock_time_is_zero(&c_start) || test_timespec_is_after(&c_start.timestamp, &exit_data[i]->metric_start.timestamp)) {
                c_start = exit_data[i]->metric_start;
            }

            if (test_timespec_is_after(&exit_data[i]->metric_start.timestamp, &c_end.timestamp)) {
                c_end = exit_data[i]->metric_end;
            }

            bcm_merge_data_detail(&merge->consumer, exit_data[i]);
        }
    }

    merge->producer.use_time = test_clock_time_sub(p_end, p_start);
    merge->consumer.use_time = test_clock_time_sub(c_end, c_start);
}

void bcm_report_printf(test_cfg *cfg, bcm_merge_data *data, bcm_exit_data **raw_data, uint32_t thread_cnt, test_thread_type_e ttype) {
    char name[10] = {0};
    double latency_ns = 0;
    double throughput = 0;

    if (ttype == TEST_THREAD_PRODUCER) {
        strcpy(name, "生产者");
    } else {
        strcpy(name, "消费者");
    }

    BBQ_CLOR_PRINT("\n---------%s---------", name);
    double use_time = test_clock_time_to_double(&data->use_time);
    BBQ_CLOR_PRINT("执行时间 : %lf 秒", use_time);
    BBQ_CLOR_PRINT("执行次数 : %lu", data->run_times);
    BBQ_CLOR_PRINT("成功次数 : %lu", data->ok_cnt);

    // 同时有生产者、消费者时才输出
    if (cfg->ring.producer_cnt > 0 && cfg->ring.consumer_cnt > 0) {
        throughput = data->ok_cnt / use_time;
        BBQ_CLOR_PRINT("吞吐 :%.0lf/s (%e/s)", throughput, throughput);

        // 多生产者单消费者 或 单生产者多消费才输出
        if ((cfg->ring.producer_cnt == 1 && cfg->ring.consumer_cnt > 1) ||
            (cfg->ring.producer_cnt > 1 && cfg->ring.consumer_cnt == 1)) {
            for (uint32_t i = 0, idx = 1; i < thread_cnt; i++) {
                if (raw_data[i]->arg->ttype == ttype) {
                    test_time_metric tmp_time = test_clock_time_sub(raw_data[i]->metric_end, raw_data[i]->metric_start);
                    throughput = raw_data[i]->ok_cnt / test_clock_time_to_double(&tmp_time);
                    BBQ_CLOR_PRINT("    %s-%d 吞吐 :%.0lf/s (%e/s)", name, idx, throughput, throughput);
                    idx++;
                }
            }
        }

        if (ttype == TEST_THREAD_CONSUMER && cfg->ring.workload == TEST_WORKLOAD_COMPLEX) {
            latency_ns = data->latency_ns * 1.0 / data->ok_cnt;
            BBQ_CLOR_PRINT("数据延迟 :%.0lf 纳秒 (%e)", latency_ns, latency_ns);
        }

        latency_ns = data->op_ok_latency_ns * 1.0 / data->ok_cnt;
        BBQ_CLOR_PRINT("操作延迟 :%.0lf 纳秒 (%e)", latency_ns, latency_ns);
    } else {
        latency_ns = data->op_err_latency_ns * 1.0 / (data->run_times - data->ok_cnt);
        if (ttype == TEST_THREAD_PRODUCER) {
            BBQ_CLOR_PRINT("满队入队操作延迟 :%.0lf 纳秒 (%e)", latency_ns, latency_ns);
        } else {
            BBQ_CLOR_PRINT("空队出队操作延迟 :%.0lf 纳秒 (%e)", latency_ns, latency_ns);
        }
    }
}

void bcm_report_generate(test_cfg *cfg, bcm_exit_data **exit_data, uint32_t thread_cnt) {
    // test_report report;

    bcm_merge merge = {0};
    bcm_merge_all_data(exit_data, thread_cnt, &merge);

    BBQ_CLOR_PRINT("ring类型:%s", test_ring_type_enum2str(cfg->ring.ring_type));
    BBQ_CLOR_PRINT("简介:%s", cfg->base.introduce);
    BBQ_CLOR_PRINT("配置:%s", cfg->base.name);
    if (cfg->ring.producer_cnt > 0) {
        bcm_report_printf(cfg, &merge.producer, exit_data, thread_cnt, TEST_THREAD_PRODUCER);
    }
    if (cfg->ring.consumer_cnt > 0) {
        bcm_report_printf(cfg, &merge.consumer, exit_data, thread_cnt, TEST_THREAD_CONSUMER);
    }
}

#if 1
int main(int argc, char *argv[]) {
    char *config;
    char *ring_type;

    if (argc == 3) {
        config = argv[1];
        ring_type = argv[2];
    } else {
        config = "/root/code/c/bbq-ly/tests/benchmark/config/bbq_debug/debug.ini";
        ring_type = "bbq";
        BBQ_ERR_LOG("use default config, ringt_type:bbq config:%s", config);
    }

    char thread_name[128] = {0};
    snprintf(thread_name, sizeof(thread_name), "main:%x", pthread_self());
    prctl(PR_SET_NAME, thread_name);

    // 加载配置
    test_cfg cfg;
    if (test_load_config(config, ring_type, &cfg) != 0) {
        BBQ_ERR_LOG("input config path");
        return -1;
    }

    // 队列初始化
    int ret = -1;
    bcm_queue_t q;
    ret = bcm_queue_init(&cfg, &q);
    if (ret != 0) {
        BBQ_ERR_LOG("init failed :%d", ret);
        return ret;
    }

    // 创建线程
    pthread_t *threads = bcm_threads_creat(&cfg, &q);
    if (threads == NULL) {
        BBQ_ERR_LOG("pthread_arr is NULL");
        return ret;
    }

    test_time_metric metric_start = test_clock_time_get();
    pthread_t thread_run_time;

    pthread_create(&thread_run_time, NULL, bcm_thread_run_time_set, &cfg);

    // 等待所有线程完成，回收数据
    uint32_t thread_cnt = cfg.ring.producer_cnt + cfg.ring.consumer_cnt;
    bcm_exit_data **exit_data = (bcm_exit_data **)test_malloc(TEST_MODULE_BCM, sizeof(bcm_exit_data **) * (thread_cnt));
    uint32_t i = 0;

    for (i = 0; i < thread_cnt; i++) {
        pthread_join(threads[i], (void **)(&exit_data[i])); // 等待每个线程结束
    }

    test_time_metric diff = test_clock_time_sub(test_clock_time_get(), metric_start);
    BBQ_INFO_LOG("use time: %lu.%lu s", diff.timestamp.tv_sec, diff.timestamp.tv_nsec);

    // 生成benchmark报告
    bcm_report_generate(&cfg, exit_data, thread_cnt);

    // 回收、释放数据
    for (i = 0; i < thread_cnt; i++) {
        bcm_exit_data_destory(exit_data[i]);
    }
    test_free(TEST_MODULE_BCM, exit_data);
    bcm_threads_destory(threads);
    bcm_queue_destory(&q);
    bbq_memory_info();
    test_memory_info();
    return 0;
}
#else

#define bn 32
#define bs 4096
int test_cnt = bn * bs;
int loop_cnt = 30;
// int bn = 8;
// int bs = 2048;
// int test_cnt = 16384;
// int loop_cnt = 3;

void *pstart(void *arg) {
    bbq_queue_s *q = (bbq_queue_s *)arg;

    int ret = 0;
    uint32_t ok_cnt = 0;

    for (int i = 0; i < test_cnt; i++) {
        int *data = (int *)malloc(sizeof(int));
        *data = 123;
        ret = bbq_enqueue(q, data);
        if (ret == 0) {
            ok_cnt++;
        } else {
            printf("enqueue err:%p ret:%d\n", data, ret);
            free(data);
        }
    }

    // bbq_debug_struct_print(q);
    printf("------------------------------p ok_cnt:%u\n", ok_cnt);
}

void *cstart(void *arg) {
    bbq_queue_s *q = (bbq_queue_s *)arg;
    int *data;
    int ret = 0;
    uint32_t ok_cnt = 0;

    for (int i = 0; i < test_cnt; i++) {
        ret = bbq_dequeue(q, (void **)&data);
        if (ret == 0) {
            if (*data != 123) {
                printf(".........!!!!!!!!  *data !=123,  is :%d\n", *data);
                exit(-1);
            }
            free(data);
            ok_cnt++;
        } else {
            printf("dequeue err ret:%d\n", ret);
        }
    }

    // bbq_debug_struct_print(q);
    printf("------------------------------c ok_cnt:%u\n", ok_cnt);
}

int main() {
#define TCNT 10
    bbq_queue_s *q = bbq_ring_create_bnbs_with_socket("bbq_ring", bn, bs, -1, BBQ_RETRY_NEW);
    if (q == NULL) {
        BBQ_ERR_LOG("bbq_ring_create_with_socket failed");
        return BBQ_NULL_PTR;
    }

#if 0
    pthread_t thread_arr[TCNT];
    for (int i = 0; i < TCNT; i++) {
        if (i % 2 == 0) {
            pthread_create(&thread_arr[i], NULL, pstart, q);
        } else {
            pthread_create(&thread_arr[i], NULL, cstart, q);
        }
    }

    for (int i = 0; i < TCNT; i++) {
        pthread_join(thread_arr[i], NULL);  // 等待每个线程结束
    }
#else

    for (int i = 0; i < loop_cnt; i++) {
        pstart(q);
        cstart(q);
    }
#endif
    bbq_ring_free(q);
    printf("end...\n");
}
#endif