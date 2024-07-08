/*
 * @Author: liuyu
 * @LastEditTime: 2024-07-07 17:44:50
 * @Email: liuyu@geedgenetworks.com
 * @Describe: bbq（Block-based Bounded Queue）实现
 * 参考：https://www.usenix.org/system/files/atc22-wang-jiawei.pdf
 */
#include "bbq.h"
#include <math.h>
#include <string.h>

// flags第1位控制入队时的数据拷贝策略，默认是"拷贝指针"
#define BBQ_F_COPY_PTR BBQ_F_DEFAULT /**< 默认为拷贝指针 */
#define BBQ_F_COPY_VALUE 0x0001      /**< 创建队列时设置为拷贝数值 */

// 判断flags标记位
#define BBQ_F_CHK_DROP_OLD(flags) (flags & BBQ_F_DROP_OLD)
#define BBQ_F_CHK_COPY_VALUE(flags) (flags & BBQ_F_COPY_VALUE)
#define BBQ_F_CHK_SP_ENQ(flags) (flags & BBQ_F_SP_ENQ)
#define BBQ_F_CHK_SC_DEQ(flags) (flags & BBQ_F_SC_DEQ)
#define BBQ_F_CHK_STAT_ENABLE(flags) (flags & BBQ_F_ENABLE_STAT)

// 避免无用参数的编译告警
#define AVOID_WARNING(param) ((void)param)

// 在Debug编译时，BBQ_MEMORY宏定义开关被打开，将在每个块的末尾多分配一个entry，值为BBQ_MEM_MAGIC，方便排查越界问题。
#ifdef BBQ_MEMORY
#define BBQ_MEM_MAGIC 0xFE
#endif

#define bbq_likely(x) __builtin_expect(!!(x), 1)
#define bbq_unlikely(x) __builtin_expect(!!(x), 0)

struct bbq_status {
    int32_t status;        // 返回状态
    uint32_t actual_burst; // 实际出/入队个数
};

enum bbq_queue_state {
    BBQ_SUCCESS = 0,
    BBQ_BLOCK_DONE,    // 当前块的entry已用完，需要移动到下一个块
    BBQ_NO_ENTRY,      // 队列里没有entry可以使用了
    BBQ_NOT_AVAILABLE, // 当前块不可以用状态（将返回busy）
    BBQ_ALLOCATED,     // 已分配，返回entry信息
    BBQ_RESERVED,      // 已保留，返回entry信息
};

struct bbq_entry_desc {
    uint64_t vsn;            // allocated或reserved的版本(vsn)
    uint64_t off;            // entry在当前块的偏移(offset)
    uint32_t actual_burst;   // 实际出/入队个数
    struct bbq_block *block; // 指向所在的块
};

struct bbq_queue_state_s {
    enum bbq_queue_state state; // 队列状态
    union {
        uint64_t vsn;            // bbq_reserve_entry state==BLOCK_DONE时生效
        struct bbq_entry_desc e; // state为ALLOCATED、RESERVED生效
    };
};

extern inline uint64_t bbq_head_idx(struct bbq *q, uint64_t x) {
    return x & q->idx_mask;
}

extern inline uint64_t bbq_head_vsn(struct bbq *q, uint64_t x) {
    return x >> q->idx_bits;
}

extern inline uint64_t bbq_cur_off(struct bbq *q, uint64_t x) {
    return x & q->off_mask;
}

extern inline uint64_t bbq_cur_vsn(struct bbq *q, uint64_t x) {
    return x >> q->off_bits;
}

static inline uint64_t bbq_set_cur_vsn(struct bbq *q, uint64_t ver) {
    return ver << q->off_bits;
}

extern inline uint64_t bbq_atomic64_load(union bbq_atomic64 *atomic, bool single) {
    if (single) {
        return atomic->s;
    } else {
        return atomic_load(&atomic->m);
    }
}

extern inline void bbq_atomic64_store(union bbq_atomic64 *atomic, uint64_t value, bool single) {
    if (single) {
        atomic->s = value;
    } else {
        atomic_store(&atomic->m, value);
    }
}
static inline uint64_t bbq_atomic64_fetch_add(union bbq_atomic64 *atomic, uint64_t value, bool single) {
    if (single) {
        uint64_t old = atomic->s;
        atomic->s += value;
        return old;
    } else {
        return atomic_fetch_add(&atomic->m, value);
    }
}

/* 原子的比较两个值大小，并设置较大的值，成功则返回设置前的旧值 */
uint64_t bbq_fetch_max(union bbq_atomic64 *atomic, uint64_t upd, bool single) {
    uint64_t old_value = 0;

    if (single) {
        old_value = atomic->s;
        atomic->s = upd;
    } else {
        do {
            old_value = atomic_load(&atomic->m);
        } while (old_value < upd && !atomic_compare_exchange_weak(&atomic->m, &old_value, upd));
    }

    return old_value;
}

/* 检查参数是否为2的N次幂 */
bool bbq_check_power_of_two(uint32_t n) {
    if (n == 0) {
        return false;
    }

    return (n & (n - 1)) == 0;
}

/* 根据entries大小返回合理的block个数
 * 计算公式：log2(blocks) = max(1, ⌊log2(count)/4⌋)   注：⌊ ⌋代表向下取整。*/
