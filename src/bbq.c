/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-20 18:48:59
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#include "bbq.h"
#include <stdio.h>

static QueueState_s allocate_entry(BlockBoundedQueue_s *q, Block_s *block);
static void commit_entry(EntryDesc_s *e, bbq_data data);
static QueueState_s advance_phead(BlockBoundedQueue_s *q, uint64_t ph);
static QueueState_s reserve_entry(BlockBoundedQueue_s *q, Block_s *block);
static bbq_data consume_entry(BlockBoundedQueue_s *q, EntryDesc_s *e);
static bool advance_chead(BlockBoundedQueue_s *q, uint64_t ch, uint64_t ver);
static inline uint64_t idx(BlockBoundedQueue_s *q, uint64_t x);
static inline uint64_t off(BlockBoundedQueue_s *q, uint64_t x);
static inline uint64_t head_vsn(BlockBoundedQueue_s *q, uint64_t x);
static inline uint64_t cur_vsn(BlockBoundedQueue_s *q, uint64_t x);
static inline uint64_t set_cur_vsn(BlockBoundedQueue_s *q, uint64_t ver);
static inline uint64_t fetch_max(aotmic_uint64 *atom, uint64_t upd);

static void *bbq_malloc(size_t size) {
    return malloc(size);
}

static void bbq_free(void *ptr) {
    free(ptr);
}

int block_init(Block_s *block, size_t bs, bool cursor_init) {
    block->entries = bbq_malloc(sizeof(bbq_data *) * bs);
    if (block->entries == NULL) {
        return BBQ_ALLOC_ERR;
    }

    block->allocated = 0;
    block->committed = 0;
    block->consumed = 0;
    block->consumed = 0;

    if (cursor_init) {
        // block数组里，除了第一块之外需要设置
        block->committed |= bs;
        block->allocated |= bs;
        block->reserved |= bs;
        block->consumed |= bs;
    }

    return BBQ_OK;
}

