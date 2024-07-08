cmake_minimum_required(VERSION 3.0)
project(BBQ_BENCHMARK)

# 头文件目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../bbq/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../bbq/unittest
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/iniparser
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/rmind_ringbuf
    /root/code/c/dpdk-21.11.4/install/include
    #/home/liuyu/code/marsio/build/support/dpdk/include
)

# 将bbq单元测试里的公共文件，添加到perf里。
SET(UT_BBQ_FUNC_DIR ${PROJECT_SOURCE_DIR}/../bbq/unittest) 

# 设置输出目录
if(NOT DEFINED OUTPUT_DIR)  
    # 如果没有被设置，则设置一个默认值  
    SET(OUTPUT_DIR ${PROJECT_SOURCE_DIR}/build/output) 
endif()

# 设置编译类型，默认Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_definitions(-D_GNU_SOURCE)
add_compile_options(-Wall -Wextra)

# 库生成的路径
set(LIB_PATH ${OUTPUT_DIR}/lib)
# 测试程序生成的路径
set(EXEC_PATH ${OUTPUT_DIR}/bin)

# 指定库路径
link_directories(${LIB_PATH})
link_directories(../bbq/build/output/lib/)
link_directories(/root/code/c/dpdk-21.11.4/install/lib64 /root/code/c/dpdk-21.11.4/install/lib64/dpdk/pmds-22.0)
# link_directories(/home/liuyu/code/marsio/build/support/dpdk/lib64 /home/liuyu/code/marsio/build/support/dpdk/lib64/dpdk/pmds-22.0)

# 可执行程序的名字
set(BENCHMARK_NAME benchmark)

# 添加子目录
add_subdirectory(benchmark)
add_subdirectory(thirdparty)