static uint32_t bbq_block_number_calc(uint32_t entries) {
    double log_entries = log2((double)entries);
    uint32_t over4 = (uint32_t)(log_entries / 4); // 向下取整
    uint32_t max_value = (over4 > 1) ? over4 : 1;
    uint32_t n = pow(2, max_value);

    return n;
}

/* 根据entries大小返回合理的bn、bs*/
int bbq_bnbs_calc(uint32_t entries, uint32_t *bn, uint32_t *bs) {
    if (bn == NULL || bs == NULL) {
        return BBQ_ERR_INPUT_NULL;
    }

    if (entries <= 1) {
        return BBQ_ERR_OUT_OF_RANGE;
    }

    if (bbq_check_power_of_two(entries) == false) {
        return BBQ_ERR_POWER_OF_TWO;
    }

    *bn = bbq_block_number_calc(entries);
    *bs = entries / *bn;

    return BBQ_OK;
}

void *bbq_malloc_from_pool(struct bbq *q, size_t size) {
    if (q->memory_pool.size - q->memory_pool.off < size) {
        return NULL;
    }

    char *mem = q->memory_pool.ptr + q->memory_pool.off;
    q->memory_pool.off += size;

    return mem;
}

/* 块初始化 */
int block_init(struct bbq *q, struct bbq_block *block, bool cursor_init) {
    size_t size = 0;

#ifdef BBQ_MEMORY
    // 末尾多分配一个entry，它永远不应该被修改，以此检查是否存在写越界的问题
    size = (q->bs + 1) * q->entry_size;
    block->entries = bbq_malloc_from_pool(q, size);
    char *last_entry = block->entries + q->entry_size * q->bs;
    memset(block->entries, 0, size);
    memset(last_entry, BBQ_MEM_MAGIC, q->entry_size);
#else
    size = q->bs * q->entry_size;
    block->entries = bbq_malloc_from_pool(q, size);
    memset(block->entries, 0, size);
#endif

    if (block->entries == NULL) {
        return BBQ_ERR_ALLOC;
    }

    if (cursor_init) {
        // block数组里，除了第一块之外需要设置
        bbq_atomic64_store(&block->committed, q->bs, q->prod_single);
        bbq_atomic64_store(&block->allocated, q->bs, q->prod_single);
        bbq_atomic64_store(&block->reserved, q->bs, q->cons_single);
        if (!BBQ_F_CHK_DROP_OLD(q->flags)) {
            bbq_atomic64_store(&block->consumed, q->bs, q->cons_single);
        }
    }

    return BBQ_OK;
}

/* 块清理函数，与block_init成对*/
void block_destory(struct bbq *q, struct bbq_block *block) {
    if (block->entries) {
#ifdef BBQ_MEMORY
        q->memory_pool.free_f(block->entries, (q->bs + 1) * q->entry_size);
#else
        q->memory_pool.free_f(block->entries, q->bs * q->entry_size);
#endif
        block->entries = NULL;
    }
}

/*
求x在二进制表示中最高位1所在的位置，x参数不能为0。
例如：x=1，return 0 (...1); x=3，return 1  （..11）; x=9，return 3  （1..1）
*/
static unsigned bbq_floor_log2(uint64_t x) {
    return x == 1 ? 0 : 1 + bbq_floor_log2(x >> 1);
}

/*
返回以2为底x的对数，并向上取整值。
例如：x=1，return 0 （2^0=1）;  x=99， return 7（2^6=64  2^7=128）
*/
static unsigned bbq_ceil_log2(uint64_t x) {
    return x == 1 ? 0 : bbq_floor_log2(x - 1) + 1;
}

/* 创建消息队列，bn和bs必须是2的N次幂，socket_id用于多numa分配内存 */
static struct bbq *__bbq_create_bnbs(const char *name, uint32_t bn, uint32_t bs,
                                     size_t obj_size, int socket_id, uint32_t flags,
                                     bbq_malloc_f malloc_f, bbq_free_f free_f) {
    int ret = 0;
    size_t size = 0;
    if (bbq_check_power_of_two(bn) == false) {
        return NULL;
    }

    if (bbq_check_power_of_two(bs) == false) {
        return NULL;
    }

    if (name == NULL) {
        return NULL;
    }

    if (strlen(name) >= BBQ_SYMBOL_MAX - 1 || obj_size == 0) {
        return NULL;
    }

    if (BBQ_F_CHK_DROP_OLD(flags) && BBQ_F_CHK_STAT_ENABLE(flags)) {
        return NULL;
    }

    if (malloc_f == NULL || free_f == NULL) {
        return NULL;
    }

    uint32_t all_size = 0;
#ifdef BBQ_MEMORY
    all_size = sizeof(struct bbq) + bn * sizeof(struct bbq_block) + bn * (bs + 1) * obj_size;
#else
    all_size = sizeof(struct bbq) + bn * sizeof(struct bbq_block) + bn * bs * obj_size;
#endif
    struct bbq *q = malloc_f(socket_id, all_size);
    if (q == NULL) {
        return NULL;
    }

    memset(q, 0, all_size);
    q->memory_pool.size = all_size;
    q->memory_pool.ptr = (char *)q;
    q->memory_pool.off += sizeof(struct bbq);
    q->memory_pool.malloc_f = malloc_f;
    q->memory_pool.free_f = free_f;
    q->bn = bn;
    q->bs = bs;
    q->entry_size = obj_size;
    q->socket_id = socket_id;
    strncpy(q->name, name, sizeof(q->name) - 1);
    q->name[sizeof(q->name) - 1] = '\0';

    if (BBQ_F_CHK_SP_ENQ(flags)) {
        q->prod_single = true;
    }
    if (BBQ_F_CHK_SC_DEQ(flags)) {
        q->cons_single = true;
    }
    q->flags = flags;

    size = bn * sizeof(*q->blocks);
    q->blocks = bbq_malloc_from_pool(q, size);
    if (q->blocks == NULL) {
        goto error;
    }
    memset(q->blocks, 0, size);

    for (uint32_t i = 0; i < bn; ++i) {
        ret = block_init(q, &(q->blocks[i]), (i == 0 ? false : true));
        if (ret != BBQ_OK) {
            goto error;
        }
    }

    q->idx_bits = bbq_ceil_log2(bn);
    uint32_t off_bits = bbq_ceil_log2(bs);
    if (off_bits < 13) {
        off_bits = 13;
    }
    q->off_bits = off_bits; // 多线程同时FAA，可能会超过最大索引，因此多分配一些空间，防止FAA溢出 TODO:多少合适？ver溢出问题？

    q->idx_mask = (1 << q->idx_bits) - 1;
    q->off_mask = (1 << q->off_bits) - 1;

    return q;

error:
    bbq_destory(q);
    return NULL;
}

