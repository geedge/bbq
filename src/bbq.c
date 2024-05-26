/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-26 13:02:53
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#include "bbq.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static bbq_queue_state_s allocate_entry(bbq_queue_s* q, bbq_block_s* block);
static void commit_entry(bbq_entry_desc_s* e, bbq_data data);
static bbq_queue_state_s advance_phead(bbq_queue_s* q, uint64_t ph);
static bbq_queue_state_s reserve_entry(bbq_queue_s* q, bbq_block_s* block);
static bbq_data consume_entry(bbq_queue_s* q, bbq_entry_desc_s* e);
static bool advance_chead(bbq_queue_s* q, uint64_t ch, uint64_t ver);
static inline uint64_t idx(bbq_queue_s* q, uint64_t x);
static inline uint64_t off(bbq_queue_s* q, uint64_t x);
static inline uint64_t head_vsn(bbq_queue_s* q, uint64_t x);
static inline uint64_t cur_vsn(bbq_queue_s* q, uint64_t x);
static inline uint64_t set_cur_vsn(bbq_queue_s* q, uint64_t ver);
static inline uint64_t fetch_max(aotmic_uint64* atom, uint64_t upd);
static void* bbq_memset(void* data, size_t size);
extern void* bbq_malloc(bbq_module_e module, size_t size);
extern void bbq_free(bbq_module_e module, void* ptr);

// 根据entries大小返回合理的blocks配置TODO:
static uint32_t bbq_blocks_calc(uint32_t entries) {
    double log_entries = log10((double)entries);   // 计算log10(entries)
    uint32_t over4 = (uint32_t)(log_entries / 4);  // 向下取整
    uint32_t max_value = (over4 > 1) ? over4 : 1;  // 取1和log_y_over_4中的较大值
    uint32_t n = pow(10, max_value);               // 计算n = 10^max_value
    return n;
}

