# Sunshine 的 StreamHub 改造文档

本目录维护 Sunshine fork 的改造计划，以及后续实际建立的构建、实现、依赖和部署说明。模块职责与当前状态见 [模块 README](../../README.md)。

## 当前文档

| 文档 | 内容 | 状态 |
| --- | --- | --- |
| [protocol 接入计划](protocol-integration-plan.md) | Receiver 分步实现、每步交付物与短时验证 | 接线与客户端验证已完成，现场 HDMI 待验 |
| [配对重试](pairing-retry.md) | 失败后同客户端重试与旧审批失效 | 143 项回归通过，已部署 |
| [Receiver 接入](receiver-integration.md) | 使用、所有权、兼容组合和有限集成入口 | 已完成客户端验证；现场 HDMI 待验 |
| [控制传输](control-transport.md) | protocol 构建依赖、socket 配置、IPC API 与隔离测试入口 | 已通过板端验证 |
| [瘦身计划](slimming-plan.md) | 保留功能、删除对象、依赖清理和回归验证 | 已确定项完成 |
| [托盘与 Qt 清理](slimming-tray.md) | 已删除内容、验证结果及当前验证环境 | 首批已实施 |
| [应用执行裁剪](slimming-applications.md) | 本机命令、启动钩子及依赖删除，保留应用元数据与会话状态 | 第二批已验证 |
| [媒体与设备后端裁剪](slimming-core.md) | 最终删除范围、保留依赖、规模对比和验证缺口 | 已验证 |

构建、隔离测试、有限两进程启动和客户端验证入口见上表。

## 维护约定

- 本 fork 的改造文档放在本目录；文件名使用小写、多词以连字符分隔。
- 上游文档保留在原位置；是否标注失效或归档，按内容适用性判断。
- 模块 README 提供当前状态和使用入口，本文件提供改造文档导航，计划中记录待实施工作。
- 跨模块架构引用 [产品架构](../../../docs/architecture.md)，正式协议引用 [protocol](../../../protocol/README.md)，不在本目录维护副本。
- 新文档按实际需要增加；移动或改名时同步导航和相对链接。
- 测试结论随对应版本和验证记录维护，不将历史通过结果描述为当前已验证。

总体文档维护规则见 [项目文档说明](../../../docs/README.md#文档维护)，开发规则见 [Sunshine AGENTS.md](../../AGENTS.md)。