void block_cleanup(Block_s *block) {
    bbq_free(block->entries);
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

BlockBoundedQueue_s *BlockBoundedQueue_create(size_t bn, size_t bs, Policy_e policy) {
    BlockBoundedQueue_s *q = bbq_malloc(sizeof(BlockBoundedQueue_s));
    if (q = NULL) {
        BBQ_ERR_LOG("bbq_malloc error\n");
        return NULL;
    }

    q->phead = 0;
    q->chead = 0;
    q->policy = policy;
    q->blocks = bbq_malloc(bn);

    block_init(&(q->blocks[0]), bs, false);
    for (size_t i = 1; i < bs; ++i) {
        block_init(&(q->blocks[i]), bs, true);
    }

    // TODO:是否需要考虑确认，bits位是否满了，无法给version留空间？？即限制BN和BS的边界?ceil_log2返回值至少<64?
    q->idx_bits = ceil_log2(bn);     // head 索引的个数，为方便快速取idx（通过掩码），把BN和BS的值扩充为2的N次幂
    q->off_bits = ceil_log2(bs) + 1; // Cursor偏移的个数 TODO:  +1 是否合理？如果BS较小，线程较多，+1够吗？
    q->idx_mask = (1 << q->idx_bits) - 1;
    q->off_mask = (1 << q->off_bits) - 1;

    return NULL;
}

// TODO:检查队列是否都被消费了再删除？
int BlockBoundedQueue_destory(BlockBoundedQueue_s *q) {
    if (q = NULL) {
        return BBQ_NULL_PTR;
    }

    for (size_t i = 0; i < q->bn; ++i) {
        block_cleanup(&(q->blocks[i]));
    }

    bbq_free(q->blocks);
    return BBQ_OK;
}

QueueStatus_s enqueue(BlockBoundedQueue_s *q, bbq_data data) {
    while (true) {
        // 获取当前phead，转为索引后获取到当前的blk
        uint64_t ph = atomic_load(&q->phead);
        Block_s *blk = &(q->blocks[q->idx(ph)]);

        QueueState_s ps;
        QueueState_s state = allocate_entry(q, blk);
        QueueStatus_s status;

        switch (state.state) {
        case ALLOCATED:
            commit_entry(&state.e, data);
            status.status = OK;
            return status;
        case BLOCK_DONE:
            ps = advance_phead(q, ph);
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

QueueStatus_s dequeue(BlockBoundedQueue_s *q) {
    while (true) {
        uint64_t ch = atomic_load(&q->chead);
        Block_s *blk = &(q->blocks[q->idx(ch)]);

        bbq_data data;
        QueueState_s state;
        state = reserve_entry(q, blk);
        QueueStatus_s status;

        switch (state.state) {
        case RESERVED:
            data = consume_entry(q, &state.e);
            if (data) {
                status.status = OK;
                status.data = data;
                return status;
            } else {
                continue;
            }
        case NO_ENTRY:
            status.status = EMPTY;
            return status;
        case NOT_AVAILABLE:
            status.status = BUSY;
            return status;
        case BLOCK_DONE:
            if (advance_chead(q, ch, state.vsn)) {
                continue;
            } else {
                status.status = EMPTY;
            }
        default:
            BBQ_ERR_LOG("Invalid QueueState in dequeue: %d\n", state.state);
        }
    }
}

static QueueState_s allocate_entry(BlockBoundedQueue_s *q, Block_s *block) {
    QueueState_s state;
    if (off(q, atomic_load(&block->allocated)) >= q->bs) {
        state.state = BLOCK_DONE;
        return state;
    }

    // TODO:check 有没有影响?检查是否会因为多线程同时add，超过了预留的位段，溢出后实际已超，但是认为还有数据？？？？
    uint64_t old = atomic_fetch_add(&block->allocated, 1);
    if (off(q, old) >= q->bs) {
        state.state = BLOCK_DONE;
        return state;
    }

    state.state = ALLOCATED;
    state.e.block = block;
    state.e.vsn = cur_vsn(q, old);
    state.e.off = off(q, old);

    return state;
}

static void commit_entry(EntryDesc_s *e, bbq_data data) {
    e->block->entries[e->off] = data;
    atomic_fetch_add(&e->block->committed, 1);
}

static QueueState_s advance_phead(BlockBoundedQueue_s *q, uint64_t ph) {
    // 获取下一个block TODO:改进，%BN修改为dpdk ring一样的
    Block_s *n_blk;
    n_blk = &(q->blocks[(idx(q, ph) + 1) % q->bn]);

    // printf("## [B]advance_phead, cur phead_vsn:%u phead_idx:%u\n", head_vsn(phead),idx(phead));
    // printf("## [B]next block, allocated_vsn:%u allocated_idx:%u\n", cur_vsn(n_blk.allocated), off(n_blk.allocated));
    // printf("## [B]next block, committed_vsn:%u committed_idx:%u\n", cur_vsn(n_blk.committed), off(n_blk.committed));
    // printf("## [B]next block, consumed_vsn:%u consumed_idx:%u\n", cur_vsn(n_blk.consumed), off(n_blk.consumed));

    uint64_t cur, reserved;
    QueueState_s state;
    switch (q->policy) {
    case RETRY_NEW:
        // consumed
        cur = atomic_load(&n_blk->consumed);
        if (cur_vsn(q, cur) < head_vsn(q, ph) || (cur_vsn(q, cur) == head_vsn(q, ph) && off(q, cur) != q->bs)) {
            // printf("@@ [B]next block, consumed_vsn:%u consumed_idx:%u\n", cur_vsn(n_blk.consumed), off(n_blk.consumed));
            // printf("@@ [B]advance_phead, cur phead_vsn:%u phead_idx:%u\n", head_vsn(phead),idx(phead));
            // printf("@@ [B]advance_phead, cur chead_vsn:%u chead_idx:%u\n", head_vsn(chead),idx(chead));
            reserved = atomic_load(&n_blk->reserved);
            if (off(q, reserved) == off(q, cur)) {
                state.state = NO_ENTRY;
            } else {
                state.state = NOT_AVAILABLE;
            }
            return state;
        }
        break;
    case DROP_OLD:
        // committed
        cur = atomic_load(&n_blk->committed);
        // 生产者避免前进到上一轮中尚未完全提交的区块，TODO: 什么时候回来出现< 或 >的情况？？？
        if (cur_vsn(q, cur) == head_vsn(q, ph) && off(q, cur) != q->bs) {
            state.state = NOT_AVAILABLE;
            return state;
        }
        break;
    default:
        break;
    }

    // 用head的version初始化下一个块，version在高位，version+1，idex/offset清零，如果没有被其他线程执行过，数值会高于旧值
    fetch_max(&n_blk->committed, set_cur_vsn(q, head_vsn(q, ph) + 1));
    fetch_max(&n_blk->allocated, set_cur_vsn(q, head_vsn(q, ph) + 1));

    // 索引+1，当超过索引范围，也就是循环下一轮块时，version+1
    fetch_max(&q->phead, ph + 1);

    // printf("!! [AF]next block, allocated_vsn:%u allocated_idx:%u\n", cur_vsn(n_blk.allocated), off(n_blk.allocated));
    // printf("!! [AF]next block, committed_vsn:%u committed_idx:%u\n", cur_vsn(n_blk.committed), off(n_blk.committed));
    // printf("!! [AF]advance_phead, cur phead_vsn:%u phead_idx:%u\n\n", head_vsn(phead),idx(phead));

    state.state = SUCCESS;
    return state;
}

static QueueState_s reserve_entry(BlockBoundedQueue_s *q, Block_s *block) {
    while (true) {
        QueueState_s state;
        uint64_t reserved = atomic_load(&block->reserved);
        if (off(q, reserved) < q->bs) {
            uint64_t committed = atomic_load(&block->committed);
            if (off(q, committed) == off(q, reserved)) { // TODO: 多entry的时候关注
                state.state = NO_ENTRY;
                return state;
            }

            // 当前块的数据没有被全部comit，需要通过判断allocated和committed来判断是否存在正在入队进行中的数据
            if (off(q, committed) != q->bs) {
                uint64_t allocated = atomic_load(&block->allocated);
                if (off(q, allocated) != off(q, committed)) {
                    state.state = NOT_AVAILABLE;
                    return state;
                }
            }

            if (fetch_max(&block->reserved, reserved + 1) == reserved) {
                // fetch_max返回的是旧值，因此预期返回的旧值要等于局部变量reserved
                state.state = RESERVED;
                state.e.block = block;
                state.e.off = off(q, reserved);
                state.e.vsn = cur_vsn(q, reserved);

                return state;
            } else {
                // 如果不等于代表block.reserved被其他线程Reserved了
                continue;
            }
        }

        state.state = BLOCK_DONE;
        state.vsn = cur_vsn(q, reserved);

        return state;
    }
}

static bbq_data consume_entry(BlockBoundedQueue_s *q, EntryDesc_s *e) {
    bbq_data data = e->block->entries[e->off];

    uint64_t allocated;
    switch (q->policy) {
    case RETRY_NEW:
        atomic_fetch_add(&e->block->consumed, 1);
        break;
    case DROP_OLD:
        allocated = atomic_fetch_add(&e->block->allocated, 1);
        if (cur_vsn(q, allocated) != e->vsn)
            return NULL;
        break;
    default:
        break;
    }

    return data;
}

static bool advance_chead(BlockBoundedQueue_s *q, uint64_t ch, uint64_t ver) {
    Block_s *n_blk = &(q->blocks[(idx(q, ch) + 1) % q->bn]);
    uint64_t committed = atomic_load(&n_blk->committed);

    // printf("ZZ [B]advance_chead, chead_vsn:%u chead_idx:%u\n", head_vsn(chead),idx(chead));
    // printf("ZZ [B]next block, consumed_vsn:%u consumed_idx:%u\n", cur_vsn(n_blk.consumed), off(n_blk.consumed));
    // printf("ZZ [B]next block, reserved_vsn:%u reserved_idx:%u\n", cur_vsn(n_blk.reserved), off(n_blk.reserved));

    switch (q->policy) {
    case RETRY_NEW:
        if (cur_vsn(q, committed) != head_vsn(q, ch) + 1)
            return false;
        fetch_max(&n_blk->consumed, set_cur_vsn(q, head_vsn(q, ch) + 1));
        fetch_max(&n_blk->reserved, set_cur_vsn(q, head_vsn(q, ch) + 1));
        break;
    case DROP_OLD:
        // 通过检查下一个块的版本是否大于或等于当前块来保证 FIFO 顺序，是这种情况，reserved 将被重置为已提交的版本·
        // 第一个块是一个特殊情况，因为与其他块相比，它的版本总是相差一个。因此，如果 ch.idx == 0，我们在比较中加 1
        if (cur_vsn(q, committed) < ver + (idx(q, ch) == 0))
            return false;
        fetch_max(&n_blk->reserved, set_cur_vsn(q, cur_vsn(q, committed)));
        break;
    default:
        break;
    }

    fetch_max(&q->chead, ch + 1);
    // printf("YY [AF]next block, consumed_vsn:%u consumed_idx:%u\n", cur_vsn(n_blk.consumed), off(n_blk.consumed));
    // printf("YY [AF]next block, reserved_vsn:%u reserved_idx:%u\n", cur_vsn(n_blk.reserved), off(n_blk.reserved));
    // printf("YY [AF]advance_chead, chead_vsn:%u chead_idx:%u\n\n", head_vsn(chead),idx(chead));
    return true;
}

static inline uint64_t idx(BlockBoundedQueue_s *q, uint64_t x) {
    return x & q->idx_mask;
}

static inline uint64_t off(BlockBoundedQueue_s *q, uint64_t x) {
    return x & q->off_mask;
}

// TODO: 可以改进？文档里version移动的位置是max(idx_bits，off_bits)，在advance里，就能直接赋值，MAX(nblk.committed, Cursor{.vsn=ph.vsn + 1});本代码里做了偏移判断。
static inline uint64_t head_vsn(BlockBoundedQueue_s *q, uint64_t x) {
    return x >> q->idx_bits;
}

static inline uint64_t cur_vsn(BlockBoundedQueue_s *q, uint64_t x) {
    return x >> q->off_bits;
}

static inline uint64_t set_cur_vsn(BlockBoundedQueue_s *q, uint64_t ver) {
    return ver << q->off_bits;
}

// TODO:测试
static inline uint64_t fetch_max(aotmic_uint64 *atom, uint64_t upd) {
    uint64_t old_value;
    do {
        old_value = atomic_load(atom); // 读取当前值
    } while (old_value < upd && !atomic_compare_exchange_weak(atom, &old_value, upd));

    return old_value;
}