cmake_minimum_required(VERSION 3.0)
project(BBQ_BENCHMARK)

# 搜索当前cmake文件所在目录下的c文件
file(GLOB SRC_LIST "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
file(GLOB SRC_CC_LIST "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")
list(APPEND SRC_LIST ${SRC_CC_LIST})

# 指定库路径
link_directories(${LIB_PATH})
# 链接静态库
link_libraries(bbq)
# 指定可执行文件输出路径
set(EXECUTABLE_OUTPUT_PATH ${EXEC_PATH})

add_executable(bbq_benchmark ${SRC_LIST}) # 添加可执行程序
target_link_libraries(bbq_benchmark benchmark pthread) # 链接gtest库