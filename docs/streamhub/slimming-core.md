# 媒体与设备后端裁剪

2026-09-13，实施于 `/home/vicent/Documents/Projects/streamhub/sunshine`。此前两批记录见 [托盘](slimming-tray.md) 和 [应用执行](slimming-applications.md)。本批完成瘦身计划中其余已经确定的删除项。

## 删除结果

- 删除所有本机视频采集、视频编码、像素转换、缩放、GPU 资源、编码器探测和显示器管理。包含软件编码及各平台硬件后端。
- 删除本机音频采集、设备枚举、切换与恢复，保留 PCM 到 Opus 的编码、声道映射和网络发送。
- 删除本机虚拟 HID、键鼠注入、虚拟手柄及驱动集成，保留 Moonlight 手柄状态、到达、触摸、运动、电量的消息解析及反馈封装。
- 删除关联的配置、Web 设置与设备管理 API、辅助程序、shader、专属测试、构建探测、安装权限和 CI 依赖。
- 删除 FFmpeg、swscale、本机编码库、CUDA、图形和设备库依赖；编码包使用自有字节存储，不再依赖 AVPacket/AVFrame。原编码器输出修补和参考帧探测随其唯一使用者删除。
- 删除十个直接子模块声明：build-deps、glad、libdisplaydevice、libvirtualhid、nvapi、plasma-wayland-protocols、TPCircularBuffer、ViGEmClient、wayland-protocols、wlr-protocols；删除 NvFBC 头文件。连同首批 tray，共删除十一个直接子模块声明。

## 保留与当前缺口

发现、主机标识、配对认证及持久状态、应用元数据、会话控制、RTSP 协商、视频分包/RTP/FEC/加密/时间戳、PCM→Opus、音频发送、关键帧与保活控制、手柄协议解析和反馈封装保留。

外部媒体来源尚未接入。当前不声明可用视频编码能力，launch/resume 在无媒体来源时返回不可用，不伪报会话成功。保留的数据包、PCM 和手柄消息入口不等于已完成 Receiver 或 Provider 适配。没有新增分辨率、帧率、会话数量等产品限制。

Web 管理整体、UPnP、跨平台支持仍属于原计划待单独决定的范围；本批只移除其中与已删功能绑定的部分。Windows 服务包装、macOS/Linux 主机发现、网络与线程调度继续保留。

## 依赖与授权

| 保留内容 | 实际用途 |
| --- | --- |
| Boost、OpenSSL、Threads | 网络、配置、并发、认证与加密 |
| moonlight-common-c 中的 ENet、协议定义及 Reed-Solomon 代码 | 控制通道、消息布局与 FEC |
| Opus | PCM 音频编码 |
| Simple-Web-Server、nlohmann/json、curl、lizardbyte-common | HTTP/Web 管理及其共用实现 |
| miniupnpc | 尚未决定删除的 UPnP |
| Linux GIO、动态库加载与 Avahi 运行库 | 调度、主机发现 |
| Windows 系统库、macOS Foundation | 保留的平台基础功能 |
| GoogleTest、doxyconfig、前端构建依赖、flatpak-builder-tools | 对应测试、文档、Web 和打包工具 |

GPL 许可证、NOTICE 和仍使用的第三方源码授权材料保留。删除依赖不改变本模块的 GPL 边界；未把 Sunshine 代码迁入 MIT 模块。完整组件授权以各依赖随附许可证为准。

## 规模对比

本批起点是前两批裁剪后的工作树快照，统计全部 `src` 普通源码文件，排除第三方源码、Git 历史和构建缓存：约 65,290 行降至 21,910 行，减少约 66%。配置解析键由 93 项降至 23 项。以上不是可执行文件体积或运行时性能指标。

## 验证

验证环境沿用 [托盘记录](slimming-tray.md) 中的私有 Linux ARM64 构建环境，缓存目录为 `cmake-build-slimming-baseline`。生产目标及测试目标使用同一裁剪后源码构建。测试的 HOME、配置、临时目录和覆盖率输出均隔离，未改动在线服务及其配对状态。

- Linux ARM64 `sunshine` 与 `test_sunshine` 构建通过；Web 生产构建通过。
- 15 个套件、75 项回归全部通过，覆盖认证、加密、控制包、发现名称、配置、应用元数据、手柄消息解析及隔离队列、PCM→Opus 编解码。复现入口为缓存目录的 `validation-core.py`，结果为 `slimming-core.xml`。
- 隔离实例的主机信息接口正常响应；SIGTERM、SIGINT 均在约 0.01 秒内以退出码 0 结束。
- 工作流 YAML、Flatpak JSON 解析通过，65 个工作流 shell 步骤及 Linux 构建脚本、Arch PKGBUILD 语法检查通过。没有将这些静态检查表述为各发行版打包成功。
- 私有 Doxygen 1.9.4 源码 XML 生成完成。旧版解析器与上游自定义文档命令仍有警告；未执行完整上游文档发布流水线，不宣称文档零警告。
- Git 差异空白检查通过。

Windows/macOS 的源码、构建和打包引用已清理，但没有在本批执行实际平台构建。未做安装、部署、重启在线服务或端到端硬件串流验收；外部媒体接入仍是后续独立工作。

## Review 修复（2026-09-14）

- 删除 `main()` 尾部 Windows 条件分支中遗留的右花括号，恢复正确的函数边界。
- RTSP DESCRIBE 的手柄触摸／运动扩展声明改为读取 `input::supports_controller_touch_events`，不再固定为 0。该原子标志默认关闭，供协议适配器按实际输入源能力设置，并在输入源断开时清除；当前尚无适配器设置它，因此不宣称已具备外部输入能力。Moonlight 对触摸和运动消息使用同一个 `LI_FF_CONTROLLER_TOUCH_EVENTS` 标志。
- 新增 `GamepadProtocolTest.DescribeTracksSourceTouchAndMotionSupport`，通过回环 TCP 连接读取实际 DESCRIBE 响应，检查能力关闭、启用及再次关闭时的声明，并确认不会同时声明未实现的其他输入扩展。

验证沿用上述私有 Linux ARM64 环境：`sunshine`、`test_sunshine` 构建通过；`validation-core.py` 的 15 个套件、76 项回归全部通过；`validation-shutdown.py sunshine` 验证主机信息接口正常，SIGTERM、SIGINT 均约 0.01 秒以退出码 0 结束。改动行格式检查及 Git 差异空白检查通过。Windows 条件分支的静态预处理检查确认多余括号已消除，但未执行 Windows/macOS 实际构建，未部署或重启在线服务。
