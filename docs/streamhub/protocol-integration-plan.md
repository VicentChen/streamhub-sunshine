# Sunshine protocol 接入计划

状态：步骤 1–8 的实现与对应验证已完成；步骤 9 的短时客户端验收、恢复、故障清理和文档已完成，真实 HDMI 拔插因板端无信号仍未验证。实际使用、结果和边界见 [Receiver 接入](receiver-integration.md)。本文保留原计划及验收要求，不以测试输入替代现场验证。

## 1. 目标与依据

在 Sunshine 内实现 Receiver，通过 protocol v0.2 使用独立 Provider 的视频、PCM 和手柄信道，沿用 Sunshine 的 Moonlight 配对、会话与网络传输。

现有依据：

- [架构与模块边界](../../../docs/architecture.md)
- [protocol 接入入口](../../../protocol/README.md)、[交互流程](../../../protocol/streamhub-protocal.md)、[信道数据结构](../../../protocol/channel-info.md)、[字节格式](../../../protocol/wire-format.md)
- [Provider 当前实现](../../../streamhub/README.md)
- [Sunshine 裁剪结果](slimming-core.md)

协议适配和 Sunshine 类型转换放在本 GPL 仓库；仅依赖公共协议目标，不链接 Provider 实现库。当前协议共享 ABI 限 Linux AArch64／x86-64，接入代码按平台编译。其他平台核心构建不因增加 Linux IPC 依赖而直接失败。

当前 Provider 的单活动会话、输出范围、静音和零手柄能力属于实现现状。Receiver 尊重实际 ACCEPT／REJECT，不将这些值固化为 Sunshine 的长期限制。真实 HDMI 音频采集和 ESP32 后端不属于本次 Receiver 实现。

## 2. 实施方式与验证输出

每一步交付源码、对应测试和简短结果记录，完成后再勾选。记录实际执行命令、退出码、测试数量、适用环境和未验证项；测试失败时不将该步标记完成。

验证采用有限输入和显式结束条件：自动测试按单项设置超时，通常不超过 30 秒，跨进程用例最多 60 秒；真实串流只做每种编码约 10 秒的功能检查，重连固定 3 次。构建耗时另计。本文不安排长时间运行、耐久测试或长时压力测试。

下文测试套件及有限辅助入口已建立；当前命令见 Receiver 接入说明。普通测试使用模拟 Provider、临时 socket、临时 HOME／配置／证书和空闲回环端口；真实硬件用例显式选择设备，不接管在线服务。

建议在 `src/streamhub/` 集中实现 `transport`、`receiver`、`negotiation`、`video`、`audio`、`gamepad`，随对应步骤建立文件，不预建空框架。

| 步骤 | 依赖 | 可验证输出 |
| --- | --- | --- |
| 1. 构建与控制传输 | 无 | 可构建适配目标；GET_INPUTS 跨进程往返测试 |
| 2. 输入目录与能力声明 | 1 | 稳定的应用／输入映射；主机信息及应用列表响应 |
| 3. 请求转换与兼容性 | 1 | 参数转换测试；真实 Provider 接受／拒绝结果 |
| 4. 资源与会话状态机 | 1、3 | 完整握手及失败清理测试 |
| 5. Moonlight 会话接线 | 2、4 | launch／ANNOUNCE／cancel／resume 的协议响应 |
| 6. 视频桥接 | 5 | Moonlight 可解码视频；DMA 归还与 IDR 测试 |
| 7. PCM 与 Opus 桥接 | 5 | 可解码 Opus；声道与时间线测试 |
| 8. 手柄输入与反馈 | 5 | 模拟 Provider 的双向事件验证 |
| 9. 短时集成验收 | 6、7、8 | 两种编码、退出和恢复的有限场景结果 |

步骤 2 与 3 在步骤 1 后可分别实现；步骤 6、7、8 共享会话接口，但各自提交和验证。

## 3. 分步实施

### 步骤 1：接入构建与控制传输

- [x] 完成；实现、命令和验证结果见 [控制传输](control-transport.md)。

**实现内容**

