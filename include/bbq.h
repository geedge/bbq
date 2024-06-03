/*
 * @Author: liuyu@geedgenetworks.com
 * @LastEditTime: 2024-06-03 22:57:34
 * @Describe: bbq（Block-based Bounded Queue）头文件
 * 参考：https://www.usenix.org/system/files/atc22-wang-jiawei.pdf
 */

#ifndef _BBQ_H_
#define _BBQ_H_

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

// #define BBQ_MEMORY
// #define BBQ_DEBUG

#ifndef __cplusplus
// C
typedef atomic_uint_fast64_t aotmic_uint64;
typedef aotmic_uint64 bbq_cursor;
typedef aotmic_uint64 bbq_head;
#else
// C++ 为了兼容gtest测试
using bbq_cursor = std::atomic<uint64_t>;
using bbq_head = std::atomic<uint64_t>;
using aotmic_uint64 = std::atomic<uint64_t>;
#endif

typedef void *bbq_data;

typedef struct {
    bbq_cursor committed; // 已提交（version|offset）
    bbq_cursor allocated; // 已分配（version|offset）
    bbq_cursor reserved;  // 已预留（version|offset）
    bbq_cursor consumed;  // 已消费（version|offset）， 在drop-old模式下没用

    bbq_data *entries; // BS大小的动态数组
} bbq_block_s;

typedef enum {
    BBQ_SUCCESS = 0,
    BBQ_BLOCK_DONE,    // 当前块已的entry已用完，需要移动到下一个块
    BBQ_NO_ENTRY,      // 没有条目可以使用
    BBQ_NOT_AVAILABLE, // 当前块不可以用状态（将返回busy）
    BBQ_ALLOCATED,     // 已分配，返回entry信息
    BBQ_RESERVED,      // 已保留，返回entry信息
} bbq_queue_state_e;

typedef struct {
    bbq_block_s *block; // 指向所在的block
    uint64_t bbq_off;   // entry在当前block的偏移(offset)
    uint64_t vsn;       // allocated游标的版本(version)
} bbq_entry_desc_s;

typedef struct {
    bbq_queue_state_e state; // 队列状态
    union {
        uint64_t vsn;       // state==BLOCK_DONE时生效
        bbq_entry_desc_s e; // state为ALLOCATED、RESERVED生效
    };
} bbq_queue_state_s;

typedef struct {
    int status;    // 操作返回值
    bbq_data data; // OKAY时生效，指向入队/出队的数据
} bbq_queue_status_s;

typedef enum {
    BBQ_DROP_OLD, // 入队时队列满了，则覆盖旧的entry
    BBQ_RETRY_NEW // 入队时队列满了，则本次入队失败
} bbq_policy_e;

typedef void (*drop_old_func)(void *arg);

typedef struct {
    int socket_id; // 在哪个socket_id上使用libnuma分配内存，-1表示无效，使用malloc分配
    bbq_policy_e policy;
    size_t bs; // 每个block里entries成员的大小
    size_t bn; // blocks个数
    unsigned idx_bits;
    unsigned off_bits;
    uint64_t idx_mask;
    uint64_t off_mask;
    drop_old_func drop_old_free; // 覆盖模式下，当出现队列数据被覆盖，用于释放资源

    bbq_head phead;      // 生产者头，指向块的索引（version|idx）
    bbq_head chead;      // 消费者头，指向块的索引（version|idx）
    bbq_block_s *blocks; // bn大小的动态数组
} bbq_queue_s;

// -----------------------------对外接口-------------------------------
/**
 * bbq_ring_create系列接口，用于创建并返回bbq队列结构体，功能类似，区别在于是否需要
 * 指定socket，是否手动手动指定块个数（bn），块的大小（bs），即每个块包含的条目数。
 * @param count
 * 队列所有条目的个数（总容量），必须大于1，且是2的N次方。将根据公式自动计算块的个数，以及块的大小。
 * 计算公式：log2(blocks) = max(1, ⌊log2(count)/4⌋)   注：⌊ ⌋代表向下取整。
 * @param free_func
 * NULL：retry-new模式，队列满时再入队将会失败。
 * 非NULL：drop-old模式，指向一个用于释放未及时出队列而被覆盖的数据。
 * 目前drop-old模式还支持的不，作为接口先预留，不论传什么目前都为retry-new模式。
 * @param socket_id
 * socket ID，多numa架构下，队列里的空间将针对指定socket调用libnuma库函数分配内存。
 * 当检测到不支持多numa，将转为malloc分配内存。
 * @param bn
 * block number，块的个数，必须是2的N次方。
 * @param bs
 * block size，块的大小，即每个块包含的条目数，必须是2的N次方。
 * @return
 * 非NULL：消息队列结构体指针，用于后续出队入队等操作。
 * NULL：创建失败。
 */
