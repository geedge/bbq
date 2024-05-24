cmake_minimum_required(VERSION 3.0)
project(BBQ_TESTS)

file(GLOB SRC_COMMON_LIST "${CMAKE_CURRENT_SOURCE_DIR}/common/*.c")

# 指定库路径
link_directories(${LIB_PATH})
# 指定头文件
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/common/
)

# 链接静态库
link_libraries(bbq iniparser)
# 指定可执行文件输出路径
set(EXECUTABLE_OUTPUT_PATH ${EXEC_PATH})

add_subdirectory(unittest)
add_subdirectory(benchmark)
