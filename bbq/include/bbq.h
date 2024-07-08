/*
 * @Author: liuyu@geedgenetworks.com
 * @LastEditTime: 2024-07-07 15:22:47
 * @Describe: bbq（Block-based Bounded Queue）头文件
 * 参考：https://www.usenix.org/system/files/atc22-wang-jiawei.pdf
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#ifndef __cplusplus
// C
#include <stdatomic.h>
typedef atomic_uint_fast64_t aotmic_uint64;
#else
// C++ 为了兼容gtest测试
using aotmic_uint64 = std::atomic<uint64_t>;
#endif

#define BBQ_SOCKET_ID_ANY -1
#define BBQ_SYMBOL_MAX 64
#define BBQ_CACHE_LINE 64
#define __BBQ_CACHE_ALIGNED __attribute__((__aligned__(BBQ_CACHE_LINE)))

union bbq_atomic64 {
    volatile uint64_t s; // single使用该字段
    aotmic_uint64 m;
};

struct bbq_head {
    union bbq_atomic64 value;
} __BBQ_CACHE_ALIGNED;

struct bbq_block {
    union bbq_atomic64 committed;      // 生产者，已提交（version|offset）
    union bbq_atomic64 allocated;      // 生产者，已分配（version|offset）
    union bbq_atomic64 reserved;       // 消费者，已预留（version|offset）
    union bbq_atomic64 consumed;       // 消费者，已消费（version|offset）注：在drop-old模式下没用到
    char *entries __BBQ_CACHE_ALIGNED; // 存储大小可变的entry，每个块分配空间：bs * entry_size
} __BBQ_CACHE_ALIGNED;

typedef void *(*bbq_malloc_f)(int32_t socket_id, size_t size);
typedef void (*bbq_free_f)(void *ptr, size_t size);

struct bbq_mempool {
    char *ptr;   // 内存池起始地址
    size_t off;  // 已使用的偏移大小
    size_t size; // 内存池总大小

    bbq_malloc_f malloc_f; // 申请内存的函数，默认为malloc
    bbq_free_f free_f;     // 申请内存的函数，默认为free
} __BBQ_CACHE_ALIGNED;

struct bbq {
    // cache line-1
    char name[BBQ_SYMBOL_MAX] __BBQ_CACHE_ALIGNED;

    // cache line-2
    int32_t socket_id;   // 用于libnuma分配内存，socket_id小于0将使用malloc分配
    uint32_t bn;         // blocks的个数
    uint32_t bs;         // blocks.entries的个数
    uint32_t flags;      // 标记：retry new 模式，还是drop old模式
    uint32_t idx_bits;   // bbq_head里idx所占的位数
    uint32_t off_bits;   // bbq_cursor里offset所占的位数
    uint64_t idx_mask;   // idx_bits偏移后的掩码
    uint64_t off_mask;   // off_bits偏移后的掩码
    uint64_t entry_size; // blocks.entries里每个entry的大小
    bool prod_single;    // 如果为单生产者或单消费者，则single为true
    bool cons_single;    // 如果为单生产者或单消费者，则single为true

    // cache line-3
    struct bbq_head phead; // 生产者头，指向块的索引，分为两部分：version|idx

    // cache line-4
    struct bbq_head chead; // 消费者头，指向块的索引，分为两部分：version|idx

    // cache line-5
    struct {
        union bbq_atomic64 n_enq;
        union bbq_atomic64 n_deq;
    } __BBQ_CACHE_ALIGNED stat;

    // cache line-6
    struct bbq_mempool memory_pool; // 仅在初始化和调试时会读写
    struct bbq_block *blocks;       // bn大小的数组
} __BBQ_CACHE_ALIGNED;

#define BBQ_F_DEFAULT 0x0

#define BBQ_F_DROP_OLD 0x0002         /**< 创建队列时设置为drop old模式（队列满时，入队成功并覆盖旧数据） */
#define BBQ_F_RETRY_NEW BBQ_F_DEFAULT /**< 创建队列时设置为retry new模式（队列满时，当前入队失败） */