/* 使用自定义的bn、bs创建指针入队的bbq，一般用于单元测试 */
struct bbq *bbq_create_with_bnbs(const char *name, uint32_t bn, uint32_t bs,
                                 int socket_id, uint32_t flags,
                                 bbq_malloc_f malloc_f, bbq_free_f free_f) {
    return __bbq_create_bnbs(name, bn, bs, sizeof(void *), socket_id, flags | BBQ_F_COPY_PTR, malloc_f, free_f);
}

/* 使用自定义的bn、bs创建值入队的bbq，一般用于单元测试 */
struct bbq *bbq_create_elem_with_bnbs(const char *name, uint32_t bn, uint32_t bs,
                                      size_t obj_size, int socket_id, uint32_t flags,
                                      bbq_malloc_f malloc_f, bbq_free_f free_f) {
    return __bbq_create_bnbs(name, bn, bs, obj_size, socket_id, flags | BBQ_F_COPY_VALUE, malloc_f, free_f);
}

/* 创建消息队列，count必须大于1，且是2的N次幂，bn和bs将根据count值自动计算，socket_id用于多numa分配内存，free_func先设置NULL */
struct bbq *bbq_create_elem(const char *name, uint32_t count, size_t obj_size,
                            int socket_id, uint32_t flags,
                            bbq_malloc_f malloc_f, bbq_free_f free_f) {
    uint32_t bn = 0;
    uint32_t bs = 0;

    if (bbq_bnbs_calc(count, &bn, &bs) != BBQ_OK) {
        return NULL;
    }

    return __bbq_create_bnbs(name, bn, bs, obj_size, socket_id, flags | BBQ_F_COPY_VALUE, malloc_f, free_f);
}

struct bbq *bbq_create(const char *name, uint32_t count, int socket_id, uint32_t flags,
                       bbq_malloc_f malloc_f, bbq_free_f free_f) {
    uint32_t bn = 0;
    uint32_t bs = 0;

    if (bbq_bnbs_calc(count, &bn, &bs) != BBQ_OK) {
        return NULL;
    }

    return __bbq_create_bnbs(name, bn, bs, sizeof(void *), socket_id, flags | BBQ_F_COPY_PTR, malloc_f, free_f);
}

/* 释放消息队列，与bbq_ring_create系列接口成对*/
void bbq_destory(struct bbq *q) {
    if (q == NULL) {
        return;
    }

    q->memory_pool.free_f(q->memory_pool.ptr, q->memory_pool.size);
}

#define BBQ_DATA_TYPE_SINGLE 0x0
#define BBQ_DATA_TYPE_1D_ARRAY 0x1
#define BBQ_DATA_TYPE_2D_ARRAY 0x2
void bbq_commit_entry(struct bbq *q, struct bbq_entry_desc *e, void const *data, uint32_t data_type) {
    size_t idx = e->off * q->entry_size;

    if (BBQ_F_CHK_COPY_VALUE(q->flags)) {
        // 数据入队列
        switch (data_type) {
        case BBQ_DATA_TYPE_1D_ARRAY:
        case BBQ_DATA_TYPE_SINGLE:
            memcpy(&(e->block->entries[idx]), data, q->entry_size * e->actual_burst);
            break;
        case BBQ_DATA_TYPE_2D_ARRAY: {
            void **tmp = (void **)data;
            char *entry = &(e->block->entries[idx]);
            for (size_t i = 0; i < e->actual_burst; i++) {
                memcpy(entry, *tmp, q->entry_size);
                entry += q->entry_size;
                tmp++;
            }
            break;
        }
        default:
            break;
        }
    } else {
        // 指针入队列
        switch (data_type) {
        case BBQ_DATA_TYPE_2D_ARRAY:
        case BBQ_DATA_TYPE_SINGLE:
            // 二维数组名等于首成员的地址，这里data其实是void **data,  &data等同于 &(data[0])
            memcpy(&(e->block->entries[idx]), data, q->entry_size * e->actual_burst);
            break;
        case BBQ_DATA_TYPE_1D_ARRAY:
        default:
            break;
        }
    }
    bbq_atomic64_fetch_add(&e->block->committed, e->actual_burst, q->prod_single);
}

