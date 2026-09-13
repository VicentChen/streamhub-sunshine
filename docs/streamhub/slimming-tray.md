# 托盘与 Qt 清理

2026-09-13，在 `/home/vicent/Documents/Projects/streamhub/sunshine` 实施。此记录只覆盖托盘这一批，其他裁剪项见 [瘦身计划](slimming-plan.md)。

## 删除内容

- 托盘菜单、通知、状态图标、托盘线程和事件循环。
- `system_tray` 配置字段、默认值、解析、Web 设置和英文说明。
- `third-party/tray` 子模块和 Qt Widgets/Svg、静态 Qt 探测与链接。
- 托盘专属测试、15 个状态图标、截图生成和发布 CI。
- 对应的系统包依赖、AppImage Qt 插件、macOS Qt 部署步骤及 Flatpak 状态通知权限。

主线程直接等待已有的 shutdown 事件。Web 中仍使用的 `notify_pre_releases` 保留。普通应用图标、其余 Web 管理、UPnP 和跨平台支持保持当前范围。

## 验证

- 修改前基线与修改后 Linux ARM64 主程序构建均通过。
- 修改后 Web 生产构建通过。
- 两个版本分别使用临时 HOME、配置、凭据、空应用列表和独立回环端口运行；主机信息接口正常响应，SIGTERM 与 SIGINT 都以退出码 0 结束。修改后约 0.01 秒退出。
- 已检查构建、脚本、打包与 CI 中的 tray/Qt 残留引用；检查 Shell、YAML、JSON 语法及 Git 空白错误。
- C++ 回归测试：11 个测试套件、50 项全部通过，涵盖控制包与分包、主机名和地址处理、URL 处理、证书认证与持久化、配置和语言列表一致性。首次测试发现误删的 Input 文档标题，恢复后全部通过。结果保存在构建目录 `slimming-regression.xml`。

未验证 Windows/macOS 实际构建或安装包，也未进行硬件串流验收。此次没有部署、重启现有服务或修改系统配置。

## 本机验证环境

构建目录为 `cmake-build-slimming-baseline`。使用远程机器已有的 Homebrew GCC 16、CMake、Ninja 和 Node；通过系统 pkg-config 查找 Debian 开发库。基线关闭托盘和 GPU/Wayland 后端，仅启用 X11，因此不以二进制大小声称 Qt 瘦身收益。

系统 libva 版本低于预编译 FFmpeg 的要求，验证使用 libva 2.23.0，编译安装在上述构建目录的 `validation-prefix`，未替换系统库。`system-headers.cmake` 仅用于协调 Homebrew 工具链和系统开发头文件；ICU 使用系统头文件及匹配的系统库。测试目标使用 `-O0`，生产目标仍使用 Release。具体选项保存在该目录 `CMakeCache.txt` 和 `system-headers.cmake`。

```sh
export PATH=/home/linuxbrew/.linuxbrew/bin:$PATH
cmake --build cmake-build-slimming-baseline --target sunshine --parallel 3
cmake --build cmake-build-slimming-baseline --target test_sunshine --parallel 3
SUNSHINE_ASSETS_DIR="$PWD/cmake-build-slimming-baseline" npm run build
python3 cmake-build-slimming-baseline/validation-shutdown.py sunshine
python3 cmake-build-slimming-baseline/validation-regression.py
```

运行此验证产物时，动态库搜索路径需依次包含构建目录的 `validation-prefix/lib`、Homebrew 的 `opt/glibc/lib`、`lib/gcc/current`、`lib` 和系统 `/usr/lib/aarch64-linux-gnu`。这些是当前机器的验证环境设置，不是新增产品限制或部署方案。
