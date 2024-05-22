/*
 * @Author: liuyu
 * @LastEditTime: 2024-05-22 15:58:21
 * @Email: liuyu@geedgenetworks.com
 * @Describe: TODO
 */
#include <benchmark/benchmark.h>

extern "C" {
#include "bbq.h"
#include "demo.h"
}

static void BM_StringCreation(benchmark::State &state) {
    printf("test....1\n");
    for (auto _ : state)
        test_func();
    printf("test....end\n");
}
// Register the function as a benchmark
BENCHMARK(BM_StringCreation);

BENCHMARK_MAIN();