struct bbq_queue_state_s bbq_allocate_entry(struct bbq *q, uint64_t ph, uint32_t n) {
    struct bbq_queue_state_s state = {0};
    uint64_t ph_idx = bbq_head_idx(q, ph);
    bool prod_single = q->prod_single;

    struct bbq_block *block = &(q->blocks[ph_idx]);
    if (bbq_cur_off(q, bbq_atomic64_load(&block->allocated, prod_single)) >= q->bs) {
        state.state = BBQ_BLOCK_DONE;
        return state;
    }

    uint64_t old = bbq_atomic64_fetch_add(&block->allocated, n, prod_single);
    uint64_t cur_vsn = bbq_cur_vsn(q, old);
    uint64_t cur_off = bbq_cur_off(q, old);

    if (cur_off >= q->bs) {
        state.state = BBQ_BLOCK_DONE;
        return state;
    }

    if (cur_off + n <= q->bs) {
        // 可以全部入队
        state.e.actual_burst = n;
    } else {
        // 部分入队
        state.e.actual_burst = q->bs - cur_off;
    }
    state.e.block = block;
    state.e.vsn = cur_vsn;
    state.e.off = cur_off;
    state.state = BBQ_ALLOCATED;

    return state;
}

enum bbq_queue_state advance_phead(struct bbq *q, uint64_t ph) {
    uint64_t cur = 0;
    // 获取下一个block
    struct bbq_block *n_blk = &(q->blocks[(bbq_head_idx(q, ph) + 1) & q->idx_mask]);
    uint64_t ph_vsn = bbq_head_vsn(q, ph);
    bool prod_single = q->prod_single;
    bool cons_single = q->cons_single;

    if (BBQ_F_CHK_DROP_OLD(q->flags)) {
        cur = bbq_atomic64_load(&n_blk->committed, prod_single);
        // 生产者head避免覆盖上一轮尚未完全提交的区块
        if (bbq_cur_vsn(q, cur) == ph_vsn && bbq_cur_off(q, cur) != q->bs) {
            return BBQ_NOT_AVAILABLE;
        }
    } else {
        cur = bbq_atomic64_load(&n_blk->consumed, cons_single);
        uint64_t reserved;
        uint64_t consumed_off = bbq_cur_off(q, cur);
        uint64_t consumed_vsn = bbq_cur_vsn(q, cur);

        if (consumed_vsn < ph_vsn ||
            (consumed_vsn == ph_vsn && consumed_off != q->bs)) {
            // 生产者赶上了消费者
            reserved = bbq_atomic64_load(&n_blk->reserved, cons_single);
            if (bbq_cur_off(q, reserved) == consumed_off) {
                return BBQ_NO_ENTRY;
            } else {
                return BBQ_NOT_AVAILABLE;
            }
        }
    }

    // 用head的version值初始化下一个块，version在高位，version+1，index或offset也会被清零
    uint64_t new_vsn = bbq_set_cur_vsn(q, ph_vsn + 1);
    // 其他线程完成了head更新，当前bbq_fetch_max不会再更新，可能在以下两种情况：
    // 1）实际ph_vsn与本次要更新的ph_vsn相同。
    // 2）当前ph_vsn已经落后于实际的ph_vsn（且移动到了下一轮），
    bbq_fetch_max(&n_blk->committed, new_vsn, prod_single);
    bbq_fetch_max(&n_blk->allocated, new_vsn, prod_single);

    // ph+1，当超过索引范围，进入下一轮时，version会自动+1
    bbq_fetch_max(&q->phead.value, ph + 1, prod_single);
    return BBQ_SUCCESS;
}

bool bbq_empty(struct bbq *q) {
    return bbq_atomic64_load(&q->stat.n_enq, q->prod_single) == bbq_atomic64_load(&q->stat.n_deq, q->cons_single);
}

static uint32_t bbq_wait_consumed_get(struct bbq *q, uint64_t enq_update, uint64_t deq_update) {
    uint64_t enq_now = 0;
    uint64_t deq_now = 0;

    if (enq_update == 0) {
        enq_now = bbq_atomic64_load(&q->stat.n_enq, q->prod_single);
    } else {
        enq_now = enq_update;
    }

    if (deq_update == 0) {
        deq_now = bbq_atomic64_load(&q->stat.n_deq, q->cons_single);
    } else {
        deq_now = deq_update;
    }

    return enq_now - deq_now;
}

