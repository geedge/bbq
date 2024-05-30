/*
 * @Author: liuyu@geedgenetworks.com
 * @LastEditTime: 2024-05-30 14:00:57
 * @Describe:
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
// typedef _Atomic uint64_t aotmic_uint64;
typedef atomic_uint_fast64_t aotmic_uint64;
typedef aotmic_uint64 bbq_cursor; // 块内的四种游标（version|offset）
typedef aotmic_uint64 bbq_head;   // 指向块的索引（version|idx）
#else
// C++ 为了兼容gtest测试
using bbq_cursor = std::atomic<uint64_t>;
using bbq_head = std::atomic<uint64_t>;
#endif

typedef void *bbq_data;

typedef struct {
    bbq_cursor committed; // 已提交
    bbq_cursor allocated; // 已分配
    bbq_cursor reserved;  // 已预留
    bbq_cursor consumed;  // 已消费， 在drop-old模式下没有被用到
    bbq_data *entries;    // BS大小的动态数组
} bbq_block_s;

typedef enum {
    SUCCESS = 0,
    BLOCK_DONE,    // 当前块已的entry已用完，需要移动到下一个块
    NO_ENTRY,      // 没有条目可以使用
    NOT_AVAILABLE, // 当前块不可以用状态（将返回busy）
    ALLOCATED,     // 已分配，返回entry信息
    RESERVED,      // 已保留，返回entry信息
} bbq_queue_state_e;

typedef struct {
    bbq_block_s *block; // 指向所在的block
    uint64_t off;       // entry在当前block的偏移(offset)
    uint64_t vsn;       // allocated游标的版本(version)
} bbq_entry_desc_s;

typedef struct {
    bbq_queue_state_e state;
    union {
        uint64_t vsn;       // state==BLOCK_DONE时生效
        bbq_entry_desc_s e; // state为ALLOCATED、RESERVED生效
    };
} bbq_queue_state_s;

typedef struct {
    int status;
    bbq_data data; // OKAY时生效，指向入队/出队的数据
} bbq_queue_status_s;

typedef enum {
    BBQ_DROP_OLD, // 入队时队列满了，则覆盖旧的entry
    BBQ_RETRY_NEW // 入队时队列满了，则本次入队失败
} bbq_policy_e;

#define BBQ_NAME_MAXLEN 32
typedef struct {
    int socket_id; // 在哪个socket_id上使用libnuma分配内存，-1表示无效，使用malloc分配
    size_t bs;     // blocks里entries成员的大小
    size_t bn;     // blocks成员的大小
    bbq_policy_e policy;
    unsigned idx_bits;
    unsigned off_bits;
    uint64_t idx_mask;
    uint64_t off_mask;

    char name[BBQ_NAME_MAXLEN];
    bbq_head phead;
    bbq_head chead;
    bbq_block_s *blocks; // BN大小的动态数组
} bbq_queue_s;

typedef enum {
    BBQ_MODULE_QUEUE = 0,
    BBQ_MODULE_QUEUE_BLOCK_NB,
    BBQ_MODULE_QUEUE_BLOCK_ENTRY,
    BBQ_MODULE_MAX,
} bbq_module_e; // 用于内存测试

// -----------------------------日志接口-------------------------------
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

// -----------------------------对外接口-------------------------------
extern bbq_queue_s *bbq_ring_create(const char *name, uint32_t count, bbq_policy_e policy);
extern bbq_queue_s *bbq_ring_create_with_socket(const char *name, uint32_t count, int socket_id, bbq_policy_e policy);
extern bbq_queue_s *bbq_ring_create_bnbs(const char *name, uint32_t bn, uint32_t bs, bbq_policy_e policy);
extern bbq_queue_s *bbq_ring_create_bnbs_with_socket(const char *name, uint32_t bn, uint32_t bs, int socket_id, bbq_policy_e policy);
extern void bbq_ring_free(bbq_queue_s *q);
extern int bbq_enqueue(bbq_queue_s *q, bbq_data data);
extern int bbq_dequeue(bbq_queue_s *q, bbq_data *deq_data);
extern void bbq_debug_struct_print(bbq_queue_s *q);

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

// -----------------------------其他宏定义-------------------------------
#define BBQ_INVALID_SOCKET -1

#endif