# Control transport 接入

本文说明控制传输层；完整输入目录、协商、资源状态机和媒体接线已实现，使用及最终验证见 [Receiver 接入](receiver-integration.md)。

## 构建与依赖

Linux 构建通过 `sunshine-streamhub` 静态目标依赖公共 MIT `streamhub-protocal::protocol`，使用 C++23。源码为 `src/streamhub/transport.{h,cpp}`；不包含或链接 Provider 实现。适配代码属于本 GPL fork，protocol 的 MIT LICENSE 保留在其独立 checkout 中，并由 Linux 安装规则附带到 share/licenses/sunshine/streamhub-protocol-LICENSE。

总仓库布局默认读取相邻 `../protocol`。独立 checkout 必须先取得公共协议仓库，并在原 Sunshine CMake 配置中传入：

```sh
cmake -S . -B cmake-build-protocol \
  -DSTREAMHUB_PROTOCOL_SOURCE_DIR=/absolute/path/to/streamhub-protocal \
  -DBUILD_TESTS=ON
cmake --build cmake-build-protocol --target sunshine test_sunshine test_streamhub_transport --parallel 3
```

其余依赖沿用 [裁剪后的依赖](slimming-core.md#依赖与授权)。缺少协议头文件时配置立即报告如何初始化 submodule 或传入路径。非 Linux 不构建或链接此适配目标；未执行非 Linux 构建验证。

## 配置与 API

`streamhub_socket` 在配置文件及 Web General 页提供。默认空，表示未配置；显式路径必须为绝对 Linux 文件系统路径，不能含 NUL，且必须放得进 `sockaddr_un.sun_path`。不会展开环境变量或 `~`。例如：

```ini
streamhub_socket = /run/user/1000/streamhub/control.sock
```

输入目录组件在启动时使用该配置建立连接。传输类本身接收显式路径，不读取全局配置。

`transport` 建立 NONBLOCK/CLOEXEC SEQPACKET 连接并检查 SO_PEERCRED 的 UID 等于本进程有效 UID。send/receive 接收绝对 steady-clock 截止时间和 stop_token；eventfd 唤醒取消等待。AF_UNIX backlog 满时 connect 明确报错，不将 EAGAIN 当成连接成功。

接收按公共编解码器验证版本、消息值和 ID 形状，再检查 RESOURCE 恰有一个 fd、其他消息没有 fd。所有收到的 fd 在验证前由 RAII 接管，并设置 CLOEXEC；错误版本、数据截断和辅助数据错误都会释放它们。资源大小、身份和状态转换检查属于 Receiver，不在此层伪称已验证。

每条连接串行调用操作；调用者在接收错误后应销毁连接。取消令牌可从其他线程发出，不通过跨线程 close 借用 fd 来取消。send 的资源参数为借用，receive 返回的资源为独占所有权。消息方向与会话关联由 Receiver 状态机检查。

## 有限测试

仅控制传输、无 HDMI 或 Provider 依赖的快速测试：

```sh
ctest --test-dir cmake-build-protocol/tests -R '^streamhub-transport$' --output-on-failure
```

带配置解析和原有回归的隔离入口：

```sh
python3 tests/run-streamhub-tests.py --build cmake-build-protocol --regression
```

该入口使用临时 HOME、配置及覆盖率目录；传输测试上限 30 秒，含回归上限 60 秒，XML 输出到构建目录。需要特殊动态库搜索路径的开发环境在调用前自行设置 LD_LIBRARY_PATH。板端现有环境见 [托盘记录](slimming-tray.md#本机验证环境)。

模拟 Provider 在临时目录建立 socket；GET_INPUTS 往返在子进程运行，子进程有 5 秒超时。错误包携带最多 253 个 fd，检查接收失败后 fd 回到基线；另外覆盖 RESOURCE、CLOEXEC、EOF、读写取消、超时和固定三次失败连接。配置测试验证显式 socket 解析。测试不替代真实 Provider 握手、DMA 映射、异 UID 拒绝或 Moonlight 串流验收。

## 板端验证记录（2026-09-14）

在权威仓库的 Linux ARM64 环境执行，沿用 `cmake-build-slimming-baseline` 及其 GCC 16／Homebrew 工具链；动态库路径沿用上文环境说明。全部最终命令退出码为 0：

```sh
export PATH=/home/linuxbrew/.linuxbrew/bin:$PATH
export LD_LIBRARY_PATH="$PWD/cmake-build-slimming-baseline/validation-prefix/lib:/home/linuxbrew/.linuxbrew/opt/glibc/lib:/home/linuxbrew/.linuxbrew/lib/gcc/current:/home/linuxbrew/.linuxbrew/lib:/usr/lib/aarch64-linux-gnu"
cmake --build cmake-build-slimming-baseline --target sunshine test_sunshine test_streamhub_transport --parallel 3
ctest --test-dir cmake-build-slimming-baseline/tests -R '^streamhub-transport$' --output-on-failure
python3 tests/run-streamhub-tests.py --build cmake-build-slimming-baseline --regression
SUNSHINE_ASSETS_DIR="$PWD/cmake-build-slimming-baseline" npm run build
clang-format --dry-run --Werror src/streamhub/transport.h src/streamhub/transport.cpp tests/unit/test_streamhub_transport.cpp
git diff --check
```

- `sunshine`、`test_sunshine`、`test_streamhub_transport` 构建通过。
- CTest 1/1 项通过，内部运行 6 个传输用例，约 0.13 秒。
- 隔离回归 16 个套件、83 项全部通过，约 6.78 秒。其中新增 7 项（上述 6 项及配置解析），既有 76 项覆盖配对认证、配置、网络控制包、应用元数据、Opus 与手柄协议等。
- Web 生产构建通过；新增 C++ 格式及 Git 空白检查通过。
- 首轮测试发现模拟 RESOURCE 的 PCM 池大小未按公共容量计算，以及配置 fixture 缺少 apps.json；分别改为使用公共容量常量和创建私有临时应用列表后重跑通过，未放宽生产校验。

此结果仅完成控制传输阶段。没有验证不同 UID 的实际对端、fd 耗尽等系统级故障注入、非 Linux 平台构建、完整 Doxygen 发布、真实 Provider 会话、DMA 访问或 Moonlight 串流。未启动／重启在线 Sunshine，也未修改 Moonlight 配对或唤醒 Xbox。