/* 消息队列入队 */
static struct bbq_status __bbq_enqueue(struct bbq *q, void const *data,
                                       uint32_t n, uint32_t data_type,
                                       uint32_t *wait_consumed) {
    uint64_t enq_update = 0;
    struct bbq_status ret = {.status = 0, .actual_burst = 0};
    bool prod_single = q->prod_single;

    if (bbq_unlikely(q == NULL || data == NULL)) {
        ret.status = BBQ_ERR_INPUT_NULL;
        return ret;
    }

    while (true) {
        uint64_t ph = bbq_atomic64_load(&q->phead.value, prod_single);
        struct bbq_queue_state_s state = bbq_allocate_entry(q, ph, n);

        switch (state.state) {
        case BBQ_ALLOCATED:
            bbq_commit_entry(q, &state.e, data, data_type);
            ret.actual_burst = state.e.actual_burst;
            ret.status = BBQ_OK;

            if (BBQ_F_CHK_STAT_ENABLE(q->flags)) {
                enq_update = bbq_atomic64_fetch_add(&q->stat.n_enq, state.e.actual_burst, prod_single) +
                             state.e.actual_burst;
            }
            break;
        case BBQ_BLOCK_DONE: {
            enum bbq_queue_state pstate = advance_phead(q, ph);
            if (pstate == BBQ_SUCCESS) {
                continue;
            }

            if (pstate == BBQ_NO_ENTRY) {
                ret.status = BBQ_ERR_FULL;
            } else if (pstate == BBQ_NOT_AVAILABLE) {
                ret.status = BBQ_ERR_BUSY;
            } else {
                ret.status = BBQ_ERR;
            }

            break;
        }
        default:
            ret.status = BBQ_ERR;
            break;
        }

        if (BBQ_F_CHK_STAT_ENABLE(q->flags) && wait_consumed != NULL) {
            *wait_consumed = bbq_wait_consumed_get(q, enq_update, 0);
        }

        return ret;
    }
}

int bbq_enqueue(struct bbq *q, void *const *data) {
    struct bbq_status ret = __bbq_enqueue(q, data, 1, BBQ_DATA_TYPE_SINGLE, NULL);
    return ret.status;
}

int bbq_enqueue_elem(struct bbq *q, void const *data) {
    struct bbq_status ret = __bbq_enqueue(q, data, 1, BBQ_DATA_TYPE_SINGLE, NULL);
    return ret.status;
}

/* 更新reserved，成功则返回更新的个数 */
uint32_t bbq_reserve_update(union bbq_atomic64 *atomic, uint64_t reserved, uint32_t n, bool single) {
    if (single) {
        atomic->s += n;
        return n;
    } else {
        // 不能用fetch_max，比如a线程burst2，b线程burst3，a更新成功，b也更新成功。
        bool ret = atomic_compare_exchange_weak(&atomic->m, &reserved, reserved + n);
        return ret == true ? n : 0;
    }
}

struct bbq_queue_state_s bbq_reserve_entry(struct bbq *q, struct bbq_block *block, uint32_t n) {
    uint32_t cont = 0;
    bool prod_single = q->prod_single;
    bool cons_single = q->cons_single;

    while (true) {
        struct bbq_queue_state_s state;
        uint64_t reserved = bbq_atomic64_load(&block->reserved, cons_single);
        uint64_t reserved_off = bbq_cur_off(q, reserved);
        uint64_t reserved_svn = bbq_cur_vsn(q, reserved);

        if (reserved_off < q->bs) {
            uint64_t committed = bbq_atomic64_load(&block->committed, prod_single);
            uint64_t committed_off = bbq_cur_off(q, committed);
            if (committed_off == reserved_off) {
                state.state = BBQ_NO_ENTRY;
                return state;
            }

            // 当前块的数据没有被全部commited，需要通过判断allocated和committed来判断是否存在正在入队进行中的数据
            if (committed_off != q->bs) {
                uint64_t allocated = bbq_atomic64_load(&block->allocated, prod_single);
                if (bbq_cur_off(q, allocated) != committed_off) {
                    state.state = BBQ_NOT_AVAILABLE;
                    return state;
                }
            }

            uint32_t tmp = committed_off - reserved_off;
            uint32_t reserved_cnt = bbq_reserve_update(&block->reserved, reserved, tmp < n ? tmp : n, q->cons_single);
            if (reserved_cnt > 0) {
                state.state = BBQ_RESERVED;
                state.e.actual_burst = reserved_cnt;
                state.e.block = block;
                state.e.off = reserved_off;
                state.e.vsn = reserved_svn;

                return state;
            } else {
                // 已经被其他线程更新过了，当前数据为旧数据，需要重新获取
                cont++;
                continue;
            }
        }

        state.state = BBQ_BLOCK_DONE;
        state.vsn = reserved_svn;
        return state;
    }
}

bool consume_entry(struct bbq *q, struct bbq_entry_desc *e, void *deq_data, uint32_t data_type) {
    size_t idx = e->off * q->entry_size;
    bool prod_single = q->prod_single;
    bool cons_single = q->cons_single;

    if (BBQ_F_CHK_COPY_VALUE(q->flags)) {
        switch (data_type) {
        case BBQ_DATA_TYPE_1D_ARRAY:
        case BBQ_DATA_TYPE_SINGLE:
            memcpy(deq_data, &(e->block->entries[idx]), q->entry_size * e->actual_burst);
            break;
        case BBQ_DATA_TYPE_2D_ARRAY: {
            void **tmp = (void **)deq_data;
            char *entry = &(e->block->entries[idx]);
            for (size_t i = 0; i < e->actual_burst; i++) {
                memcpy(*tmp, entry, q->entry_size);
                entry += q->entry_size;
                tmp++;
            }
            break;
        }
        default:
            break;
        }
    } else {
        switch (data_type) {
        case BBQ_DATA_TYPE_2D_ARRAY:
        case BBQ_DATA_TYPE_SINGLE:
            memcpy(deq_data, &(e->block->entries[idx]), q->entry_size * e->actual_burst);
            break;
        case BBQ_DATA_TYPE_1D_ARRAY:
        default:
            break;
        }
    }

    uint64_t allocated;
    if (BBQ_F_CHK_DROP_OLD(q->flags)) {
        // TODO:优化，考虑allocated vsn溢出？考虑判断如果生产满了，直接移动head？
        allocated = bbq_atomic64_load(&e->block->allocated, prod_single);
        // 预留的entry所在的块，已经被新生产的数据赶上了
        if (bbq_cur_vsn(q, allocated) != e->vsn) {
            return false;
        }
    } else {
        bbq_atomic64_fetch_add(&e->block->consumed, e->actual_burst, cons_single);
    }

    return true;
}

