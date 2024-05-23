cmake_minimum_required(VERSION 3.0)
project(BBQ_BENCHMARK_DPDK)

# 搜索当前cmake文件所在目录下的c文件
file(GLOB SRC_LIST "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
list(APPEND SRC_LIST ${SRC_COMMON_LIST})

# 指定库路径
link_directories(${LIB_PATH})

# 链接静态库
# link_libraries(bbq iniparser rte_ring rte_kvargs rte_telemetry)
link_libraries(bbq iniparser)
# 指定可执行文件输出路径
set(EXECUTABLE_OUTPUT_PATH ${EXEC_PATH})

add_executable(benchmark ${SRC_LIST}) # 添加可执行程序
target_link_libraries(benchmark pthread rte_ring rte_eal rte_kvargs rte_telemetry m) # 链接库