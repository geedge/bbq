/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-25 11:13:29
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <unistd.h>
#include "bbq.h"
#include "bcm_queue.h"
#include "iniparser.h"
#include "test_common.h"

typedef struct {
    test_thread_type_e ttype;
    int core;
    test_cfg* cfg;
    bcm_queue_t* q;
} bcm_thread_arg_s;

typedef uint8_t bcm_simple_data;

typedef struct {
    bcm_simple_data* data;
    uint64_t data_cnt;
} bcm_complex_data;

#define BCM_SIMPLE_DATA_MAGIC 234
typedef struct {
    pthread_t thread_id;
    bcm_thread_arg_s* arg;
    bcm_simple_data simple_data;
    bcm_complex_data complex_data;
} bcm_exit_data;

// 记录已启动的线程
atomic_uint bcm_thread_now = 0;

extern void* bbq_malloc(bbq_module_e module, size_t size);
extern void bbq_free(bbq_module_e module, void* ptr);
extern void bbq_memory_info();
extern bool bbq_malloc_free_equal();

void bcmbusy_loopof() {
    uint32_t loop = 0;
    while (loop <= 50) {
        loop++;
    };
}

void bcm_wait_all_threads_ready(bcm_exit_data* exit_data) {
    BBQ_INFO_LOG("wait other thread init....");
    uint32_t thread_cnt_max = exit_data->arg->cfg->ring.consumer_cnt +
                              exit_data->arg->cfg->ring.producer_cnt;

    atomic_fetch_add(&bcm_thread_now, 1);
    while (atomic_load(&bcm_thread_now) != thread_cnt_max) {
        // 忙等待其他线程准备好
        sleep(1);
    }
    BBQ_INFO_LOG("wait other thread init done!");
}

bcm_exit_data* bcm_exit_data_create(bcm_thread_arg_s* t_arg) {
    bcm_exit_data* exit_data = (bcm_exit_data*)bbq_malloc(BBQ_MODULE_BCM, sizeof(bcm_exit_data));
    if (exit_data == NULL) {
        BBQ_ERR_LOG("malloc failed");
        perror("malloc failed");
    }

    exit_data->arg = t_arg;
    exit_data->thread_id = pthread_self();
    exit_data->simple_data = BCM_SIMPLE_DATA_MAGIC;
    exit_data->complex_data.data = bbq_malloc(BBQ_MODULE_BCM, t_arg->cfg->run.enqueue_cnt * sizeof(bcm_simple_data));
    exit_data->complex_data.data_cnt = 0;

    return exit_data;
}

bcm_exit_data* bcm_exit_data_destory(bcm_exit_data* data) {
    bbq_free(BBQ_MODULE_BCM, data->complex_data.data);
    bbq_free(BBQ_MODULE_BCM, data->arg);
    bbq_free(BBQ_MODULE_BCM, data);
}

void* bcm_thread_producer_start(void* arg) {
    int ret = 0;
    bcm_thread_arg_s* t_arg = (bcm_thread_arg_s*)arg;
    bcm_exit_data* exit_data = bcm_exit_data_create(t_arg);
    char thread_name[128] = {0};
    snprintf(thread_name, sizeof(thread_name), "producer:%x", exit_data->thread_id);
    prctl(PR_SET_NAME, thread_name);

    bcm_wait_all_threads_ready(exit_data);

    uint64_t en_ok_cnt = 0;

    for (uint64_t i; i < t_arg->cfg->run.enqueue_cnt; i++) {
        if (t_arg->cfg->ring.workload == TEST_WORKLOAD_SIMPLE) {
            ret = t_arg->q->enqueue_f(t_arg->q->ring, &(exit_data->simple_data));
        } else {
            bcm_simple_data* data = bbq_malloc(BBQ_MODULE_BCM, sizeof(bcm_simple_data));
            if (data == NULL) {
                BBQ_ERR_LOG("malloc falied");
                perror("malloc falied");
            }
            *data = BCM_SIMPLE_DATA_MAGIC;
            ret = t_arg->q->enqueue_f(t_arg->q->ring, data);
            if (ret == 0) {
                exit_data->complex_data.data[exit_data->complex_data.data_cnt++] = *data;
            } else {
                bbq_free(BBQ_MODULE_BCM, data);
            }
            bcmbusy_loopof();
        }

        if (ret == 0) {
            en_ok_cnt++;
        }
    }
    BBQ_INFO_LOG("producer-----> en_ok:%lu", en_ok_cnt);
    pthread_exit(exit_data);
}

