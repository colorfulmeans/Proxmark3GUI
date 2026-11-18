# Feature migration status / 功能迁移状态

This offline fork of `wh201906/Proxmark3GUI` starts from commit `b74ad1b0de83af93523ce6fbe766ad7a274d840e`. It is an open-source replacement interface, not a modified seller executable. “Implemented” describes source behavior. A successful build is separate evidence; device and firmware compatibility need their own test record.

本分支在开源 GUI 上实现离线使用边界，不是修改卖家可执行文件。“已实现”表示源码中已实现，不自动代表 Windows 制品、某版客户端或物理设备已经验证。

| Area / 功能 | Source status / 源码状态 | Scope and remaining work / 范围与待验证项 |
| --- | --- | --- |
| Seller account and validation / 卖家账号与联网验证 | Not required / 不需要 | No seller authentication integration or credentials. / 不接入卖家认证，不保存卖家凭据。 |
| Native GUI startup / 原生 GUI 启动 | Implemented / 已实现 | Local Qt resources; no remote UI. / 本地 Qt 资源，无需远程界面服务。 |
| Backend selection and fingerprint / 后端选择与指纹 | Implemented / 已实现 | Explicit selection records SHA-256; launch/reconnect refuses a changed executable. No origin or DLL authentication. / 手动选择记录 SHA-256，启动及重连拒绝已变化的程序；不认证来源或 DLL。 |
| Process and command boundary / 进程与命令边界 | Implemented / 已实现 | Direct process launch, local port, fixed launch form, exact command allowlist; not an OS sandbox. / 直接启动、本地串口、固定启动形式和精确命令白名单；不是系统沙箱。 |
| Local workspace / 本地工作目录 | Implemented / 已实现 | Per-user AppLocalDataLocation with settings/workspace/dictionaries/logs; Unix directories are owner-only. / 用户应用数据目录中分别保存设置、工作区、字典和日志；Unix 目录仅所有者可访问。 |
| Dictionary management / 字典管理 | Implemented / 已实现 | User text import/export, validation, deduplication, preview and selection; no cloud lookup. / 用户文本导入导出、校验、去重、预览、选用；不查询云字典。 |
| Selected dictionary to client / 所选字典交给客户端 | Limited profile integration / 限定配置接入 | Only the bundled RRG v4.16717 profile declares a source-reviewed dictionary option; other profiles refuse while a dictionary is selected. Hardware validation pending. / 内置配置仅 RRG v4.16717 增加已核源码的字典选项，其他配置在选中字典时拒绝执行；硬件待验证。 |
| Session log export / 会话日志导出 | Implemented / 已实现 | User-triggered local UTF-8 file export; no upload. / 用户主动导出本地 UTF-8 文件，不上传。 |
| Simplified Chinese UI / 简体中文界面 | New translated strings supplied / 已提供新增翻译 | 104 added translatable messages in both language catalogs; policy/pre-translation startup errors remain English. / 两份语言表覆盖 104 条新增可翻译文本，policy 与加载语言前的启动错误仍为英文。 |
| File and block-data editing / 文件与块数据编辑 | Inherited / 继承上游 | Existing interfaces retained; local file use can be checked without hardware. / 保留原有界面，本地文件操作可不连接硬件。 |
| Device tabs and output parsing / 设备页与输出解析 | Inherited; compatibility pending / 继承上游，兼容待验证 | HF/LF interfaces depend on allowed profiles and a matching client/firmware pair. / 高低频界面依赖允许的命令配置及匹配的客户端与固件。 |
| Manual client commands / 手动客户端命令 | Restricted / 受约束 | Reviewed exact paths only; unknown commands and abbreviations are rejected. / 仅允许审核过的精确命令，未知命令和缩写会被拒绝。 |
| Seller cloud analysis / 卖家云分析 | Not migrated / 未迁移 | No equivalent proprietary recognition, business rules, or remote database claimed. / 不宣称等价实现其专有识别、业务规则或服务器数据库。 |
| Seller programs and updater / 卖家程序与更新器 | Excluded / 不包含 | No seller binary import, executable download, or automatic update path. / 不导入卖家程序，不自动下载可执行文件或更新。 |
| Windows outbound-rule helper / Windows 出站规则脚本 | Manual script only; not executed / 仅手动脚本，未执行 | Two explicit executable paths, dedicated rule group; no child-process coverage or Windows enforcement claim. / 两个明确程序路径和专用规则组，不覆盖子进程，不宣称已验证 Windows 阻断。 |
| Linux and Windows builds / Linux 与 Windows 编译 | Workflow configured / 已配置工作流 | Qt 5 builds and discovered QtTest projects; actual runs must be checked separately. / Qt 5 编译及自动发现的 QtTest，实际运行结果需单独核对。 |
| Windows portable package / Windows 便携包 | GUI-only packaging configured / 已配置纯 GUI 打包 | Qt/MinGW runtime, docs and hash; no client/firmware. Requires a successful Windows job. / 运行依赖、文档及哈希；无客户端或固件，须以实际构建为准。 |
| Firmware flashing / 固件刷写 | Not provided by this workflow / 本工作流不提供 | CI and unit tests never access or flash a device. / CI 与单元测试不会连接或刷写设备。 |
| Verified RRG release / 已验证的 RRG 版本 | Not established / 尚未建立 | Historical profile labels are not a compatibility matrix. / 历史配置版本号不是适配验证表。 |
| Physical hardware acceptance / 实物验收 | Pending / 待验证 | Record hardware, backend commit/hash, firmware commit, and observed results. / 应记录硬件、后端提交与哈希、固件提交及实际结果。 |
| macOS release / macOS 发布 | Not validated / 未验证 | No macOS CI job or device evidence. / 尚无 macOS CI 或硬件验证证据。 |

