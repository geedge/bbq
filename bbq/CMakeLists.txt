cmake_minimum_required(VERSION 3.0)
project(BBQ)

# 头文件目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
) 

# 设置输出目录
if(NOT DEFINED OUTPUT_DIR)  
    # 如果没有被设置，则设置一个默认值  
    SET(OUTPUT_DIR ${PROJECT_SOURCE_DIR}/build/output) 
endif()

# 设置编译类型，默认Release
add_compile_options(-Wall -Wextra)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-DBBQ_MEMORY)
endif()
add_definitions(-D_GNU_SOURCE)

# 库生成的路径
set(LIB_PATH ${OUTPUT_DIR}/lib)
# 测试程序生成的路径
set(EXEC_PATH ${OUTPUT_DIR}/bin)

# 静态库的名字
set(BBQ_LIB bbq)
# 可执行程序的名字
set(TESTS_NAME tests)

enable_testing() # 开启测试，否则无法执行make test

# 添加子目录
add_subdirectory(src)
add_subdirectory(unittest)