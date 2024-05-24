
#include "test_common.h"

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