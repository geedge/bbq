#!/bin/bash
###
# @Author: liuyu
# @LastEditTime: 2024-05-27 16:13:01
# @Email: liuyu@geedgenetworks.com
# @Describe: TODO
###

ring_type_arr=("bbq" "dpdk")

# 检查参数数量
if [ "$#" -ne 3 ]; then
    echo "Usage: $0 <path_to_benchmark> <path_to_config_directory> <ring_type>"
    echo "Where <ring_type> is one of: ${my_array[*]} or input all" # 列出可选的 ring_type
    exit 1
fi

# 获取参数值
BENCHMARK_PATH=$1
CONFIG_DIR=$2
RING_TYPE=$3

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

# 使用 find 命令递归地搜索所有的 .ini 文件，并按文件名排序
find "$CONFIG_DIR" -type f -name "*.ini" -print0 | sort -z | while IFS= read -r -d '' INI_FILE; do
    if [ -f "$INI_FILE" ]; then
        # 提取配置文件名（不带路径）
        INI_FILENAME=$(basename "$INI_FILE")

        # 执行benchmark命令并传递配置文件作为参数
        echo "Executing benchmark with $INI_FILENAME"

        # 第三个参数没有配置，使用所有ring_type
        if [ "$3" == "all" ]; then
            for ring_type in "${ring_type_arr[@]}"; do
                "$BENCHMARK_PATH" "$INI_FILE" "$ring_type"
            done
        else
            "$BENCHMARK_PATH" "$INI_FILE" "$3"
        fi
    else
        echo "Warning: '$INI_FILE' is not a regular file, skipping."
    fi
done

echo "All .ini configurations have been tested."
