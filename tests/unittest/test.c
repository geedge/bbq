/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-20 19:55:12
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#include "bbq.h"
#include <stdio.h>

void bbq_memory_info();

int main() {
    BBQ_DBG_LOG("hello\n");
    BlockBoundedQueue_s *q;
    q = BlockBoundedQueue_create(4, 5, RETRY_NEW);
    if (q == NULL) {
        BBQ_ERR_LOG("BlockBoundedQueue_create err ,ptr is NULL\n");
        return -1;
    }

    BlockBoundedQueue_destory(q);
    bbq_memory_info();
}