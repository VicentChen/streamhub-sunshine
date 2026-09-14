# Receiver 接入与短时验证

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
| PCM | 48 kHz；Receiver 支持 2／6／8 声道、5／10／20 ms，Provider 当前输出静音 |
| 手柄 | Receiver 实现基础状态和振动；当前真实 Provider 接受零手柄能力 |

Mac Moonlight 6.1.0 的默认请求是 BT.601 limited。本次在独立 Provider 中增加实际 MPP BT.601 RGB 转换及 SPS/VUI 校验。原生 BT.709 NV12 需要该输出时，先由 RGA 按 BT.709 转 BGR，再由 MPP 按 BT.601 转换；没有在 Receiver 修改请求或仅改色彩标签。BT.709 NV12 可继续直入 MPP；尺寸／对齐不足时仍由 RGA 处理。

不声明 AV1、HDR、4:4:4、RFI 或未实现的触摸／运动扩展。超过 Provider 当前能力的完整请求明确失败，包括 full range、多 slice 和过大尺寸。

## 会话和所有权

- /launch、/resume 固定输入，ANNOUNCE 转换完整参数后才 CONNECT。有限异步握手不会阻塞其他 RTSP 请求；200 在 CONNECTED 后返回，不等待后续 UDP ping。
- 资源校验覆盖 4 个队列、8 个真正的 DMA-BUF 和 PCM 池；验证预算、布局、大小、权限、唯一性、memfd 固定大小、空队列和单调时钟。错误／取消路径关闭 fd 并回滚。
- 视频先 READ START，然后验证和引用共享槽。网络分包缓冲复制所需字节后发出完成通知；原 consumer 执行 READ END 和 dequeue。没有预先整帧复制到通用编码包。映射和会话由租约保活。
- Provider PTS 驱动 RTP，采集时间只做延迟统计。网络帧号从 1 开始。IDR 请求既检查 RESULT，也检查之后实际 IDR，最多三次有限等待。
- PCM 复制后立即归还共享槽，之后进行 Opus 编码。sample_index 保留缺块时间，discontinuity 重置编码器并重新对齐音频 FEC 块；不会压缩时间缺口。
- 本地队列采用有界提交，满队列不清空引用链。Linux UDP 使用非阻塞发送，每次调用共用 100 ms 截止时间；发送失败结束对应会话，不让等待无限占住映射和路由状态。
- 停止顺序为结束本地共享读取、STOP_REQUEST、STOPPED／连接失败、释放映射。网络包独立保活会话，清理后等待包所有权归零。
- 手柄在单一控制 worker 中分配非零会话内 ID，显式转换按键、轴和扳机；仅使用 ACCEPT 和设备能力交集。旧 ID 的迟到振动被丢弃。零能力不会发布共享手柄事件。

20 ms 的高码率 7.1 Opus 包可能大于以太网 MTU；本端按实际编码包长度分配缓冲，不将大包截断。真实 Mac 客户端本次使用立体声 5 ms，其余声道与时长由独立 Opus 解码测试验证。

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
