/*
 * @Author: liuyu
 * @LastEditTime: 2024-07-07 21:19:02
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#include "bbq.h"
#include "bcm_queue.h"
#include "iniparser.h"
#include "ut_bbq_func.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>

void bcm_report_printf(struct ut_cfg *cfg, struct ut_merge_data *data, struct ut_exit_data **raw_data, uint32_t thread_cnt, enum ut_thread_type ttype) {
    char name[10] = {0};
    double latency_ns = 0;
    double throughput = 0;

    printf("\n---------%s---------\n", ttype == UT_THREAD_PRODUCER ? "生产者" : "消费者");
    double use_time = ut_clock_time_to_double(&data->use_time);
    printf("执行时间 : %lf 秒\n", use_time);
    printf("执行次数 : %lu (burst=%u)\n", data->run_times, cfg->ring.burst_cnt);
    printf("成功%s : %lu\n", ttype == UT_THREAD_PRODUCER ? "入队" : "出队", data->ok_cnt);
    printf("数据错误次数 : %lu\n", data->data_error_cnt);

    // 同时有生产者、消费者时才输出
    if (cfg->ring.producer_cnt > 0 && cfg->ring.consumer_cnt > 0) {
        throughput = data->ok_cnt / use_time;
        printf("吞吐 :%.0lf/s (%e/s)\n", throughput, throughput);

        // 多生产者单消费者 或 单生产者多消费才输出
        if ((cfg->ring.producer_cnt == 1 && cfg->ring.consumer_cnt > 1) ||
            (cfg->ring.producer_cnt > 1 && cfg->ring.consumer_cnt == 1)) {
            for (uint32_t i = 0, bbq_head_idx = 1; i < thread_cnt; i++) {
                if (raw_data[i]->arg->ttype == ttype) {
                    struct ut_metric tmp_time = ut_clock_time_sub(raw_data[i]->metric_end, raw_data[i]->metric_start);
                    throughput = raw_data[i]->ok_cnt / ut_clock_time_to_double(&tmp_time);
                    printf("    %s-%d 吞吐 :%.0lf/s (%e/s)", name, bbq_head_idx, throughput, throughput);
                    bbq_head_idx++;
                }
            }
        }

        if (ttype == UT_THREAD_CONSUMER && cfg->ring.workload == UT_WORKLOAD_COMPLEX) {
            latency_ns = data->latency_ns * 1.0 / data->ok_cnt;
            printf("数据延迟 :%.0lf 纳秒 (%e)\n", latency_ns, latency_ns);
        }

        latency_ns = data->op_ok_latency_ns * 1.0 / data->ok_cnt;
        printf("操作延迟 :%.0lf 纳秒 (%e)\n", latency_ns, latency_ns);
    } else {
        latency_ns = data->op_err_latency_ns * 1.0 / (data->run_times - data->ok_cnt);
        if (ttype == UT_THREAD_PRODUCER) {
            printf("满队入队操作延迟 :%.0lf 纳秒 (%e)\n", latency_ns, latency_ns);
        } else {
            printf("空队出队操作延迟 :%.0lf 纳秒 (%e)\n", latency_ns, latency_ns);
        }
    }
}

void bcm_report_generate(struct ut_cfg *cfg, struct ut_exit_data **exit_data, uint32_t thread_cnt) {
    // ut_report  report;

    struct ut_merge_s merge = {0};
    ut_merge_all_data(exit_data, thread_cnt, &merge);

    printf("ring类型: %s\n", ut_ring_type_enum2str(cfg->ring.ring_type));
    printf("简介: %s\n", cfg->base.introduce);
    printf("配置: %s\n", cfg->base.name);
    if (cfg->ring.producer_cnt > 0) {
        bcm_report_printf(cfg, &merge.producer, exit_data, thread_cnt, UT_THREAD_PRODUCER);
    }

    if (cfg->ring.consumer_cnt > 0) {
        bcm_report_printf(cfg, &merge.consumer, exit_data, thread_cnt, UT_THREAD_CONSUMER);
    }

    if (cfg->ring.producer_cnt > 0 && cfg->ring.consumer_cnt > 0) {
        printf("生产消费个数验证: %s\n", merge.consumer.ok_cnt == merge.producer.ok_cnt ? "相等" : "不等!!!!!!!!!");
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
        config = "/root/code/c/bbq/perf/benchmark/config/compare/case1_simple_spsc.ini";
        ring_type = "dpdk";
        burst_cnt = 16;
        UT_ERR_LOG("use default config, ringt_type:%s burst:%u config:%s argc:%d", ring_type, burst_cnt, config, argc);
    }

    char thread_name[128] = {0};
    snprintf(thread_name, sizeof(thread_name), "main:%lu", pthread_self());
    prctl(PR_SET_NAME, thread_name);

    // 加载配置
    struct ut_info_s info;
    if (ut_load_config(config, ring_type, burst_cnt, &info.cfg) != 0) {
        UT_ERR_LOG("load config error");
        return -1;
    }

    // 队列初始化
    int ret = -1;
    struct ut_queue q;
    ret = bcm_queue_init(&info.cfg, &q);
    if (ret != 0) {
        UT_ERR_LOG("init failed :%d", ret);
        return ret;
    }

    // 创建线程
    pthread_t *threads = ut_threads_create(&info, &q);
    if (threads == NULL) {
        UT_ERR_LOG("pthread_arr is NULL");
        return ret;
    }

    // 等待所有线程完成，回收数据
    uint32_t thread_cnt = info.cfg.ring.producer_cnt + info.cfg.ring.consumer_cnt;
    struct ut_exit_data **exit_data = (struct ut_exit_data **)ut_malloc(UT_MODULE_BCM, sizeof(struct ut_exit_data **) * (thread_cnt));
    uint32_t i = 0;

    ut_wait_all_threads_exit(&info, thread_cnt, threads, exit_data);

    // 生成benchmark报告
    bcm_report_generate(&info.cfg, exit_data, thread_cnt);

    // 回收、释放数据
    for (i = 0; i < thread_cnt; i++) {
        ut_exit_data_destory(exit_data[i]);
    }
    ut_free(UT_MODULE_BCM, exit_data);
    ut_threads_destory(&info, threads);
    ut_queue_destory(&q);
    ut_memory_counter_print();

    return 0;
}