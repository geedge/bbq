/*
 * @Author: liuyu
 * @LastEditTime: 2024-07-07 21:57:13
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#pragma once

#include "bbq.h"
#include <pthread.h>

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#ifndef __cplusplus
// C
#include <stdatomic.h>
#endif

enum ut_thread_type {
    UT_THREAD_PRODUCER,
    UT_THREAD_CONSUMER,
    UT_THREAD_TYPE_MAX,
};

struct ut_metric {
    struct timespec timestamp; // 系统时间戳
    // uint64_t cycles;     // cpu运行的cycle
};

struct ut_report {
    uint64_t throughput;       // 吞吐量：每秒消耗的条目总数。
    double data_latency;       // 数据延迟：每个数据在队列中停留的平均时间。
    double op_latency;         // 操作延迟：每个入队或出队操作的平均延迟。
    double *fairness;          // 公平性：每个生产者/消费者的吞吐量（占总吞吐的百分比）
    double full_empty;         // 队列满时入队的延迟/队列空时出队的延迟（仅用于简单工作负载）。
    uint64_t oversubscription; // 比核心/超线程更多的生产者和消费者的吞吐量
};

enum ut_workload {
    UT_WORKLOAD_SIMPLE,  // 简单负载，每个生产者或消费者都有自己的线程，它们在循环中不断执行入队或出队操作。每次出队后都会验证数据。
    UT_WORKLOAD_COMPLEX, // 复杂负载，基于简单工作负载。生产者和消费者为数据分配空间，执行入队和出队，然后手动释放
    UT_WORKLOAD_MAX,
};

#define UT_RING_TYPE_BBQ_STR "bbq"
#define UT_RING_TYPE_DPDK_STR "dpdk"
#define UT_RING_TYPE_RMIND_STR "rmind"
enum ut_ring_type {
    UT_RING_TYPE_BBQ,
    UT_RING_TYPE_DPDK,
    UT_RING_TYPE_RMIND,
    UT_RING_TYPE_MAX,
};

struct ut_cfg_base {
    char name[128];      // 配置文件名
    char introduce[128]; // 测试配置说明
    uint16_t core_begin; // 起始核心
    uint16_t core_end;   // 终止核心
};

struct ut_cfg_ring {
    enum ut_ring_type ring_type; // ring buffer类型
    uint32_t producer_cnt;       // 生产者个数
    uint32_t consumer_cnt;       // 消费者个数
    enum ut_workload workload;   // 负载模式
    uint64_t entries_cnt;        // ring初始化时分配entry的个数
    uint32_t block_count;        // bbq block个数，为0时表示根据entries_cnt自动计算
    uint32_t burst_cnt;          // 批量出入队个数
};

struct ut_cfg_run {
    uint64_t run_ok_times; // 成功入队/入队次数
    uint64_t run_time;     // 整体运行时间，单位秒
};

struct ut_cfg {
    struct ut_cfg_base base;
    struct ut_cfg_ring ring;
    struct ut_cfg_run run;
};

struct ut_ctl {
    volatile bool running; // 默认为true，当设置为false，即所有生产者消费者即将退出
    pthread_barrier_t all_threads_start;
#ifndef __cplusplus
    // C
    atomic_uint producer_exit;
#else
    // C++ 为了兼容gtest测试
    std::atomic<uint32_t> producer_exit;
#endif
};

struct ut_info_s {
    struct ut_cfg cfg;
    struct ut_ctl ctl;
};

enum ut_module {
    UT_MODULE_UTEST,
    UT_MODULE_COMMON,
    UT_MODULE_DATA,
    UT_MODULE_BCM,
    UT_MODULE_TABLE,
    UT_MODULE_RMIND,
    UT_MODULE_MAX,
};

#ifdef UT_DEBUG
#define UT_DBG_LOG(fmt, ...)                                                             \
    do {                                                                                 \
        printf("[DBG][%s:%d:%s]" fmt "\n", __func__, __LINE__, __FILE__, ##__VA_ARGS__); \
    } while (0)

#else
#define UT_DBG_LOG(fmt, ...) \
    do {                     \
    } while (0)
#endif

#define UT_ERR_LOG(fmt, ...)                                                                             \
    do {                                                                                                 \
        printf("\x1b[31m [ERR][%s:%d:%s]" fmt "\x1b[0m\n", __func__, __LINE__, __FILE__, ##__VA_ARGS__); \
    } while (0)

#define UT_INFO_LOG(fmt, ...)                                                             \
    do {                                                                                  \
        printf("[INFO][%s:%d:%s]" fmt "\n", __func__, __LINE__, __FILE__, ##__VA_ARGS__); \
    } while (0)

#define UT_AVOID_WARNING(param) ((void)param)

#define UT_PTR_ARRAY_DATA_INIT(table, t_type, t_count)                           \
    do {                                                                         \
        if (table != NULL) {                                                     \
            memset(table, 0, sizeof(t_type *) * t_count);                        \
            for (uint32_t i = 0; i < t_count; i++) {                             \
                table[i] = (t_type *)ut_malloc(UT_MODULE_TABLE, sizeof(t_type)); \
                if (table[i] == NULL) {                                          \
                    for (uint32_t j = 0; j < i; j++) {                           \
                        ut_free(UT_MODULE_TABLE, table[j]);                      \
                        table[j] = NULL;                                         \
                    }                                                            \
                    ut_free(UT_MODULE_TABLE, table);                             \
                    break;                                                       \
                }                                                                \
                *table[i] = (t_type)UT_DATA_MAGIC;                               \
            }                                                                    \
        }                                                                        \
    } while (0)

#define UT_PTR_ARRAY_DATA_DESTORY(table, t_count)    \
    do {                                             \
        if (table != NULL) {                         \
            for (uint32_t i = 0; i < t_count; i++) { \
                ut_free(UT_MODULE_TABLE, table[i]);  \
                table[i] = NULL;                     \
            }                                        \
        }                                            \
    } while (0)

#define UT_DOUBLE_PTR_DATA_INIT(table, t_type, t_count)                            \
    do {                                                                           \
        table = (t_type **)ut_malloc(UT_MODULE_TABLE, sizeof(t_type *) * t_count); \
        if (table != NULL) {                                                       \
            UT_PTR_ARRAY_DATA_INIT(table, t_type, t_count);                        \
        }                                                                          \
    } while (0)

#define UT_DOUBLE_PTR_DATA_DESTORY(table, t_count)     \
    do {                                               \
        if (table != NULL) {                           \
            UT_PTR_ARRAY_DATA_DESTORY(table, t_count); \
            ut_free(UT_MODULE_TABLE, table);           \
            table = NULL;                              \
        }                                              \
    } while (0)

#define UT_ARRAY_DATA_INIT(table, t_count)  \
    do {                                    \
        for (int i = 0; i < t_count; i++) { \
            table[i] = UT_DATA_MAGIC;       \
        }                                   \
    } while (0)

#define UT_DATA_MAGIC 0x1F // 为了兼容所有类型的数据，存储1字节大小的数据

typedef void (*ut_ring_free_f)(void *ring);
typedef int (*ut_ring_enqueue_f)(void *ring, void *obj);
typedef int (*ut_ring_dequeue_f)(void *ring, void *obj);
typedef uint32_t (*ut_enqueue_burst_f)(void *ring, void **obj_table, uint32_t n, uint16_t thread_idx, uint32_t *wait_consumed);
typedef uint32_t (*ut_dequeue_burst_f)(void *ring, void **obj_table, uint32_t n, uint32_t *wait_consumed);
typedef bool (*ut_ring_empty_f)(void *ring);

struct ut_queue {
    void *ring;
    enum ut_ring_type ring_type;
    ut_ring_free_f ring_free_f;
    ut_ring_enqueue_f enqueue_f;
    ut_ring_dequeue_f dequeue_f;
    ut_enqueue_burst_f enqueue_burst_f;
    ut_dequeue_burst_f dequeue_burst_f;
};

struct ut_thread_arg {
    int core;
    uint16_t thread_idx; // 线程索引，不是pthread_id
    enum ut_thread_type ttype;
    struct ut_info_s *info;
    struct ut_queue *q;
};

struct ut_data {
    uint64_t data;                 // 数据
    struct ut_metric enqueue_time; // 入队时间
};

struct ut_exit_data {
    pthread_t thread_id;
    uint64_t run_times;
    uint64_t ok_cnt;
    uint64_t latency_ns;        // 仅消费者有效，数据停留的时延
    uint64_t op_ok_latency_ns;  // 成功操作的时延
    uint64_t op_err_latency_ns; // 操作失败的时延， 如满队入队，空队出队
    uint64_t data_error_cnt;    // 发生过至少一次数据不一致的次数
    size_t simple_data_cnt;
    struct ut_thread_arg *arg;
    struct ut_metric metric_start;
    struct ut_metric metric_end;
    struct ut_data **simple_data;
};

struct ut_merge_data {
    uint64_t run_times;
    uint64_t ok_cnt;
    uint64_t latency_ns;        // 仅消费者有效，数据停留的时延
    uint64_t op_ok_latency_ns;  // 成功操作的时延
    uint64_t op_err_latency_ns; // 操作失败的时延， 如满队入队，空队出队
    uint64_t data_error_cnt;    // 发生过至少一次数据不一致的次数
    struct ut_metric use_time;
};

struct ut_merge_s {
    struct ut_merge_data producer;
    struct ut_merge_data consumer;
};

enum ut_data_type {
    UT_DATA_MAGIC_TYPE,
    UT_DATA_UINTPTR_TYPE,
};
extern struct ut_metric ut_clock_time_get();
extern struct ut_metric ut_clock_time_sub(struct ut_metric now, struct ut_metric last);
extern int ut_load_config(const char *config, const char *ring_type, uint32_t burst_cnt, struct ut_cfg *cfg);
extern enum ut_workload ut_workload_str2enum(const char *workload);
extern enum ut_ring_type ut_ring_type_str2enum(const char *ring_type);
extern bool ut_clock_time_is_zero(struct ut_metric *metric);
extern bool ut_timespec_is_after(const struct timespec *a, const struct timespec *b);
extern char *ut_ring_type_enum2str(enum ut_ring_type ring_type);
extern uint64_t ut_clock_time_to_ns(struct ut_metric *metric);
extern double ut_clock_time_to_double(struct ut_metric *metric);
extern void *ut_malloc(enum ut_module module, size_t size);
extern void ut_free(enum ut_module module, void *ptr);
extern void ut_memory_counter_print();
extern void ut_memory_counter_clear();
extern bool ut_malloc_free_equal();
extern int ut_setaffinity(int core_id);
extern void *ut_malloc_def_callback(int32_t socket_id __attribute__((unused)), size_t size);
extern void ut_free_def_callback(void *ptr, size_t size __attribute__((unused)));
extern void ut_threads_destory(struct ut_info_s *info, pthread_t *threads);
extern pthread_t *ut_threads_create(struct ut_info_s *info, struct ut_queue *q);
extern void ut_one_thread_create(struct ut_info_s *info, struct ut_queue *q, enum ut_thread_type ttype, int core, uint16_t thread_id, pthread_t *thread);
extern void ut_wait_all_threads_exit(struct ut_info_s *info, uint32_t thread_cnt, pthread_t *threads, struct ut_exit_data **exit_data);
extern void *ut_thread_consumer_start(void *arg);
extern void *ut_thread_producer_start(void *arg);
extern uint32_t ut_exec_dequeue(struct ut_queue *q, struct ut_data **data, size_t burst_cnt, struct ut_metric *op_use_diff);
extern uint32_t ut_exec_enqueue(struct ut_queue *q, struct ut_data **data, size_t burst_cnt, struct ut_metric *op_use_diff, uint16_t thread_idx);
extern void ut_exit_data_destory(struct ut_exit_data *data);
extern struct ut_exit_data *ut_exit_data_create(struct ut_thread_arg *t_arg);
extern void ut_wait_all_threads_ready(struct ut_ctl *ctl);
extern void ut_queue_destory(struct ut_queue *q);
extern int ut_queue_init_bbq(struct ut_cfg *cfg, struct ut_queue *q);
extern void ut_merge_all_data(struct ut_exit_data **exit_data, uint32_t thread_cnt, struct ut_merge_s *merge);
extern uint64_t bbq_head_idx(struct bbq *q, uint64_t x);
extern uint64_t bbq_cur_off(struct bbq *q, uint64_t x);
extern uint64_t bbq_head_vsn(struct bbq *q, uint64_t x);
extern uint64_t bbq_cur_vsn(struct bbq *q, uint64_t x);
extern struct ut_data **ut_data_create(size_t cnt, enum ut_data_type);
extern void ut_data_destory(struct ut_data **data, size_t cnt);
extern bool bbq_debug_check_array_bounds(struct bbq *q);
extern struct bbq *bbq_create_elem_with_bnbs(const char *name, uint32_t bn,
                                             uint32_t bs, size_t obj_size,
                                             int socket_id, uint32_t flags,
                                             bbq_malloc_f malloc_f, bbq_free_f free_f);
extern struct bbq *bbq_create_with_bnbs(const char *name, uint32_t bn, uint32_t bs,
                                        int socket_id, uint32_t flags,
                                        bbq_malloc_f malloc_f, bbq_free_f free_f);