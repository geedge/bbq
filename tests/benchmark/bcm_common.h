/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-24 16:26:41
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#ifndef _BBQ_COMMON_H_
#define _BBQ_COMMON_H_

#include <stdint.h>

typedef enum {
    BCM_RING_TYPE_BBQ,
    BCM_RING_TYPE_DPDK,
    BCM_RING_TYPE_MAX,
} bcm_ring_type;

typedef enum {
    BCM_WORKLOAD_SIMPLE,  // 简单负载，每个生产者或消费者都有自己的线程，它们在循环中不断执行入队或出队操作。每次出队后都会验证数据。
    BCM_WORKLOAD_COMPLEX, // 复杂负载，基于简单工作负载。生产者和消费者为数据分配空间，执行入队和出队，然后手动释放
    BCM_WORKLOAD_MAX,
} bcm_workload_e;

typedef struct {
    char *introduce;         // 测试配置说明
    bcm_workload_e workload; // 负载模式

    uint16_t cores_cnt;    // 测试用核心个数
    uint32_t producer_cnt; // 生产者个数
    uint32_t consumer_cnt; // 消费者个数
    uint64_t enqueue_cnt;  // 每个线程入队次数
    uint64_t dequeue_cnt;  // 每个线程出队次数

    bcm_ring_type ring_type; // ring buffer类型
    uint64_t entries_cnt;    // ring初始化时分配entry的个数
} bcm_cfg;

typedef struct {
    uint64_t throughput;       // 吞吐量：每秒消耗的条目总数。
    double data_latency;       // 数据延迟：每个数据在队列中停留的平均时间。
    double op_latency;         // 操作延迟：每个入队或出队操作的平均延迟。
    double *fairness;          // 公平性：每个生产者/消费者的吞吐量（占总吞吐的百分比）
    double full_empty;         // 队列满时入队的延迟/队列空时出队的延迟（仅用于简单工作负载）。
    uint64_t oversubscription; // 比核心/超线程更多的生产者和消费者的吞吐量
} bcm_report;

extern int bxm_load_config(const char *config, bcm_cfg *cfg);

#endif