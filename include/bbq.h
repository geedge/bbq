/*
 * @Author: liuyu@geedgenetworks.com
 * @LastEditTime: 2024-05-24 12:01:33
 * @Describe:
 */

#ifndef _BBQ_H_
#define _BBQ_H_

#define BBQ_DEBUG

#include "bbq_errno.h"
#include "bbq_log.h"
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#ifndef __cplusplus
// C

#if 1
typedef atomic_uint_fast64_t aotmic_uint64;
#else
typedef _Atomic uint64_t aotmic_uint64;
#endif

typedef aotmic_uint64 Cursor; // 块内的四种游标（version+off）
typedef aotmic_uint64 Head;   // 向块的索引（version+idx）
#else
// C++ 为了兼容gtest测试
using Cursor = std::atomic<uint64_t>;
using Head = std::atomic<uint64_t>;
#endif

typedef void *bbq_data;

typedef struct Block {
    Cursor committed;
    Cursor allocated;
    Cursor reserved;
    Cursor consumed;   // 已消费， 在drop-old模式下没有被用到
    bbq_data *entries; // BS 大小的动态数组
} Block_s;

typedef enum {
    SUCCESS = 0,
    BLOCK_DONE,    // 当前块已的entry已用完，需要移动到下一个块
    NO_ENTRY,      // 没有条目可以使用
    NOT_AVAILABLE, // 当前块不可以用状态（将返回busy）
    ALLOCATED,     // 已分配，返回entry信息
    RESERVED,      // 已保留，返回entry信息
} QueueState_e;

typedef struct EntryDesc {
    Block_s *block; // 指向所在的block
    uint64_t off;   // entry在当前block的偏移(offset)
    uint64_t vsn;   // allocated游标的版本(version)
} EntryDesc_s;

typedef struct {
    QueueState_e state;
    union {
        uint64_t vsn;  // state==BLOCK_DONE时生效
        EntryDesc_s e; // state为ALLOCATED、RESERVED生效
    };
} QueueState_s;

typedef struct {
    int status;
    bbq_data data; // OKAY时生效，指向入队/出队的数据
} QueueStatus_s;

typedef enum {
    DROP_OLD, // 入队时队列满了，则覆盖旧的entry
    RETRY_NEW // 入队时队列满了，则本次入队失败
} Policy_e;

typedef struct BlockBoundedQueue {
    Head phead;
    Head chead;
    Policy_e policy;
    Block_s *blocks; // BN大小的动态数组
    size_t bs;       // blocks里entries成员的大小
    size_t bn;       // blocks成员的大小

    unsigned idx_bits;
    unsigned off_bits;
    unsigned idx_mask;
    unsigned off_mask;
} BlockBoundedQueue_s;

extern BlockBoundedQueue_s *bbq_ring_create(const char *name, uint32_t count,
                                            int socket_id, Policy_e policy);

extern BlockBoundedQueue_s *bbq_ring_create_bnbs(const char *name, size_t bn, size_t bs, int socket_id, Policy_e policy);
extern void bbq_ring_free(BlockBoundedQueue_s *q);
extern int bbq_enqueue(BlockBoundedQueue_s *q, bbq_data data);
extern int bbq_dequeue(BlockBoundedQueue_s *q, bbq_data *deq_data);

#endif