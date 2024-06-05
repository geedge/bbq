/*
 * @Author: liuyu
 * @LastEditTime: 2024-06-05 17:36:03
 * @Email: liuyu@geedgenetworks.com
 * @Describe: bbq（Block-based Bounded Queue）实现
 * 参考：https://www.usenix.org/system/files/atc22-wang-jiawei.pdf
 */
#include "bbq.h"
#include <math.h>
#include <numa.h>
#include <stdio.h>
#include <string.h>

extern bbq_queue_state_s allocate_entry(bbq_queue_s *q, bbq_block_s *block);
extern void commit_entry(bbq_queue_s *q, bbq_entry_desc_s *e, void *data);
extern bbq_queue_state_s advance_phead(bbq_queue_s *q, uint64_t ph);
extern bbq_queue_state_s reserve_entry(bbq_queue_s *q, bbq_block_s *block);
extern bool consume_entry(bbq_queue_s *q, bbq_entry_desc_s *e, void *deq_data);
extern bool advance_chead(bbq_queue_s *q, uint64_t ch, uint64_t ver);
extern inline uint64_t bbq_idx(bbq_queue_s *q, uint64_t x);
extern inline uint64_t bbq_off(bbq_queue_s *q, uint64_t x);
extern inline uint64_t bbq_head_vsn(bbq_queue_s *q, uint64_t x);
extern inline uint64_t bbq_cur_vsn(bbq_queue_s *q, uint64_t x);
extern inline uint64_t set_cur_vsn(bbq_queue_s *q, uint64_t ver);
extern void *bbq_memset(void *data, size_t size);
extern void *bbq_malloc(bbq_module_e module, int socket_id, size_t size);
extern void bbq_free(bbq_module_e module, int socket_id, void *ptr, size_t size);

/* 原子的比较两个值大小，并设置较大的值，成功则返回设置前的旧值 */
uint64_t fetch_max(aotmic_uint64 *atom, uint64_t upd) {
    uint64_t old_value;
    do {
        old_value = atomic_load(atom); // 读取当前值
    } while (old_value < upd && !atomic_compare_exchange_weak(atom, &old_value, upd));

    return old_value;
}

/* 检查参数是否为2的N次幂 */
bool bbq_check_power_of_two(uint32_t n) {
    if (n <= 0) {
        return false;
    }

    return (n & (n - 1)) == 0;
}

/* 根据entries大小返回合理的block个数
 * 计算公式：log2(blocks) = max(1, ⌊log2(count)/4⌋)   注：⌊ ⌋代表向下取整。*/
uint32_t bbq_blocks_calc(uint32_t entries) {
    double log_entries = log2((double)entries);
    uint32_t over4 = (uint32_t)(log_entries / 4); // 向下取整
    uint32_t max_value = (over4 > 1) ? over4 : 1;
    uint32_t n = pow(2, max_value);
    return n;
}

/* 块初始化 */
int block_init(bbq_queue_s *q, bbq_block_s *block, bool cursor_init) {
    block->entries = bbq_malloc(BBQ_MODULE_QUEUE_BLOCK_ENTRY, q->socket_id,
                                sizeof(*block->entries) * q->bs * q->entry_size);
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
        block->committed = ATOMIC_VAR_INIT(q->bs);
        block->allocated = ATOMIC_VAR_INIT(q->bs);
        block->reserved = ATOMIC_VAR_INIT(q->bs);
        if (BBQ_POLIC_RETRY_NEW(q->flags)) {
            block->consumed = ATOMIC_VAR_INIT(q->bs);
        } else {
            block->consumed = ATOMIC_VAR_INIT(0);
        }
    }

    return BBQ_OK;
}

/* 块清理函数，与block_init成对*/
void block_cleanup(bbq_queue_s *q, bbq_block_s *block) {
    if (block->entries) {
        bbq_free(BBQ_MODULE_QUEUE_BLOCK_ENTRY, q->socket_id,
                 block->entries, sizeof(*block->entries) * q->bs * q->entry_size);
        block->entries = NULL;
    }
}

