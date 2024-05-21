/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-21 17:52:02
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */

#ifndef _UNITTEST_H_
#define _UNITTEST_H_
#include <stdint.h>

typedef struct {
    uint64_t v1;
    uint64_t v2;
} testdata_s;

typedef enum {
    THREAD_PRODUCER,
    THREAD_CONSUMER,
} thread_type;
typedef struct {
    BlockBoundedQueue_s *q;
    uint32_t usleep; // 该线程每次执行间隔睡眠时间
} thread_arg_s;

typedef struct {
    pthread_t thread_id;
    thread_type type;   // 线程类型
    size_t exec_times;  // 执行次数
    size_t succ_times;  // 成功次数
    size_t full_times;  // 失败，队列满次数
    size_t busy_times;  // 失败，队列忙次数
    size_t empty_times; // 失败，队列空次数
    testdata_s *data;   // 线程数据(v1 > 0的数据才有效)
    void *arg;          // 创建线程时传入的参数
} thread_data_s;
#endif