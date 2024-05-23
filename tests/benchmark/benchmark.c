/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-23 18:06:14
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#include "bbq.h"
#include "bcm_queue.h"
#include "common.h"
#include "iniparser.h"
#include <pthread.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    char *config;

    if (argc == 2) {
        config = argv[1];
    } else {
        config = "/root/code/c/bbq-ly/tests/benchmark/config/case1.ini";
        BBQ_ERR_LOG("!!!!!! not input config path, use default config:%s", config);
        // sleep(2);
    }

    // 加载配置
    bcm_cfg cfg;
    if (bxm_load_config(config, &cfg) != 0) {
        BBQ_ERR_LOG("input config path");
        return -1;
    }

    int ret = -1;
    bcm_queue_t q;
    ret = bcm_queue_init(&cfg, &q);
    if (ret != 0) {
        BBQ_ERR_LOG("init failed :%d", ret);
        return ret;
    }

    // ret = bcm_threads_creat();

    // pthread_t *threads = NULL; // 存储所有线程ID的数组
    // int ret = -1;

    // BlockBoundedQueue_s *q = BlockBoundedQueue_create(bn, bs, RETRY_NEW);
    // EXPECT_TRUE(q != NULL) << "FAILED-1";

    // // 创建生产者消费者线程
    // threads = utest_threads_create(q, bs * 10, pthread_cnt, 0, cthread_cnt, 10);
    // size_t thread_cnt = pthread_cnt + cthread_cnt;
    // thread_data_s **exit_data = (thread_data_s **)malloc(sizeof(thread_data_s **) * (thread_cnt + 1));

    // 等待所有线程完成，回收数据
    // uint32_t i = 0;
    // for (i = 0; i < thread_cnt; i++) {
    //     pthread_join(threads[i], (void **)(&exit_data[i])); // 等待每个线程结束
    // }

    // 输出测试报告
    BBQ_INFO_LOG("report\n");

    return 0;
}