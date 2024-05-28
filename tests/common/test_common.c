/*
 * @Description: 描述信息
 * @Date: 2024-05-25 10:55:48
 * @LastEditTime: 2024-05-28 19:52:35
 */

#include "test_common.h"
#include "bbq.h"
#include "iniparser.h"
#include <string.h>
#include <sys/time.h>

typedef struct {
    aotmic_uint64 malloc_cnt;
    aotmic_uint64 free_cnt;
} test_memory_s;

test_memory_s test_memory_g[TEST_MODULE_MAX] = {0};

void *test_malloc(test_module_e module, size_t size) {
    void *ptr = malloc(size);
    if (ptr != NULL) {
        atomic_fetch_add(&test_memory_g[module].malloc_cnt, 1);
    }

    return ptr;
}

void test_free(test_module_e module, void *ptr) {
    if (ptr != NULL) {
        atomic_fetch_add(&test_memory_g[module].free_cnt, 1);
    }
    free(ptr);
}

bool test_malloc_free_equal() {
    bool ret = true;
    for (int i = 0; i < TEST_MODULE_MAX; i++) {
        uint64_t malloc_cnt = atomic_load(&test_memory_g[i].malloc_cnt);
        uint64_t free_cnt = atomic_load(&test_memory_g[i].free_cnt);
        if (malloc_cnt != free_cnt) {
            BBQ_ERR_LOG("[module:%d] malloc:%lu free:%lu, test malloc-free not equal\n", i, malloc_cnt, free_cnt);
            ret = false;
        }
    }
    return ret;
}

void test_memory_info() {
    for (int i = 0; i < TEST_MODULE_MAX; i++) {
        uint64_t malloc_cnt = atomic_load(&test_memory_g[i].malloc_cnt);
        uint64_t free_cnt = atomic_load(&test_memory_g[i].free_cnt);
        if (malloc_cnt == 0 && free_cnt == 0) {
            continue;
        }

        BBQ_INFO_LOG("[%d]test malloc:%lu free:%lu", i,
                     atomic_load(&test_memory_g[i].malloc_cnt),
                     atomic_load(&test_memory_g[i].free_cnt));
    }

    if (test_malloc_free_equal()) {
        BBQ_INFO_LOG("all memory free");
    } else {
        BBQ_ERR_LOG("memory not all free");
    }
}

int test_load_config(const char *config, const char *ring_type, test_cfg *cfg) {
    // 加载配置
    dictionary *ini = iniparser_load(config);
    if (ini == NULL) {
        return -1;
    }

    BBQ_INFO_LOG("load config:%s", config);
    strncpy(cfg->base.name, config, sizeof(cfg->base.name) - 1);
    cfg->base.name[sizeof(cfg->base.name) - 1] = '\0';

    // 获取键值
    char *introduce = (char *)iniparser_getstring(ini, "base:introduce", "none");
    strncpy(cfg->base.introduce, introduce, sizeof(cfg->base.introduce) - 1);
    cfg->base.introduce[sizeof(cfg->base.introduce) - 1] = '\0'; // 手工写上 \0

    BBQ_INFO_LOG("introduce:%s", cfg->base.introduce);

    cfg->base.cores_cnt = iniparser_getint(ini, "base:cores_cnt", 0);
    BBQ_INFO_LOG("cores_cnt:%u", cfg->base.cores_cnt);

    char *workload = (char *)iniparser_getstring(ini, "ring:workload", "unknown");
    cfg->ring.workload = test_workload_str2enum(workload);
    BBQ_INFO_LOG("workload:%s(%u)", workload, cfg->ring.workload);

    cfg->ring.entries_cnt = iniparser_getuint64(ini, "ring:entries_cnt", 0);
    BBQ_INFO_LOG("entries_cnt:%u", cfg->ring.entries_cnt);

    cfg->ring.producer_cnt = iniparser_getint(ini, "ring:producer_cnt", 0);
    BBQ_INFO_LOG("producer_cnt:%u", cfg->ring.producer_cnt);

    cfg->ring.consumer_cnt = iniparser_getint(ini, "ring:consumer_cnt", 0);
    BBQ_INFO_LOG("consumer_cnt:%u", cfg->ring.consumer_cnt);

    cfg->ring.block_count = iniparser_getint(ini, "ring:block_count", 0);
    BBQ_INFO_LOG("block_count:%u", cfg->ring.block_count);

    cfg->run.enqueue_cnt = iniparser_getint(ini, "run:enqueue_cnt", 0);
    BBQ_INFO_LOG("enqueue_cnt:%lu", cfg->run.enqueue_cnt);

    cfg->run.dequeue_cnt = iniparser_getint(ini, "run:dequeue_cnt", 0);
    BBQ_INFO_LOG("dequeue_cnt:%lu", cfg->run.dequeue_cnt);

    cfg->run.run_time = iniparser_getuint64(ini, "run:run_time", 0);
    BBQ_INFO_LOG("run_time:%lu", cfg->run.run_time);

    // 设置ring_type
    cfg->ring.ring_type = test_ring_type_str2enum(ring_type);
    BBQ_INFO_LOG("ring_type:%s(%u)", ring_type, cfg->ring.ring_type);

    // 释放dictionary对象
    iniparser_freedict(ini);
    return 0;
}

test_time_metric test_clock_time_get() {
    test_time_metric metric = {0};
    struct timespec timestamp;

    // clock_gettime(CLOCK_MONOTONIC, &metric.timestamp); //从系统启动这一刻起开始计时,不受系统时间被用户改变的影响
    clock_gettime(CLOCK_REALTIME, &metric.timestamp); // 系统实时时间,随系统实时时间改变而改变
    return metric;
}

uint64_t test_clock_time_to_ns(test_time_metric *metric) {
    return metric->timestamp.tv_nsec + metric->timestamp.tv_sec * 1000 * 1000 * 1000;
}

uint64_t test_clock_time_to_double(test_time_metric *metric) {
    return metric->timestamp.tv_sec +
           metric->timestamp.tv_nsec * 1.0 / 1000 / 1000 / 1000;
}

bool test_clock_time_is_zero(test_time_metric *metric) {
    return metric->timestamp.tv_sec == 0 && metric->timestamp.tv_nsec == 0;
}

bool test_timespec_is_after(const struct timespec *a, const struct timespec *b) {
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

test_time_metric test_clock_time_sub(test_time_metric now, test_time_metric last) {
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

test_workload_e test_workload_str2enum(const char *workload) {
    if (strcmp(workload, "simple") == 0) {
        return TEST_WORKLOAD_SIMPLE;
    } else if (strcmp(workload, "complex") == 0) {
        return TEST_WORKLOAD_COMPLEX;
    }

    return TEST_WORKLOAD_MAX;
}

char *ring_type_map[TEST_RING_TYPE_MAX] = {
    [TEST_RING_TYPE_BBQ] = TEST_RING_TYPE_BBQ_STR,
    [TEST_RING_TYPE_DPDK] = TEST_RING_TYPE_DPDK_STR,
};

test_ring_type test_ring_type_str2enum(const char *ring_type) {
    if (strcmp(ring_type, TEST_RING_TYPE_BBQ_STR) == 0) {
        return TEST_RING_TYPE_BBQ;
    } else if (strcmp(ring_type, TEST_RING_TYPE_DPDK_STR) == 0) {
        return TEST_RING_TYPE_DPDK;
    }

    return TEST_RING_TYPE_MAX;
}

char *test_ring_type_enum2str(test_ring_type ring_type) {
    if (ring_type >= TEST_RING_TYPE_MAX) {
        return "unknown";
    } else {
        return ring_type_map[ring_type];
    }
}