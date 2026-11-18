# Offline use and build instructions / 离线使用与编译

This fork of [wh201906/Proxmark3GUI](https://github.com/wh201906/Proxmark3GUI) starts from upstream commit `b74ad1b0de83af93523ce6fbe766ad7a274d840e`. This guide describes source behavior and its boundaries; it does not declare that every device operation has been tested. See [MIGRATION_STATUS.md](MIGRATION_STATUS.md) for feature status.

## English

### What offline means here

The application is a native Qt interface for a user-selected local Proxmark3 executable. The GUI does not require a seller login, call a seller analysis service, upload card data, download a backend, or perform an automatic online update. Its interface resources are local. Local file editing and dictionary management do not require a connected device.

The boundary is implemented at the GUI's process-launch and command-write points. The backend starts directly through `QProcess`, using a local serial port and the fixed argument form supported by this fork. Shell wrappers, environment scripts, network connection arguments, and unreviewed command paths are not accepted. Substituted command templates are checked again before they reach the client.

**The GUI is not a network sandbox.** A native backend, a library it loads, or another program can act independently of GUI validation. Local path checks cannot prove that every storage layer is physically local. If no process may send traffic, disconnect networking or apply an operating-system network policy covering the GUI and backend. A valid path, executable header, or displayed hash is not a malware verdict.

### First run and backend selection

1. Build this source or obtain the GUI-only artifact from a successful run of this fork's workflow. Keep the ZIP, its SHA-256 file, and the producing commit together. Extract the whole portable folder so that the Qt plugins remain next to the application.
2. Start the GUI. A backend is only needed for device operations; no seller account is requested.
3. Use **Choose local client** to select a local native Proxmark3 client from a source you trust. Select the executable itself, not a batch file, shell script, shortcut, URL, or network share. This explicit selection records its current SHA-256 in local settings. Local file and format checks do not certify its origin.
4. Open **Client details** to see its path, current and recorded SHA-256, profile, and data directories. The GUI compares the executable with its recorded hash before each launch and reconnect; a missing or changed hash blocks launch until the file is explicitly selected again. Compare the initial hash with an independently trusted source or your own reviewed build record. This detects changes to the executable only; it does not authenticate its origin or pin its DLLs, scripts, configuration, firmware, or other dependencies.
5. Select the device's local serial port. Use a client and device firmware built from a matching source revision, then verify the GUI's command templates and output parsing against that specific pair.

This repository does not name an RRG version as validated with this fork. A release number in an inherited JSON configuration identifies a historical command profile, not a compatibility certificate. A client may launch successfully while individual GUI operations remain incompatible.

The [RRG repository](https://github.com/RfidResearchGroup/proxmark3) is the primary source for client and firmware build instructions. Downloading tools or source is preparation, distinct from runtime use. CI obtains public build dependencies and does not contact a seller service.

### Local data and dictionaries

The GUI automatically uses Qt's `QStandardPaths::AppLocalDataLocation`, with organization `colorfulmeans` and application `Proxmark3GUIOffline`. Check **Client details** for the actual path. A typical Windows path is `%LOCALAPPDATA%\colorfulmeans\Proxmark3GUIOffline`; Linux uses the user's local application-data location. The GUI does not use the executable folder or source checkout as its data directory.

| Local item | Purpose |
| --- | --- |
| `GUIsettings.ini` | GUI preferences, selected paths, and recorded executable hashes |
| `workspace/` | GUI/client working directory and temporary dictionary copies |
| `dictionaries/` | Imported and normalized local dictionaries |
| `logs/` | Default destination offered by **Save session log** |

Settings, output, and imported resources can contain personal data. The Unix implementation restricts these directories to their owner (`0700`); Windows uses the user's application-data ACLs. Keep the directory separate from shared or synchronized folders. **Save session log** writes the current displayed client output to a user-selected local UTF-8 text file; no log is uploaded.

The dictionary manager imports, lists, previews, exports, and selects user-supplied text dictionaries. Each non-comment entry must be exactly 12 hexadecimal characters. Blank lines and `#`, `;`, or `//` comments, including trailing comments, are accepted. Import normalizes entries to uppercase and removes duplicates while preserving first occurrence. Any invalid line rejects the whole import and reports its line number. Files larger than 32 MiB are rejected; previews show at most 500 entries. Saving is atomic, and an existing dictionary of the same name is not silently overwritten.

No user dictionary or card data is distributed with this fork. Import checks text format; it does not establish that a dictionary is valid for any device. Export writes to the user-selected local file. Known URL, network-share, device-path, and symlink cases are rejected; system-level storage and network isolation remain separate concerns.

Use **Local dictionaries** to import and select a dictionary. **Clear dictionary selection** restores the client's built-in default behavior. The selected dictionary can be supplied to the existing check operation only when the selected profile declares a reviewed dictionary option. Among the bundled profiles, only `config_rrgv4.16717.json` has this option (`-f`), checked against that upstream client's source. Other profiles refuse the operation while a local dictionary is selected. This limited command-option review is not a claim that the entire RRG release or hardware behavior has been validated.

Before use, the GUI revalidates the dictionary and writes a copy into `workspace/` under a random ASCII filename, passed to the backend as a relative name. The copy is retained while the client may still be reading it and is removed when the client stops or the window closes normally. A crash can leave a local temporary copy. Local tests cover Unicode filenames; Windows-specific mapped-drive checks and dictionary use with real hardware still need target-platform verification.

Common build directories, runtime workspaces, logs, dumps, keys, and separately supplied backend directories are ignored by Git. `.gitignore` is a convenience, not a privacy guarantee: unusual filenames or already tracked files can still be committed. Review staged changes before sharing a repository or support bundle. Do not attach user data to CI artifacts.

### Optional manual Windows outbound rules

The source includes [scripts/Set-OfflineFirewall.ps1](scripts/Set-OfflineFirewall.ps1). The GUI never runs it. To use it, close the GUI and client, review the script, then run it yourself in an administrator PowerShell session, supplying your actual local paths. The examples show a preview first:

```powershell
.\scripts\Set-OfflineFirewall.ps1 -GuiPath 'C:\PM3GUI\Proxmark3GUI.exe' -ClientPath 'C:\PM3Client\proxmark3.exe' -WhatIf
.\scripts\Set-OfflineFirewall.ps1 -GuiPath 'C:\PM3GUI\Proxmark3GUI.exe' -ClientPath 'C:\PM3Client\proxmark3.exe'
```

The script accepts only the named GUI and standard `proxmark3.exe` client as existing local PE executables. It refuses unknown helper names, scripts, network drives, network/device paths, and reparse points. Filename and file-header checks do not establish trust. It creates outbound block rules for those two paths under the dedicated group `Proxmark3GUIOffline.LocalOnly`, covering all profiles. It does not launch either program, download anything, change antivirus settings, or change the global firewall policy. Re-running it replaces only its own rules for the same paths. Use `-Remove` with the same two absolute paths to remove those rules; the files need not still exist.

These are rules for the **two specified executable paths**, not their child processes, separate helpers, or services. Unknown helpers are not automatically added. Windows Firewall must be enabled, and enterprise policy may prevent local rules from taking effect. Inspect the effective policy before relying on it; this script does not prove complete network isolation. Moving an executable requires new rules for the new path. The script has not been executed or verified on Windows as part of the current local Linux validation.

The commands follow Microsoft's [New-NetFirewallRule documentation](https://learn.microsoft.com/en-us/powershell/module/netsecurity/new-netfirewallrule) and [PowerShell requirements](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.core/about/about_requires?view=powershell-5.1). No execution-policy bypass is included.

### Commands and migration scope

Only exact command paths on the reviewed allowlist are admitted. Abbreviations, aliases, scripts, shell execution, network transports, multi-command input, and unknown commands are rejected. Input also has a length limit and cannot contain embedded line terminators. Manual commands and commands produced by GUI buttons use the same boundary.

The allowlist covers this repository's inherited profiles, not every command of a current RRG client. A new command may be refused even if it performs a local operation. Add support only after reviewing its implementation and updating the relevant parser, templates, and tests together. Do not remove the final command validation to make an unreviewed profile work.

These restrictions do not make every retained device operation read-only, validate every command option, or prove the behavior of a changed backend. The separate client interprets its own commands and data files.

Seller authentication, proprietary cloud analysis, cloud dictionary lookup, remote update, and seller executable features are not migrated. No claim is made that proprietary analysis results have been recreated locally. Retaining upstream editing and device interfaces does not establish feature parity with a seller GUI.

### Build and tests

#### Linux

The workflow uses Ubuntu 22.04 with Qt 5 development packages. From the repository root:

```sh
sudo apt-get update
sudo apt-get install --no-install-recommends -y \
  build-essential qtbase5-dev qtbase5-dev-tools qt5-qmake \
  qttools5-dev-tools libqt5serialport5-dev
mkdir -p build-gui
cd build-gui
qmake ../src/Proxmark3GUI.pro CONFIG+=release CONFIG-=debug
make -j2
```

A Qt 5 SDK installed in your user directory is also suitable. Qt Core, Gui, Widgets, SerialPort, and Test are required. The repository includes compiled translation resources; translation regeneration is a separate maintenance step.

Run all QtTest projects from the repository root with Bash:

```sh
set -euo pipefail
export QT_QPA_PLATFORM=offscreen
repo="$PWD"
shopt -s nullglob
projects=("$repo"/tests/*/*.pro)
test "${#projects[@]}" -gt 0
for project in "${projects[@]}"; do
  name="$(basename "$project" .pro)"
  mkdir -p "$repo/build-tests/$name"
  (
    cd "$repo/build-tests/$name"
    qmake "$project" CONFIG+=release CONFIG-=debug
    make -j2
    make check
  )
done
```

#### Windows x64

The workflow selects Qt 5.15.2 `win64_mingw81` and MinGW 8.1. In a shell with those toolchain `bin` directories on `PATH`, create a separate build directory and run:

```text
qmake ../src/Proxmark3GUI.pro CONFIG+=release CONFIG-=debug CONFIG-=debug_and_release
mingw32-make -j2
```

Build each `tests/*/*.pro` in its own directory with the same qmake configuration. From that test build directory, use PowerShell to run the single `tst_*.exe` by its full path and check its exit code:

```powershell
$env:QT_QPA_PLATFORM = 'windows'
$testExecutables = @(Get-ChildItem -LiteralPath (Get-Location).Path -File -Filter 'tst_*.exe')
if ($testExecutables.Count -ne 1) {
    throw "Expected exactly one tst_*.exe; found $($testExecutables.Count)"
}
$testExecutable = $testExecutables[0].FullName
& $testExecutable
if ($LASTEXITCODE -ne 0) { throw 'QtTest failed' }
```

The workflow discovers test projects automatically, requires exactly one matching executable at the root of each build directory, and runs every test executable this way. This avoids relying on qmake's shell wrapper to find a Windows executable. Windows tests use the native `windows` platform plugin included in the portable package; Linux headless tests use `offscreen` and `make check`. The Windows job prints a text result log for each project, collects failures across projects, and refuses packaging if any project fails.

For packaging, the workflow copies the release GUI into a clean staging directory and runs the selected SDK's `windeployqt --compiler-runtime --no-translations`. Qt 5.15.2 uses automatic plugin matching for MinGW because its PE debug/release detection is unreliable; forcing `--release` can exclude the official SDK's unstripped `qwindows.dll`. The application remains a release build, and deployment must succeed with `platforms/qwindows.dll` present. The result includes Qt/MinGW runtime dependencies, this guide, migration status, and build metadata. It creates `Proxmark3GUI-offline-windows-x64-GUI-only.zip` plus a SHA-256 file. No seller directory, client executable, firmware, imported dictionary, or user workspace is copied. The GUI cannot operate hardware without a separately supplied compatible client.

Both jobs are in [.github/workflows/build.yml](.github/workflows/build.yml). Third-party actions are pinned to commit IDs; repository permission is read-only. No seller account or service secret is needed. Public dependency downloads occur during CI. The presence of a workflow file is not evidence of a successful remote build.

### Verification limits

QtTest covers local input and resource logic. Compilation checks C++/Qt integration. Neither is a hardware test, an audit of an external executable, an antivirus certification, or proof of zero traffic from the whole system. Evaluate actual Windows packaging and physical-device evidence before declaring a tested release. Build and test jobs do not flash firmware.

## 简体中文

### 这里的“离线”指什么

本分支使用原生 Qt 界面，通过用户指定的本地 Proxmark3 客户端连接设备。GUI 不需要卖家账号，不调用卖家分析服务，不上传卡片数据，不自动下载后端，也不进行在线自动更新；界面资源随程序提供。本地文件编辑与字典管理不需要连接设备。

离线约束落实在启动客户端和写入命令的边界：使用 `QProcess` 直接启动本地原生程序，仅接受本地串口和固定启动参数；不接受 shell 包装器、环境脚本、网络连接参数或未经审核的命令路径。模板替换后的最终命令会再次检查。

**这不是系统级禁网沙箱。** 外部原生客户端、它加载的库或其他程序仍可能自行操作。路径校验也不能证明底层存储一定物理离线。如要求所有进程都不能向外发包，需要断开网络，或使用覆盖 GUI 与后端的操作系统网络策略。可执行文件格式、路径检查和哈希展示均不等于查杀结论或可信认证。

### 首次使用

1. 编译本分支，或取得本分支 CI 成功运行生成的 GUI-only 压缩包。保留压缩包、SHA-256 文件及对应提交号。完整解压目录，保留旁边的 Qt 运行库与插件。
2. 启动 GUI。只有设备操作需要后端，不需要输入卖家账号。
3. 使用**选择本地客户端**按钮，选择来源可信的本地 Proxmark3 原生客户端。选择可执行文件本身，不要选择批处理、shell 脚本、快捷方式、网址或网络共享。此次手动选择会在本地设置中记录其当前 SHA-256。路径与格式检查不认证来源。
4. 打开**客户端详情**，查看路径、当前与已记录的 SHA-256、命令配置和数据目录。每次启动及重连前都会核对文件，缺少记录或哈希变化时会拒绝启动，需要再次手动选择。初次记录的哈希应与独立可信来源或自己的审核构建记录比对。这只检测可执行文件的变化，不认证来源，也不固定 DLL、脚本、配置、固件或其他依赖。
5. 选择本地串口。客户端和设备固件应来自相匹配的源码版本，再逐项核对该版本下 GUI 的命令模板和输出解析。

本分支尚未声明某个 RRG 版本已完成适配验证。继承的 JSON 配置中出现的版本号只代表历史命令配置，客户端能启动不代表每个按钮都兼容；实际硬件验证仍需完成。[RRG 源码仓库](https://github.com/RfidResearchGroup/proxmark3)提供客户端、固件和构建说明。准备工具链、下载源码与 GUI 日常运行是不同阶段：CI 会下载公开构建依赖，不访问卖家服务。

### 本地数据与字典

程序自动使用 Qt 的 `QStandardPaths::AppLocalDataLocation`，组织名为 `colorfulmeans`，应用名为 `Proxmark3GUIOffline`。实际位置可在**客户端详情**中查看；Windows 通常为 `%LOCALAPPDATA%\colorfulmeans\Proxmark3GUIOffline`，Linux 使用当前用户的本地应用数据位置，不以程序目录或源码仓库作为数据目录。

| 本地项目 | 用途 |
| --- | --- |
| `GUIsettings.ini` | GUI 设置、所选路径、已记录的可执行文件哈希 |
| `workspace/` | GUI 与客户端工作目录、字典暂存副本 |
| `dictionaries/` | 导入后规范化的本地字典 |
| `logs/` | **保存会话日志**默认提供的保存位置 |

设置、输出和资源可能包含个人数据。Unix 实现将这些目录权限限制为所有者访问（`0700`）；Windows 遵循用户应用数据目录的 ACL。应与共享目录或云同步文件夹分开保存。**保存会话日志**将当前显示的客户端输出写入用户指定的本地 UTF-8 文本文件，不上传日志。

字典管理支持导入、列表、预览、导出和选用用户自己的文本字典。每条有效记录必须为 12 个十六进制字符；支持空行及 `#`、`;`、`//` 开头或行尾的注释。导入时统一大写，按首次出现顺序去重；任意非法行都会拒绝整次导入，并报告行号。文件上限 32 MiB，预览最多 500 条；文件原子保存，同名资源不会静默覆盖。

本分支不附带用户字典或卡片数据。导入成功只表示文本格式通过检查，不代表内容对任何设备有效。导出写入用户指定的本地文件。已知网址、网络共享、设备路径和符号链接情况会被拒绝；系统存储与网络隔离需要另外落实。

通过**本地字典**导入并选用字典；**清除字典选择**恢复客户端内置默认行为。仅当命令配置声明了经核实的字典选项时，现有检查操作才接受所选字典。内置配置中只有 `config_rrgv4.16717.json` 增加了对应的 `-f` 选项，并核对了该客户端版本的上游源码。其他配置在选中字典时会拒绝该操作。这项参数核对不等于整个 RRG 版本或硬件行为已经验证。

调用前会重新检查字典，在 `workspace/` 中写入随机 ASCII 文件名的副本，并将相对文件名交给后端。客户端可能尚未读完文件时会保留副本；客户端停止或窗口正常关闭时清理。异常崩溃可能留下本地暂存副本。本地测试覆盖 Unicode 文件名；Windows 映射盘检查及真实设备上的字典使用仍需验证。

仓库忽略常见构建目录、工作目录、日志、dump、密钥和外部后端目录，但 `.gitignore` 不能代替隐私检查：特殊命名或已被 Git 跟踪的文件仍可能被提交。分享代码或问题附件前检查暂存内容，CI 制品中不要放入用户数据。

### 可选的手动 Windows 出站规则

仓库附带 [scripts/Set-OfflineFirewall.ps1](scripts/Set-OfflineFirewall.ps1)，GUI 不会调用它。需要时，关闭 GUI 和客户端、审阅脚本，再自行在管理员 PowerShell 中执行上文示例，替换为真实的本地路径。先加 `-WhatIf` 可预览操作；去掉该参数后才修改规则。

脚本仅接受名为 `Proxmark3GUI.exe` 与标准 `proxmark3.exe` 的本地 PE 可执行文件，拒绝未知 helper 名称、脚本、网络盘、网络或设备路径及重解析点。名称与文件头检查不是来源认证。脚本在专用规则组 `Proxmark3GUIOffline.LocalOnly` 下，对这两个路径配置覆盖所有配置文件的出站阻断规则；不启动程序、不下载文件、不更改杀毒设置或全局防火墙策略。再次运行只替换相同路径下它自己的规则；使用相同绝对路径并加 `-Remove` 可删除这些规则，此时文件无需仍然存在。

**规则只覆盖指定的两个可执行文件路径**，不覆盖它们启动的子进程、其他 helper 或服务，也不会自动加入未知 helper。Windows 防火墙必须启用，企业策略也可能阻止本地规则生效；应查看实际有效策略后再依赖这些规则，不能将脚本等同于完整网络沙箱。移动程序后需为新路径建立规则。当前本地 Linux 验证没有执行该脚本，也没有验证 Windows 上的实际阻断效果；不提供绕过 PowerShell 执行策略的命令。

### 命令限制与迁移范围

仅允许经过审核、逐级精确匹配的命令路径。缩写、别名、脚本、shell 执行、网络传输、多命令输入和未知命令均被拒绝；命令还受长度和换行规则限制。手动输入与 GUI 按钮使用同一边界。

白名单覆盖本仓库继承的命令配置，并不等于当前 RRG 所有命令的列表。新版新增的本地命令也可能被拒绝。增加支持时，应共同审核客户端命令实现、配置、解析和测试，不能删除最终校验来适配未知配置。

这些限制不意味着全部设备操作变为只读，也不会验证每一个命令选项，更不能证明被替换的客户端会如何执行。客户端仍负责解释自身命令与数据文件。

卖家登录、专有云分析、云端字典查询、远程更新及卖家二进制功能不属于已迁移部分。本分支没有宣称本地重建卖家云服务的分析结果，也没有宣称与卖家软件全部功能等价。完整状态见 [MIGRATION_STATUS.md](MIGRATION_STATUS.md)。

### 编译与验证范围

Linux 和 Windows 编译命令见上面的 [Build and tests](#build-and-tests)。CI 使用 Ubuntu 22.04 / Qt 5 与 Windows x64 / Qt 5.15.2 / MinGW 8.1，自动发现 `tests/*/*.pro` 并运行 QtTest。Windows 通过 `windeployqt` 生成便携 GUI-only 包，包含 Qt/MinGW 运行依赖、说明文档、构建信息和 SHA-256；不包含客户端、固件、卖家文件或用户数据。

Windows 下，每个测试工程分别编译后，在该构建目录根部查找 `tst_*.exe`，必须恰好找到一个。按上面的 PowerShell 示例，用调用运算符 `&` 和完整路径直接运行，并检查 `$LASTEXITCODE`；不依赖 qmake 生成的 shell 包装器查找程序。所有测试照常执行，Linux 仍使用 `make check`。

QtTest 验证本地校验与资源逻辑，编译验证 C++/Qt 集成；二者都不能代替物理设备测试、外部二进制审计、杀毒软件认证或全系统零出网验证。仓库中存在 workflow 文件不代表远程 CI 已经成功运行。发布前应以实际 Windows 制品、客户端版本与硬件验证记录为准。构建及测试不会刷写设备固件。
