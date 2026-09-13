# Sunshine for StreamHub

本项目的 Sunshine fork，负责 Moonlight 配对、发现、会话、网络传输和 StreamHub 协议适配。硬件采集、视频编码策略、主机专属行为及 ESP32 控制由独立 StreamHub 软件或固件承担。

## 当前状态

已完成计划中确定的功能裁剪：删除本机采集、视频编码、图像处理、显示管理、音频采集、虚拟输入、应用命令与托盘。保留 Moonlight 协议、网络传输、PCM→Opus、手柄解析及反馈。

Receiver 尚未实现；当前没有媒体源连接，启动/恢复不会伪报串流成功。外部接入和真实串流验收仍是后续工作。没有新增分辨率、帧率、会话数或音频/手柄限制。构建、依赖和验证详情见 [完整裁剪记录](docs/streamhub/slimming-core.md)。

## 开发与文档入口

- [开发规范](AGENTS.md)：本 fork 的职责与开发要求。
- [StreamHub 改造文档](docs/streamhub/README.md)：计划与后续模块说明的统一入口。
- [瘦身计划](docs/streamhub/slimming-plan.md)：功能取舍、删除顺序与回归验证。
- [产品架构](../docs/architecture.md)：进程与模块边界。
- [公共协议](../protocol/README.md)：两端通信规范和基础组件。
- [独立 StreamHub 软件](../streamhub/README.md)：Provider 当前实现与使用入口。

本 fork 保留 [GPL 许可证](LICENSE) 和 [第三方声明](NOTICE)。协议与独立软件的授权及依赖边界以项目说明为准。

## 文档归属

本 fork 的改造文档集中在 docs/streamhub/。原有上游文档保留位置，按内容是否仍适用逐项维护，不整体标记为归档。跨模块架构与正式协议分别在总项目和 protocol 中维护，本模块通过链接引用。

## 上游来源

本 fork 基于 [LizardByte/Sunshine](https://github.com/LizardByte/Sunshine)。上游的本机采集、编码与输入功能说明可作为历史参考，不代表本 fork 当前能力。版权、GPL 和第三方声明继续保留。
