/*
 * @Author: liuyu@geedgenetworks.com
 * @LastEditTime: 2024-05-16 21:39:19
 * @Describe:调试信息
 */
#ifndef _BBQ_H_
#define _BBQ_H_

#ifdef BBQ_DEBUG
#define BBQ_DBG_LOG(fmt, ...)                      \
    do {                                           \
        printf("[debug]" fmt "\n", ##__VA_ARGS__); \
    } while (0)

#else
#define BBQ_DBG_LOG(fmt, ...) \
    do {                      \
    } while (0)
#endif
#endif

#define BBQ_ERR_LOG(fmt, ...)                      \
    do {                                           \
        printf("[error]" fmt "\n", ##__VA_ARGS__); \
    } while (0)