bool advance_chead(struct bbq *q, uint64_t ch, uint64_t ver) {
    uint64_t ch_idx = bbq_head_idx(q, ch);
    struct bbq_block *n_blk = &(q->blocks[(ch_idx + 1) & q->idx_mask]);
    bool prod_single = q->prod_single;
    bool cons_single = q->cons_single;
    uint64_t ch_vsn = bbq_head_vsn(q, ch);
    uint64_t committed = bbq_atomic64_load(&n_blk->committed, prod_single);
    uint64_t committed_vsn = bbq_cur_vsn(q, committed);

    if (BBQ_F_CHK_DROP_OLD(q->flags)) {
        // 通过检查下一个块的版本是否大于或等于当前块来保证 FIFO 顺序.
        // 第一个块是一个特殊情况，因为与其他块相比，它的版本总是相差一个。因此，如果 ch_idx == 0，我们在比较中加 1
        if (committed_vsn < ver + (ch_idx == 0)) {
            return false;
        }

        bbq_fetch_max(&n_blk->reserved, bbq_set_cur_vsn(q, committed_vsn), cons_single);
    } else {
        if (committed_vsn != ch_vsn + 1) {
            // 消费者追上了生产者，下一块还未开始生产
            return false;
        }
        uint64_t new_vsn = bbq_set_cur_vsn(q, ch_vsn + 1);
        bbq_fetch_max(&n_blk->consumed, new_vsn, cons_single);
        bbq_fetch_max(&n_blk->reserved, new_vsn, cons_single);
    }

    bbq_fetch_max(&q->chead.value, ch + 1, cons_single);
    return true;
}

/* 消息队列出队 */
static struct bbq_status __bbq_dequeue(struct bbq *q, void *deq_data, uint32_t n, uint32_t data_type, uint32_t *wait_consumed) {
    uint64_t deq_update = 0;
    bool cons_single = q->cons_single;
    struct bbq_status ret = {.status = 0, .actual_burst = 0};
    if (bbq_unlikely(q == NULL || deq_data == NULL)) {
        ret.status = BBQ_ERR_INPUT_NULL;
        return ret;
    }

    while (true) {
        uint64_t ch = bbq_atomic64_load(&q->chead.value, cons_single);
        struct bbq_block *blk = &(q->blocks[bbq_head_idx(q, ch)]);
        struct bbq_queue_state_s state;
        state = bbq_reserve_entry(q, blk, n);

        switch (state.state) {
        case BBQ_RESERVED:
            if (!consume_entry(q, &state.e, deq_data, data_type)) {
                continue;
            }
            ret.status = BBQ_OK;
            ret.actual_burst = state.e.actual_burst;

            if (BBQ_F_CHK_STAT_ENABLE(q->flags)) {
                deq_update = bbq_atomic64_fetch_add(&q->stat.n_deq, state.e.actual_burst, cons_single) +
                             state.e.actual_burst;
            }
            break;
        case BBQ_NO_ENTRY:
            ret.status = BBQ_ERR_EMPTY;
            break;
        case BBQ_NOT_AVAILABLE:
            ret.status = BBQ_ERR_BUSY;
            break;
        case BBQ_BLOCK_DONE:
            if (advance_chead(q, ch, state.vsn)) {
                continue;
            }
            ret.status = BBQ_ERR_EMPTY;
            break;
        default:
            ret.status = BBQ_ERR;
            break;
        }

        if (BBQ_F_CHK_STAT_ENABLE(q->flags) && wait_consumed != NULL) {
            *wait_consumed = bbq_wait_consumed_get(q, 0, deq_update);
        }

        return ret;
    }
}

int bbq_dequeue(struct bbq *q, void **data) {
    struct bbq_status ret = __bbq_dequeue(q, data, 1, BBQ_DATA_TYPE_SINGLE, NULL);
    return ret.status;
}

int bbq_dequeue_elem(struct bbq *q, void *data) {
    struct bbq_status ret = __bbq_dequeue(q, data, 1, BBQ_DATA_TYPE_SINGLE, NULL);
    return ret.status;
}

static inline uint32_t bbq_max_burst(struct bbq *q, uint32_t n) {
    if (bbq_unlikely(n > q->bs)) {
        return q->bs;
    }

    return n;
}

static uint32_t bbq_dequeue_burst_1d_array(struct bbq *q, void *obj_table, uint32_t n, uint32_t *wait_consumed) {
    if (bbq_unlikely(q == NULL || obj_table == NULL)) {
        return 0;
    }

    if (bbq_unlikely(!BBQ_F_CHK_COPY_VALUE(q->flags))) {
        return 0;
    }

    uint32_t burst = 0;
    uint32_t ready = 0;
    void *obj = obj_table;
    struct bbq_status ret = {0};

    while (ready < n) {
        burst = bbq_max_burst(q, n - ready);
        ret = __bbq_dequeue(q, obj, burst, BBQ_DATA_TYPE_1D_ARRAY, wait_consumed);
        if (ret.status != BBQ_OK) {
            break;
        }
        obj += q->entry_size * ret.actual_burst;
        ready += ret.actual_burst;
    }

    return ready;
}

