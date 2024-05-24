cmake_minimum_required(VERSION 3.0)
project(BBQ_UNITTEST)

# 搜索当前cmake文件所在目录下的c文件
file(GLOB SRC_LIST "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")
file(GLOB SRC_COMMON_LIST "${CMAKE_CURRENT_SOURCE_DIR}/../*.c")
list(APPEND SRC_LIST ${SRC_COMMON_LIST})

# 指定库路径
link_directories(${LIB_PATH})
# 指定头文件
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../
)
# 链接静态库
link_libraries(bbq)
# 指定可执行文件输出路径
set(EXECUTABLE_OUTPUT_PATH ${EXEC_PATH})

add_executable(bbq_unittest ${SRC_LIST}) # 添加可执行程序
target_link_libraries(bbq_unittest gtest gtest_main pthread) # 链接gtest库

add_test(bbq_unittest ${EXEC_PATH}/bbq_unittest) # 添加测试，保证make test可以执行该测试用例