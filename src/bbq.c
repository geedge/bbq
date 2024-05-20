/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-20 16:33:24
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#include <stdio.h>
#include "bbq.h"

int block_init(Block_s *block, size_t bs,bool cursor_init) {
    block->entries = BBQ_ALLOC(sizeof(bbq_data) * bs);
    if (unlinkely(block->entries) == NULL){
        return BBQ_ALLOC_ERR;
    }

    block->allocated = 0;
    block->committed = 0;
    block->consumed = 0;
    block->consumed = 0;
    block->bs = bs;

    if (cursor_init){
        // block数组里，除了第一块之外需要设置
        block->committed |= bs;
        block->allocated |= bs;
        block->reserved |= bs;
        block->consumed |= bs; 
    }

    return BBQ_OK;
}

void block_cleanup(Block_s *block){
    BBQ_FREE(block->entries); 
}

// 返回以2为底的对数并向下取整值：也就是求x 的二进制表示中最高位（最左边的1）的位置（位置从0开始）
// 参数不能为0，否则会死循环最终段错误
// 例如：x=1，return 0 (...1)
// x=3，return 1  （..11）
// x=9，return 3  （1..1）
unsigned floor_log2(uint64_t x) {
    return x == 1 ? 0 : 1 + floor_log2(x >> 1);
}

// 返回以2为底的对数并向上取整值。
// 例如：x=1，return 0 （2^0=1）
// x=99， return 7（2^6=64  2^7=128）
unsigned ceil_log2(uint64_t x) {
    return x == 1 ? 0 : floor_log2(x - 1) + 1;
}


int BlockBoundedQueue_init(BlockBoundedQueue_s *q, size_t bn, size_t bs, Policy_e policy){
    if (q = NULL){
        return BBQ_NULL_PTR;
    }

    q->phead = 0;
    q->chead = 0;
    q->policy = policy;
    q->blocks = BBQ_ALLOC(bn);

    block_init(&(q->blocks[0]), bs, false);
    for (size_t i = 1; i < bs; ++i) {
        block_init(&(q->blocks[i]), bs, true);
    }

    // TODO:是否需要考虑确认，bits位是否满了，无法给version留空间？？即限制BN和BS的边界?ceil_log2返回值至少<64?
    q->idx_bits = ceil_log2(bn); // head 索引的个数，为方便快速取idx（通过掩码），把BN和BS的值扩充为2的N次幂
    q->off_bits = ceil_log2(bs) + 1; // Cursor偏移的个数 TODO:  +1 是否合理？如果BS较小，线程较多，+1够吗？
    q->idx_mask = (1 << q->idx_bits) - 1;
    q->off_mask = (1 << q->off_bits) - 1;

    q->enqueue = enqueue;
    q->dequeue = dequeue;
    q->allocate_entry = allocate_entry;
    q->commit_entry = commit_entry;
    q->advance_phead = advance_phead;
    q->reserve_entry = reserve_entry;
    q->consume_entry = consume_entry;
    q->advance_chead = advance_chead;
}

int BlockBoundedQueue_cleanup(BlockBoundedQueue_s *q){
    if (q = NULL){
        return BBQ_NULL_PTR;
    }

    for (size_t i = 0; i < q->bn; ++i) {
        block_cleanup(&(q->blocks[i]));
    }

    BBQ_FREE(q->blocks);
}

QueueStatus_s enqueue (BlockBoundedQueue_s *q, bbq_data data){
    while (true) {
        // 获取当前phead，转为索引后获取到当前的blk
        uint64_t ph = atomic_load(&q->phead);
        Block_s *blk = &(q->blocks[q->idx(ph)]);

        QueueState_s ps;
        QueueState_s state = allocate_entry(blk);
        QueueStatus_s status;

        switch (state.state) {
        case ALLOCATED:
            commit_entry(&state.data.e, data);
            status.status = OK;
            return status;
        case BLOCK_DONE:
            QueueStatus_s status;
            ps = advance_phead(ph);
            switch (ps.state) {
            case NO_ENTRY:
                status.status = FULL;
                return status;
            case NOT_AVAILABLE:
                status.status = BUSY;
                return status;
            case SUCCESS:
                continue;
            }
            break;
        default:
            BBQ_ERR_LOG("Invalid QueueState in enqueue: %d\n", state.state);
        }
    }
}

QueueStatus_s dequeue (){
     while (true) {
        auto ch = chead.load(std::memory_order_acquire);
        auto &blk = blocks[idx(ch)];

        T *data;
        auto state = reserve_entry(blk);
        switch (state.index()) {
        case RESERVED:
            data = consume_entry(std::get_if<Reserved<T, BS>>(&state)->e);
            if (data)
                return OK<T>{.data = *data};
            else
                continue;
        case NO_ENTRY:
            return Empty{};
        case NOT_AVAILABLE:
            return Busy{};
        case BLOCK_DONE:
            if (advance_chead(ch, std::get_if<BlockDone>(&state)->vsn))
                continue;
            else
                return Empty{};
        default:
            throw std::range_error("Invalid QueueState in dequeue: " + std::to_string(state.index()));
        }
    }
}

QueueState_s allocate_entry(Block_s *block){
    
}

void commit_entry(EntryDesc_s *e, bbq_data data) {

}

QueueState_s advance_phead(uint64_t ph){
    
}

QueueState_s reserve_entry(Block_s *block){
    
}

bbq_data consume_entry(EntryDesc_s *e){
    
}

bool advance_chead(uint64_t ch, uint64_t ver){
    
}

inline uint64_t idx(uint64_t x, unsigned idx_mask) { 
    return x & idx_mask; 
}

inline uint64_t off(uint64_t x, unsigned off_mask){
    return x & off_mask;
}

// TODO: 可以改进？文档里version移动的位置是max(idx_bits，off_bits)，在advance里，就能直接赋值，MAX(nblk.committed, Cursor{.vsn=ph.vsn + 1});本代码里做了偏移判断。
inline uint64_t head_vsn(uint64_t x, unsigned idx_bits) {
    return x >> idx_bits;
}

inline uint64_t cur_vsn(uint64_t x, unsigned off_bits) {
    return x >> off_bits;
}

inline uint64_t set_cur_vsn(uint64_t ver, unsigned off_bits) {
    return ver << off_bits;
}

// TODO:测试
inline uint64_t fetch_max(aotmic_uint64 *atom, uint64_t upd) {  
    uint64_t old_value;  
    do {  
        old_value = atomic_load(atom); // 读取当前值  
    } while (old_value < upd && !atomic_compare_exchange_weak(atom, &old_value, upd));

    return old_value;  
}