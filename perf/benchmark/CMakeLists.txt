cmake_minimum_required(VERSION 3.0)
project(BBQ_BENCHMARK)

# 搜索当前cmake文件所在目录下的c文件
file(GLOB SRC_LIST "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
file(GLOB SRC_BBQ_UT_FUNC_LIST "${UT_BBQ_FUNC_DIR}/*.c")
list(APPEND SRC_LIST ${SRC_BBQ_UT_FUNC_LIST})

# 指定可执行文件输出路径
set(EXECUTABLE_OUTPUT_PATH ${EXEC_PATH})

add_executable(benchmark ${SRC_LIST}) # 添加可执行程序
target_link_libraries(benchmark dl iniparser pthread rte_ring rte_eal rte_kvargs rte_telemetry rmind_ringbuf bbq m) # 链接库
#target_link_libraries(benchmark dl iniparser pthread dpdk rmind_ringbuf bbq m) # 链接库

add_custom_command(  
    TARGET benchmark POST_BUILD  
    COMMAND ${CMAKE_COMMAND} -E copy  
        ${CMAKE_CURRENT_SOURCE_DIR}/benchmark.sh  
        ${EXECUTABLE_OUTPUT_PATH}/benchmark.sh  
)  
