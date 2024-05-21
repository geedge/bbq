/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-21 11:21:17
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#include "bbq.h"
#include <stdio.h>

void bbq_memory_info();

int main() {
    BBQ_DBG_LOG("hello!!!\n");
    BlockBoundedQueue_s *q;
    q = BlockBoundedQueue_create(4, 5, RETRY_NEW);
    if (q == NULL) {
        BBQ_ERR_LOG("BlockBoundedQueue_create err ,ptr is NULL\n");
        return -1;
    }

    int cnt = 20;
    int *data = malloc(sizeof(int) * cnt);
    QueueStatus_e en_status;
    for (int i = 0; i < cnt; i++) {
        data[i] = i;
        en_status = bbq_enqueue(q, &data[i]);
        if (en_status == OK) {
            BBQ_INFO_LOG("enqueue ok:%d", data[i]);
        } else {
            BBQ_ERR_LOG("enqueue error:%d data:%d", en_status, data[i]);
        }
    }

    int data2 = 222;
    en_status = bbq_enqueue(q, &data2);
    if (en_status == OK) {
        BBQ_INFO_LOG("enqueue ok:%d", data2);
    } else {
        BBQ_ERR_LOG("enqueue error:%d data:%d", en_status, data2);
    }

    QueueStatus_s status;

    for (int i = 0; i < cnt; i++) {
        status = bbq_dequeue(q);
        if (status.status == OK) {
            BBQ_INFO_LOG("status:%d data:%d", status.status, *(int *)(status.data));
        } else {
            BBQ_ERR_LOG("dequeue error:%d", en_status);
        }
    }

    BlockBoundedQueue_destory(q);
    bbq_memory_info();
}