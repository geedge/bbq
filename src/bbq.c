/*
 * @Author: liuyu
 * @LastEditTime: 2024-06-03 16:51:15
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#include "bbq.h"
#include <math.h>
#include <numa.h>
#include <stdio.h>
#include <string.h>

extern bbq_queue_state_s allocate_entry(bbq_queue_s *q, bbq_block_s *block);
extern void commit_entry(bbq_entry_desc_s *e, bbq_data data);
extern bbq_queue_state_s advance_phead(bbq_queue_s *q, uint64_t ph);
extern bbq_queue_state_s reserve_entry(bbq_queue_s *q, bbq_block_s *block);
extern bbq_data consume_entry(bbq_queue_s *q, bbq_entry_desc_s *e);
extern bool advance_chead(bbq_queue_s *q, uint64_t ch, uint64_t ver);
extern inline uint64_t bbq_idx(bbq_queue_s *q, uint64_t x);
extern inline uint64_t bbq_off(bbq_queue_s *q, uint64_t x);
extern inline uint64_t bbq_head_vsn(bbq_queue_s *q, uint64_t x);
extern inline uint64_t bbq_cur_vsn(bbq_queue_s *q, uint64_t x);
extern inline uint64_t set_cur_vsn(bbq_queue_s *q, uint64_t ver);
extern inline uint64_t fetch_max(aotmic_uint64 *atom, uint64_t upd);
extern void *bbq_memset(void *data, size_t size);
extern void *bbq_malloc(bbq_module_e module, int socket_id, size_t size);
extern void bbq_free(bbq_module_e module, int socket_id, void *ptr, size_t size);

bool bbq_is_power_of_two(uint32_t n) {
    // 负数和0都不是2的N次方
    if (n <= 0) {
        return false;
    }
    // 检查n的二进制表示是否只有一个1
    return (n & (n - 1)) == 0;
}

// 根据entries大小返回合理的blocks配置
uint32_t bbq_blocks_calc(uint32_t entries) {
    double log_entries = log2((double)entries);
    uint32_t over4 = (uint32_t)(log_entries / 4); // 向下取整
    uint32_t max_value = (over4 > 1) ? over4 : 1;
    uint32_t n = pow(2, max_value);
    return n;
}

int block_init(bbq_policy_e policy, bbq_block_s *block, uint32_t bs, bool cursor_init, int socket_id) {
    block->entries = bbq_malloc(BBQ_MODULE_QUEUE_BLOCK_ENTRY, socket_id, sizeof(bbq_data) * bs);
    if (block->entries == NULL) {
        BBQ_ERR_LOG("bbq_malloc error");
        return BBQ_ALLOC_ERR;
    }
    block->committed = ATOMIC_VAR_INIT(0);
    block->allocated = ATOMIC_VAR_INIT(0);
    block->reserved = ATOMIC_VAR_INIT(0);
    block->consumed = ATOMIC_VAR_INIT(0);

    if (cursor_init) {
        // block数组里，除了第一块之外需要设置
        block->committed = ATOMIC_VAR_INIT(bs);
        block->allocated = ATOMIC_VAR_INIT(bs);
        block->reserved = ATOMIC_VAR_INIT(bs);
        if (policy == BBQ_RETRY_NEW) {
            block->consumed = ATOMIC_VAR_INIT(bs);
        } else {
            block->consumed = ATOMIC_VAR_INIT(0);
        }
    }

    return BBQ_OK;
}

void block_cleanup(bbq_block_s *block, uint32_t bs, int socket_id) {
    bbq_free(BBQ_MODULE_QUEUE_BLOCK_ENTRY, socket_id, block->entries, sizeof(bbq_data) * bs);
    block->entries = NULL;
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

bbq_queue_s *bbq_ring_create_bnbs(const char *name, uint32_t bn, uint32_t bs, drop_old_free_func free_func) {
    return bbq_ring_create_bnbs_with_socket(name, bn, bs, BBQ_INVALID_SOCKET, free_func);
}

bbq_queue_s *bbq_ring_create_bnbs_with_socket(const char *name, uint32_t bn, uint32_t bs, int socket_id, drop_old_free_func free_func) {
    int ret = 0;
    bool numa_enable = true;

    if (bbq_is_power_of_two(bn) == false) {
        BBQ_ERR_LOG("block number is not power of two, now is  :%lu", bn);
        return NULL;
    }

    if (bbq_is_power_of_two(bs) == false) {
        BBQ_ERR_LOG("block size is not power of two, now is  :%lu", bs);
        return NULL;
    }

    if (numa_available() < 0) {
        // 不支持numa，设置
        socket_id = BBQ_INVALID_SOCKET;
    }

    bbq_queue_s *q = bbq_malloc(BBQ_MODULE_QUEUE, socket_id, sizeof(bbq_queue_s));
    if (q == NULL) {
        BBQ_ERR_LOG("malloc for bbq queue error");
        return NULL;
    }
    bbq_memset(q, sizeof(bbq_queue_s));

    ret = snprintf(q->name, sizeof(q->name), "bbq_%s", name);
    if (ret < 0 || ret >= (int)sizeof(q->name)) {
        BBQ_ERR_LOG("bbq queue name too long");
        goto error;
    }
    q->bn = bn;
    q->bs = bs;
    q->socket_id = socket_id;
    q->phead = 0;
    q->chead = 0;

    if (free_func == NULL) {
        q->policy = BBQ_RETRY_NEW;
        q->drop_old_free = NULL;
    } else {
        q->policy = BBQ_DROP_OLD;
        q->drop_old_free = free_func;
    }
    q->blocks = bbq_malloc(BBQ_MODULE_QUEUE_BLOCK_NB, socket_id, bn * sizeof(bbq_block_s));
    if (q->blocks == NULL) {
        BBQ_ERR_LOG("bbq malloc for blocks error");
        goto error;
    }

    for (uint32_t i = 0; i < bn; ++i) {
        // 第一个block不需要设置cursor_init_flag
        bool cursor_init_flag = (i == 0 ? false : true);
        ret = block_init(q->policy, &(q->blocks[i]), bs, cursor_init_flag, socket_id);
        if (ret != BBQ_OK) {
            BBQ_ERR_LOG("bbq block init error");
            goto error;
        }
    }

    q->idx_bits = ceil_log2(bn);
    q->off_bits = ceil_log2(bs) + 1;

    q->idx_mask = (1 << q->idx_bits) - 1;
    q->off_mask = (1 << q->off_bits) - 1;

    return q;

error:
    if (q != NULL) {
        if (q->blocks) {
            for (uint32_t i = 0; i < bn; ++i) {
                bbq_free(BBQ_MODULE_QUEUE_BLOCK_ENTRY, q->socket_id, q->blocks->entries, sizeof(bbq_data) * bs);
            }
            bbq_free(BBQ_MODULE_QUEUE_BLOCK_NB, q->socket_id, q->blocks, bn * sizeof(bbq_block_s));
        }
        bbq_free(BBQ_MODULE_QUEUE, q->socket_id, q, sizeof(bbq_queue_s));
    }
    return NULL;
}

bbq_queue_s *bbq_ring_create(const char *name, uint32_t count, drop_old_free_func free_func) {
    // 传入无效socket_id，将使用malloc分配内存
    return bbq_ring_create_with_socket(name, count, BBQ_INVALID_SOCKET, free_func);
}

bbq_queue_s *bbq_ring_create_with_socket(const char *name, uint32_t count, int socket_id, drop_old_free_func free_func) {
    if (bbq_is_power_of_two(count) == false) {
        BBQ_ERR_LOG("bbq entries number must be power of two and greater than 1, now is  :%lu", count);
        return NULL;
    }

    uint32_t bn = bbq_blocks_calc(count);
    uint32_t bs = count / bn;
    return bbq_ring_create_bnbs_with_socket(name, bn, bs, socket_id, free_func);
}

void bbq_ring_free(bbq_queue_s *q) {
    if (q == NULL) {
        return;
    }

    for (uint32_t i = 0; i < q->bn; ++i) {
        block_cleanup(&(q->blocks[i]), q->bs, q->socket_id);
    }

    bbq_free(BBQ_MODULE_QUEUE_BLOCK_NB, q->socket_id, q->blocks, q->bn * sizeof(bbq_block_s));
    bbq_free(BBQ_MODULE_QUEUE, q->socket_id, q, sizeof(bbq_queue_s));
}

int bbq_enqueue(bbq_queue_s *q, bbq_data data) {
    while (true) {
        // 获取当前phead，转为索引后获取到当前的blk
        uint64_t ph = atomic_load(&q->phead);
        bbq_block_s *blk = &(q->blocks[bbq_idx(q, ph)]);

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

// int bbq_dequeue_burst(bbq_queue_s *q, void **obj_table, unsigned int n, unsigned int *available) {
// }

// int bbq_enqueue_burst(bbq_queue_s *q, void *const *obj_table,
//                       unsigned int n, unsigned int *free_space) {
// }

int bbq_dequeue(bbq_queue_s *q, bbq_data *deq_data) {
    while (true) {
        uint64_t ch = atomic_load(&q->chead);
        bbq_block_s *blk = &(q->blocks[bbq_idx(q, ch)]);

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

bbq_queue_state_s allocate_entry(bbq_queue_s *q, bbq_block_s *block) {
    bbq_queue_state_s state = {0};
    if (bbq_off(q, atomic_load(&block->allocated)) >= q->bs) {
        state.state = BLOCK_DONE;
        return state;
    }

    uint64_t old = atomic_fetch_add(&block->allocated, 1);
    uint64_t committed_vsn = bbq_cur_vsn(q, atomic_load(&block->committed));

    // committed_vsn，在当前块被初始化后值是不变的，判断vsn是考虑到极限情况下给off预留的空间不足导致，allocated的off溢出（vsn+1）
    if ((bbq_cur_vsn(q, old) != committed_vsn) || (bbq_off(q, old) >= q->bs)) {
        state.state = BLOCK_DONE;
        return state;
    }

    state.state = ALLOCATED;
    state.e.block = block;
    state.e.vsn = bbq_cur_vsn(q, old);
    state.e.bbq_off = bbq_off(q, old);

    return state;
}

void commit_entry(bbq_entry_desc_s *e, bbq_data data) {
    e->block->entries[e->bbq_off] = data;
    atomic_fetch_add(&e->block->committed, 1);
}

bbq_queue_state_s advance_phead(bbq_queue_s *q, uint64_t ph) {
    // 获取下一个block
    bbq_block_s *n_blk;
    n_blk = &(q->blocks[(bbq_idx(q, ph) + 1) & q->idx_mask]);

    uint64_t cur, reserved;
    bbq_queue_state_s state = {0};
    switch (q->policy) {
    case BBQ_RETRY_NEW:
        // consumed
        cur = atomic_load(&n_blk->consumed);
        if (bbq_cur_vsn(q, cur) < bbq_head_vsn(q, ph) || // 生产者赶上了消费者
            (bbq_cur_vsn(q, cur) == bbq_head_vsn(q, ph) && bbq_off(q, cur) != q->bs)) {
            reserved = atomic_load(&n_blk->reserved);
            if (bbq_off(q, reserved) == bbq_off(q, cur)) {
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
        // 生产者避免前进到上一轮中尚未完全提交的区块
        if (bbq_cur_vsn(q, cur) == bbq_head_vsn(q, ph) && bbq_off(q, cur) != q->bs) {
            state.state = NOT_AVAILABLE;
            return state;
        }
        break;
    default:
        break;
    }

    // 用head的version初始化下一个块，version在高位，version+1，idex/offset清零，如果没有被其他线程执行过，数值会高于旧值。多线程同时只更新一次。
    fetch_max(&n_blk->committed, set_cur_vsn(q, bbq_head_vsn(q, ph) + 1));
    fetch_max(&n_blk->allocated, set_cur_vsn(q, bbq_head_vsn(q, ph) + 1));

    // 索引+1，当超过索引范围，也就是循环下一轮块时，version+1
    fetch_max(&q->phead, ph + 1);
    state.state = SUCCESS;
    return state;
}

bbq_queue_state_s reserve_entry(bbq_queue_s *q, bbq_block_s *block) {
    while (true) {
        bbq_queue_state_s state;
        uint64_t reserved = atomic_load(&block->reserved);
        if (bbq_off(q, reserved) < q->bs) {
            uint64_t consumed = atomic_load(&block->consumed);
            if (q->policy == BBQ_RETRY_NEW && bbq_cur_vsn(q, reserved) != bbq_cur_vsn(q, consumed)) {
                // consumed溢出了，这种情况只发生在BBQ_RETRY_NEW，因为BBQ_DROP_OLD模式，consumed没有用到
                state.state = BLOCK_DONE;
                state.vsn = bbq_cur_vsn(q, reserved);
                return state;
            }

            uint64_t committed = atomic_load(&block->committed);
            if (bbq_off(q, committed) == bbq_off(q, reserved)) { // TODO:多entry关注
                state.state = NO_ENTRY;
                return state;
            }

            // 当前块的数据没有被全部commited，需要通过判断allocated和committed来判断是否存在正在入队进行中的数据
            if (bbq_off(q, committed) != q->bs) {
                uint64_t allocated = atomic_load(&block->allocated);
                if (bbq_off(q, allocated) != bbq_off(q, committed)) {
                    state.state = NOT_AVAILABLE;
                    return state;
                }
            }

            if (fetch_max(&block->reserved, reserved + 1) == reserved) { // TODO:多entry时关注
                // fetch_max返回的是旧值，因此预期返回的旧值要等于局部变量reserved
                state.state = RESERVED;
                state.e.block = block;
                state.e.bbq_off = bbq_off(q, reserved);
                state.e.vsn = bbq_cur_vsn(q, reserved);

                return state;
            } else {
                // 如果不等于代表block.reserved被其他线程Reserved了
                continue;
            }
        }

        state.state = BLOCK_DONE;
        state.vsn = bbq_cur_vsn(q, reserved);
        return state;
    }
}

bbq_data consume_entry(bbq_queue_s *q, bbq_entry_desc_s *e) {
    bbq_data data = e->block->entries[e->bbq_off];

    uint64_t allocated;
    switch (q->policy) {
    case BBQ_RETRY_NEW:
        atomic_fetch_add(&e->block->consumed, 1);
        break;
    case BBQ_DROP_OLD:
        allocated = atomic_load(&e->block->allocated);
        // 预留的entry已经被新生产的数据覆盖了，旧数据丢弃并释放
        if (bbq_cur_vsn(q, allocated) != e->vsn) {
            printf("drop %p\n", data);
            return NULL;
        }
        break;
    default:
        break;
    }

    return data;
}

bool advance_chead(bbq_queue_s *q, uint64_t ch, uint64_t ver) {
    bbq_block_s *n_blk = &(q->blocks[(bbq_idx(q, ch) + 1) & q->idx_mask]);

    uint64_t committed = atomic_load(&n_blk->committed);
    switch (q->policy) {
    case BBQ_RETRY_NEW:
        if (bbq_cur_vsn(q, committed) != bbq_head_vsn(q, ch) + 1) {
            // 消费者追上了生产者，下一块还未开始生产
            return false;
        }
        fetch_max(&n_blk->consumed, set_cur_vsn(q, bbq_head_vsn(q, ch) + 1));
        fetch_max(&n_blk->reserved, set_cur_vsn(q, bbq_head_vsn(q, ch) + 1));
        break;
    case BBQ_DROP_OLD:
        // 通过检查下一个块的版本是否大于或等于当前块来保证 FIFO 顺序.
        // 第一个块是一个特殊情况，因为与其他块相比，它的版本总是相差一个。因此，如果 ch.bbq_idx == 0，我们在比较中加 1
        if (bbq_cur_vsn(q, committed) < ver + (bbq_idx(q, ch) == 0))
            return false;
        fetch_max(&n_blk->reserved, set_cur_vsn(q, bbq_cur_vsn(q, committed)));
        break;
    default:
        break;
    }

    fetch_max(&q->chead, ch + 1);
    return true;
}

inline uint64_t bbq_idx(bbq_queue_s *q, uint64_t x) {
    return x & q->idx_mask;
}

inline uint64_t bbq_off(bbq_queue_s *q, uint64_t x) {
    return x & q->off_mask;
}

inline uint64_t bbq_head_vsn(bbq_queue_s *q, uint64_t x) {
    return x >> q->idx_bits;
}

inline uint64_t bbq_cur_vsn(bbq_queue_s *q, uint64_t x) {
    return x >> q->off_bits;
}

inline uint64_t set_cur_vsn(bbq_queue_s *q, uint64_t ver) {
    return ver << q->off_bits;
}

uint64_t fetch_max(aotmic_uint64 *atom, uint64_t upd) {
    uint64_t old_value;
    do {
        old_value = atomic_load(atom); // 读取当前值
    } while (old_value < upd && !atomic_compare_exchange_weak(atom, &old_value, upd));

    return old_value;
}

#ifdef BBQ_MEMORY
typedef struct {
    aotmic_uint64 malloc_cnt;
    aotmic_uint64 malloc_size;
    aotmic_uint64 free_cnt;
    aotmic_uint64 free_size;
} bbq_memory_s;
bbq_memory_s bbq_memory_g[BBQ_MODULE_MAX] = {0};
#endif

void *bbq_malloc(bbq_module_e module, int socket_id, size_t size) {
    void *ptr = NULL;
    if (socket_id >= 0) {
        ptr = numa_alloc_onnode(size, 0);
    } else {
        ptr = malloc(size);
    }
#ifdef BBQ_MEMORY
    if (ptr != NULL) {
        atomic_fetch_add(&bbq_memory_g[module].malloc_cnt, 1);
        atomic_fetch_add(&bbq_memory_g[module].malloc_size, size);
    }
#endif

    return ptr;
}

void bbq_free(bbq_module_e module, int socket_id, void *ptr, size_t size) {
#ifdef BBQ_MEMORY
    if (ptr != NULL) {
        atomic_fetch_add(&bbq_memory_g[module].free_cnt, 1);
        atomic_fetch_add(&bbq_memory_g[module].free_size, size);
    }
#endif
    // free(ptr);
    if (socket_id >= 0) {
        numa_free(ptr, size);
    } else {
        free(ptr);
    }
}

void *bbq_memset(void *data, size_t size) {
    if (data != NULL && size > 0) {
        memset(data, 0, size);
    }
}

bool bbq_malloc_free_equal() {
#ifdef BBQ_MEMORY
    bool ret = true;
    for (int i = 0; i < BBQ_MODULE_MAX; i++) {
        uint64_t malloc_cnt = atomic_load(&bbq_memory_g[i].malloc_cnt);
        uint64_t free_cnt = atomic_load(&bbq_memory_g[i].free_cnt);
        if (malloc_cnt != free_cnt) {
            BBQ_ERR_LOG("[module:%d] malloc:%lu free:%lu, bbq mmalloc-free count not equal\n", i, malloc_cnt, free_cnt);
            ret = false;
        }

        uint64_t malloc_size = atomic_load(&bbq_memory_g[i].malloc_size);
        uint64_t free_size = atomic_load(&bbq_memory_g[i].free_size);
        if (malloc_size != free_size) {
            BBQ_ERR_LOG("[module:%d] malloc:%lu free:%lu, bbq mmalloc-free size not equal\n", i, malloc_cnt, free_cnt);
            ret = false;
        }
    }
    return ret;
#else
    return true;
#endif
}

void bbq_memory_info() {
#ifdef BBQ_MEMORY
    for (int i = 0; i < BBQ_MODULE_MAX; i++) {
        uint64_t malloc_cnt = atomic_load(&bbq_memory_g[i].malloc_cnt);
        uint64_t free_cnt = atomic_load(&bbq_memory_g[i].free_cnt);
        if (malloc_cnt == 0 && free_cnt == 0) {
            continue;
        }

        BBQ_INFO_LOG("[%d]bbq malloc:%lu free:%lu", i,
                     atomic_load(&bbq_memory_g[i].malloc_cnt),
                     atomic_load(&bbq_memory_g[i].free_cnt));
    }

    if (bbq_malloc_free_equal()) {
        BBQ_INFO_LOG("all memory free");
    } else {
        BBQ_ERR_LOG("memory not all free");
    }
#endif
}