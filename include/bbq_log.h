/*
 * @Author: liuyu@geedgenetworks.com
 * @LastEditTime: 2024-05-20 21:53:01
 * @Describe:调试信息
 */
#ifndef _BBQ_LOG_H_
#define _BBQ_LOG_H_

#ifdef BBQ_DEBUG
#define BBQ_DBG_LOG(fmt, ...)                                                            \
    do {                                                                                 \
        printf("[DBG][%s:%d:%s]" fmt "\n", __func__, __LINE__, __FILE__, ##__VA_ARGS__); \
    } while (0)

#else
#define BBQ_DBG_LOG(fmt, ...) \
    do {                      \
    } while (0)
#endif

#define BBQ_ERR_LOG(fmt, ...)                                                            \
    do {                                                                                 \
        printf("[ERR][%s:%d:%s]" fmt "\n", __func__, __LINE__, __FILE__, ##__VA_ARGS__); \
    } while (0)

#define BBQ_INFO_LOG(fmt, ...)                                                            \
    do {                                                                                  \
        printf("[INFO][%s:%d:%s]" fmt "\n", __func__, __LINE__, __FILE__, ##__VA_ARGS__); \
    } while (0)
#endif