## Local validation record / 本地验证记录

Validated locally on Linux with Qt 5.15.2 on 2026-11-18:

- The GUI compiled successfully.
- Four QtTest projects (`offlinepolicy`, `localdictionary`, `processboundary`, and `guismoke`) completed with **196 passed** results in total.
- Process-boundary tests exercised the actual `PM3Process`/`Util` integration with a native synthetic fixture. GUI tests exercised startup, local resource selection, and dictionary staging/cleanup. No seller executable or real Proxmark3 device was used.
- Both translation catalogs include 104 added translated messages; the pre-existing translation text and placeholders were checked for preservation. Both `.qm` files were regenerated with Qt 5 tools.
- The Windows workflow has not run remotely. Windows portable execution, firewall-rule enforcement, and physical-device compatibility remain unverified.

2026-11-18 在本地 Linux / Qt 5.15.2 环境完成验证：GUI 编译成功；四个 QtTest 工程合计 **196 项通过**。进程测试使用实际 `PM3Process`/`Util` 与原生模拟测试程序，GUI 测试覆盖启动、本地资源选用及字典暂存与清理，没有运行卖家程序，也没有使用真实 PM3 硬件。两份语言表新增 104 条翻译，核对保留既有译文和占位符，并重新生成 `.qm`。Windows 工作流尚未远程运行，Windows 制品运行、防火墙实际阻断及物理设备兼容性仍未验证。

## What tests establish / 测试能证明什么

Unit tests exercise the input policy and dictionary parser with local test data. They do not inspect a separately supplied backend or reproduce seller server results. Build success proves that the selected compiler and Qt SDK accept the source; it does not establish command compatibility. Inspect and launch a portable archive on its target platform before calling it a tested release.

单元测试使用本地测试数据验证输入规则与字典解析，不审计用户自行选择的后端，也不重现卖家服务器结果。编译成功说明源码能被对应编译器和 Qt SDK 接受，不代表客户端命令全部兼容。便携包应在目标系统检查并启动后，才能称为已测试发布版。

See [OFFLINE.md](OFFLINE.md) for operating boundaries, builds, and data handling.
