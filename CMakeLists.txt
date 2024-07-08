cmake_minimum_required(VERSION 3.0)
project(BBQ_TOP)

SET(OUTPUT_DIR ${PROJECT_SOURCE_DIR}/build/output) 

# 添加子目录
add_subdirectory(bbq) # bbq库
add_subdirectory(perf)# perf目录用于各类消息队列的性能测试，需要依赖dpdk等环境