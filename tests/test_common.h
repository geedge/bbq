/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-24 16:26:50
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#ifndef _TEST_COMMON_H_
#define _TEST_COMMON_H_

#include <time.h>

typedef struct {
    struct timespec st; // 系统时间戳
    // uint64_t cycles;     // cpu运行的cycle
} bcm_time_metric;

extern bcm_time_metric clock_time_get();
extern time_t clock_nstime_diff(bcm_time_metric *slow, bcm_time_metric *fast);
#endif