void* bcm_thread_consumer_start(void* arg) {
    int ret = 0;
    uint64_t de_ok_cnt = 0;

    bcm_thread_arg_s* t_arg = (bcm_thread_arg_s*)arg;
    bcm_exit_data* exit_data = bcm_exit_data_create(t_arg);

    char thread_name[128] = {0};
    snprintf(thread_name, sizeof(thread_name), "consumer:%x", exit_data->thread_id);
    prctl(PR_SET_NAME, thread_name);

    bcm_wait_all_threads_ready(exit_data);

    for (uint64_t i; i < t_arg->cfg->run.dequeue_cnt; i++) {
        bcm_simple_data* data = NULL;
        ret = t_arg->q->dequeue_f(t_arg->q->ring, (void**)(&data));
        if (ret == 0) {
            if (*data != BCM_SIMPLE_DATA_MAGIC) {
                BBQ_ERR_LOG("the obtained data is not consistent with the expectation, expect:%u actual:%u", BCM_SIMPLE_DATA_MAGIC, *data);
                perror("the obtained data is not consistent with the expectation");
            }

            if (t_arg->cfg->ring.workload == TEST_WORKLOAD_COMPLEX) {
                exit_data->complex_data.data[exit_data->complex_data.data_cnt++] = *data;
                bbq_free(BBQ_MODULE_BCM, data);
                bcmbusy_loopof();
            }
            de_ok_cnt++;
        }
    }

    BBQ_INFO_LOG("consumer-----> de_ok:%lu", de_ok_cnt);
    pthread_exit(exit_data);
}

pthread_t*
bcm_one_thread_creat(test_cfg* cfg, bcm_queue_t* q, test_thread_type_e ttype, int core, pthread_t* thread) {
    BBQ_INFO_LOG("thread type:%d core:%d", ttype, core);

    bcm_thread_arg_s* arg = (bcm_thread_arg_s*)bbq_malloc(BBQ_MODULE_BCM, sizeof(bcm_thread_arg_s));  // 线程回收时free
    arg->cfg = cfg;
    arg->q = q;
    arg->ttype = ttype;
    arg->core = core;

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

pthread_t* bcm_threads_creat(test_cfg* cfg, bcm_queue_t* q) {
    // 创建生产者消费者线程
    int ret;
    size_t thread_id = 0;
    int core_id = 0;
    size_t thread_cnt = cfg->ring.producer_cnt + cfg->ring.consumer_cnt;
    pthread_t* threads = (pthread_t*)bbq_malloc(BBQ_MODULE_BCM, sizeof(pthread_t) * thread_cnt);  // 存储所有线程ID的数组

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
        uint32_t pcnt = cfg->ring.producer_cnt;  // 生产者个数
        uint32_t ccnt = cfg->ring.consumer_cnt;  // 消费者个数
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

void bcm_threads_destory(pthread_t* threads) {
    bbq_free(BBQ_MODULE_BCM, threads);
}

void bcm_report_generate(bcm_exit_data** exit_data, uint32_t thread_cnt) {
    // test_report report;
    for (uint32_t i = 0; i < thread_cnt; i++) {
        BBQ_INFO_LOG("[%d] thread_id:%u", i, exit_data[i]->thread_id);
    }
}

int main(int argc, char* argv[]) {
    char* config;
    if (argc == 2) {
        config = argv[1];
    } else {
        config = "/root/code/c/bbq-ly/tests/benchmark/config/case1.ini";
        BBQ_ERR_LOG("!!!!!! not input config path, use default config:%s", config);
    }

    char thread_name[128] = {0};
    snprintf(thread_name, sizeof(thread_name), "main:%x", pthread_self());
    prctl(PR_SET_NAME, thread_name);

    // 加载配置
    test_cfg cfg;
    if (bxm_load_config(config, &cfg) != 0) {
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
    pthread_t* threads = bcm_threads_creat(&cfg, &q);
    if (threads == NULL) {
        BBQ_ERR_LOG("pthread_arr is NULL");
        return ret;
    }

    // 等待所有线程完成，回收数据
    uint32_t thread_cnt = cfg.ring.producer_cnt + cfg.ring.consumer_cnt;
    bcm_exit_data** exit_data = (bcm_exit_data**)bbq_malloc(BBQ_MODULE_BCM, sizeof(bcm_exit_data**) * (thread_cnt));
    uint32_t i = 0;
    for (i = 0; i < thread_cnt; i++) {
        pthread_join(threads[i], (void**)(&exit_data[i]));  // 等待每个线程结束
    }

    // 生成benchmark报告
    bcm_report_generate(exit_data, thread_cnt);

    // 回收、释放数据
    for (i = 0; i < thread_cnt; i++) {
        bcm_exit_data_destory(exit_data[i]);
    }
    bbq_free(BBQ_MODULE_BCM, exit_data);
    bcm_threads_destory(threads);

    bbq_memory_info();

    if (bbq_malloc_free_equal()) {
        BBQ_INFO_LOG("all memory free");
    } else {
        BBQ_ERR_LOG("memory not all free");
    }
    return 0;
}