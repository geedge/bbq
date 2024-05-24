/*
 * @Author: liuyu@geedgenetworks.com
 * @LastEditTime: 2024-05-24 12:03:31
 * @Describe:
 */
#ifndef _BBQ_ERROR_H_
#define _BBQ_ERROR_H_

// 通用返回码
#define BBQ_OK 0            // 成功
#define BBQ_ERROR -1        // 通用错误
#define BBQ_ALLOC_ERR -2    // 内存分配失败
#define BBQ_NULL_PTR -3     // 空指针
#define BBQ_UNKNOWN_TYPE -3 // 未知类型

// 队列错误
#define BBQ_QUEUE_FULL -1001  // 队列已满（入队失败）
#define BBQ_QUEUE_BUSY -1002  // 队列忙碌中（入队或出队失败）
#define BBQ_QUEUE_EMPTY -1003 // 队列已空（出队失败）

#endif