#define BBQ_F_SP_ENQ 0x0004
#define BBQ_F_MP_ENQ BBQ_F_DEFAULT
#define BBQ_F_SC_DEQ 0x0008
#define BBQ_F_MC_DEQ BBQ_F_DEFAULT
#define BBQ_F_ENABLE_STAT 0x0010
#define BBQ_F_DISABLE_STAT BBQ_F_DEFAULT

/**
 * 创建bbq队列，使用当前函数创建的队列，后续操作会把指针入队。
 * 对应入队函数：bbq_enqueue、bbq_enqueue_burst
 * 对应出队函数：bbq_dequeue、bbq_dequeue_burst
 *
 * @param[in] name
 *   队列名称
 * @param[in] count
 *   队列大小，参数必须大于1，且是2的N次方。
 * @param[in] socket_id
 *   多numa架构下，针对指定socket分配内存。
 * @param[in] flags
 *   设置入队策略：
 *      - BBQ_F_DROP_OLD：队列满时，覆盖旧数据，入队成功
 *      - BBQ_F_RETRY_NEW：队列满了当前入队失败（默认）。
 *   设置生产者模式：
 *      - BBQ_F_SP_ENQ：单生产者
 *      - BBQ_F_MP_ENQ：多生产者（默认）
 *   设置消费者模式：
 *      - BBQ_F_SC_DEQ：单消费者
 *      - BBQ_F_MC_DEQ：多消费者（默认）
 *   设置统计功能：
 *      在出入队的时候同时累计成功次数，并推算出当前队列的剩余个数。注：目前仅retry new模式下支持统计功能
 *      - BBQ_F_ENABLE_STAT：开启统计功能
 *      - BBQ_F_DISABLE_STAT：关闭统计功能（默认）
 * @return
 *   非NULL：消息队列结构体指针，用于后续出队入队等操作。
 *   NULL：创建失败，可能存在的原因：
 *      - name或count参数超出范围
 *      - 申请内存失败
 *      - count不为2的n次方
 *      - name传入空指针
 *      - drop old模式下不支持
 */
extern struct bbq *bbq_create(const char *name, uint32_t count, int socket_id, uint32_t flags,
                              bbq_malloc_f malloc_f, bbq_free_f free_f);

/**
 * 消息队列单个指针入队
 *
 * @param[in] q
 *   队列指针
 * @param[in] data
 *   指向入队指针的指针，如：
 *   int *data = malloc(sizeof(int));*data = TEST_DATA; 传入&data
 * @return
 *   成功返回0，失败返回小于0的错误码：
 *      - BBQ_ERR_INPUT_NULL：传入空指针
 *      - BBQ_ERR_FULL：队列已满
 *      - BBQ_ERR_BUSY：队列忙碌中
 *      - BBQ_ERR：其它错误
 */
extern int bbq_enqueue(struct bbq *q, void *const *data);

/**
 * 消息队列单个指针出队
 *
 * @param[in] q
 *   队列指针
 * @param[out] data
 *   传入二级指针，如：
 *   int *data = NULL; 传入&data
 * @return
 *   成功返回0，失败返回小于0的错误码：
 *     - BBQ_ERR_INPUT_NULL：传入空指针
 *     - BBQ_ERR_EMPTY：队列已空
 *     - BBQ_ERR_BUSY：队列忙碌中
 *     - BBQ_ERR：其它错误
 */
extern int bbq_dequeue(struct bbq *q, void **data);

