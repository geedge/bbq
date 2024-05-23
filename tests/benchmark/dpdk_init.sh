#!/bin/bash
###
# @Description: dpdk环境初始化脚本
# @Date: 2023-12-01 16:48:39
# @LastEditTime: 2024-05-23 16:42:18
###

set -e          #一但有任何一个语句返回非真的值，则退出
set -u          #使用未初始化的变量时，让bash自动退出，放在引用文件之后，否则会报错
set -o pipefail #搭配set -e使用，防止管道中存在执行错误的命令

DPDK_DIR=/root/code/c/dpdk-23.07
DPDK_TOOLS_DIR=${DPDK_DIR}/usertools
DPDK_LD_CONF=${PWD}/ld.dpdk.conf
DPDK_IGB_UIO=/root/code/c/dpdk-kmods/linux/igb_uio/igb_uio.ko

# 需要绑定的接口
ETH1_NAME=ens160
ETH1_PCI=0000:03:00.0

ETH2_NAME=ens161
ETH2_PCI=0000:04:00.0

ETH_DRIVE=vfio-pci

function dpdk_drive_setup() {
    if [ "$1" = "igb_uio" ]; then
        sudo modprobe uio
        sudo insmod ${DPDK_IGB_UIO}
    elif [ "$1" = "vfio-pci" ]; then
        sudo modprobe vfio-pci
    else
        echo "unknown type:" "$1"
        exit 1
    fi
}

function dpdk_bind_dev() {
    sudo ifconfig "$3" down 2>/dev/null || true

    local res
    local bind_cmd=${DPDK_TOOLS_DIR}/dpdk-devbind.py
    res=$($bind_cmd --status | grep -c "$2.*drv=$1" || true)
    if [ "$res" -eq 0 ]; then
        echo "bind $2 by $1"
        sudo modprobe vfio-pci
        $bind_cmd --bind="$1" "$2"
    else
        echo "bind $2 by $1, nothing todo"
    fi
}

function dpdk_set_hugepages() {
    # 简单根据是否有0来判断是否配置了大页内存
    # Node Pages Size Total
    # 0   512   2Mb    1Gb
    # Hugepages mounted on /dev/hugepages
    local res
    local hugepages_cmd=${DPDK_TOOLS_DIR}/dpdk-hugepages.py
    res=$($hugepages_cmd --show | grep -c "0" || true)
    if [ "$res" -eq 0 ]; then
        echo "set hugepages: $1"
        ${hugepages_cmd} --setup 1024M
    else
        echo "set hugepages: $1, nothing todo"
    fi
}

function dpdk_show_all() {
    echo "---hugepages---"
    ${DPDK_TOOLS_DIR}/dpdk-hugepages.py -s
    echo "---devbind---"
    ${DPDK_TOOLS_DIR}/dpdk-devbind.py --status | grep -E 'drv|DPDK|kernel'
}

# echo "【1】dpdk_drive_setup..."
# dpdk_drive_setup ${ETH_DRIVE}

# echo "【2】dpdk_bind_dev..."
# dpdk_bind_dev ${ETH_DRIVE} ${ETH1_PCI} ${ETH1_NAME}
# dpdk_bind_dev ${ETH_DRIVE} ${ETH2_PCI} ${ETH2_NAME}

echo "【3】dpdk_set_hugepages..."
dpdk_set_hugepages "2048M"

# echo "【4】dpdk_show_all..."
# dpdk_show_all

# echo "【5】ldconfig..."
# ldconfig -f "${DPDK_LD_CONF}"

echo "【end】..."
