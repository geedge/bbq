/*
 * @Author: liuyu@geedgenetworks.com
 * @LastEditTime: 2024-05-20 16:34:18
 * @Describe:
 */

#ifndef _BBQ_H_
#define _BBQ_H_

#include "bbq_error.h"
#include "bbq_log.h"
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

typedef _Atomic uint64_t aotmic_uint64;
typedef aotmic_uint64 Cursor; // 块内的四种游标（version+off）
typedef aotmic_uint64 Head; // 向块的索引（version+idx）
typedef void* bbq_data ;

typedef struct Block {
    Cursor committed; 
    Cursor allocated;
    Cursor reserved;
    Cursor consumed; // 已消费， 在drop-old模式下没有被用到
    bbq_data entries; //BS 大小的动态数组
    size_t bs; // blocks数组大小
}Block_s;


typedef enum {
    SUCCESS= 0,
    BLOCK_DONE , // 当前块已的entry已用完，需要移动到下一个块
    NO_ENTRY, // 没有条目可以使用
    NOT_AVAILABLE, // 当前块不可以用状态（将返回busy）
    ALLOCATED, // 已分配，返回entry信息
    RESERVED, // 已保留，返回entry信息
} QueueState_e;

typedef struct EntryDesc {
    Block_s *block; // 指向所在的block
    uint64_t off; // entry在当前block的偏移(offset)
    uint64_t vsn;// allocated游标的版本(version)
}EntryDesc_s;

typedef struct {
    QueueState_e state;
    union {
        uint64_t vsn; // state==BLOCK_DONE时生效
        EntryDesc_s e; // state为ALLOCATED、RESERVED生效
    } data;
} QueueState_s;

typedef enum {
    OK = 0, // 入队或出队成功
    FULL, // 队列已满（入队失败）
    BUSY, // 队列忙碌中（入队或出队失败）
    EMPTY,// 队列已空（出队失败）
} QueueStatus_e;

typedef struct {
    QueueStatus_e status;
    bbq_data data; // OKAY时生效，指向入队/出队的数据
} QueueStatus_s;

typedef enum {
    DROP_OLD, // 入队时队列满了，则覆盖旧的entry
    RETRY_NEW// 入队时队列满了，则本次入队失败
} Policy_e;

typedef struct BlockBoundedQueue {
    QueueStatus_s (*enqueue) (bbq_data data);
    QueueStatus_s (*dequeue) ();
    Head phead;
    Head chead;
    Policy_e policy;
    Block_s *blocks; // BN大小的动态数组
    size_t bs; // blocks里entries成员的大小
    size_t bn; // blocks成员的大小

    unsigned idx_bits ; 
    unsigned off_bits; 
    unsigned idx_mask ;
    unsigned off_mask ;

    QueueState_s (*allocate_entry)(Block_s *block);
    void (*commit_entry)(EntryDesc_s *e, bbq_data data);
    
    QueueState_s (*advance_phead)(uint64_t ph);
    QueueState_s (*reserve_entry)(Block_s *block);
    bbq_data (*consume_entry)(EntryDesc_s *e);
    bool (*advance_chead)(uint64_t ch, uint64_t ver);
    
    uint64_t (*idx)(uint64_t x);
    uint64_t (*off)(uint64_t x);
    // TODO: 可以改进？文档里version移动的位置是max(idx_bits，off_bits)，在advance里，就能直接赋值，MAX(nblk.committed, Cursor{.vsn=ph.vsn + 1});本代码里做了偏移判断。
    uint64_t (*head_vsn)(uint64_t x);
    uint64_t (*cur_vsn)(uint64_t x);
    uint64_t (*set_cur_vsn)(uint64_t ver);
}BlockBoundedQueue_s;

#endif