- 链接 `streamhub-protocal::protocol`，使用总仓库的 protocol checkout；明确独立构建 Sunshine 时如何传入协议源码位置，缺失时给出清楚错误。
- 增加 Provider socket 配置及 Linux IPC 实现：SEQPACKET、同 UID 对端检查、控制编解码、SCM_RIGHTS、fd RAII、可取消的发送／接收等待。
- 接收时检查截断、附带 fd 数量与消息类型，错误路径关闭已收到的全部 fd。
- 建立仅供测试使用的最小模拟 Provider 和隔离测试入口；不依赖 MPP、RGA 或 HDMI。

**交付物**：CMake 接入、socket 配置、`transport` 实现、`StreamHubTransportTest` 与模拟 Provider fixture。

**短时验证**：生产目标和测试目标构建通过；GET_INPUTS／INPUT_LIST 跨进程往返；错误版本、截断包、额外 fd、EOF 和取消等待均有确定结果。固定运行 3 次失败连接，检查 fixture 持有的 fd 已关闭。

### 步骤 2：接入输入目录与启动前能力声明

- [x] 完成；见 [Receiver 接入与验证](receiver-integration.md)。

**实现内容**

- 首次 GET_INPUTS 后维护完整快照，接收后续列表事件；区分 Provider 离线、输入移除和输入无信号。
- 建议由 Provider 输入生成可选择条目，维护稳定的 `appid ↔ input_id` 映射；映射基于不透明 input_id，处理 ID 冲突，名称只用于展示。不自动重写旧 apps.json；已有元数据与来源映射的关系明确记录。
- Provider 离线时保留配对与 Web 管理，启动请求明确报告来源不可用；仍存在但无信号的输入允许继续由 Provider 决定是否接受。
- v0.2 没有编码能力查询：维护显式、经验证的接入能力配置，用于 serverinfo／DESCRIBE；最终输出以逐次协商为准，不从输入状态猜编码能力，也不通过临时占用编码会话探测。

**交付物**：输入目录及身份映射实现、能力声明来源说明、`StreamHubInputCatalogTest`。

**短时验证**：模拟列表重排、改名、删除和恢复；同一 input_id 的 appid 保持稳定，不同输入不串用；Provider 离线与无信号响应不同；能力响应不声明未支持的 AV1、HDR、4:4:4 或 RFI。

### 步骤 3：实现精确请求转换，解决客户端兼容性

- [x] 完成；见 [Receiver 接入与验证](receiver-integration.md)。

**实现内容**

- 从 Sunshine 协商参数构造 CONNECT_REQUEST：编码／profile、约分帧率、bit/s、颜色／范围、参考帧和 slice 约束、PCM 布局／块长、手柄能力与共享内存预算。
- 检查取值、溢出和无法表达的需求；ACCEPT 后逐字段验证媒体匹配和手柄能力交集。不能为了接受请求丢弃参考帧限制、修改尺寸或静默降档。
- 明确启动前能力快照和会话实际能力的关系：DESCRIBE 早于 CONNECT_ACCEPT，扩展处理仍按最终会话能力检查；不让一个会话的结果覆盖其他会话的能力状态。
- 优先处理当前兼容性缺口：Moonlight 可能请求 max_ref_frames=1，而 Provider 当前拒绝任何非零值。若实际码流可证明满足限制，在 Provider 模块单独实现支持并测试；若不能满足，保留明确拒绝。不得在 Receiver 中把 1 改成 0。

**交付物**：`negotiation` 实现、`StreamHubNegotiationTest`、简短参数兼容表；确需 Provider 修改时附独立变更和验证结果。

**短时验证**：表驱动检查 60、60000/1001 帧率、码率单位、颜色、2／6／8 声道、块长和非法值；用真实 Provider 分别完成一组 H.264／HEVC 请求及一个不支持约束的拒绝。参考帧支持若有修改，以有限帧解码／参数集验证实际约束，而非仅检查返回成功。

### 步骤 4：实现资源校验与 Receiver 状态机

- [x] 完成；见 [Receiver 接入与验证](receiver-integration.md)。

**实现内容**

- 完整处理 CONNECT_ACCEPT／REJECT、RESOURCE、READY、CONNECTED、STATUS、RESULT、STOPPED，校验请求关联、消息方向、session 和状态转换。
- 收齐四个队列、八个视频 DMA-BUF 和一个 PCM 池后，检查大小、类型、权限、唯一性、slot、布局、固定大小要求、总预算及时间基准兼容性，再发送 READY。
- 建立队列视图，不重新构造 Provider 已初始化的共享对象。视频／PCM 只读映射，队列按协议读写映射。
- 为资源准备、控制请求和停止设置可取消等待。资源准备失败在 ACCEPT 后走 STOP_REQUEST；EOF 时独立清理本地状态。