/*
求x在二进制表示中最高位1所在的位置，x参数不能为0。
例如：x=1，return 0 (...1)
x=3，return 1  （..11）
x=9，return 3  （1..1）
*/
unsigned floor_log2(uint64_t x) {
    return x == 1 ? 0 : 1 + floor_log2(x >> 1);
}

/*
返回以2为底x的对数，并向上取整值。
例如：x=1，return 0 （2^0=1）
x=99， return 7（2^6=64  2^7=128）
*/
unsigned ceil_log2(uint64_t x) {
    return x == 1 ? 0 : floor_log2(x - 1) + 1;
}

/* 创建消息队列，bn和bs必须是2的N次幂，socket_id用于多numa分配内存，free_func先设置NULL */
bbq_queue_s *bbq_ring_create_bnbs_with_socket(uint32_t bn, uint32_t bs, size_t obj_size, int socket_id, unsigned int flags) {
    int ret = 0;
    bool numa_enable = true;

    if (bbq_check_power_of_two(bn) == false) {
        BBQ_ERR_LOG("block number is not power of two, now is  :%lu", bn);
        return NULL;
    }

    if (bbq_check_power_of_two(bs) == false) {
        BBQ_ERR_LOG("block size is not power of two, now is  :%lu", bs);
        return NULL;
    }

    if (obj_size == 0) {
        BBQ_ERR_LOG("obj_size is 0");
        return NULL;
    }

    if (numa_available() < 0) {
        // 不支持numa，设置
        socket_id = BBQ_INVALID_SOCKET;
    }

    bbq_queue_s *q = bbq_malloc(BBQ_MODULE_QUEUE, socket_id, sizeof(*q));
    if (q == NULL) {
        BBQ_ERR_LOG("malloc for bbq queue error");
        return NULL;
    }
    bbq_memset(q, sizeof(*q));

    q->bn = bn;
    q->bs = bs;
    q->obj_size = obj_size;
    if (BBQ_COPY_POINTER(flags)) {
        q->entry_size = sizeof(uintptr_t);
    } else {
        q->entry_size = obj_size;
    }
    q->socket_id = socket_id;
    q->phead = 0;
    q->chead = 0;
    q->flags = flags;

    q->blocks = bbq_malloc(BBQ_MODULE_QUEUE_BLOCK_NB, socket_id, bn * sizeof(*q->blocks));
    if (q->blocks == NULL) {
        BBQ_ERR_LOG("bbq malloc for blocks error");
        goto error;
    }
    bbq_memset(q->blocks, sizeof(*q->blocks));

    for (uint32_t i = 0; i < bn; ++i) {
        // 第一个block不需要设置cursor_init_flag
        bool cursor_init_flag = (i == 0 ? false : true);
        ret = block_init(q, &(q->blocks[i]), cursor_init_flag);
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
    bbq_ring_free(q);
    return NULL;
}

/* 创建消息队列，bn和bs必须是2的N次幂，free_func先设置NULL */
bbq_queue_s *bbq_ring_create_bnbs(uint32_t bn, uint32_t bs, size_t obj_size, unsigned int flags) {
    return bbq_ring_create_bnbs_with_socket(bn, bs, obj_size, BBQ_INVALID_SOCKET, flags);
}

/* 创建消息队列，count必须大于1，且是2的N次幂，bn和bs将根据count值自动计算，socket_id用于多numa分配内存，free_func先设置NULL */
bbq_queue_s *bbq_ring_create_with_socket(uint32_t count, size_t obj_size, int socket_id, unsigned int flags) {
    if (bbq_check_power_of_two(count) == false || count == 1) {
        BBQ_ERR_LOG("bbq entries number must be power of two and greater than 1, now is  :%lu", count);
        return NULL;
    }

    uint32_t bn = bbq_blocks_calc(count);
    uint32_t bs = count / bn;
    return bbq_ring_create_bnbs_with_socket(bn, bs, obj_size, socket_id, flags);
}

/* 创建消息队列，count必须大于1，且是2的N次幂，bn和bs将根据count值自动计算，free_func先设置NULL */
bbq_queue_s *bbq_ring_create(uint32_t count, size_t obj_size, unsigned int flags) {
    // 传入无效socket_id，将使用malloc分配内存
    return bbq_ring_create_with_socket(count, obj_size, BBQ_INVALID_SOCKET, flags);
}

/* 释放消息队列，与bbq_ring_create系列接口成对*/
void bbq_ring_free(bbq_queue_s *q) {
    if (q == NULL) {
        return;
    }

    for (uint32_t i = 0; i < q->bn; ++i) {
        block_cleanup(q, &(q->blocks[i]));
    }

    bbq_free(BBQ_MODULE_QUEUE_BLOCK_NB, q->socket_id, q->blocks, q->bn * sizeof(*q->blocks));
    bbq_free(BBQ_MODULE_QUEUE, q->socket_id, q, sizeof(*q));
}

/* 消息队列入队 */
int bbq_enqueue(bbq_queue_s *q, void *data) {
    if (q == NULL || data == NULL) {
        return BBQ_NULL_PTR;
    }

    while (true) {
        // 获取当前phead，转为索引后获取到当前的blk
        uint64_t ph = atomic_load(&q->phead);
        bbq_block_s *blk = &(q->blocks[bbq_idx(q, ph)]);

        bbq_queue_state_s ps;
        bbq_queue_state_s state = allocate_entry(q, blk);

        switch (state.state) {
        case BBQ_ALLOCATED:
            commit_entry(q, &state.e, data);
            return BBQ_OK;
        case BBQ_BLOCK_DONE:
            ps = advance_phead(q, ph);
            switch (ps.state) {
            case BBQ_NO_ENTRY:
                return BBQ_QUEUE_FULL;
            case BBQ_NOT_AVAILABLE:
                return BBQ_QUEUE_BUSY;
            case BBQ_SUCCESS:
                continue;
            }
            break;
        default:
            BBQ_ERR_LOG("Invalid QueueState in bbq_enqueue: %d", state.state);
            return BBQ_ERROR;
        }
    }
}

/* 消息队列出队 */
int bbq_dequeue(bbq_queue_s *q, void *deq_data) {
    if (q == NULL || deq_data == NULL) {
        return BBQ_NULL_PTR;
    }

    while (true) {
        uint64_t ch = atomic_load(&q->chead);
        bbq_block_s *blk = &(q->blocks[bbq_idx(q, ch)]);

        bbq_queue_state_s state;
        state = reserve_entry(q, blk);

        switch (state.state) {
        case BBQ_RESERVED:
            if (consume_entry(q, &state.e, deq_data)) {
                return BBQ_OK;
            } else {
                continue;
            }
        case BBQ_NO_ENTRY:
            return BBQ_QUEUE_EMPTY;
        case BBQ_NOT_AVAILABLE:
            return BBQ_QUEUE_BUSY;
        case BBQ_BLOCK_DONE:
            if (advance_chead(q, ch, state.vsn)) {
                continue;
            } else {
                return BBQ_QUEUE_EMPTY;
            }
        default:
            BBQ_ERR_LOG("Invalid QueueState in dequeue state: %d", state.state);
            return BBQ_ERROR;
        }
    }
}

/* 将多个对象从一个环形队列ring取出，直到达到最大数量，或是出队失败 */
uint32_t bbq_dequeue_burst(bbq_queue_s *q, void *obj_table, uint32_t n) {
    uint32_t cnt = 0;
    int ret = 0;
    void *obj = NULL;

    for (cnt = 0; cnt < n; cnt++) {
        if (BBQ_COPY_VALUE(q->flags)) {
            obj = (obj == NULL ? obj_table : obj + q->obj_size);
        } else {
            obj = &((void **)obj_table)[cnt];
            // printf("obj :%p %p\n", obj, &obj);
        }

        ret = bbq_dequeue(q, obj);
        if (ret != BBQ_OK) {
            return cnt;
        }
    }

    return cnt;
}

/* 尝试一次入队多个数据，直到达到最大数量，或是入队失败 */
uint32_t bbq_enqueue_burst(bbq_queue_s *q, void *obj_table, size_t n, unsigned int flags) {
    uint32_t cnt = 0;
    void *obj = NULL;

    for (cnt = 0; cnt < n; cnt++) {
        if (BBQ_ARRAY_1D(flags)) {
            obj = (obj == NULL ? obj_table : obj + q->obj_size);
        } else {
            obj = ((void **)obj_table)[cnt];
        }

        if (BBQ_OK != bbq_enqueue(q, obj)) {
            return cnt;
        }
    }

    return cnt;
}

bbq_queue_state_s allocate_entry(bbq_queue_s *q, bbq_block_s *block) {
    bbq_queue_state_s state = {0};
    if (bbq_off(q, atomic_load(&block->allocated)) >= q->bs) {
        state.state = BBQ_BLOCK_DONE;
        return state;
    }

    uint64_t old = atomic_fetch_add(&block->allocated, 1);
    uint64_t committed_vsn = bbq_cur_vsn(q, atomic_load(&block->committed));

    // committed_vsn，在当前块被初始化后值是不变的，判断vsn是考虑到极限情况下给off预留的空间不足导致，allocated的off溢出（vsn+1）
    uint64_t cur_vsn = bbq_cur_vsn(q, old);
    uint64_t cur_off = bbq_off(q, old);
    if ((cur_vsn != committed_vsn) || (cur_off >= q->bs)) {
        state.state = BBQ_BLOCK_DONE;
        return state;
    }

    state.state = BBQ_ALLOCATED;
    state.e.block = block;
    state.e.vsn = cur_vsn;
    state.e.bbq_off = cur_off;

    return state;
}

void commit_entry(bbq_queue_s *q, bbq_entry_desc_s *e, void *data) {
    size_t idx = e->bbq_off * q->entry_size;
    if (BBQ_COPY_POINTER(q->flags)) {
        uintptr_t *uptr = (uintptr_t *)(&(e->block->entries[idx]));
        *uptr = (uintptr_t)(data);
    } else {
        memcpy(&(e->block->entries[idx]), data, q->entry_size);
    }
    atomic_fetch_add(&e->block->committed, 1);
}

bbq_queue_state_s advance_phead(bbq_queue_s *q, uint64_t ph) {
    // 获取下一个block
    bbq_block_s *n_blk;
    n_blk = &(q->blocks[(bbq_idx(q, ph) + 1) & q->idx_mask]);

    uint64_t cur, reserved;
    bbq_queue_state_s state = {0};
    if (BBQ_POLIC_RETRY_NEW(q->flags)) {
        cur = atomic_load(&n_blk->consumed);
        if (bbq_cur_vsn(q, cur) < bbq_head_vsn(q, ph) || // 生产者赶上了消费者
            (bbq_cur_vsn(q, cur) == bbq_head_vsn(q, ph) && bbq_off(q, cur) != q->bs)) {
            reserved = atomic_load(&n_blk->reserved);
            if (bbq_off(q, reserved) == bbq_off(q, cur)) {
                state.state = BBQ_NO_ENTRY;
            } else {
                state.state = BBQ_NOT_AVAILABLE;
            }
            return state;
        }
    } else {
        cur = atomic_load(&n_blk->committed);
        // 生产者避免前进到上一轮中尚未完全提交的区块
        if (bbq_cur_vsn(q, cur) == bbq_head_vsn(q, ph) && bbq_off(q, cur) != q->bs) {
            state.state = BBQ_NOT_AVAILABLE;
            return state;
        }
    }

    // 用head的version初始化下一个块，version在高位，version+1，idex/offset清零，如果没有被其他线程执行过，数值会高于旧值。多线程同时只更新一次。
    fetch_max(&n_blk->committed, set_cur_vsn(q, bbq_head_vsn(q, ph) + 1));
    fetch_max(&n_blk->allocated, set_cur_vsn(q, bbq_head_vsn(q, ph) + 1));

    // 索引+1，当超过索引范围，也就是循环下一轮块时，version+1
    fetch_max(&q->phead, ph + 1);
    state.state = BBQ_SUCCESS;
    return state;
}

bbq_queue_state_s reserve_entry(bbq_queue_s *q, bbq_block_s *block) {
    while (true) {
        bbq_queue_state_s state;
        uint64_t reserved = atomic_load(&block->reserved);
        if (bbq_off(q, reserved) < q->bs) {
            uint64_t consumed = atomic_load(&block->consumed);
            if (BBQ_POLIC_RETRY_NEW(q->flags) && bbq_cur_vsn(q, reserved) != bbq_cur_vsn(q, consumed)) {
                // consumed溢出了，这种情况只发生在BBQ_RETRY_NEW，因为BBQ_DROP_OLD模式，consumed没有用到
                state.state = BBQ_BLOCK_DONE;
                state.vsn = bbq_cur_vsn(q, reserved);
                return state;
            }

            uint64_t committed = atomic_load(&block->committed);
            if (bbq_off(q, committed) == bbq_off(q, reserved)) { // TODO:多entry关注
                state.state = BBQ_NO_ENTRY;
                return state;
            }

            // 当前块的数据没有被全部commited，需要通过判断allocated和committed来判断是否存在正在入队进行中的数据
            if (bbq_off(q, committed) != q->bs) {
                uint64_t allocated = atomic_load(&block->allocated);
                if (bbq_off(q, allocated) != bbq_off(q, committed)) {
                    state.state = BBQ_NOT_AVAILABLE;
                    return state;
                }
            }

            if (fetch_max(&block->reserved, reserved + 1) == reserved) { // TODO:多entry时关注
                // fetch_max返回的是旧值，因此预期返回的旧值要等于局部变量reserved
                state.state = BBQ_RESERVED;
                state.e.block = block;
                state.e.bbq_off = bbq_off(q, reserved);
                state.e.vsn = bbq_cur_vsn(q, reserved);

                return state;
            } else {
                // 如果不等于代表block.reserved被其他线程Reserved了
                continue;
            }
        }

        state.state = BBQ_BLOCK_DONE;
        state.vsn = bbq_cur_vsn(q, reserved);
        return state;
    }
}

bool consume_entry(bbq_queue_s *q, bbq_entry_desc_s *e, void *deq_data) {
    size_t idx = e->bbq_off * q->entry_size;
    if (BBQ_COPY_POINTER(q->flags)) {
        uintptr_t *uptr = (uintptr_t *)(&(e->block->entries[idx]));
        *((void **)deq_data) = (void *)(*uptr);
    } else {
        memcpy(deq_data, &(e->block->entries[idx]), q->entry_size);
    }

    uint64_t allocated;
    if (BBQ_POLIC_RETRY_NEW(q->flags)) {
        atomic_fetch_add(&e->block->consumed, 1);
    } else {
        allocated = atomic_load(&e->block->allocated);
        // 预留的entry所在的块，已经被新生产的数据赶上了
        if (bbq_cur_vsn(q, allocated) != e->vsn) {
            return false;
        }
    }

    return true;
}

bool advance_chead(bbq_queue_s *q, uint64_t ch, uint64_t ver) {
    bbq_block_s *n_blk = &(q->blocks[(bbq_idx(q, ch) + 1) & q->idx_mask]);

    uint64_t committed = atomic_load(&n_blk->committed);
    if (BBQ_POLIC_RETRY_NEW(q->flags)) {
        if (bbq_cur_vsn(q, committed) != bbq_head_vsn(q, ch) + 1) {
            // 消费者追上了生产者，下一块还未开始生产
            return false;
        }
        fetch_max(&n_blk->consumed, set_cur_vsn(q, bbq_head_vsn(q, ch) + 1));
        fetch_max(&n_blk->reserved, set_cur_vsn(q, bbq_head_vsn(q, ch) + 1));
    } else {
        // 通过检查下一个块的版本是否大于或等于当前块来保证 FIFO 顺序.
        // 第一个块是一个特殊情况，因为与其他块相比，它的版本总是相差一个。因此，如果 ch.bbq_idx == 0，我们在比较中加 1
        if (bbq_cur_vsn(q, committed) < ver + (bbq_idx(q, ch) == 0))
            return false;
        fetch_max(&n_blk->reserved, set_cur_vsn(q, bbq_cur_vsn(q, committed)));
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

void *bbq_memset(void *data, size_t size) {
    if (data != NULL && size > 0) {
        memset(data, 0, size);
    }
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