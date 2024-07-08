/*
 * @Author: liuyu
 * @LastEditTime: 2024-07-07 22:13:33
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#include "bbq.h"
#include "iniparser.h"
#include "ut_bbq_func.h"

#include <string.h>

int ut_load_config(const char *config, const char *ring_type, uint32_t burst_cnt, struct ut_cfg *cfg) {
    int ret = 0;
    // 加载配置
    UT_INFO_LOG("load config:%s", config);
    dictionary *ini = iniparser_load(config);
    if (ini == NULL) {
        return -1;
    }

    strncpy(cfg->base.name, config, sizeof(cfg->base.name) - 1);
    cfg->base.name[sizeof(cfg->base.name) - 1] = '\0';

    // 获取键值
    const char *introduce = iniparser_getstring(ini, "base:introduce", "none");
    strncpy(cfg->base.introduce, introduce, sizeof(cfg->base.introduce) - 1);
    cfg->base.introduce[sizeof(cfg->base.introduce) - 1] = '\0'; // 手工写上 \0
    const char *cores = iniparser_getstring(ini, "base:cores", "unknown");
    ret = sscanf(cores, "%hu-%hu", &cfg->base.core_begin, &cfg->base.core_end);
    if (ret != 2) {
        UT_ERR_LOG("cores error: %s", cores);
        return -1;
    }

    const char *workload = iniparser_getstring(ini, "ring:workload", "unknown");
    cfg->ring.workload = ut_workload_str2enum(workload);
    cfg->ring.entries_cnt = iniparser_getuint64(ini, "ring:entries_cnt", 0);
    cfg->ring.producer_cnt = iniparser_getint(ini, "ring:producer_cnt", 0);
    cfg->ring.consumer_cnt = iniparser_getint(ini, "ring:consumer_cnt", 0);
    cfg->ring.block_count = iniparser_getint(ini, "ring:block_count", 0);
    cfg->run.run_ok_times = iniparser_getint(ini, "run:run_ok_times", 0);
    cfg->run.run_time = iniparser_getuint64(ini, "run:run_time", 0);

    // 设置ring_type
    cfg->ring.ring_type = ut_ring_type_str2enum(ring_type);
    if (cfg->ring.ring_type >= UT_RING_TYPE_MAX) {
        UT_ERR_LOG("unknown ring type:%d", cfg->ring.ring_type);
        return -1;
    }
    cfg->ring.burst_cnt = burst_cnt;

    if (cfg->ring.ring_type == UT_RING_TYPE_RMIND) {
        // rmind仅支持1个消费者，仅支持burst方式
        if (cfg->ring.consumer_cnt > 1) {
            UT_ERR_LOG("ring type:%s only support single consumer", UT_RING_TYPE_RMIND_STR);
            return -1;
        }

        if (cfg->ring.burst_cnt <= 1) {
            UT_ERR_LOG("ring type:%s only support burst_cnt > 1 !", UT_RING_TYPE_RMIND_STR);
            return -1;
        }

        if (cfg->ring.workload == UT_WORKLOAD_COMPLEX) {
            UT_ERR_LOG("ring type:%s only support simple workload !", UT_RING_TYPE_RMIND_STR);
            return -1;
        }
    }

    if (cfg->run.run_time == 0 && cfg->run.run_ok_times == 0) {
        UT_ERR_LOG("At least one of run_time or run_ok_times is not 0");
        return -1;
    }

    UT_INFO_LOG("introduce:%s", cfg->base.introduce);
    UT_INFO_LOG("cores:%u-%u", cfg->base.core_begin, cfg->base.core_end);
    UT_INFO_LOG("workload:%s(%u)", workload, cfg->ring.workload);
    UT_INFO_LOG("entries_cnt:%lu", cfg->ring.entries_cnt);
    UT_INFO_LOG("producer_cnt:%u", cfg->ring.producer_cnt);
    UT_INFO_LOG("consumer_cnt:%u", cfg->ring.consumer_cnt);
    UT_INFO_LOG("block_count:%u", cfg->ring.block_count);
    UT_INFO_LOG("run_ok_times:%lu", cfg->run.run_ok_times);
    UT_INFO_LOG("run_time:%lu", cfg->run.run_time);
    UT_INFO_LOG("ring_type:%s(%u)", ring_type, cfg->ring.ring_type);
    UT_INFO_LOG("burst_cnt:%u", burst_cnt);

    // 释放dictionary对象
    iniparser_freedict(ini);
    return 0;
}