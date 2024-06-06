/*
 * @Author: liuyu
 * @LastEditTime: 2024-06-05 23:20:47
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#ifndef _TEST_QUEUE_H_
#define _TEST_QUEUE_H_
#include <pthread.h>
#include "bbq.h"
#include "test_mix.h"

#define TEST_DATA_MAGIC 1234567890

typedef void (*test_ring_free_f)(void* ring);
typedef int (*test_ring_enqueue_f)(void* ring, void* obj);
typedef int (*test_ring_dequeue_f)(void* ring, void* obj);
typedef uint32_t (*test_enqueue_burst_f)(void* ring, void* obj_table, size_t n, unsigned int flags);
typedef uint32_t (*test_dequeue_burst_f)(void* ring, void* obj_table, uint32_t n);

typedef struct {
    test_ring_type ring_type;
    test_ring_free_f ring_free_f;
    test_ring_enqueue_f enqueue_f;
    test_ring_dequeue_f dequeue_f;
    test_enqueue_burst_f enqueue_burst_f;
    test_dequeue_burst_f dequeue_burst_f;
    void* ring;
} test_queue_s;

typedef struct {
    int core;
    uint32_t busy_loop;  // 忙循环次数
    test_thread_type_e ttype;
    test_cfg* cfg;
    test_queue_s* q;
} test_thread_arg_s;

typedef struct bcm_benchmark {
    uint32_t data;                  // 数据
    test_time_metric enqueue_time;  // 入队时间
} test_data;

typedef struct {
    test_data* data;
    uint64_t data_cnt;
} test_complex_data;

typedef struct {
    pthread_t thread_id;
    test_time_metric metric_start;
    test_time_metric metric_end;
    uint64_t run_times;
    uint64_t ok_cnt;
    uint64_t latency_ns;         // 仅消费者有效，数据停留的时延
    uint64_t op_ok_latency_ns;   // 成功操作的时延
    uint64_t op_err_latency_ns;  // 操作失败的时延， 如满队入队，空队出队
    uint64_t data_error_cnt;     // 发生过至少一次数据不一致的次数
    test_thread_arg_s* arg;
    size_t simple_data_cnt;
    test_data** simple_data;
} test_exit_data;

typedef struct {
    test_time_metric use_time;
    uint64_t run_times;
    uint64_t ok_cnt;
    uint64_t latency_ns;         // 仅消费者有效，数据停留的时延
    uint64_t op_ok_latency_ns;   // 成功操作的时延
    uint64_t op_err_latency_ns;  // 操作失败的时延， 如满队入队，空队出队
    uint64_t data_error_cnt;     // 发生过至少一次数据不一致的次数
} test_merge_data;

typedef struct {
    test_merge_data producer;
    test_merge_data consumer;
} test_merge_s;

extern void test_threads_destory(pthread_t* threads);
extern pthread_t* test_threads_create(test_cfg* cfg, test_queue_s* q);
extern pthread_t* test_one_thread_create(test_cfg* cfg, test_queue_s* q, test_thread_type_e ttype, int core, pthread_t* thread);
extern void test_check_run_time(test_cfg* cfg);
extern void* test_thread_consumer_start(void* arg);
extern void* test_thread_producer_start(void* arg);
extern int test_exec_dequeue(test_queue_s* q, void* obj, test_time_metric* op_use_diff);
extern uint32_t test_exec_enqueue(test_queue_s* q, test_data** data, size_t simple_data_cnt, test_time_metric* op_use_diff);
extern test_exit_data* test_exit_data_destory(test_exit_data* data);
extern test_exit_data* test_exit_data_create(test_thread_arg_s* t_arg);
extern void test_wait_all_threads_ready(test_cfg* cfg);
extern void test_queue_destory(test_queue_s* q);
extern int test_queue_init_bbq(test_cfg* cfg, test_queue_s* q);
extern void test_merge_all_data(test_exit_data** exit_data, uint32_t thread_cnt, test_merge_s* merge);

extern uint64_t bbq_idx(bbq_queue_s* q, uint64_t x);
extern uint64_t bbq_off(bbq_queue_s* q, uint64_t x);
extern uint64_t bbq_head_vsn(bbq_queue_s* q, uint64_t x);
extern uint64_t bbq_cur_vsn(bbq_queue_s* q, uint64_t x);

extern test_data** test_data_create(size_t cnt);
extern void test_data_destory(test_data** data, size_t cnt);
#endif