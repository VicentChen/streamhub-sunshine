# Receiver 接入与短时验证

> 当前运行、构建和测试必须遵守[产品写入限制](../../../docs/storage.md)，通过总仓库 tools/run.py 入口执行。下方历史验证路径保留当时记录，不再作为当前外部目录写入授权。有限集成 --state 改为产品根目录 var/tests/ 下的独立目录。

Sunshine 已接通 protocol v0.2 输入目录、精确协商、资源握手、视频、PCM→Opus 和基础手柄／振动桥接。适配源码集中在 `src/streamhub/`，只依赖公共 MIT protocol；独立 Provider 仍负责采集、RGA 和 MPP。

## 使用

按 [构建说明](control-transport.md) 构建 Sunshine，并按 [Provider 文档](../../../streamhub/README.md) 构建独立软件。启动顺序不强制；Sunshine 会重新连接离线 Provider。

```ini
streamhub_socket = /run/user/1000/streamhub/control.sock
streamhub_codecs = h264,hevc
```

socket 必须为绝对路径，两进程 UID 相同。配置也在 Web General 页提供，重启 Sunshine 后生效。`streamhub_codecs` 可为 `h264`、`hevc` 或 `h264,hevc`。v0.2 没有能力查询，因此它是管理员显式设置的接入能力；不会通过占用编码器探测。最终每次 ANNOUNCE 仍逐项协商，拒绝不满足的参数。

输入列表成为 Moonlight 的应用列表。应用 ID 来自不透明 input_id 的持久映射，注册表 `streamhub-input-ids` 存在 Sunshine 应用数据目录。重排、改名和删除后恢复不会改变同一来源的 ID；碰撞通过持久分配解决。原 apps.json 不被目录刷新重写；Provider 输入使用默认图标，不借用同号旧应用的元数据。离线时保留配对、管理和最近目录，启动明确失败；输入存在但无信号时仍允许 Provider 决定是否接受。

当前板端 Provider 经验证的组合：

| 项目 | 当前实现 |
| --- | --- |
| 编码 | H.264 High 4.2、HEVC Main 4.1；SDR 8-bit 4:2:0 |
| 尺寸／帧率 | Provider 范围至 1920×1080、1–60 FPS；本次实测 1080p60 和 60000/1001 |
| 色彩 | BT.709 limited、BT.601-525 limited |
| 参考／slice | 实际单前向参考、无 B 帧、单 slice；max_ref_frames=1 原样协商 |
| PCM | 48 kHz；Receiver 支持 2／6／8 声道；普通质量及立体声支持 5／10／20 ms，高质量 5.1／7.1 仅支持 5 ms；Provider 当前输出静音 |
| 手柄 | Receiver 实现基础状态和振动；当前真实 Provider 接受零手柄能力 |

Mac Moonlight 6.1.0 的默认请求是 BT.601 limited。本次在独立 Provider 中增加实际 MPP BT.601 RGB 转换及 SPS/VUI 校验。原生 BT.709 NV12 需要该输出时，先由 RGA 按 BT.709 转 BGR，再由 MPP 按 BT.601 转换；没有在 Receiver 修改请求或仅改色彩标签。BT.709 NV12 可继续直入 MPP；尺寸／对齐不足时仍由 RGA 处理。

不声明 AV1、HDR、4:4:4、RFI 或未实现的触摸／运动扩展。超过 Provider 当前能力的完整请求明确失败，包括 full range、多 slice 和过大尺寸。

## 会话和所有权