**交付物**：`receiver`、资源所有权对象、`StreamHubSessionTest`、`StreamHubResourceTest`。

**短时验证**：正确握手完成一次 CONNECTED→STOPPED；资源乱序可接受，重复／缺失槽、别名 fd、错误大小、错误 request_id／session_id 均被拒绝；在每个准备状态注入 EOF／取消，检查资源归零。真实 DMA mmap／同步能力使用板端有限资源测试。

### 步骤 5：连接 Moonlight 会话生命周期

- [x] 完成；见 [Receiver 接入与验证](receiver-integration.md)。

**实现内容**

- `/launch` 和 `/resume` 选择输入并创建待协商状态；完整输出需求到 RTSP ANNOUNCE 后才提交给 Provider。
- ANNOUNCE 成功响应以协议会话 CONNECTED 为前提；Provider 等待不阻塞整个 RTSP 处理循环，期间取消仍可执行。接入中间状态不对外伪报已有媒体发送。
- ANNOUNCE 响应不能等待只有后续才会到来的客户端 UDP 握手，避免互相等待。实际媒体发送在各自 peer 就绪后开始；等待期间仍处理控制和取消。
- 建立、拒绝、超时和启动中取消均回滚待启动状态；保留“活动串流结束”与“Moonlight 可恢复的应用选择”的区别。
- 每个串流持有独立 Receiver 会话。当前 Provider 返回 busy 时原样映射为可诊断失败；不在 Sunshine 新增永久单会话限制。
- 停止顺序为：停止提交新数据，唤醒并等待本地异步读取结束，发送 STOP_REQUEST，收到 STOPPED 或控制失败后释放映射；旧会话资源不复用。

**交付物**：nvhttp／rtsp／stream 接线、会话所有权管理、`StreamHubMoonlightSessionTest`。

**短时验证**：模拟完整 launch→ANNOUNCE→cancel；分别注入 CONNECT_REJECT、资源超时、启动中 cancel；确认错误响应和应用状态回滚。固定执行 3 次 resume／cancel，验证 session_id 和资源更新、无旧数据继续发送。

### 步骤 6：接通视频、IDR 与 DMA-BUF 生命周期

- [x] 完成；见 [Receiver 接入与验证](receiver-integration.md)。

**实现内容**

- 用引用共享槽的编码包对接 Sunshine 分包路径，不先将整帧搬进 packet_raw_generic 再重复复制；保留网络分包、FEC 和加密所需的自身缓冲。
- 先采用每个视频队列最多一条待完成读取的包装，保持单 consumer；发送线程完成源数据读取后通知原 consumer，后者执行 READ END 和 dequeue。READ START 必须先于首次 CPU 读取，异常／丢弃路径同样完成同步与回收。
- 在现有分包缓冲已接管字节后归还 DMA 槽，不必等 UDP 发送完成；此后不再访问已归还源数据。session 和映射寿命覆盖全部使用者。
- 校验帧标志、长度、发布槽序、首帧和 discontinuity 的 IDR 契约；恢复等待可取消，不能随意丢编码参考帧后继续发送依赖帧。
- 分离 PTS 与采集时间：PTS 驱动 RTP／呈现时间，capture_time_ns 仅用于延迟统计；为 Moonlight 建立从 1 开始的网络帧序号，保留 Provider frame_id 作诊断。
- 将客户端 IDR 及需要恢复的控制事件映射成 REQUEST_IDR；RESULT=ok 只表示受理，检查后续实际 IDR，有限重试后明确失败。继续不声明 RFI 能力。
- 处理 Sunshine 本地 mailbox 满时清空队列的旧行为：接入路径采用有界背压和明确的包丢弃／归还处理，不能留下未归还槽或悄悄破坏参考链。

**交付物**：视频包装、发送完成通知、时间与帧序映射、IDR 转发、`StreamHubVideoTest`。

**短时验证**：延迟发送线程，确认读取结束前 Provider 不能复写对应槽；验证源读取完成后的归还顺序及取消路径。有限帧覆盖首帧编号、帧号间隙、PTS、未知采集时间、IDR 和恢复。真实 Moonlight 分别播放 H.264／HEVC 约 10 秒，验证画面和一次关键帧恢复。