/**
 * 消息队列批量入队（指针入队），尽可能一次入队n个指针，返回实际成功入队个数
 *
 * @param[in] q
 *   队列指针
 * @param[in] obj_table
 *   即将入队的指针数组，如：
 *   uint16_t **obj_table = malloc(sizeof(uint16_t **) * BUF_CNT);
 *   for(int i=0;i<BUF_CNT;i++){
 *      obj_table[i] = malloc(sizeof(uint16_t));
 *      obj_table[i] = TEST_DATA;
 *   }
 *   传入obj_table
 * @param[in] n
 *   尝试一次入队的个数
 * @param[out] wait_consumed
 *   如果为非NULL，返回当前队列剩余的个数。注：该赋值可能会带来些许的性能损耗。
 * @return
 *   返回实际成功入队的个数。如果始终返回0，可能存在的错误原因：
 *     - 传入空指针
 *     - 队列已满
 *     - 队列忙碌中
 */
extern uint32_t bbq_enqueue_burst(struct bbq *q, void *const *obj_table, uint32_t n, uint32_t *wait_consumed);

/**
 * 消息队列批量指针出队，尽可能一次出队n个数据，返回实际成功出队个数
 *
 * @param[in] q
 *   队列指针
 * @param[out] obj_table
 *   用于存储出队的指针，如：
 *   uint16_t **obj_table = malloc(sizeof(uint16_t *)); 传入obj_table
 * @param[in] n
 *   尝试一次出队的个数
 * @param[out] wait_consumed
 *   如果为非NULL，返回当前队列中，已入队的个数。注：该赋值可能会带来些许的性能损耗
 * @return
 *   返回实际出队的个数，如果始终返回0，可能存在的原因：
 *     - 传入空指针
 *     - 队列已空
 *     - 队列忙碌中
 */
extern uint32_t bbq_dequeue_burst(struct bbq *q, void **obj_table, uint32_t n, uint32_t *wait_consumed);

/**
 * 创建bbq队列，使用当前函数创建的队列，在后续操作会把指针指向的数据拷贝入队。
 * 对应入队函数：bbq_enqueue_elem、bbq_enqueue_burst_elem
 * 对应出队函数：bbq_dequeue_elem、bbq_dequeue_burst_elem
 *
 * @param[in] name
 *   队列名称
 * @param[in] count
 *   队列大小，参数必须大于1，且是2的N次方。
 * @param[in] socket_id
 *   多numa架构下，针对指定socket分配内存。
 * @param[in] flags
 *   设置入队策略：
 *      - BBQ_F_DROP_OLD：队列满时，覆盖旧数据，入队成功
 *      - BBQ_F_RETRY_NEW：队列满了当前入队失败（默认）。
 *   设置生产者模式：
 *      - BBQ_F_SP_ENQ：单生产者
 *      - BBQ_F_MP_ENQ：多生产者（默认）
 *   设置消费者模式：
 *      - BBQ_F_SC_DEQ：单消费者
 *      - BBQ_F_MC_DEQ：多消费者（默认）
 *   设置统计功能：
 *      在出入队的时候同时累计成功次数，并推算出当前队列的剩余个数。注：目前仅retry new模式下支持统计功能
 *      - BBQ_F_ENABLE_STAT：开启统计功能
 *      - BBQ_F_DISABLE_STAT：关闭统计功能（默认）
 * @return
 *   非NULL：消息队列结构体指针，用于后续出队入队等操作。
 *   NULL：创建失败。可能存在的错误原因：
 *      - name或count参数超出范围
 *      - 申请内存失败
 *      - count不为2的n次方
 *      - name传入空指针
 *      - drop old模式下不支持
 */
extern struct bbq *bbq_create_elem(const char *name, uint32_t count, size_t obj_size,
                                   int socket_id, uint32_t flags,
                                   bbq_malloc_f malloc_f, bbq_free_f free_f);