- /launch、/resume 固定输入，ANNOUNCE 转换完整参数后才 CONNECT。有限异步握手不会阻塞其他 RTSP 请求；200 在 CONNECTED 后返回，不等待后续 UDP ping。
- 资源校验覆盖 4 个队列、8 个真正的 DMA-BUF 和 PCM 池；验证预算、布局、大小、权限、唯一性、memfd 固定大小、空队列和单调时钟。错误／取消路径关闭 fd 并回滚。
- 视频先 READ START，然后验证和引用共享槽。网络分包缓冲复制所需字节后发出完成通知；原 consumer 执行 READ END 和 dequeue。没有预先整帧复制到通用编码包。映射和会话由租约保活。
- Provider PTS 驱动会话独立的 RTP 时钟，首帧从 tick 1 开始，后续保留源时间间隔；已排队的首帧不会因早于发送线程启动而回绕。采集时间只做延迟统计。网络帧号从 1 开始。IDR 请求既检查 RESULT，也检查之后实际 IDR，最多三次有限等待。
- PCM 复制后立即归还共享槽，之后进行 Opus 编码。sample_index 保留缺块时间，discontinuity 重置编码器并重新对齐音频 FEC 块；不会压缩时间缺口。
- 本地队列采用有界提交，满队列不清空引用链。Linux UDP 使用非阻塞发送，每次调用共用 100 ms 截止时间；发送失败结束对应会话，不让等待无限占住映射和路由状态。
- 停止顺序为结束本地共享读取、STOP_REQUEST、STOPPED／连接失败、释放映射。网络包独立保活会话，清理后等待包所有权归零。
- 手柄在单一控制 worker 中分配非零会话内 ID，显式转换按键、轴和扳机；仅使用 ACCEPT 和设备能力交集。旧 ID 的迟到振动被丢弃。零能力不会发布共享手柄事件。

Moonlight 的音频 UDP 接收缓冲为 1400 字节，包含 RTP／FEC 头。Opus 载荷上限为 1360 字节，并预留 CBC padding；数据包和 FEC 包的上限由编译期检查保证。高质量 5.1／7.1 的 10／20 ms 请求在连接 Provider 前明确拒绝，不静默降低质量或改变包长。真实 Mac 客户端此前使用立体声 5 ms；本地 Opus 解码通过不能替代客户端数据包大小校验。

## 可复现的有限入口

普通自动测试使用假 Provider、临时 HOME、临时 socket、隔离证书和回环 TCP：

```sh
cmake --build cmake-build-slimming-baseline --target sunshine test_sunshine test_streamhub_transport streamhub-receiver-smoke --parallel 3
python3 tests/run-streamhub-tests.py --build cmake-build-slimming-baseline --regression
```

特殊开发工具链可给脚本传 `--runtime-library-path PATH`，只影响被测子进程。不要把 Homebrew glibc 的 LD_LIBRARY_PATH 导出给系统 shell、Python 或构建器。

真实 DMA 接收冒烟测试（有限 120 帧）：

```sh
cmake-build-slimming-baseline/tests/streamhub-receiver-smoke /absolute/provider.sock h264
cmake-build-slimming-baseline/tests/streamhub-receiver-smoke /absolute/provider.sock hevc
```

隔离启动两进程进行 Moonlight 测试：

```sh
python3 tests/run-streamhub-integration.py \
  --build cmake-build-slimming-baseline \
  --provider /absolute/path/to/streamhub \
  --state /tmp/streamhub-acceptance \
  --source test-cycle --port 49089 --seconds 120
```

脚本拒绝端口冲突，创建独立状态，生成仅本次测试使用的 Web 管理凭据（`web-auth.json`，0600），默认 120 秒、最多 1800 秒后停止自身子进程。测试并非长时间压力运行。保留日志供检查；SIGINT／SIGTERM 同样清理。可用 `--reuse-state` 重用由该入口创建的隔离配对状态，不能用它接管用户生产配置。需要真实输入时显式改成 `--source hdmi --device /dev/video0`。

Moonlight 添加 `rock-5b-plus.local:49089`，在该实例 Web UI（49090）批准对应 PIN，选择 HDMI，设置 1080p60、20 Mbps、立体声。不要修改已有服务。Provider 中断场景可向状态目录的 `provider-command` 写入 `stop`，待 Sunshine 退出会话后写入 `start`；两个命令只影响此入口拥有的 Provider。进程 ID 存在 `pids.json`。

## 本次验证（2026-09-14）

环境：Rock 5B+ Linux AArch64，Sunshine GCC 16／Homebrew 构建，Provider 系统 GCC 12；Mac Moonlight 6.1.0。测试输入明确使用 test-cycle，包含 BGR、无源占位和尺寸切换。无第三方编码实现进入 Receiver 或公共 protocol。

