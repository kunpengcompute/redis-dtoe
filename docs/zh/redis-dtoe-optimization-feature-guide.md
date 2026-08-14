# Redis DTOE优化 特性指南

## 特性描述

本文主要介绍如何在使用openEuler操作系统的鲲鹏920新型号处理器上使能Redis DTOE优化特性及其性能测试方法。

当前Redis客户端与Redis服务端的主备通信流程使用操作系统TCP/IP协议栈，存在如下痛点：

- TCP通信存在软件路径长、上下文切换开销和网络报文拷贝开销大的问题，导致整体CPU开销占比高。
- 硬中断、软中断频繁打断业务，影响性能。

DTOE（Direct TOE）是基于网卡TOE（TCP Offload Engine）引擎加速技术，可以将协议栈的数据收发处理流程卸载到网卡内部的CPU进行处理，进而可以将HOST的协议栈处理算力节省出来给其他应用使用，提升应用使用效率。

通过Redis DTOE优化特性为Redis应用加速，提升吞吐量。

**原理描述**

DTOE是基于网卡TOE（TCP Offload Engine）引擎加速技术，可以将HOST端协议栈的数据收发处理流程卸载到网卡内部的CPU进行处理，DTOE相对传统网卡加TCP的优缺点对比如图1所示。

**图 1** 传统网卡+TCP与DTOE对比图

![传统网卡+TCP与DTOE对比图](figures/传统网卡+TCP与DTOE对比图.png)

**表 1** 传统网卡+TCP与DTOE对比

| 项目       | 优点                                         | 缺点                         |
| -------- | ------------------------------------------ | -------------------------- |
| 传统网卡+TCP | 基础方案，对NIC要求比较少                             | 传统网卡需要CPU处理TCP/IP协议，资源消耗严重 |
| DTOE     | 芯片实现TCP/IP协议栈，bypass内核协议栈用户态数据面直通，消除跨态内存拷贝 | 专用驱动，需要Redis侧适配            |

**约束与限制**

当前特性支持组bond且为模式4、k8s容器场景以及不组bond场景，不支持本地通讯。

## 环境要求

本文基于特定环境提供指导，在正式操作前请确保软硬件均满足要求。

**表 2** 硬件要求

| 项目  | 规格                   |
| --- | -------------------- |
| CPU | 鲲鹏920新型号处理器、鲲鹏950处理器 |
| 网卡  | 1825网卡 (2*100GE)     |

**表 3** 操作系统和软件要求

