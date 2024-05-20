/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-20 18:39:38
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#include "bbq.h"
#include <stdio.h>

int main() {
    BlockBoundedQueue_s *q;
    q = BlockBoundedQueue_create(5, 4, RETRY_NEW);
    if (q == NULL) {
        bbq_error_log("BlockBoundedQueue_create err ,ptr is NULL\n");
        return;
    }

    BlockBoundedQueue_destory(q);
    printf("hello\n");
}