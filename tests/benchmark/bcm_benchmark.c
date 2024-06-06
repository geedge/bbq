/*
 * @Author: liuyu
 * @LastEditTime: 2024-06-06 18:28:48
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#include "bbq.h"
#include "bcm_queue.h"
#include "iniparser.h"
#include "test_mix.h"
#include "test_queue.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>

extern void bbq_memory_info();
extern bool bbq_malloc_free_equal();

void bcm_report_printf(test_cfg *cfg, test_merge_data *data, test_exit_data **raw_data, uint32_t thread_cnt, test_thread_type_e ttype) {
    char name[10] = {0};
    double latency_ns = 0;
    double throughput = 0;

    if (ttype == TEST_THREAD_PRODUCER) {
        strcpy(name, "生产者");
    } else {
        strcpy(name, "消费者");
    }

    BBQ_COLOR_PRINT("\n---------%s---------", name);
    double use_time = test_clock_time_to_double(&data->use_time);
    BBQ_COLOR_PRINT("执行时间 : %lf 秒", use_time);
    BBQ_COLOR_PRINT("执行次数 : %lu", data->run_times);
    BBQ_COLOR_PRINT("成功次数 : %lu", data->ok_cnt);
    BBQ_COLOR_PRINT("数据错误次数 : %lu", data->data_error_cnt);

    // 同时有生产者、消费者时才输出
    if (cfg->ring.producer_cnt > 0 && cfg->ring.consumer_cnt > 0) {
        throughput = data->ok_cnt / use_time;
        BBQ_COLOR_PRINT("吞吐 :%.0lf/s (%e/s)", throughput, throughput);

        // 多生产者单消费者 或 单生产者多消费才输出
        if ((cfg->ring.producer_cnt == 1 && cfg->ring.consumer_cnt > 1) ||
            (cfg->ring.producer_cnt > 1 && cfg->ring.consumer_cnt == 1)) {
            for (uint32_t i = 0, bbq_idx = 1; i < thread_cnt; i++) {
                if (raw_data[i]->arg->ttype == ttype) {
                    test_time_metric tmp_time = test_clock_time_sub(raw_data[i]->metric_end, raw_data[i]->metric_start);
                    throughput = raw_data[i]->ok_cnt / test_clock_time_to_double(&tmp_time);
                    BBQ_COLOR_PRINT("    %s-%d 吞吐 :%.0lf/s (%e/s)", name, bbq_idx, throughput, throughput);
                    bbq_idx++;
                }
            }
        }

        if (ttype == TEST_THREAD_CONSUMER && cfg->ring.workload == TEST_WORKLOAD_COMPLEX) {
            latency_ns = data->latency_ns * 1.0 / data->ok_cnt;
            BBQ_COLOR_PRINT("数据延迟 :%.0lf 纳秒 (%e)", latency_ns, latency_ns);
        }

        latency_ns = data->op_ok_latency_ns * 1.0 / data->ok_cnt;
        BBQ_COLOR_PRINT("操作延迟 :%.0lf 纳秒 (%e)", latency_ns, latency_ns);
    } else {
        latency_ns = data->op_err_latency_ns * 1.0 / (data->run_times - data->ok_cnt);
        if (ttype == TEST_THREAD_PRODUCER) {
            BBQ_COLOR_PRINT("满队入队操作延迟 :%.0lf 纳秒 (%e)", latency_ns, latency_ns);
        } else {
            BBQ_COLOR_PRINT("空队出队操作延迟 :%.0lf 纳秒 (%e)", latency_ns, latency_ns);
        }
    }
}

void bcm_report_generate(test_cfg *cfg, test_exit_data **exit_data, uint32_t thread_cnt) {
    // test_report report;

    test_merge_s merge = {0};
    test_merge_all_data(exit_data, thread_cnt, &merge);

    BBQ_COLOR_PRINT("ring类型: %s", test_ring_type_enum2str(cfg->ring.ring_type));
    BBQ_COLOR_PRINT("简介: %s", cfg->base.introduce);
    BBQ_COLOR_PRINT("配置: %s", cfg->base.name);
    if (cfg->ring.producer_cnt > 0) {
        bcm_report_printf(cfg, &merge.producer, exit_data, thread_cnt, TEST_THREAD_PRODUCER);
    }

    if (cfg->ring.consumer_cnt > 0) {
        bcm_report_printf(cfg, &merge.consumer, exit_data, thread_cnt, TEST_THREAD_CONSUMER);
    }

    if (cfg->ring.producer_cnt > 0 && cfg->ring.consumer_cnt > 0) {
        BBQ_COLOR_PRINT("生产消费个数验证: %s", merge.consumer.ok_cnt == merge.producer.ok_cnt ? "相等" : "不等!!!!!!!!!");
    }
}

int main(int argc, char *argv[]) {
    char *config;
    char *ring_type;
    uint32_t burst_cnt = 0;

    if (argc == 4) {
        config = argv[1];
        ring_type = argv[2];
        burst_cnt = strtoul(argv[3], NULL, 0);
        if (burst_cnt <= 0) {
            burst_cnt = 1;
        }
    } else {
        config = "/root/code/c/bbq-ly/tests/benchmark/config/compare/general/case8_simple_mpmc.ini";
        ring_type = "rmind";
        // ring_type = "dpdk";
        // ring_type = "bbq";
        burst_cnt = 100;
        BBQ_ERR_LOG("use default config, ringt_type:%s burst:%u config:%s argc:%d", ring_type, burst_cnt, config, argc);
    }

    char thread_name[128] = {0};
    snprintf(thread_name, sizeof(thread_name), "main:%x", pthread_self());
    prctl(PR_SET_NAME, thread_name);

    // 加载配置
    test_cfg cfg;
    if (test_load_config(config, ring_type, burst_cnt, &cfg) != 0) {
        BBQ_ERR_LOG("input config path");
        return -1;
    }

    // 队列初始化
    int ret = -1;
    test_queue_s q;
    ret = bcm_queue_init(&cfg, &q);
    if (ret != 0) {
        BBQ_ERR_LOG("init failed :%d", ret);
        return ret;
    }

    // 创建线程
    pthread_t *threads = test_threads_create(&cfg, &q);
    if (threads == NULL) {
        BBQ_ERR_LOG("pthread_arr is NULL");
        return ret;
    }

    test_check_run_time(&cfg);
    pthread_t thread_run_time;

    // 等待所有线程完成，回收数据
    uint32_t thread_cnt = cfg.ring.producer_cnt + cfg.ring.consumer_cnt;
    test_exit_data **exit_data = (test_exit_data **)test_malloc(TEST_MODULE_BCM, sizeof(test_exit_data **) * (thread_cnt));
    uint32_t i = 0;

    for (i = 0; i < thread_cnt; i++) {
        pthread_join(threads[i], (void **)(&exit_data[i])); // 等待每个线程结束
    }

    // 生成benchmark报告
    bcm_report_generate(&cfg, exit_data, thread_cnt);

    // 回收、释放数据
    for (i = 0; i < thread_cnt; i++) {
        test_exit_data_destory(exit_data[i]);
    }
    test_free(TEST_MODULE_BCM, exit_data);
    test_threads_destory(threads);
    test_queue_destory(&q);
    bbq_memory_info();
    test_memory_info();

    return 0;
}