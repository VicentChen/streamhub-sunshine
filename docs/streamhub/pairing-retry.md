# 配对失败后的立即重试

Moonlight 的首次 getservercert 请求可能在等待 Web PIN 审批期间超时或被用户退出。此前 Sunshine 保留请求最多五分钟，同一 uniqueid 的再次请求无条件返回 409，用户无法立即重试。

现在相同 uniqueid、相同非空客户端证书和相同非空来源地址的新 getservercert 请求，会替换旧的未完成会话。旧的等待响应收到 superseded 错误；新的请求使用全新审批 ID、盐和握手状态，重新等待 Web PIN 审批。旧审批 ID 不能批准或取消新请求。已进入加密握手的未完成会话也可从头重试；已成功配对的持久记录不在此临时注册表中，不会因此删除。

来源地址或证书不一致的重复 uniqueid 仍然拒绝。此检查是重试范围限制，不代替 PIN 和证书握手验证。注册表仍限制最多 32 项，五分钟截止时间继续适用于没有重试的旧请求。这里只在收到新请求时替换，不声称断连后立即自动清理。

## 验证与部署（2026-09-15，中国标准时间）

板端 Sunshine 和 test_sunshine 构建成功，扩展后的隔离回归入口通过 143 项测试、31 个套件。新增覆盖：

- 不同握手阶段的同客户端重试、旧审批 ID 失效、新请求重新等待审批。
- 不同证书、空证书、不同地址、空地址不能顶替已有请求。
- 真实 HTTP 旧等待响应完成、新请求等待新的审批。
- 客户端请求一秒超时断开后，立即重试成功进入新审批。

测试入口为总仓库 tools/run.py --development -- python3 sunshine/tests/run-streamhub-tests.py --build sunshine/cmake-build-slimming-baseline --regression，特殊工具链通过 --runtime-library-path 提供运行库。日志位于总仓库 var/logs/pairing-retry-tests.log。

验证后重启 sunshine.service 应用修复，未改动用户管理凭据和已保存的配对。Moonlight 实机重试体验仍需用户确认；HTTP 自动测试已覆盖失败后再次请求的场景。
