cmake_minimum_required(VERSION 3.0)
project(BBQ_UNIT_TESTS)

# 搜索当前cmake文件所在目录下的c文件
file(GLOB SRC_LIST "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
# 包含态库路径
link_directories(${LIB_PATH})
# 链接静态库
link_libraries(bbq)
# 指定可执行文件输出路径
set(EXECUTABLE_OUTPUT_PATH ${EXEC_PATH})

add_executable(tests ${SRC_LIST})