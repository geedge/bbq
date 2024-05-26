/*
 * @Description: 描述信息
 * @Date: 2024-05-25 10:55:48
 * @LastEditTime: 2024-05-26 23:52:08
 */

#include "test_common.h"
#include <string.h>
#include <sys/time.h>
#include "bbq.h"
#include "iniparser.h"

int bxm_load_config(const char* config, test_cfg* cfg) {
    // 加载配置
    dictionary* ini = iniparser_load(config);
    if (ini == NULL) {
        return -1;
    }

    BBQ_INFO_LOG("--------------load config:%s--------------", config);

    // 获取键值
    cfg->base.introduce = (char*)iniparser_getstring(ini, "base:introduce", "none");
    BBQ_INFO_LOG("introduce:%s", cfg->base.introduce);

    cfg->base.cores_cnt = iniparser_getint(ini, "base:cores_cnt", 0);
    BBQ_INFO_LOG("cores_cnt:%u", cfg->base.cores_cnt);

    char* ring_type = (char*)iniparser_getstring(ini, "ring:ring_type", "unknown");
    cfg->ring.ring_type = bcm_ring_type_str2enum(ring_type);
    BBQ_INFO_LOG("ring_type:%s(%u)", ring_type, cfg->ring.ring_type);

    char* workload = (char*)iniparser_getstring(ini, "ring:workload", "unknown");
    cfg->ring.workload = bcm_workload_str2enum(workload);
    BBQ_INFO_LOG("workload:%s(%u)", workload, cfg->ring.workload);

    cfg->ring.entries_cnt = iniparser_getuint64(ini, "ring:entries_cnt", 0);
    BBQ_INFO_LOG("entries_cnt:%u", cfg->ring.entries_cnt);

    cfg->ring.producer_cnt = iniparser_getint(ini, "ring:producer_cnt", 0);
    BBQ_INFO_LOG("producer_cnt:%u", cfg->ring.producer_cnt);

    cfg->ring.consumer_cnt = iniparser_getint(ini, "ring:consumer_cnt", 0);
    BBQ_INFO_LOG("consumer_cnt:%u", cfg->ring.consumer_cnt);

    cfg->run.enqueue_cnt = iniparser_getint(ini, "run:enqueue_cnt", 0);
    BBQ_INFO_LOG("enqueue_cnt:%lu", cfg->run.enqueue_cnt);

    cfg->run.dequeue_cnt = iniparser_getint(ini, "run:dequeue_cnt", 0);
    BBQ_INFO_LOG("dequeue_cnt:%lu", cfg->run.dequeue_cnt);

    cfg->run.run_time = iniparser_getuint64(ini, "run:run_time", 0);
    BBQ_INFO_LOG("run_time:%lu", cfg->run.run_time);

    // 释放dictionary对象
    iniparser_freedict(ini);
    return 0;
}

test_time_metric clock_time_get() {
    test_time_metric metric = {0};
    struct timespec timestamp;

    clock_gettime(CLOCK_MONOTONIC, &metric.timestamp);

    return metric;
}

bool clock_time_is_zero(test_time_metric* metric) {
    return metric->timestamp.tv_sec == 0 && metric->timestamp.tv_nsec;
}

bool timespec_is_after(const struct timespec* a, const struct timespec* b) {
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

test_time_metric clock_time_sub(test_time_metric now, test_time_metric last) {
    test_time_metric diff = {
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

test_workload_e bcm_workload_str2enum(const char* workload) {
    if (strcmp(workload, "simple") == 0) {
        return TEST_WORKLOAD_SIMPLE;
    } else if (strcmp(workload, "complex") == 0) {
        return TEST_WORKLOAD_COMPLEX;
    }

    return TEST_WORKLOAD_MAX;
}

test_ring_type bcm_ring_type_str2enum(const char* ring_type) {
    if (strcmp(ring_type, "bbq") == 0) {
        return TEST_RING_TYPE_BBQ;
    } else if (strcmp(ring_type, "dpdk") == 0) {
        return TEST_RING_TYPE_DPDK;
    }

    return TEST_RING_TYPE_MAX;
}