static uint32_t bbq_dequeue_burst_2d_array(struct bbq *q, void **obj_table, uint32_t n, uint32_t *wait_consumed) {
    if (bbq_unlikely(q == NULL || obj_table == NULL)) {
        return 0;
    }

    uint32_t burst = 0;
    uint32_t ready = 0;
    void **obj_table_tmp = obj_table;
    struct bbq_status ret = {0};

    while (ready < n) {
        burst = bbq_max_burst(q, n - ready);
        ret = __bbq_dequeue(q, obj_table_tmp, burst, BBQ_DATA_TYPE_2D_ARRAY, wait_consumed);
        if (ret.status != BBQ_OK) {
            break;
        }
        obj_table_tmp += ret.actual_burst;
        ready += ret.actual_burst;
    }

    return ready;
}

/* 尝试一次入队多个数据，直到达到最大数量，或是入队失败 */
static uint32_t bbq_enqueue_burst_1d_array(struct bbq *q, void const *obj_table, uint32_t n, uint32_t *wait_consumed) {
    if (bbq_unlikely(q == NULL || obj_table == NULL)) {
        return 0;
    }

    if (bbq_unlikely(!BBQ_F_CHK_COPY_VALUE(q->flags))) {
        return 0;
    }

    uint32_t burst = 0;
    uint32_t ready = 0;
    void const *obj = obj_table;
    struct bbq_status ret = {0};

    while (ready < n) {
        burst = bbq_max_burst(q, n - ready);
        ret = __bbq_enqueue(q, obj, burst, BBQ_DATA_TYPE_1D_ARRAY, wait_consumed);
        if (ret.status != BBQ_OK) {
            break;
        }
        obj += q->entry_size * ret.actual_burst;
        ready += ret.actual_burst;
    }

    return ready;
}

/* 尝试一次入队多个数据，直到达到最大数量，或是入队失败 */
static uint32_t bbq_enqueue_burst_2d_array(struct bbq *q, void *const *obj_table, uint32_t n, uint32_t *wait_consumed) {
    if (bbq_unlikely(q == NULL || obj_table == NULL)) {
        return 0;
    }

    uint32_t burst = 0;
    uint32_t ready = 0;
    void *const *obj_table_tmp = obj_table;
    struct bbq_status ret = {0};

    while (ready < n) {
        burst = bbq_max_burst(q, n - ready);
        ret = __bbq_enqueue(q, obj_table_tmp, burst, BBQ_DATA_TYPE_2D_ARRAY, wait_consumed);
        if (ret.status != BBQ_OK) {
            break;
        }
        obj_table_tmp += ret.actual_burst;
        ready += ret.actual_burst;
    }

    return ready;
}

uint32_t bbq_enqueue_burst_elem(struct bbq *q, void const *obj_table, uint32_t n, uint32_t *wait_consumed) {
    return bbq_enqueue_burst_1d_array(q, obj_table, n, wait_consumed);
}

uint32_t bbq_enqueue_burst_elem_2d_array(struct bbq *q, void *const *obj_table, uint32_t n, uint32_t *wait_consumed) {
    return bbq_enqueue_burst_2d_array(q, obj_table, n, wait_consumed);
}

uint32_t bbq_enqueue_burst(struct bbq *q, void *const *obj_table, uint32_t n, uint32_t *wait_consumed) {
    return bbq_enqueue_burst_2d_array(q, obj_table, n, wait_consumed);
}

uint32_t bbq_dequeue_burst(struct bbq *q, void **obj_table, uint32_t n, uint32_t *wait_consumed) {
    return bbq_dequeue_burst_2d_array(q, obj_table, n, wait_consumed);
}

uint32_t bbq_dequeue_burst_elem(struct bbq *q, void *obj_table, uint32_t n, uint32_t *wait_consumed) {
    return bbq_dequeue_burst_1d_array(q, obj_table, n, wait_consumed);
}

bool bbq_debug_check_array_bounds(struct bbq *q) {
#ifdef BBQ_MEMORY
    void *value = malloc(q->entry_size);
    memset(value, BBQ_MEM_MAGIC, q->entry_size);

    for (size_t i = 0; i < q->bn; i++) {
        // 针对内存检查版本，申请了bs+1个entry
        char *last_entry = q->blocks[i].entries + q->bs * q->entry_size;
        if (memcmp(last_entry, value, q->entry_size) != 0) {
            return false;
        }
    }
#else
    AVOID_WARNING(q);
#endif
    return true;
}

#if 0
#include <stdio.h>
/* 位置有关代码，需要-fPIC，这里注释掉用于调试 */
void bbq_debug_block_print(struct bbq *q, struct bbq_block *block) {
    bool prod_single = q->prod_single;
    bool cons_single = q->cons_single;

    uint64_t allocated = bbq_atomic64_load(&block->allocated, prod_single);
    uint64_t committed = bbq_atomic64_load(&block->committed, prod_single);
    uint64_t reserved = bbq_atomic64_load(&block->reserved, cons_single);
    uint64_t consumed = bbq_atomic64_load(&block->consumed, cons_single);
    printf("  allocated:%lu(ver:%lu)   committed:%lu(ver:%lu)  reserved:%lu(ver:%lu)",
           bbq_cur_off(q, allocated), bbq_cur_vsn(q, allocated),
           bbq_cur_off(q, committed), bbq_cur_vsn(q, committed),
           bbq_cur_off(q, reserved), bbq_cur_vsn(q, reserved));
    if (BBQ_F_CHK_DROP_OLD(q->flags)) {
        printf("\n");
    } else {
        printf("  consumed:%lu(ver:%lu)\n", bbq_cur_off(q, consumed), bbq_cur_vsn(q, consumed));
    }
}

