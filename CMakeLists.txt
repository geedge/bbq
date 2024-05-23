cmake_minimum_required(VERSION 3.0)
project(BBQ_TOP)

# 头文件目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/iniparser
    "/root/code/c/dpdk-23.07/install/include"
) 

# 设置输出目录
SET(OUTPUT_DIR ${PROJECT_SOURCE_DIR}/build/output) 
# 库生成的路径
set(LIB_PATH ${OUTPUT_DIR}/lib)
# 测试程序生成的路径
set(EXEC_PATH ${OUTPUT_DIR}/bin)


# 静态库的名字
set(BBQ_LIB bbq)
# 可执行程序的名字
set(BENCHMARK_NAME benchmark)
set(TESTS_NAME tests)

enable_testing() # 开启测试，否则无法执行make test

# 添加子目录
add_subdirectory(src)
add_subdirectory(tests)
add_subdirectory(thirdparty)