### 步骤 7：接通 PCM、Opus 与音频时间线

- [x] 完成；见 [Receiver 接入与验证](receiver-integration.md)。

**实现内容**

- 优先请求 48 kHz 交错 float PCM，块长匹配协商的 Opus 包时长；必要的格式转换、声道重排和积累在本端完成。
- PCM 复制／转换到 Sunshine 自有缓冲后即可 dequeue，后续 Opus 编码不再引用共享池；所有本地队列有界且停止可打断。
- 根据 sample_index／PTS 识别缺口，处理 DISCONTINUITY 时清理积累并按明确策略补静音或重同步；保留时间间隙，不将缺块压缩成连续时间。
- 检查音视频使用同一单调时钟基准；Opus 失败只结束所属会话，不停止其他会话的全局发送队列。

**交付物**：PCM 桥接、音频时间元数据、`StreamHubAudioTest`。

**短时验证**：模拟 Provider 为每个声道输出不同的短测试信号，解码 Opus 检查 2／6／8 声道顺序、采样数和包时长；注入一次缺块和停止，检查时间线及资源回收。真实 Provider 的静音仅验证传输和节奏，不作为真实音频采集验收。

### 步骤 8：接通手柄输入与反馈

- [x] 完成；见 [Receiver 接入与验证](receiver-integration.md)。

**实现内容**

- 由单一 producer 汇合 Moonlight 手柄事件，维护客户端索引与本会话非零 controller_id 的映射，重连使用新 ID。
- 显式转换按键位、Y 轴方向、扳机范围和事件时间；只发送 ACCEPT 与设备能力交集允许的事件。
- connected 后发送首个完整 state，断开清理触点／订阅；反馈按 controller_id 找回客户端，丢弃已断开 ID 的迟到反馈。
- 基础状态与振动先完成；触摸、运动、电量及其他反馈按已实现转换启用，不能因为公共类型存在就声明支持。对 Sunshine 无现成封装的反馈明确不声明。
- 满队列采用可取消背压，不静默丢弃按下／释放／断开；持续无法投递时明确报告失败。会话结束时不依赖最后一条 stop 一定送达。

**交付物**：`gamepad` 适配、会话能力处理、`StreamHubGamepadTest`。

**短时验证**：模拟 Provider 检查连接、按下／释放、轴极值和振动往返；一次重连后旧反馈不作用于新手柄；满队列时取消能退出。真实 Provider 协商零手柄能力时，两条共享手柄队列保持空，媒体继续工作。

### 步骤 9：完成短时集成与使用说明

- [ ] 实现、客户端与故障验收已完成；真实 HDMI 拔插仍待现场信号。

**实现内容**

- 提供隔离启动 Provider／Sunshine、指定 socket、运行测试和清理进程的可复现命令。
- 更新模块状态、构建依赖、当前兼容组合及已知缺口；结果记录放入本模块验证文档并添加索引。
- 每个场景显式结束并检查错误是否能定位到输入、会话或信道；实现完成后再更新总仓库状态说明。

**交付物**：短时集成入口、使用说明、逐项结果记录。

**短时验证清单**

- H.264、HEVC 各播放约 10 秒，确认视频及静音音轨可接收。
- 进行一次 HDMI 拔出／插回，观察占位及恢复，协商输出不变；有条件时另验证一次输入模式变化。
- 启动／取消／恢复固定 3 次，检查自身 fd、映射和工作线程回到基线。
- 串流中结束测试 Provider 一次，Sunshine 及时释放会话；Provider 重启后可重新启动串流。
- 模拟一次忙拒绝、一次资源失败、一次网络发送阻塞，检查有界退出与正常下一次连接。
- 运行本次新增测试及受影响的既有配对、配置、协议传输回归；构建、测试、真实客户端验证分别记录，不互相代替。

## 4. 完成标记

阶段性里程碑：步骤 1–7 完成后，可交付使用真实 Provider 的 Moonlight 视频与静音音轨，并具备短时启动、取消和恢复验证。

本接入计划完成：步骤 1–9 的实际交付物和验证结果齐全。手柄以模拟 Provider 验证 Receiver 的实现，真实硬件能力按 Provider 当前状态记录。未执行的真实设备场景保留未验证标记，不以模拟结果替代。