void bbq_debug_struct_print(struct bbq *q) {
    printf("-----bbq:%s-----\n", BBQ_F_CHK_DROP_OLD(q->flags) ? "drop old" : "retry new");
    uint64_t phead = bbq_atomic64_load(&q->phead.value, q->prod_single);
    uint64_t chead = bbq_atomic64_load(&q->chead.value, q->cons_single);

    printf("block number:%u block size:%u total entries:%u\n", q->bn, q->bs, q->bn * q->bs);
    printf("producer header idx:%lu vsn:%lu\n", bbq_head_idx(q, phead), bbq_head_vsn(q, phead));

    uint64_t ph_idx = bbq_head_idx(q, phead);
    uint64_t ch_idx = bbq_head_idx(q, chead);
    if (ph_idx != ch_idx) {
        printf("block[%lu]\n", ph_idx);
        bbq_debug_block_print(q, &(q->blocks[ph_idx]));
    }

    printf("consumer header idx:%lu vsn:%lu\n", bbq_head_idx(q, chead), bbq_head_vsn(q, chead));
    printf("block[%lu]\n", ch_idx);
    bbq_debug_block_print(q, &(q->blocks[ch_idx]));
}
#endif

#if 0
/* 根据实际head以及块上的游标推算出待消费的个数，该函数很影响性能 */
static uint32_t bbq_wait_consumed_get_from_head(struct bbq *q,
                                                                      uint64_t *ch_ptr,
                                                                      uint64_t *ph_ptr,
                                                                      struct bbq_block *blk_ph) {
    uint64_t ch = 0;
    uint64_t ph = 0;
    if (ch_ptr != NULL) {
        ch = *ch_ptr;
    } else {
        ch = bbq_atomic64_load(&q->chead.value);
    }

    if (ph_ptr != NULL) {
        ph = *ph_ptr;
    } else {
        ph = bbq_atomic64_load(&q->phead.value);
    }

    uint64_t ph_idx = bbq_head_idx(q, ph);
    uint64_t ch_idx = bbq_head_idx(q, ch);
    uint64_t committed_off = bbq_cur_off(q, bbq_atomic64_load(&blk_ph->committed));

    struct bbq_block *blk_ch = &(q->blocks[bbq_head_idx(q, ch)]);
    uint64_t reserved_off = bbq_cur_off(q, bbq_atomic64_load(&blk_ch->reserved));

    // "生产者"超过"消费者"块的个数
    uint64_t idx_diff = ph_idx >= ch_idx ? ph_idx - ch_idx : q->bn - ch_idx + ph_idx;
    if (!BBQ_F_CHK_DROP_OLD(q->flags)) {
        // 这里idx_diff-1=-1也是正确。
        return (idx_diff - 1) * q->bs + (q->bs - reserved_off + committed_off);
    }

    uint64_t ch_vsn = bbq_head_vsn(q, ch);
    uint64_t ph_vsn = bbq_head_vsn(q, ph);

    if (ph_vsn == ch_vsn || (ph_vsn == (ch_vsn + 1) && (ph_idx < ch_idx))) {
        // drop old模式，未发生覆盖
        return (idx_diff - 1) * q->bs + (q->bs - reserved_off + committed_off);
    }

    if (ph_idx == ch_idx) {
        // drop old模式，发生了覆盖，当前块以及之前已生产的都作废
        return 0;
    }

    return (idx_diff - 1) * q->bs + committed_off;
}

/* 根据head偏移判断是否为空，耗性能函数 */
bool bbq_empty_from_head(struct bbq *q) {
    uint64_t phead = bbq_atomic64_load(&q->phead.value);
    uint64_t chead = bbq_atomic64_load(&q->chead.value);

    uint64_t ph_vsn = bbq_head_vsn(q, phead);
    uint64_t ch_vsn = bbq_head_vsn(q, chead);
    uint64_t ph_idx = bbq_head_idx(q, phead);
    uint64_t ch_idx = bbq_head_idx(q, chead);

    struct bbq_block *block;

    if (ph_idx != ch_idx) {
        return false;
    }

    block = &q->blocks[ph_idx];
    if (ph_vsn == ch_vsn) {
        if (bbq_cur_off(q, bbq_atomic64_load(&block->reserved)) == bbq_cur_off(q, bbq_atomic64_load(&block->committed))) {
            return true;
        }
    }

    uint64_t reserved = bbq_atomic64_load(&block->reserved);
    uint64_t reserved_off = bbq_cur_off(q, reserved);

    if (BBQ_F_CHK_DROP_OLD(q->flags) &&
        ph_vsn > ch_vsn &&
        reserved_off != q->bs) {
        // 生产者追上了消费者，当前块以及未消费的全部
        // 如果reserved指向当前块的最后一个entry，可以移动head消费下一块，否则返回空
        return true;
    }

    return false;
}
#endif