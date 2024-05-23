/*
 * @Author: liuyu@geedgenetworks.com
 * @LastEditTime: 2024-05-23 14:23:25
 * @Describe:
 */
#ifndef _BBQ_ERROR_H_
#define _BBQ_ERROR_H_
typedef enum {
    BBQ_OK = 0,            // 成功
    BBQ_ERROR = -1,        // 通用错误
    BBQ_ALLOC_ERR = -2,    // 内存分配失败
    BBQ_NULL_PTR = -3,     // 空指针
    BBQ_UNKNOWN_TYPE = -3, // 未知类型
} bbq_error;

#endif