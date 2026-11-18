# Local validation record / 本地验证记录

Date: 2026-11-18. Base: `wh201906/Proxmark3GUI` at
`b74ad1b0de83af93523ce6fbe766ad7a274d840e`.

The complete GUI was compiled and linked on Linux using Qt 5.15.2, GCC and
C++11. This is a source development build, not a Windows release.

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

`git diff --check` passed. The Windows and Linux GitHub Actions definitions are
included, but remote CI had not run when this record was written. The PowerShell
firewall helper has not been executed on Windows. The workflow parses its syntax
without applying any firewall rules.

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

这份记录验证的是当前源码的本地行为。Windows 实际构建和运行、真实串口与
设备功能、杀毒软件判断及整个系统的禁止联网效果仍未验证。已经写入仓库的
CI 配置不等于远程构建成功；手动防火墙脚本也尚未在 Windows 执行。
