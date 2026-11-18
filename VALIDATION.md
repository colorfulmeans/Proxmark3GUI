# Validation records / 验证记录

Date: 2026-11-18. Base: `wh201906/Proxmark3GUI` at
`b74ad1b0de83af93523ce6fbe766ad7a274d840e`.

The following initial local record is preserved separately from the remote,
commit-specific results below. The complete GUI was compiled and linked on Linux
using Qt 5.15.2, GCC and C++11. This local build was not a Windows release.

| QtTest project | Passed | Failed | Skipped | Evidence covered |
| --- | ---: | ---: | ---: | --- |
| `tests/localdictionary` | 38 | 0 | 0 | Parsing, normalization, invalid input, atomic local file operations and path restrictions |
| `tests/offlinepolicy` | 136 | 0 | 0 | Command and argument boundaries, executable/local path validation and inherited configuration templates |
| `tests/processboundary` | 16 | 0 | 0 | Actual native fixture process, stdin boundary, split output, disconnects, reconnect digest checks and startup failures |
| `tests/guismoke` | 6 | 0 | 0 | Main window, dictionary selection and settings, staging cleanup, unsupported profile behavior and worker shutdown |
| **Total QtTest reported passes** | **196** | **0** | **0** | Counts include QtTest initialization and cleanup entries |

The process tests compile their own native C++ fixture. They do not start a
seller executable, use the PM3 backend, connect to hardware or access a network.
The GUI tests use isolated Qt test-data paths and do not launch a PM3 client.

`git diff --check` passed for that local validation. The PowerShell firewall
helper was not executed on Windows; the workflow checks its syntax without
applying any firewall rules.

## Remote Actions evidence

Repository: [colorfulmeans/Proxmark3GUI](https://github.com/colorfulmeans/Proxmark3GUI).
Commit: `c03d3b584bd31f1bf5fefc7a6cf9de266439cb60`.
Run: [44323970258](https://github.com/colorfulmeans/Proxmark3GUI/actions/runs/44323970258).

The [Linux job](https://github.com/colorfulmeans/Proxmark3GUI/actions/runs/44323970258/job/134870125668)
completed successfully. Its actual QtTest log totals were `guismoke` 6,
`localdictionary` 38, `offlinepolicy` 136, and `processboundary` 16: **196 passed,
0 failed, 0 skipped**. GUI compilation also succeeded.

The [Windows job](https://github.com/colorfulmeans/Proxmark3GUI/actions/runs/44323970258/job/134870126190)
compiled the GUI successfully, but its QtTest step failed. Packaging and artifact
upload were skipped, so that run produced no Windows GUI-only package. The overall
run conclusion was failure; Linux success is not a claim that the whole run passed.

These statements describe that exact commit and run. For a later revision, check
its matching SHA, each job, and any uploaded artifact in [Actions](https://github.com/colorfulmeans/Proxmark3GUI/actions).
Do not transfer an earlier commit's result to a newer build.

No physical device, firmware update, Windows serial connection, Windows runtime
package, Defender verdict or whole-system network isolation was verified by these
local tests. A GUI allowlist does not sandbox a separately selected native client
or its dependencies. See [OFFLINE.md](OFFLINE.md) for the implemented boundaries
and [MIGRATION_STATUS.md](MIGRATION_STATUS.md) for migration scope.

## 中文

完整 GUI 已在 Linux / Qt 5.15.2 环境下编译链接成功。上表四个 QtTest 项目
共报告 196 项通过、0 失败、0 跳过；数量包含测试框架的初始化与清理条目。
进程测试使用自行编译的原生测试程序，界面测试使用隔离的测试数据目录。
测试未运行卖家程序，未连接 PM3 硬件，也未刷写固件。

本地表格保留的是首次 Linux 验证结果，不自动代表后续提交。远程运行
[44323970258](https://github.com/colorfulmeans/Proxmark3GUI/actions/runs/44323970258)
对应提交 `c03d3b584bd31f1bf5fefc7a6cf9de266439cb60`：Linux GUI 编译及测试成功，
真实日志分别报告 6、38、136、16 项通过，合计 **196 通过、0 失败、0 跳过**。
Windows GUI 编译成功，但 QtTest 步骤失败，打包和上传被跳过，该次运行没有
Windows 纯 GUI 制品，整个运行的结论仍为失败。

后续版本需在 [Actions](https://github.com/colorfulmeans/Proxmark3GUI/actions) 核对
对应提交 SHA、各 job 与制品。上述证据不验证 Windows 便携包运行、真实串口和
设备功能、Defender 判断或整个系统的禁止联网效果；手动防火墙脚本未执行。
