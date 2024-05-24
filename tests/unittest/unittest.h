/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-22 00:17:49
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
    THREAD_TYPE_MAX,
} thread_type;

typedef struct {
    bbq_queue_s *q;
    uint32_t usleep;            // 该线程每次执行间隔睡眠时间
    bool until_end;             // 循环读取，直到队列空或满
    uint64_t thread_exec_times; // 每个线程生产/消费次数
} thread_arg_s;

typedef struct {
    pthread_t thread_id;
    thread_type type;     // 线程类型
    uint64_t exec_times;  // 执行次数
    uint64_t succ_times;  // 成功次数
    uint64_t full_times;  // 失败，队列满次数
    uint64_t busy_times;  // 失败，队列忙次数
    uint64_t empty_times; // 失败，队列空次数
    testdata_s *data;     // 线程数据
    thread_arg_s *arg;    // 创建线程时传入的参数
} thread_data_s;
#endif