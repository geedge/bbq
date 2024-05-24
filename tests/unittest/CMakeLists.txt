cmake_minimum_required(VERSION 3.0)
project(BBQ_UNITTEST)

# 搜索当前cmake文件所在目录下的c文件
file(GLOB SRC_LIST "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")
list(APPEND SRC_LIST ${SRC_COMMON_LIST})

add_executable(bbq_unittest ${SRC_LIST}) # 添加可执行程序
target_link_libraries(bbq_unittest gtest gtest_main pthread) # 链接gtest库

add_test(bbq_unittest ${EXEC_PATH}/bbq_unittest) # 添加测试，保证make test可以执行该测试用例