/*
 * @Author: liuyu@geedgenetworks.com
 * @LastEditTime: 2024-05-20 18:44:14
 * @Describe:调试信息
 */
#ifndef _BBQ_LOG_H_
#define _BBQ_LOG_H_

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

#define BBQ_ERR_LOG(fmt, ...)                      \
    do {                                           \
        printf("[error]" fmt "\n", ##__VA_ARGS__); \
    } while (0)
#endif