extern bbq_queue_s *bbq_ring_create(uint32_t count, drop_old_func free_func);
extern bbq_queue_s *bbq_ring_create_with_socket(uint32_t count, int socket_id, drop_old_func free_func);
extern bbq_queue_s *bbq_ring_create_bnbs(uint32_t bn, uint32_t bs, drop_old_func free_func);
extern bbq_queue_s *bbq_ring_create_bnbs_with_socket(uint32_t bn, uint32_t bs, int socket_id, drop_old_func free_func);

/**
 * 用于释放消息队列，与bbq_ring_create系列函数成对。
 * @param q
 * 队列指针
 */
extern void bbq_ring_free(bbq_queue_s *q);

/**
 * 消息队列入队
 * @param q
 * 队列指针
 * @param data
 * void *类型，需要调用者在函数外自行申请空间。
 * @return
 * BBQ_OK           0    ：成功
 * BBQ_QUEUE_FULL  -1001 : 失败，队列已满
 * BBQ_QUEUE_BUSY  -1002 : 失败,队列忙碌中
 * BBQ_ERROR       -1    ：未知错误
 */
extern int bbq_enqueue(bbq_queue_s *q, bbq_data data);

/**
 * 消息队列出队
 * @param q
 * 队列指针
 * @param deq_data
 * void **类型，出队成功将对*deq_data赋值，出队失败，【不会】对*deq_data置NULL。
 * 请不要以此判断是否出队列成功，应以return值作为判断依据。
 * @return
 * BBQ_OK           0    ：成功
 * BBQ_QUEUE_BUSY  -1002 : 失败,队列忙碌中
 * BBQ_QUEUE_EMPTY -1003 : 失败,队列已空（出队）
 * BBQ_ERROR       -1    ：未知错误
 */
extern int bbq_dequeue(bbq_queue_s *q, bbq_data *deq_data);

// -----------------------------用于内存测试-------------------------------
// 当BBQ_MEMORY宏定义开关打开，将对内存分配释放进行统计，方便排查内存泄漏
typedef enum {
    BBQ_MODULE_QUEUE = 0,
    BBQ_MODULE_QUEUE_BLOCK_NB,
    BBQ_MODULE_QUEUE_BLOCK_ENTRY,
    BBQ_MODULE_MAX,
} bbq_module_e;

// -----------------------------错误码-------------------------------
// 通用返回码
#define BBQ_OK 0            // 成功
#define BBQ_ERROR -1        // 通用错误
#define BBQ_ALLOC_ERR -2    // 内存分配失败
#define BBQ_NULL_PTR -3     // 空指针
#define BBQ_UNKNOWN_TYPE -3 // 未知类型

// 队列错误
#define BBQ_QUEUE_FULL -1001  // 队列已满（入队失败）
#define BBQ_QUEUE_BUSY -1002  // 队列忙碌中（入队或出队失败）
#define BBQ_QUEUE_EMPTY -1003 // 队列已空（出队失败）

// -----------------------------日志宏定义-------------------------------
#ifdef BBQ_DEBUG
#define BBQ_DBG_LOG(fmt, ...)                                                            \
    do {                                                                                 \
        printf("[DBG][%s:%d:%s]" fmt "\n", __func__, __LINE__, __FILE__, ##__VA_ARGS__); \
    } while (0)

#else
#define BBQ_DBG_LOG(fmt, ...) \
    do {                      \
    } while (0)
#endif

#define BBQ_ERR_LOG(fmt, ...)                                                            \
    do {                                                                                 \
        printf("[ERR][%s:%d:%s]" fmt "\n", __func__, __LINE__, __FILE__, ##__VA_ARGS__); \
    } while (0)

#define BBQ_INFO_LOG(fmt, ...)                                                            \
    do {                                                                                  \
        printf("[INFO][%s:%d:%s]" fmt "\n", __func__, __LINE__, __FILE__, ##__VA_ARGS__); \
    } while (0)

#define BBQ_CLOR_PRINT(fmt, ...)                           \
    do {                                                   \
        printf("\033[32m" fmt "\033[0m\n", ##__VA_ARGS__); \
    } while (0)

// -----------------------------其他宏定义-------------------------------
#define BBQ_INVALID_SOCKET -1

#endif