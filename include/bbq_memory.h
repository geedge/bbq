/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-20 14:19:46
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#ifndef _BBQ_MEMORY_H_
#define _BBQ_MEMORY_H_

#define BBQ_ALLOC(size)                      \
    do {                                           \
        malloc(size); \
    } while (0)


#define BBQ_FREE(size)                      \
    do {                                           \
        free(size); \
    } while (0)

#endif