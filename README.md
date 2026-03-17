# Redis DTOE优化（redis-dtoe）

## 最新消息

- [2026.03.30] Redis DTOE优化特性指南首次正式发布。
- [2026.03.05] 完成 Redis 7.0.15 的 DTOE 适配与使能验证。

## 项目介绍

`redis-dtoe` 提供 Redis 在 DTOE（Direct TOE）场景下的用户态接口库 `kbdtoe`，用于配合 Redis DTOE patch 进行网络收发加速。

- 核心能力：通过网卡 TOE 引擎卸载传统内核 TCP/IP 处理路径，降低 CPU 开销并提升吞吐。
- 代码形态：仓库构建产物为共享库 `kbdtoe.so`，并安装头文件 `kbdtoe.h` 供 Redis 侧调用。
- 适用场景：鲲鹏平台 Redis 远端压测与高吞吐场景。
- 原理说明：详见特性指南中的 DTOE 原理与对比说明。

## 目录结构

```text
redis-dtoe/
├── CMakeLists.txt                          # 构建脚本（生成并安装 kbdtoe）
├── include/
│   └── kbdtoe.h                            # 对外接口头文件
├── src/                                    # kbdtoe 实现源码
│   ├── kbdtoe.c
│   ├── kbdtoe_base.h
│   ├── dtoe_mempool_mr.c
│   ├── dtoe_mempool_mr.h
│   ├── libdtoe_send.c
│   ├── libdtoe_recv.c
│   └── libdtoe_poll.c
├── docs/
│   ├── LICENSE                             # 文档许可证
│   └── zh/
│       ├── redis-dtoe-optimization-feature-guide.md
│       ├── redis-dtoe-optimization-release-notes.md
│       ├── figures/
│       └── public_sys-resources/
└── README.md
```

## 特性介绍

| 特性 | Link | 简介 |
|--|--|--|
| Redis DTOE优化特性指南 | [docs/zh/redis-dtoe-optimization-feature-guide.md](docs/zh/redis-dtoe-optimization-feature-guide.md) | 包含环境要求、安装部署、使能与验证步骤 |

### 特性简介

Redis DTOE优化通过将 TCP/IP 数据通路从主机协议栈下沉到网卡 TOE，实现更低的软件路径开销与更高吞吐。  
当前特性约束见特性指南“约束与限制”章节。

### 版本说明

Redis DTOE优化版本、配套关系与变更记录见：[版本说明书](docs/zh/redis-dtoe-optimization-release-notes.md)。

### 快速入门

1. 准备环境：按特性指南完成硬件/OS/驱动与网络前置配置。  
2. 构建本仓库：

```bash
mkdir build
cd build
cmake ..
make -j
cmake --install .
```

3. 在 Redis 7.0.15 源码中合入 DTOE patch 并重新编译。  
4. 配置 `redis.conf` 的 `dtoe-ip` 后，使用 `redis-benchmark` 做功能与性能验证。

## 学习文档

| 学习资源类别 | 学习资源名称 | 学习资源简介 |
|--|--|--|
| 文档 | [Redis DTOE优化特性指南](docs/zh/redis-dtoe-optimization-feature-guide.md) | 提供 DTOE 场景下环境、部署、使能、验证与加固指导 |
| 文档 | [Redis DTOE优化版本说明书](docs/zh/redis-dtoe-optimization-release-notes.md) | 提供版本配套信息与版本变更说明 |
| 代码接口 | [kbdtoe.h](include/kbdtoe.h) | 提供 Redis 侧集成所需接口声明 |

## 兼容性信息

| 维度 | 兼容范围 |
|--|--|
| 处理器 | 鲲鹏920新型号、鲲鹏950 |
| 操作系统 | openEuler 22.03 LTS SP4、openEuler 24.03 LTS SP2 |
| Redis版本 | 7.0.15（需合入 DTOE patch） |

## 工具限制与注意事项

- 当前仅支持组bond网络且bond模式为mode 4。
- 当前不支持本地通讯、pipeline、主从、集群、容器、虚拟网络场景。
- 仅建议在远端压测场景验证DTOE性能收益。

## 贡献声明

欢迎通过 Issue 或 Pull Request 提交问题、建议和改进。  
提交前建议附带复现步骤、环境信息与日志，便于快速定位。

## License

本项目文档 License 详见 [docs/LICENSE](docs/LICENSE)。
本项目的文档适用CC-BY 4.0许可证，具体请参见LICENSE文件。
