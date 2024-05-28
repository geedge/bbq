/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-28 19:52:59
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#ifndef _TEST_COMMON_H_
#define _TEST_COMMON_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

typedef enum {
    TEST_THREAD_PRODUCER,
    TEST_THREAD_CONSUMER,
    TEST_THREAD_TYPE_MAX,
} test_thread_type_e;

typedef struct {
    struct timespec timestamp; // 系统时间戳
    // uint64_t cycles;     // cpu运行的cycle
} test_time_metric;

typedef struct {
    uint64_t throughput;       // 吞吐量：每秒消耗的条目总数。
    double data_latency;       // 数据延迟：每个数据在队列中停留的平均时间。
    double op_latency;         // 操作延迟：每个入队或出队操作的平均延迟。
    double *fairness;          // 公平性：每个生产者/消费者的吞吐量（占总吞吐的百分比）
    double full_empty;         // 队列满时入队的延迟/队列空时出队的延迟（仅用于简单工作负载）。
    uint64_t oversubscription; // 比核心/超线程更多的生产者和消费者的吞吐量
} test_report;

typedef enum {
    TEST_WORKLOAD_SIMPLE,  // 简单负载，每个生产者或消费者都有自己的线程，它们在循环中不断执行入队或出队操作。每次出队后都会验证数据。
    TEST_WORKLOAD_COMPLEX, // 复杂负载，基于简单工作负载。生产者和消费者为数据分配空间，执行入队和出队，然后手动释放
    TEST_WORKLOAD_MAX,
} test_workload_e;

#define TEST_RING_TYPE_BBQ_STR "bbq"
#define TEST_RING_TYPE_DPDK_STR "dpdk"
typedef enum {
    TEST_RING_TYPE_BBQ,
    TEST_RING_TYPE_DPDK,
    TEST_RING_TYPE_MAX,
} test_ring_type;

typedef struct {
    char name[128];      // 配置文件名
    char introduce[128]; // 测试配置说明
    uint16_t cores_cnt;  // 测试用核心个数
} test_cfg_base;

typedef struct {
    test_ring_type ring_type; // ring buffer类型
    uint32_t producer_cnt;    // 生产者个数
    uint32_t consumer_cnt;    // 消费者个数
    test_workload_e workload; // 负载模式
    uint64_t entries_cnt;     // ring初始化时分配entry的个数
    uint32_t block_count;     // bbq block个数，为0时表示根据entries_cnt自动计算
} test_cfg_ring;

typedef struct {
    uint64_t enqueue_cnt; // 每个线程入队次数
    uint64_t dequeue_cnt; // 每个线程出队次数
    uint64_t run_time;    // 整体运行时间，单位秒
} test_cfg_run;

typedef struct {
    test_cfg_base base;
    test_cfg_ring ring;
    test_cfg_run run;
} test_cfg;

typedef enum {
    TEST_MODULE_UTEST,
    TEST_MODULE_BCM,
    TEST_MODULE_BCM_EXIT_DATA,
    TEST_MODULE_MAX,
} test_module_e;

extern test_time_metric test_clock_time_get();
extern test_time_metric test_clock_time_sub(test_time_metric now, test_time_metric last);
extern int test_load_config(const char *config, const char *ring_type, test_cfg *cfg);
extern test_workload_e test_workload_str2enum(const char *workload);
extern test_ring_type test_ring_type_str2enum(const char *ring_type);
extern bool test_clock_time_is_zero(test_time_metric *metric);
extern bool test_timespec_is_after(const struct timespec *a, const struct timespec *b);
extern char *test_ring_type_enum2str(test_ring_type ring_type);
extern uint64_t test_clock_time_to_ns(test_time_metric *metric);
extern uint64_t test_clock_time_to_double(test_time_metric *metric);
extern void *test_malloc(test_module_e module, size_t size);
extern void test_free(test_module_e module, void *ptr);
extern void test_memory_info();
#endif