/**
 * 消息队列单个数据入队（指针指向的数据将被拷贝）
 *
 * @param[in] q
 *   队列指针
 * @param[in] data
 *   传入一级指针，如：int data = 1; 传入&data
 * @return
 *   成功返回0，失败返回小于0的错误码：
 *     - BBQ_ERR_INPUT_NULL：传入空指针
 *     - BBQ_ERR_FULL：队列已满
 *     - BBQ_ERR_BUSY：队列忙碌中
 *     - BBQ_ERR：其它错误
 */
extern int bbq_enqueue_elem(struct bbq *q, void const *data);

/**
 * 消息队列单个数据出队
 *
 * @param[in] q
 *   队列指针
 * @param[in] data
 *   则传入一级指针，如：int data; 传入&data
 * @return
 *   成功返回0，失败返回小于0的错误码：
 *     - BBQ_ERR_INPUT_NULL：传入空指针
 *     - BBQ_ERR_EMPTY：队列已空
 *     - BBQ_ERR_BUSY：队列忙碌中
 *     - BBQ_ERR：其它错误
 */
extern int bbq_dequeue_elem(struct bbq *q, void *data);

/**
 * 消息队列批量入队（数据入队），尽可能一次入队n个数据，返回实际成功入队个数
 *
 * @param[in] q
 *   队列指针
 * @param[in] obj_table
 *   将数组里的每个数据入队，如：
 *   uint16_t obj_table[1024] = {初始化数据};  传入obj_table
 * @param[in] n
 *   尝试一次入队的个数
 * @param[out] wait_consumed
 *   如果为非NULL，返回当前队列中，已入队的个数。。注：该赋值可能会带来些许的性能损耗
 * @return
 *   返回实际成功入队的个数。如果始终返回0，可能存在的错误原因：
 *     - 传入空指针
 *     - 队列已满
 *     - 队列忙碌中
 */
extern uint32_t bbq_enqueue_burst_elem(struct bbq *q, void const *obj_table, uint32_t n, uint32_t *wait_consumed);

/**
 * 消息队列批量出队（数据出队），尽可能一次出队n个数据，返回实际成功出队个数
 *
 * @param[in] q
 *   队列指针
 * @param[out] obj_table
 *   存储出队的数据，如：
 *   uint16_t obj_table[BUF_CNT] = {0}; 传入(void *)obj_table
 * @param[in] n
 *   尝试一次出队的个数
 * @param[out] wait_consumed
 *   如果为非NULL，返回当前队列中，已入队的个数。注：该赋值可能会带来些许的性能损耗
 * @return
 *   返回实际出队的个数，如果始终返回0，可能存在的原因：
 *     - 传入空指针
 *     - 队列已空
 *     - 队列忙碌中
 */
extern uint32_t bbq_dequeue_burst_elem(struct bbq *q, void *obj_table, uint32_t n, uint32_t *wait_consumed);

/**
 * 用于释放消息队列。
 *
 * @param[in] q
 *   队列指针
 * @return
 *   - true: 队列为空返回
 *   - false: 队列非空
 */
bool bbq_empty(struct bbq *q);

/**
 * 用于释放消息队列。
 *
 * @param[in] q
 *   队列指针
 */
extern void bbq_destory(struct bbq *q);

// 错误码
#define BBQ_OK 0                    // 成功
#define BBQ_ERR -1                  // 通用错误，无法分类时使用
#define BBQ_ERR_ALLOC -2            // 内存分配失败
#define BBQ_ERR_INPUT_NULL -3       // 传入空指针
#define BBQ_ERR_POWER_OF_TWO -4     // 不是2的n次方
#define BBQ_ERR_OUT_OF_RANGE -5     // 超出范围
#define BBQ_ERR_STAT_NOT_SUPPORT -6 // 不支持统计

#define BBQ_ERR_FULL -101        // 队列已满（入队失败）
#define BBQ_ERR_BUSY -102        // 队列忙碌中（入队或出队失败）
#define BBQ_ERR_EMPTY -103       // 队列已空（出队失败）
#define BBQ_ERR_NOT_SUPPORT -104 // 不支持的操作