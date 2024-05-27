#!/bin/bash
###
# @Author: liuyu
# @LastEditTime: 2024-05-27 11:33:04
# @Email: liuyu@geedgenetworks.com
# @Describe: TODO
###

# 检查参数数量
if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <path_to_benchmark> <path_to_config_directory>"
    exit 1
fi

# 获取参数值
BENCHMARK_PATH=$1
CONFIG_DIR=$2

# 检查benchmark文件是否存在且可执行
if [ ! -x "$BENCHMARK_PATH" ]; then
    echo "Error: Benchmark executable '$BENCHMARK_PATH' does not exist or is not executable."
    exit 1
fi

# 检查配置文件目录是否存在
if [ ! -d "$CONFIG_DIR" ]; then
    echo "Error: Config directory '$CONFIG_DIR' does not exist."
    exit 1
fi

# 遍历配置文件目录中的所有.ini文件
for INI_FILE in "$CONFIG_DIR"/*.ini; do
    if [ -f "$INI_FILE" ]; then
        # 提取配置文件名（不带路径）
        INI_FILENAME=$(basename "$INI_FILE")

        # 执行benchmark命令并传递配置文件作为参数
        echo "Executing benchmark with $INI_FILENAME"
        "$BENCHMARK_PATH" "$INI_FILE"

        # 休眠3秒
        echo "--------------------------------------------------------------------"
    else
        echo "Warning: '$INI_FILE' is not a regular file, skipping."
    fi
done

echo "All .ini configurations have been tested."
