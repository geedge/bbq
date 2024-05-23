/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-23 22:28:57
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include "bbq.h"
#include "bcm_queue.h"
#include "common.h"
#include "iniparser.h"

typedef enum {
    BCM_PRODUCER,
    BCM_CONSUMER,
} bcm_threadtype_e;

typedef struct {
    bcm_threadtype_e ttype;
    int core;
    bcm_cfg* cfg;
    bcm_queue_t* q;
} bcm_thread_arg_s;

typedef struct {
    bcm_thread_arg_s* arg;
} bcm_exit_data;

void* thread_producer_start(void*) {
    bcm_exit_data* exit_data = (bcm_exit_data*)malloc(sizeof(bcm_exit_data));
    printf("111\n");
    pthread_exit(exit_data);
}

void* thread_consumer_start(void*) {
    bcm_exit_data* exit_data = (bcm_exit_data*)malloc(sizeof(bcm_exit_data));
    printf("222\n");
    pthread_exit(exit_data);
}

pthread_t*
bcm_one_thread_creat(bcm_cfg* cfg, bcm_queue_t* q, bcm_threadtype_e ttype, int core, pthread_t* thread) {
    BBQ_INFO_LOG("thread type:%d core:%d", ttype, core);

    bcm_thread_arg_s* arg = (bcm_thread_arg_s*)malloc(sizeof(bcm_thread_arg_s));  // 线程回收时free
    arg->cfg = cfg;
    arg->q = q;
    arg->ttype = ttype;
    arg->core = core;

    if (ttype == BCM_PRODUCER) {
        pthread_create(thread, NULL, thread_producer_start, arg);
    } else {
        pthread_create(thread, NULL, thread_consumer_start, arg);
    }
}

#define CORE_ID_CHK_SET(core_id, max_id)                            \
    do {                                                            \
        core_id = (core_id + 1) < max_id ? (core_id + 1) : core_id; \
    } while (0)

pthread_t* bcm_threads_creat(bcm_cfg* cfg, bcm_queue_t* q) {
    // 创建生产者消费者线程
    int ret;
    size_t thread_id = 0;
    int core_id = 0;
    size_t thread_cnt = cfg->producer_cnt + cfg->consumer_cnt;
    pthread_t* threads = (pthread_t*)malloc(sizeof(pthread_t) * thread_cnt);  // 存储所有线程ID的数组

    // MPSC 或 SPMC 场景在第一个核心/超线程上分配单个生产者或消费者，然后将其他线程按顺序分配给核心/超线程。
    // MPMC，我们将生产者和消费者一一交错分配
    // 如果数量不同，则在最后分配剩余部分。
    if (cfg->producer_cnt == 1 && cfg->consumer_cnt >= 1) {
        // SPMC，第一个核心给生产者，其他分配给消费者
        bcm_one_thread_creat(cfg, q, BCM_PRODUCER, core_id, &(threads[thread_id++]));
        for (int i = 0; i < cfg->consumer_cnt; i++) {
            CORE_ID_CHK_SET(core_id, cfg->cores_cnt);
            bcm_one_thread_creat(cfg, q, BCM_CONSUMER, core_id, &(threads[thread_id++]));
        }
    } else if (cfg->consumer_cnt == 1 && cfg->producer_cnt >= 1) {
        // MPSC，第一个核心给消费者，其他分配给生产者
        bcm_one_thread_creat(cfg, q, BCM_CONSUMER, core_id, &(threads[thread_id++]));
        for (int i = 0; i < cfg->producer_cnt; i++) {
            CORE_ID_CHK_SET(core_id, cfg->cores_cnt);
            bcm_one_thread_creat(cfg, q, BCM_PRODUCER, core_id, &(threads[thread_id++]));
        }
    } else {
        // MPMC 或 只有生产者 或这有消费者，核心交错分配
        uint32_t pcnt = cfg->producer_cnt;  // 生产者个数
        uint32_t ccnt = cfg->consumer_cnt;  // 消费者个数
        for (core_id = 0; core_id < cfg->cores_cnt && pcnt > 0 && ccnt > 0;) {
            if ((core_id & 1) == 0) {
                // 偶数
                bcm_one_thread_creat(cfg, q, BCM_PRODUCER, core_id, &(threads[thread_id++]));
                pcnt--;
            } else {
                bcm_one_thread_creat(cfg, q, BCM_CONSUMER, core_id, &(threads[thread_id++]));
                ccnt--;
            }
            CORE_ID_CHK_SET(core_id, cfg->cores_cnt);
        }

        for (int i = 0; i < pcnt; i++, pcnt--) {
            bcm_one_thread_creat(cfg, q, BCM_PRODUCER, core_id, &(threads[thread_id++]));
            CORE_ID_CHK_SET(core_id, cfg->cores_cnt);
        }

        for (int i = 0; i < ccnt; i++, ccnt--) {
            bcm_one_thread_creat(cfg, q, BCM_CONSUMER, core_id, &(threads[thread_id++]));
            CORE_ID_CHK_SET(core_id, cfg->cores_cnt);
        }
    }

    return threads;
}

int main(int argc, char* argv[]) {
    char* config;
    if (argc == 2) {
        config = argv[1];
    } else {
        config = "/root/code/c/bbq/tests/benchmark/config/case1.ini";
        BBQ_ERR_LOG("!!!!!! not input config path, use default config:%s", config);
    }

    // 加载配置
    bcm_cfg cfg;
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
    pthread_t* threads;
    threads = bcm_threads_creat(&cfg, &q);
    if (threads == NULL) {
        BBQ_ERR_LOG("pthread_arr is NULL");
        return ret;
    }

    // 等待所有线程完成，回收数据
    uint32_t thread_cnt = cfg.producer_cnt + cfg.consumer_cnt;
    bcm_exit_data** exit_data = (bcm_exit_data**)malloc(sizeof(bcm_exit_data**) * (thread_cnt));
    uint32_t i = 0;
    for (i = 0; i < thread_cnt; i++) {
        pthread_join(threads[i], (void**)(&exit_data[i]));  // 等待每个线程结束
    }

    // 输出测试报告
    BBQ_INFO_LOG("report\n");

    return 0;
}