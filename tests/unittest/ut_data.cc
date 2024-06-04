// /*
//  * @Author: liuyu
//  * @LastEditTime: 2024-06-02 09:18:55
//  * @Email: liuyu@geedgenetworks.com
//  * @Describe: TODO
//  */

// #include "gtest/gtest.h"
// extern "C" {
// #include "test_mix.h"
// #include "test_queue.h"
// #include "ut.h"
// extern bool bbq_malloc_free_equal();
// extern bool test_malloc_free_equal();
// extern void bbq_memory_info();
// }

// TEST(data, correct) {
//     test_memory_counter_clear();

//     test_cfg cfg = {
//         .base = {
//             .name = "data correct",
//             .introduce = "data correct",
//             .cores_cnt = 4,
//         },
//         .ring = {
//             .ring_type = TEST_RING_TYPE_BBQ,
//             .producer_cnt = 5,
//             .consumer_cnt = 5,
//             .workload = TEST_WORKLOAD_SIMPLE,
//             .entries_cnt = 4096,
//             .block_count = 0,
//         },
//         .run = {
//             .enq_per_producer = 1000000,
//             .deq_per_consumer = 1000000,
//             .running = true,
//         },
//     }; // namespace

//     // 队列初始化
//     int ret = -1;
//     test_queue_s q;
//     ret = test_queue_init_bbq(&cfg, &q);
//     ASSERT_TRUE(ret == 0);

//     // 创建线程
//     pthread_t *threads = test_threads_create(&cfg, &q);
//     ASSERT_TRUE(threads);

//     // 等待所有线程完成，回收数据
//     uint32_t thread_cnt = cfg.ring.producer_cnt + cfg.ring.consumer_cnt;
//     test_exit_data **exit_data = (test_exit_data **)test_malloc(TEST_MODULE_UTEST, sizeof(test_exit_data **) * (thread_cnt));
//     uint32_t i = 0;

//     for (i = 0; i < thread_cnt; i++) {
//         pthread_join(threads[i], (void **)(&exit_data[i])); // 等待每个线程结束
//     }

//     // 比较数据
//     test_merge_s merge = {0};
//     test_merge_all_data(exit_data, thread_cnt, &merge);
//     EXPECT_EQ(merge.consumer.data_error_cnt, 0);
//     EXPECT_EQ(merge.consumer.ok_cnt, merge.producer.ok_cnt);
//     EXPECT_EQ(merge.producer.run_times, cfg.run.enq_per_producer * cfg.ring.producer_cnt);
//     // 消费者要消费完再退出，因此可能超过次数
//     EXPECT_GE(merge.consumer.run_times, cfg.run.deq_per_consumer * cfg.ring.consumer_cnt);

//     // 释放数据
//     for (i = 0; i < thread_cnt; i++) {
//         test_exit_data_destory(exit_data[i]);
//     }
//     test_free(TEST_MODULE_UTEST, exit_data);
//     test_threads_destory(threads);
//     test_queue_destory(&q);
//     EXPECT_TRUE(bbq_malloc_free_equal());
//     EXPECT_TRUE(test_malloc_free_equal());
// }