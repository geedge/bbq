
#include "test_common.h"
#include <string.h>

test_time_metric clock_time_get() {
    test_time_metric metric = {0};
    struct timespec st;

    clock_gettime(CLOCK_MONOTONIC, &metric.st);

    return metric;
}

time_t clock_nstime_diff(test_time_metric *slow, test_time_metric *fast) {
    time_t time_ns_fast = fast->st.tv_sec * 1000 * 1000 * 1000 + fast->st.tv_nsec;
    time_t time_ns_slow = slow->st.tv_sec * 1000 * 1000 * 1000 + slow->st.tv_nsec;

    return time_ns_fast - time_ns_slow;
}

test_workload_e bcm_workload_str2enum(const char *workload) {
    if (strcmp(workload, "simple") == 0) {
        return TEST_WORKLOAD_SIMPLE;
    } else if (strcmp(workload, "complex") == 0) {
        return TEST_WORKLOAD_COMPLEX;
    }

    return TEST_WORKLOAD_MAX;
}

test_ring_type bcm_ring_type_str2enum(const char *ring_type) {
    if (strcmp(ring_type, "bbq") == 0) {
        return TEST_RING_TYPE_BBQ;
    } else if (strcmp(ring_type, "dpdk") == 0) {
        return TEST_RING_TYPE_DPDK;
    }

    return TEST_RING_TYPE_MAX;
}