| 验证 | 结果 |
| --- | --- |
| Sunshine 主程序、适配库、测试、Web 构建 | 通过；最终 111 项回归，26 个套件，5.33 秒，退出码 0 |
| 隔离适配目标 | 25 项测试全部通过，约 0.29 秒；覆盖传输、协商、目录、资源、媒体及手柄 |
| Linux 发送阻塞 | 饱和真实 socket 缓冲，50 ms 截止返回；排空后重新可写 |
| Receiver 真 DMA 握手 | H.264／HEVC 各 120 帧、约 400 PCM 块，实际 IDR、STOP 均通过 |
| Provider 独立解码 | 两编码各 120 帧，max_ref_frames=1、60000/1001、无 B 帧，拒绝多 slice 通过 |
| 色彩硬件测试 | BT.601／BT.709 参数集、红绿蓝像素、NV12→RGA→MPP、4K BGR 缩放及参考帧验证通过，fd 回到 14 |
| Mac H.264 | 1080p60 实际出画，约 12 秒；截图解码约 60 FPS、网络丢帧 0 |
| Mac HEVC | 1080p60 实际出画，约 17 秒；截图解码约 60 FPS、网络丢帧 0 |
| 三次恢复／断开 | Provider session 2／3／4；每次 Sunshine 回到 27 fd、13 线程、0 共享媒体映射 |
| 应用取消 | Moonlight Quit 走 /cancel，移除可恢复选择，随后可重新 launch |
| busy | 活动串流中独立第二路请求明确返回 busy，原会话继续 |
| Provider 中断／重启 | EOF 到 Session ended 约 70 ms；共享映射归零；重启后重新协商并实际出画 |
| PCM／Opus | 2／6／8 声道、5／10／20 ms 的独立解码及声道隔离；缺块和异常的单会话清理 |
| 手柄 | 模拟连接、状态、极值、断开、重新分配、迟到振动与背压；真实 Provider 为零能力 |
| 最终二进制无信号输入 | 使用真实 HDMI source（HDMIRX 无信号）仍可在 Moonlight 播放 HEVC 占位；约 60 FPS，正常退出 |
| 物理 HDMI 拔插 | **未验证**：已执行一次现有唤醒命令并收到完成确认，HDMIRX 仍报告无信号；test-cycle 不代替真实拔插 |

此实现不包含真实 HDMI 音频采集或 ESP32 手柄后端，也不将静音传输解释为真实音频采集。当前未验证真实 HDMI 拔插／输入模式变化、4K 输出和非 Linux 构建。后续只需在有信号的 HDMI 链路上补做有限现场验收，不应将本记录中的模拟输入称作 Xbox 画面。


## 代码评审修复验证（2026-09-14）

修复了首帧早于发送线程启动时的 RTP 时间戳回绕，以及高质量环绕声长包超过 Moonlight 接收缓冲的问题。每个会话以首帧 PTS 建立独立时钟；音频根据质量与包长校验 CBR 大小，并在 CONNECT_REQUEST 前拒绝超限组合。CBC 输出槽显式预留完整 padding 块。

板端重新构建 sunshine、test_sunshine 和 test_streamhub_transport 成功；上述隔离回归入口运行 116 项测试、26 个套件，全部通过。新增覆盖首帧排队、PTS 间隙、会话时钟隔离、四个超限音频组合在连接 Provider 前被拒绝，以及最大载荷的 CBC padding 边界。独立 Opus 解码覆盖剩余 14 个质量／声道／包长组合和声道隔离。

额外调用实际 UDP 分包路径的有限回环复现通过：首帧 RTP tick 为 1，源 PTS 相隔 50 ms 的下一帧为 4501，同一发送线程上的新会话重新从 1 开始。本轮没有重跑真实 Moonlight／HDMI 现场验收，也没有部署或重启在线服务。

## 板端直接运行的部署注意事项（2026-09-14）

构建目录直接运行时，编译定义 `SUNSHINE_ASSETS_DIR_DEF` 必须指向实际 assets 目录；本机使用 `/home/vicent/Documents/Projects/streamhub/sunshine/cmake-build-slimming-baseline/assets`。Web 文件由既有 Vite 构建输出到该目录的 web 子目录。默认 `/usr/local/assets` 未安装时，认证仍可成功，但首页会返回 HTTP 200、零字节正文，表现为白屏。改变工作目录不能修复绝对资源路径，应重新配置并构建主程序。