static int block_init(bbq_block_s* block, size_t bs, bool cursor_init) {
    block->entries = bbq_malloc(BBQ_MODULE_QUEUE, sizeof(bbq_data) * bs);
    if (block->entries == NULL) {
        BBQ_ERR_LOG("bbq_malloc error");
        return BBQ_ALLOC_ERR;
    }
    block->committed = 0;
    block->allocated = 0;
    block->reserved = 0;
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

static void block_cleanup(bbq_block_s* block) {
    bbq_free(BBQ_MODULE_QUEUE, block->entries);
    block->entries = NULL;
}

// 返回以2为底的对数并向下取整值：也就是求x 的二进制表示中最高位（最左边的1）的位置（位置从0开始）
// 参数不能为0，否则会死循环最终段错误
// 例如：x=1，return 0 (...1)
// x=3，return 1  （..11）
// x=9，return 3  （1..1）
static unsigned floor_log2(uint64_t x) {
    return x == 1 ? 0 : 1 + floor_log2(x >> 1);
}

// 返回以2为底的对数并向上取整值。
// 例如：x=1，return 0 （2^0=1）
// x=99， return 7（2^6=64  2^7=128）
static unsigned ceil_log2(uint64_t x) {
    return x == 1 ? 0 : floor_log2(x - 1) + 1;
}

bbq_queue_s* bbq_ring_create_bnbs(const char* name, size_t bn, size_t bs, int socket_id, bbq_policy_e policy) {
    int ret = 0;

    bbq_queue_s* q = bbq_malloc(BBQ_MODULE_QUEUE, sizeof(bbq_queue_s));
    if (q == NULL) {
        BBQ_ERR_LOG("malloc error");
        return NULL;
    }
    bbq_memset(q, sizeof(bbq_queue_s));

    ret = snprintf(q->name, sizeof(q->name), "bbq_%s", name);
    if (ret < 0 || ret >= (int)sizeof(q->name)) {
        BBQ_ERR_LOG("bbq name too long");
        goto error;
    }

    q->phead = 0;
    q->chead = 0;
    q->policy = policy;
    q->blocks = bbq_malloc(BBQ_MODULE_QUEUE, bn * sizeof(bbq_block_s));
    if (q->blocks == NULL) {
        goto error;
    }
    q->bn = bn;
    q->bs = bs;

    for (size_t i = 0; i < bn; ++i) {
        // 第一个block不需要设置cursor_init_flag
        bool cursor_init_flag = (i == 0 ? false : true);
        ret = block_init(&(q->blocks[i]), bs, cursor_init_flag);
        if (ret != BBQ_OK) {
            goto error;
        }
    }

    // TODO:是否需要考虑确认，bits位是否满了，无法给version留空间？？即限制BN和BS的边界?ceil_log2返回值至少<64?
    q->idx_bits = ceil_log2(bn);      // head 索引的个数，为方便快速取idx（通过掩码），把BN和BS的值扩充为2的N次幂
    q->off_bits = ceil_log2(bs) + 1;  // Cursor偏移的个数 TODO:  +1 是否合理？如果BS较小，线程较多，+1够吗？
    q->idx_mask = (1 << q->idx_bits) - 1;
    q->off_mask = (1 << q->off_bits) - 1;

    return q;

error:
    if (q != NULL) {
        if (q->blocks) {
            for (size_t i = 0; i < bn; ++i) {
                bbq_free(BBQ_MODULE_QUEUE, q->blocks->entries);
            }
            bbq_free(BBQ_MODULE_QUEUE, q->blocks);
        }
    }
    return NULL;
}

bbq_queue_s* bbq_ring_create(const char* name, uint32_t count, int socket_id, bbq_policy_e policy) {
    uint32_t bn = bbq_blocks_calc(count);
    uint32_t bs = count / bn;
    return bbq_ring_create_bnbs(name, bn, bs, socket_id, policy);
}

// TODO:检查队列是否都被消费了再删除？
void bbq_ring_free(bbq_queue_s* q) {
    if (q == NULL) {
        return;
    }

    for (size_t i = 0; i < q->bn; ++i) {
        block_cleanup(&(q->blocks[i]));
    }

    bbq_free(BBQ_MODULE_QUEUE, q->blocks);
    bbq_free(BBQ_MODULE_QUEUE, q);
}

int bbq_enqueue(bbq_queue_s* q, bbq_data data) {
    while (true) {
        // 获取当前phead，转为索引后获取到当前的blk
        uint64_t ph = atomic_load(&q->phead);
        bbq_block_s* blk = &(q->blocks[idx(q, ph) % q->bn]);  // TODO:check

        bbq_queue_state_s ps;
        bbq_queue_state_s state = allocate_entry(q, blk);

        switch (state.state) {
            case ALLOCATED:
                commit_entry(&state.e, data);
                return BBQ_OK;
            case BLOCK_DONE:
                ps = advance_phead(q, ph);
                switch (ps.state) {
                    case NO_ENTRY:
                        return BBQ_QUEUE_FULL;
                    case NOT_AVAILABLE:
                        return BBQ_QUEUE_BUSY;
                    case SUCCESS:
                        continue;
                }
                break;
            default:
                BBQ_ERR_LOG("Invalid QueueState in bbq_enqueue: %d", state.state);
        }
    }
}

// TODO:入参检查，出队失败，数据设置NULL？
int bbq_dequeue(bbq_queue_s* q, bbq_data* deq_data) {
    while (true) {
        uint64_t ch = atomic_load(&q->chead);
        bbq_block_s* blk = &(q->blocks[idx(q, ch) % q->bn]);  // TODO:check???

        bbq_data data;
        bbq_queue_state_s state;
        state = reserve_entry(q, blk);

        switch (state.state) {
            case RESERVED:
                data = consume_entry(q, &state.e);
                if (data) {
                    *deq_data = data;
                    return BBQ_OK;
                } else {
                    continue;
                }
            case NO_ENTRY:
                return BBQ_QUEUE_EMPTY;
            case NOT_AVAILABLE:
                return BBQ_QUEUE_BUSY;
            case BLOCK_DONE:
                if (advance_chead(q, ch, state.vsn)) {
                    continue;
                } else {
                    return BBQ_QUEUE_EMPTY;
                }
            default:
                BBQ_ERR_LOG("Invalid QueueState in dequeue state: %d", state.state);
        }
    }
}

static bbq_queue_state_s allocate_entry(bbq_queue_s* q, bbq_block_s* block) {
    bbq_queue_state_s state;
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

static void commit_entry(bbq_entry_desc_s* e, bbq_data data) {
    e->block->entries[e->off] = data;
    atomic_fetch_add(&e->block->committed, 1);
}

static bbq_queue_state_s advance_phead(bbq_queue_s* q, uint64_t ph) {
    // 获取下一个block TODO:改进，%BN修改为dpdk ring一样的
    bbq_block_s* n_blk;
    n_blk = &(q->blocks[(idx(q, ph) + 1) % q->bn]);

    // printf("## [B]advance_phead, cur phead_vsn:%u phead_idx:%u\n", head_vsn(phead),idx(phead));
    // printf("## [B]next block, allocated_vsn:%u allocated_idx:%u\n", cur_vsn(n_blk.allocated), off(n_blk.allocated));
    // printf("## [B]next block, committed_vsn:%u committed_idx:%u\n", cur_vsn(n_blk.committed), off(n_blk.committed));
    // printf("## [B]next block, consumed_vsn:%u consumed_idx:%u\n", cur_vsn(n_blk.consumed), off(n_blk.consumed));

    uint64_t cur, reserved;
    bbq_queue_state_s state;
    switch (q->policy) {
        case BBQ_RETRY_NEW:
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
        case BBQ_DROP_OLD:
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

    // 索引+1，当超过索引范围，也就是循环下一轮块时，version+1????
    fetch_max(&q->phead, ph + 1);

    // printf("!! [AF]next block, allocated_vsn:%u allocated_idx:%u\n", cur_vsn(n_blk.allocated), off(n_blk.allocated));
    // printf("!! [AF]next block, committed_vsn:%u committed_idx:%u\n", cur_vsn(n_blk.committed), off(n_blk.committed));
    // printf("!! [AF]advance_phead, cur phead_vsn:%u phead_idx:%u\n\n", head_vsn(phead),idx(phead));

    state.state = SUCCESS;
    return state;
}

static bbq_queue_state_s reserve_entry(bbq_queue_s* q, bbq_block_s* block) {
    while (true) {
        bbq_queue_state_s state;
        uint64_t reserved = atomic_load(&block->reserved);
        if (off(q, reserved) < q->bs) {
            uint64_t committed = atomic_load(&block->committed);
            if (off(q, committed) == off(q, reserved)) {  // TODO: 多entry的时候关注
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

static bbq_data consume_entry(bbq_queue_s* q, bbq_entry_desc_s* e) {
    bbq_data data = e->block->entries[e->off];

    uint64_t allocated;
    switch (q->policy) {
        case BBQ_RETRY_NEW:
            atomic_fetch_add(&e->block->consumed, 1);
            break;
        case BBQ_DROP_OLD:
            allocated = atomic_fetch_add(&e->block->allocated, 1);
            if (cur_vsn(q, allocated) != e->vsn)
                return NULL;
            break;
        default:
            break;
    }

    return data;
}

static bool advance_chead(bbq_queue_s* q, uint64_t ch, uint64_t ver) {
    bbq_block_s* n_blk = &(q->blocks[(idx(q, ch) + 1) % q->bn]);  // TODO: check

    uint64_t committed = atomic_load(&n_blk->committed);

    // printf("ZZ [B]advance_chead, chead_vsn:%u chead_idx:%u\n", head_vsn(chead),idx(chead));
    // printf("ZZ [B]next block, consumed_vsn:%u consumed_idx:%u\n", cur_vsn(n_blk.consumed), off(n_blk.consumed));
    // printf("ZZ [B]next block, reserved_vsn:%u reserved_idx:%u\n", cur_vsn(n_blk.reserved), off(n_blk.reserved));

    switch (q->policy) {
        case BBQ_RETRY_NEW:
            if (cur_vsn(q, committed) != head_vsn(q, ch) + 1)
                return false;
            fetch_max(&n_blk->consumed, set_cur_vsn(q, head_vsn(q, ch) + 1));
            fetch_max(&n_blk->reserved, set_cur_vsn(q, head_vsn(q, ch) + 1));
            break;
        case BBQ_DROP_OLD:
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

static inline uint64_t idx(bbq_queue_s* q, uint64_t x) {
    return x & q->idx_mask;
}

static inline uint64_t off(bbq_queue_s* q, uint64_t x) {
    return x & q->off_mask;
}

// TODO: 可以改进？文档里version移动的位置是max(idx_bits，off_bits)，在advance里，就能直接赋值，MAX(nblk.committed, bbq_cursor{.vsn=ph.vsn + 1});本代码里做了偏移判断。
static inline uint64_t head_vsn(bbq_queue_s* q, uint64_t x) {
    return x >> q->idx_bits;
}

static inline uint64_t cur_vsn(bbq_queue_s* q, uint64_t x) {
    return x >> q->off_bits;
}

static inline uint64_t set_cur_vsn(bbq_queue_s* q, uint64_t ver) {
    return ver << q->off_bits;
}

// TODO:测试
static inline uint64_t fetch_max(aotmic_uint64* atom, uint64_t upd) {
    uint64_t old_value;
    do {
        old_value = atomic_load(atom);  // 读取当前值
    } while (old_value < upd && !atomic_compare_exchange_weak(atom, &old_value, upd));

    return old_value;
}

#ifdef BBQ_MEMORY
typedef struct {
    aotmic_uint64 malloc_cnt;
    aotmic_uint64 free_cnt;
} bbq_memory_s;
bbq_memory_s bbq_memory_g[BBQ_MODULE_MAX] = {0};
#endif

void* bbq_malloc(bbq_module_e module, size_t size) {
    void* ptr = malloc(size);
#ifdef BBQ_MEMORY
    if (ptr != NULL) {
        atomic_fetch_add(&bbq_memory_g[module].malloc_cnt, 1);
    }
#endif
    return ptr;
}

void bbq_free(bbq_module_e module, void* ptr) {
#ifdef BBQ_MEMORY
    if (ptr != NULL) {
        atomic_fetch_add(&bbq_memory_g[module].free_cnt, 1);
    }
#endif
    free(ptr);
}

static void* bbq_memset(void* data, size_t size) {
    if (data != NULL && size > 0) {
        memset(data, 0, size);
    }
}

void bbq_memory_info() {
#ifdef BBQ_MEMORY
    for (int i = 0; i < BBQ_MODULE_MAX; i++) {
        uint64_t malloc_cnt = atomic_load(&bbq_memory_g[i].malloc_cnt);
        uint64_t free_cnt = atomic_load(&bbq_memory_g[i].free_cnt);
        if (malloc_cnt == 0 && free_cnt == 0) {
            continue;
        }

        BBQ_INFO_LOG("[%d] malloc:%lu free:%lu", i,
                     atomic_load(&bbq_memory_g[i].malloc_cnt),
                     atomic_load(&bbq_memory_g[i].free_cnt));
    }
#else
    BBQ_INFO_LOG("BBQ_MEMORY is not define");
#endif
}

bool bbq_malloc_free_equal() {
#ifdef BBQ_MEMORY
    bool ret = true;
    for (int i = 0; i < BBQ_MODULE_MAX; i++) {
        uint64_t malloc_cnt = atomic_load(&bbq_memory_g[i].malloc_cnt);
        uint64_t free_cnt = atomic_load(&bbq_memory_g[i].free_cnt);
        if (malloc_cnt != free_cnt) {
            BBQ_ERR_LOG("[%d] malloc:%lu free:%lu, not equal\n", i, malloc_cnt, free_cnt);
            ret = false;
        }
    }
    return ret;
#else
    BBQ_INFO_LOG("BBQ_MEMORY is not define");
    return true;
#endif
}
