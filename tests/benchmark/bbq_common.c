/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-23 19:26:52
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#include "bbq_common.h"
#include "bbq_log.h"
#include "iniparser.h"
// #include <linux/time.h>
#include <string.h>

bcm_workload_e bcm_workload_str2enum(const char *workload) {
    if (strcmp(workload, "simple") == 0) {
        return BCM_WORKLOAD_SIMPLE;
    } else if (strcmp(workload, "complex") == 0) {
        return BCM_WORKLOAD_COMPLEX;
    }

    return BCM_WORKLOAD_MAX;
}

bcm_ring_type bcm_ring_type_str2enum(const char *ring_type) {
    if (strcmp(ring_type, "bbq") == 0) {
        return BCM_RING_TYPE_BBQ;
    } else if (strcmp(ring_type, "dpdk") == 0) {
        return BCM_RING_TYPE_DPDK;
    }

    return BCM_RING_TYPE_MAX;
}

bcm_time_metric clock_time_get() {
    bcm_time_metric metric = {0};
    struct timespec st;

    clock_gettime(CLOCK_MONOTONIC, &metric.st);

    return metric;
}

time_t clock_nstime_diff(bcm_time_metric *slow, bcm_time_metric *fast) {
    time_t time_ns_fast = fast->st.tv_sec * 1000 * 1000 * 1000 + fast->st.tv_nsec;
    time_t time_ns_slow = slow->st.tv_sec * 1000 * 1000 * 1000 + slow->st.tv_nsec;

    return time_ns_fast - time_ns_slow;
}

int bxm_load_config(const char *config, bcm_cfg *cfg) {
    // 加载配置
    dictionary *ini = iniparser_load(config);
    if (ini == NULL) {
        return -1;
    }

    BBQ_INFO_LOG("--------------load config:%s--------------", config);

    // 获取键值
    cfg->introduce = (char *)iniparser_getstring(ini, "base:introduce", "none");
    BBQ_INFO_LOG("introduce:%s", cfg->introduce);

    char *workload = (char *)iniparser_getstring(ini, "base:workload", "unknown");
    cfg->workload = bcm_workload_str2enum(workload);
    BBQ_INFO_LOG("workload:%s(%u)", workload, cfg->workload);

    char *ring_type = (char *)iniparser_getstring(ini, "base:ring_type", "unknown");
    cfg->ring_type = bcm_ring_type_str2enum(ring_type);
    BBQ_INFO_LOG("ring_type:%s(%u)", ring_type, cfg->ring_type);

    cfg->cores_cnt = iniparser_getint(ini, "base:cores_cnt", 0);
    BBQ_INFO_LOG("cores_cnt:%u", cfg->cores_cnt);
    if (cfg->cores_cnt <= 2) {
        BBQ_ERR_LOG("atleast 2 cores");
        perror("atleast 2 cores");
    }

    cfg->entries_cnt = iniparser_getuint64(ini, "base:entries_cnt", 0);
    BBQ_INFO_LOG("entries_cnt:%u", cfg->entries_cnt);

    cfg->producer_cnt = iniparser_getint(ini, "base:producer_cnt", 0);
    BBQ_INFO_LOG("producer_cnt:%u", cfg->producer_cnt);

    cfg->consumer_cnt = iniparser_getint(ini, "base:consumer_cnt", 0);
    BBQ_INFO_LOG("consumer_cnt:%u", cfg->consumer_cnt);

    cfg->enqueue_cnt = iniparser_getint(ini, "base:enqueue_cnt", 0);
    BBQ_INFO_LOG("enqueue_cnt:%lu", cfg->enqueue_cnt);

    cfg->dequeue_cnt = iniparser_getint(ini, "base:dequeue_cnt", 0);
    BBQ_INFO_LOG("dequeue_cnt:%lu", cfg->dequeue_cnt);

    // 释放dictionary对象
    iniparser_freedict(ini);
    return 0;
}