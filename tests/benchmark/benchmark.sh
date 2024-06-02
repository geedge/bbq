#!/bin/bash
###
# @Author: liuyu
 # @LastEditTime: 2024-06-03 01:48:19
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

function exec_benchmark_ring_type() {
    local ini="$1"
    local ring="$2"

    # 如果以perf开头的配置文件，还要执行perf统计
    if [[ $(basename "$ini") == perf* ]]; then
        echo "perf stat -a -e L1-dcache-loads,L1-dcache-load-misses,cache-references,cache-misses "$BENCHMARK_PATH" "$ini" "$ring""
        perf stat -a -e L1-dcache-loads,L1-dcache-load-misses,cache-references,cache-misses "$BENCHMARK_PATH" "$ini" "$ring"
    else
        "$BENCHMARK_PATH" "$ini" "$ring"
    fi
}

function exec_benchmark() {
    # 提取配置文件名（不带路径）
    INI_FILE=$1

    # 执行benchmark命令并传递配置文件作为参数
    echo "Executing benchmark with $INI_FILE"

    # 使用所有ring_type
    if [ "$RING_TYPE" == "all" ]; then
        for ring_type_tmp in "${ring_type_arr[@]}"; do
            exec_benchmark_ring_type "$INI_FILE" "$ring_type_tmp"
            echo "--------------------------------------------------------------"
        done
    else
        exec_benchmark_ring_type "$INI_FILE" "$RING_TYPE"
    fi
}

# 检查benchmark文件是否存在且可执行
if [ ! -x "$BENCHMARK_PATH" ]; then
    echo "Error: Benchmark executable '$BENCHMARK_PATH' does not exist or is not executable."
    exit 1
fi

# 检查配置文件目录是否存在
if [ ! -e "$CONFIG_DIR" ]; then
    echo "Error: Config directory '$CONFIG_DIR' does not exist."
    exit 1
fi

if [[ -f "$CONFIG_DIR" ]]; then
    # 如果是文件，直接执行
    exec_benchmark $CONFIG_DIR
else
    # 使用 find 命令递归地搜索所有的 .ini 文件，并按文件名排序
    find "$CONFIG_DIR" -type f -name "*.ini" -print0 | sort -z | while IFS= read -r -d '' INI_FILE; do
        if [ -f "$INI_FILE" ]; then
            exec_benchmark $INI_FILE
        fi
    done
fi

echo "done......."
