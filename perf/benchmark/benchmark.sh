#!/bin/bash
###
# @Author: liuyu
 # @LastEditTime: 2024-06-30 21:43:01
# @Email: liuyu@geedgenetworks.com
# @Describe: 运行性能测试的脚本
###

ring_type_arr=("bbq" "dpdk" "rmind")
burst_arr=("32" "16" "8" "1")

# 检查参数数量
if [ "$#" -ne 3 ]; then
    echo "Usage: $0 <path to benchmark> <path to config file or directory> <ring type>"
    exit 1
fi

# 获取参数值
BENCHMARK_PATH=$1
CONFIG_DIR=$2
RING_TYPE=$3

function exec_benchmark_ring_type() {
    local ini="$1"
    local ring="$2"
    local log_file=$3
    local burst=$4

    # 如果以perf开头的配置文件，还要执行perf统计
    if [[ $(basename "$ini") == perf* ]]; then
        echo "skip perf*"
        # echo perf stat -e L1-dcache-loads,L1-dcache-load-misses "$BENCHMARK_PATH" "$ini" "$ring" "$BURST_CNT"
        # perf stat -e L1-dcache-loads,L1-dcache-load-misses "$BENCHMARK_PATH" "$ini" "$ring" "$BURST_CNT"
    else
        "$BENCHMARK_PATH" "$ini" "$ring" "$burst" 2>&1 | tee -a "$log_file"
    fi
}

function exec_benchmark() {
    # 提取配置文件名（不带路径）
    local INI_FILE=$1
    local log_file=$2
    local burst=$3

    # 执行benchmark命令并传递配置文件作为参数
    echo "Executing benchmark with $INI_FILE"

    # 使用所有ring_type
    if [ "$RING_TYPE" == "all" ]; then
        for ring_type_tmp in "${ring_type_arr[@]}"; do
            echo start ring:"$ring_type_tmp"
            exec_benchmark_ring_type "$INI_FILE" "$ring_type_tmp" "$log_file" "$burst"
        done
    else
        exec_benchmark_ring_type "$INI_FILE" "$RING_TYPE" "$log_file" "$burst"
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

# 创建报告目录
timestamp=$(date +"%Y%m%d_%H%M%S")
folder_path="/tmp/bbq/$timestamp"
rm -rf "$folder_path"
mkdir -p "$folder_path"

# 开始时间
start_time=$(date +%s)

for burst in "${burst_arr[@]}"; do
    burst_dir="$folder_path"/burst_"$burst"
    mkdir -p "$burst_dir"
    for i in {1..3}; do
        echo ======"$i"========
        report_path="$burst_dir"/report_"$i".txt
        if [[ -f "$CONFIG_DIR" ]]; then
            # 如果是文件，直接执行
            exec_benchmark "$CONFIG_DIR" "$report_path" "$burst"
        else
            # 使用 find 命令递归地搜索所有的 .ini 文件，并按文件名排序
            find "$CONFIG_DIR" -type f -name "*.ini" -print0 | sort -z | while IFS= read -r -d '' INI_FILE; do
                if [ -f "$INI_FILE" ]; then
                    exec_benchmark "$INI_FILE" "$report_path" "$burst"
                fi
            done
        fi

        sleep 1
    done
done

# 结束时间
end_time=$(date +%s)
# 计算时间差（秒）
runtime=$((end_time - start_time))
# 输出运行时间
echo "done, use $runtime second"
