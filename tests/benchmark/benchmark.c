/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-23 19:53:39
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#include "bbq.h"
#include "bcm_queue.h"
#include "common.h"
#include "iniparser.h"
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

typedef enum {
    BCM_PRODUCER,
    BCM_CONSUMER,
} bcm_threadtype_e;

pthread_t *
bcm_one_thread_creat(bcm_cfg *cfg, bcm_queue_t *q, bcm_threadtype_e ttype, int core, pthread_t *threads) {
    BBQ_INFO_LOG("thread type:%d core:%d", ttype, core);
}

pthread_t *bcm_threads_creat(bcm_cfg *cfg, bcm_queue_t *q) {
    // 创建生产者消费者线程
    int ret;
    size_t thread_id = 0;
    int core_id = 0;
    size_t thread_cnt = cfg->producer_cnt + cfg->consumer_cnt;
    pthread_t *threads = (pthread_t *)malloc(sizeof(pthread_t) * thread_cnt); // 存储所有线程ID的数组

    // MPSC 或 SPMC 场景在第一个核心/超线程上分配单个生产者或消费者，然后将其他线程按顺序分配给核心/超线程。
    // MPMC，我们将生产者和消费者一一交错分配
    // 如果数量不同，则在最后分配剩余部分。
    if (cfg->producer_cnt == 1 && cfg->consumer_cnt >= 1) {
        // SPMC，第一个核心给生产者，其他分配给消费者
        bcm_one_thread_creat(cfg, q, BCM_PRODUCER, core_id, &(threads[thread_id++]));
        for (int i = 0; i < cfg->consumer_cnt; i++) {
            core_id = (core_id + 1) < cfg->cores_cnt ? (core_id + 1) : core_id;
            bcm_one_thread_creat(cfg, q, BCM_CONSUMER, core_id, &(threads[thread_id++]));
        }
    } else if (cfg->consumer_cnt == 1 && cfg->producer_cnt >= 1) {
        // MPSC，第一个核心给消费者，其他分配给生产者
        bcm_one_thread_creat(cfg, q, BCM_CONSUMER, core_id, &(threads[thread_id++]));
        for (int i = 0; i < cfg->producer_cnt; i++) {
            core_id = (core_id + 1) < cfg->cores_cnt ? (core_id + 1) : core_id;
            bcm_one_thread_creat(cfg, q, BCM_PRODUCER, core_id, &(threads[thread_id++]));
        }
    } else {
        // MPMC 或 只有生产者 或这有消费者，核心交错分配
        uint32_t pcnt = cfg->producer_cnt; // 生产者个数
        uint32_t ccnt = cfg->consumer_cnt; // 消费者个数
        for (core_id = 0; core_id < cfg->cores_cnt && pcnt > 0 && ccnt > 0; core_id++) {
            if (core_id & 1 == 0) {
                // 偶数
                bcm_one_thread_creat(cfg, q, BCM_PRODUCER, core_id, &(threads[thread_id++]));
                pcnt--;
            } else {
                bcm_one_thread_creat(cfg, q, BCM_CONSUMER, core_id, &(threads[thread_id++]));
                ccnt--;
            }
        }

        core_id = cfg->cores_cnt - 1;
        for (int i = 0; i < pcnt; i++, pcnt--) {
            bcm_one_thread_creat(cfg, q, BCM_PRODUCER, core_id, &(threads[thread_id++]));
        }

        for (int i = 0; i < ccnt; i++, ccnt--) {
            bcm_one_thread_creat(cfg, q, BCM_CONSUMER, core_id, &(threads[thread_id++]));
        }
    }

    // // 创建线程
    // if (cthread_cnt > 0) {
    //     // 消费者线程（先启动，读取空队列的场景）
    //     for (size_t i = 0; i < cthread_cnt; i++, thread_id++) {
    //         utest_thread_create(&threads[thread_id], utest_consumer_thread, q, cthread_usleep, false, thread_exec_times);
    //     }
    // }

    // // 创建生产者线程
    // if (pthread_cnt > 0) {
    //     for (size_t i = 0; i < pthread_cnt; i++, thread_id++) {
    //         utest_thread_create(&threads[thread_id], utest_producer_thread, q, pthread_usleep, false, thread_exec_times);
    //     }
    // }

    // thread_data_s **exit_data = (thread_data_s **)malloc(sizeof(thread_data_s **) * (thread_cnt + 1));
}

int main(int argc, char *argv[]) {
    char *config;
    if (argc == 2) {
        config = argv[1];
    } else {
        config = "/root/code/c/bbq-ly/tests/benchmark/config/case1.ini";
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
    pthread_t *pthread_arr;
    pthread_arr = bcm_threads_creat(&cfg, &q);
    if (pthread_arr == NULL) {
        BBQ_ERR_LOG("pthread_arr is NULL");
        return ret;
    }

    // 等待所有线程完成，回收数据
    // uint32_t i = 0;
    // for (i = 0; i < thread_cnt; i++) {
    //     pthread_join(threads[i], (void **)(&exit_data[i])); // 等待每个线程结束
    // }

    // 输出测试报告
    BBQ_INFO_LOG("report\n");

    return 0;
}