| 项目          | 版本                      | 获取地址                                                                                                                       |
| ----------- | ----------------------- | -------------------------------------------------------------------------------------------------------------------------- |
| 操作系统        | openEuler 22.03 LTS SP3 | [获取链接](https://repo.huaweicloud.com/openeuler/openEuler-22.03-LTS-SP3/ISO/aarch64/openEuler-22.03-LTS-SP3-aarch64-dvd.iso) |
| 操作系统        | openEuler 24.03 LTS SP3 | [获取链接](https://repo.huaweicloud.com/openeuler/openEuler-24.03-LTS-SP3/ISO/aarch64/openEuler-24.03-LTS-SP3-aarch64-dvd.iso) |
| 网卡及DTOE相关驱动 |              | 提issue获取                                                                                                                   |
| Knet DTOE       | -                       | [获取链接](https://gitcode.com/openeuler/knet/tree/dtoe)                                                                      |
| Redis           | 7.0.15                  | [获取链接](https://download.redis.io/releases/redis-7.0.15.tar.gz)                                                              |
| Redis DTOE Patch | 0001-adapt-dtoe.patch   | [获取链接](https://gitcode.com/twwang1/Redis/blob/master/src/Redis-7.0.15/feature-patches/0001-adapt-dtoe.patch)              |

## 安装部署DTOE

根据提供的《FlexDA DTOE开发指南》安装部署DTOE。

## 编译与使能特性

1. 下载knet dtoe源码、编译并安装。

   ```bash
   sudo yum install cmake rpmbuild libboundscheck.aarch64 libcap.aarch64 libcap-devel.aarch64
   git clone https://gitcode.com/openeuler/knet.git
   cd knet 
   git checkout dtoe
   python3 build.py Release dtoe rpm
   rpm -ivh ./build/rpmbuild/RPMS/ubs-knet-1.0.0.aarch64.rpm --nodeps --force
   ```

2. 下载kbdtoe库源码、编译并安装。

   ```bash
   git clone https://gitcode.com/boostkit/redis-dtoe.git
   cd redis-dtoe
   mkdir build
   cd build
   cmake  ..
   make -j
   cmake --install . # 默认会将kbdtoe.h和libkbdtoe.so文件分别安装到/usr/include/和/usr/lib64
   ```

3. 将Redis中的**0001-adapt-dtoe.patch** 移到Redis源码目录下，执行合入patch的命令。其中`path/`为本地Redis源码目录路径。

   ```bash
   git clone https://gitcode.com/BoostKit/Redis.git
   cp Redis/src/Redis-7.0.15/feature-patches/0001-adapt-dtoe.patch path/redis-7.0.15/
   cd path/redis-7.0.15
   patch -p1 < 0001-adapt-dtoe.patch
   ```

4. 重新编译Redis。

   ```bash
   cd path/redis-7.0.15
   make distclean
   make -j
   ```

## 验证特性

Redis DTOE优化特性必须基于远端压测，不支持本地压测，需要在redis.conf中修改dtoe-ip的值与实际环境相符合。

测试中需要设置风扇转速比例95%，不然网卡接可能口会自动down，具体操作步骤如下：

1. SSH 登陆服务器iBMC。
2. 使用命令ipmcset -d fanmod -v 1 0设置风扇控制模式为手动模式，不超时。
3. 使用命令ipmcset -d fanlevel -v 95设置风扇转速比例为95%。
4. 使用命令ipmcget -d faninfo查询风扇状态。

**功能测试**

1. 修改redis.conf文件中dtoe-ip字段。

   ```bash
   dtoe-ip "" # 该字段改成dtoe bond的ip地址，目前只支持ipv4
   protected-mode yes #改成no
   ```

2. 在Server端环境启动一个适配DTOE特性后的redis-server实例。

   ```bash
   cd path/redis-7.0.15
   ./src/redis-server ./redis.conf --bind 0.0.0.0 --port 6379
   ```

3. 在Client端环境中进入Redis目录，使用下面命令测试。

   ```bash
   cd path/redis-7.0.15
   ./src/redis-benchmark -h server-ip -p server-port -c 50 -t set,get -n 10000000 -r 10000000 -d 3 --threads 20 # -d 3 这里3可以设置为其它值，如10、128、1024、4096
   ```

   测试结果类似如下。

4. 在Client端环境中也可以使用redis-cli和memtier-benchmark工具测试。
   若redis-server使能dtoe成功会回显下面内容：

**性能测试**

性能测试采用Redis自带的redis-benchmark工具，分别测试data-size为10和128字节在以下场景下的性能提升。

- 单实例/10实例相比原生Redis绑核，中断NUMA均衡绑核。

1. 在Server端执行以下命令进行基础环境配置。

   ```bash
   #停止irqbalance服务
   systemctl stop irqbalance.service
   # 关闭irqbalance
   systemctl disable irqbalance.service
   ulimit -n 65536
   #网卡中断绑核
   #下面的enp65s0f0和enp65s0f1根据实际环境进行修改
   ethtool -L enp65s0f0 combined 32
   ethtool -L enp65s0f1 combined 32
   irq1=`cat /proc/interrupts| grep  enp65s0f0  | awk -F ':' '{print $1}'`
   irq1=`echo $irq1`
   #选择每个NUMA最后8个核
   cpulist=({72..79} {152..159} {232..239} {312..319})
   c=0
   for irq in $irq1
   do
       echo ${cpulist[c]} "->" $irq
       echo ${cpulist[c]} > /proc/irq/$irq/smp_affinity_list
       let "c++"
   done

   irq1=`cat /proc/interrupts| grep enp65s0f1  | awk -F ':' '{print $1}'`
   irq1=`echo $irq1`
   cpulist=({61..68} {141..148} {221..228} {301..308})
   c=0
   for irq in $irq1
   do
       echo ${cpulist[c]} "->" $irq
       echo ${cpulist[c]} > /proc/irq/$irq/smp_affinity_list
       let "c++"
   done
   ```

2. 测试脚本自行准备，压测命令参考如下。

   ```bash
   redis-benchmark -h server-ip -p port -c 50  -t set,get  -n 10000000  -r 10000000 -d data-size --threads 20
   #server-ip是实际环境中redis-server的ip
   #port是实际环境中redis-server的port
   #data-size大小可以是10和128
   ```

3. 检查单实例/10实例在data-size为10字节时性能提升至少100%；检查单实例/10实例在data-size为128字节时性能提升至少70%。

## 安全检查与加固

ASLR（Address Space Layout Randomization，地址空间布局随机化）是一种针对缓冲区溢出的安全保护技术，通过对堆、栈、共享库映射等线性区布局的随机化，增加攻击者预测目的地址的难度，防止攻击者直接定位攻击代码位置，达到阻止溢出攻击的目的。

```bash
echo 2 >/proc/sys/kernel/randomize_va_space
```

该设置为临时生效，重启后失效。如需持久化，请将配置写入 `/etc/sysctl.d/` 目录：

```bash
echo "kernel.randomize_va_space = 2" > /etc/sysctl.d/99-aslr.conf
sysctl -p /etc/sysctl.d/99-aslr.conf
```

## 修订记录

| 发布日期       | 修订记录     |
| ---------- | -------- |
| 2026-06-30 | 第一次正式发布。 |