本机 Homebrew libdbus 的默认系统总线地址不属于系统 Avahi。运行 Sunshine 时显式设置 `DBUS_SYSTEM_BUS_ADDRESS=unix:path=/run/dbus/system_bus_socket`，否则即使系统 avahi-daemon 正常也会报告 `Failed to create client: Daemon not running`，导致 Moonlight 自动发现缺失。正确启动日志应包含 Avahi service successfully established；局域网 DNS-SD 查询应发现 `_nvstream._tcp` 并返回实际端口。

隔离验收最初使用 Moonlight 地址 `192.168.0.110:49089` 或 `rock-5b-plus.local:49089`，管理页面为 `https://192.168.0.110:49090`。非默认串流端口必须包含在手动添加的地址中，不能只填写主机名。手机侧是否能解析 .local 仍取决于其网络与 mDNS 支持。

本次重新构建并重启后，认证首页、配置页、PIN 页分别返回 5186、4559、3529 字节，引用的 18 个 JS/CSS/module 资源全部返回 HTTP 200 且非空。Mac DNS-SD 实测发现 rock-5b-plus 服务并解析到 rock-5b-plus.local:49089；StreamHub 和 Sunshine 均保持 active。手机界面需由用户刷新复核。首次重启旧进程时日志出现一次退出阶段 SIGSEGV；修正总线后的下一次重启未再出现，退出异常根因未在本次部署修复中调查。

当时的持续运行实例为排查 iPhone 主机名添加与自动发现问题，改为默认 port=47989；Moonlight 可填写 rock-5b-plus.local 或 192.168.0.110，管理页面改为 https://192.168.0.110:47990。主机 ID 与配对状态保留。默认端口服务信息和认证首页均已实测可用，Avahi 注册成功；用户随后明确：默认端口下手动添加主机名成功，但 iPhone 自动发现仍然失败。保留默认端口用于测试；手动添加改善不代表自动发现已修复，也未定位客户端内部根因。用户已确认 Android 能自动发现，iPhone Safari 在变更前能用主机名访问 serverinfo 并收到正确 LocalIP，但 Moonlight iOS 带端口的主机名添加报告仅允许本地网络。

## iPhone 默认发现域修复（2026-09-15）

现场 iPhone（iOS 26）主机列表为空，Android 和 Mac 能发现同一服务，iPhone Safari 可通过 rock-5b-plus.local 访问正确 serverinfo；默认端口只解决了手动主机名添加，不能据此声称自动发现已恢复。

通过 macOS Console 读取已配对 iPhone 的日志，Moonlight 每五秒发起 DNSServiceBrowse，随后记录默认域枚举与取消；对应时间窗没有观察到来自该手机的 _nvstream 查询。仅 IPv4 的新服务名称对照仍不能被自动发现。临时发布 lb._dns-sd._udp.local PTR local. 后，用户立即确认自动出现主机。该结果定位本次自动发现阻塞于默认浏览域枚举，不证明所有 iOS 26 设备均存在同样状态。

Sunshine Linux Avahi 发布现在将这条共享 PTR 与串流服务放入同一 EntryGroup，TTL 为 120 秒；发布失败明确记录错误。原有进程退出、碰撞重建和组重置共同管理记录，不需要额外常驻脚本、手机代理修改或手动录入地址。通过动态加载 Avahi 既有公开 API 实现，未引入新生产依赖。

生产 sunshine 重新构建通过。新增真实 Avahi 生命周期测试 tests/run-browse-domain-test.py，观察启动后的 PTR 和正常 SIGTERM 后的撤销；实测通过，证据保存在 var/tests/browse-domain-dtyyc0wu。该有限测试要求板端 python3-dbus、PyGObject 和系统 Avahi，无需打开 HDMI，使用独立 50089 端口及 var/tests 隔离状态。已有同名浏览域记录时明确拒绝测试，以免把其他发布者当成被测进程的结果。

复现：在产品根目录执行 python3 tools/run.py --development -- python3 sunshine/tests/run-browse-domain-test.py，传 --sunshine 指向本次构建程序，--runtime-library-path 沿用本页板端 Homebrew 动态库路径；--interface 默认 wlP2p33s0。

临时诊断发布者已按时退出，iPhone 日志采集已停止。在线实例切换到产品 var/ 状态的进度由总项目 docs/storage-validation.md 记录；不得通过启动旧 /tmp 状态绕过当前目录限制。
