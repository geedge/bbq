cmake_minimum_required(VERSION 3.0)
project(INIPARSER)

file(GLOB SRC_LIST ${CMAKE_CURRENT_SOURCE_DIR}/*.c) #搜索当前cmake所在目录下的c文件
set(LIBRARY_OUTPUT_PATH ${LIB_PATH}) #设置库生成目录

add_library(iniparser STATIC ${SRC_LIST}) #生成静态库
# add_library(${BBQ_LIB} SHARED ${SRC_LIST}) #生成动态库