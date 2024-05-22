/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-22 15:59:55
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#include "demo.h"
#include "bbq.h"
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <unistd.h>

void test_func() {
    printf("zzz\n");
}

// #define NUM_THREADS 4 // 假设我们想要启动的线程数

// void *thread_func(void *arg) {
//     long core_id = (long)arg;
//     cpu_set_t cpuset;
//     CPU_ZERO(&cpuset);
//     CPU_SET(core_id, &cpuset);

//     if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0) {
//         fprintf(stderr, "Error setting CPU affinity\n");
//         return NULL;
//     }

//     printf("Hello, World! from core %ld\n", core_id);
//     // 这里可以添加更多代码来执行线程的任务

//     return NULL;
// }

// void bcm_thread_create() {
//     pthread_t threads[NUM_THREADS];
//     int num_cores = sysconf(_SC_NPROCESSORS_ONLN); // 获取在线的CPU核心数

//     if (num_cores < NUM_THREADS) {
//         fprintf(stderr, "Not enough CPU cores to run all threads\n");
//         return 1;
//     }

//     for (long i = 0; i < NUM_THREADS; ++i) {
//         // 假设我们只使用前NUM_THREADS个核心
//         if (pthread_create(&threads[i], NULL, thread_func, (void *)i) != 0) {
//             fprintf(stderr, "Error creating thread\n");
//             return 1;
//         }
//     }

//     for (long i = 0; i < NUM_THREADS; ++i) {
//         pthread_join(threads[i], NULL);
//     }
// }

// void test_func() {
//     bcm_thread_create();
// }