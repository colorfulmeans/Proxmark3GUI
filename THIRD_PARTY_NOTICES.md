# Third-party notices

The GUI and its portable build use the components below. License texts are included in `LICENSE` and `LICENSES/`. This file records their source; it does not change their licenses.

| Component | Source used or identified | Notices |
| --- | --- | --- |
| Proxmark3GUI | [wh201906/Proxmark3GUI](https://github.com/wh201906/Proxmark3GUI), base commit `b74ad1b0de83af93523ce6fbe766ad7a274d840e` | Existing attribution and LGPL-2.1 text in `LICENSE` are retained. This repository contains the offline fork's modified source. |
| Qt | Windows workflow uses Qt 5.15.2; [QtBase](https://github.com/qt/qtbase/tree/v5.15.2) and [QtSerialPort](https://github.com/qt/qtserialport/tree/v5.15.2) | Dynamically linked Qt libraries. LGPL-3.0 and its referenced GPL-3.0 text are included under `LICENSES/`; individual Qt components retain their own notices. [Qt 5.15.2 source archive](https://download.qt.io/archive/qt/5.15/5.15.2/single/) is available from the Qt project. |
| QDarkStyleSheet | The inherited `src/qdarkstyle/README.md` identifies [commit 6ff5fdf](https://github.com/ColinDuquesnoy/QDarkStyleSheet/tree/6ff5fdf) | Copyright 2013–2019 Colin Duquesnoy and the contributors named in the included `QDarkStyleSheet-LICENSE.rst`. Its code is MIT-licensed and its images are CC BY 4.0. The resources were inherited from Proxmark3GUI. |
| GCC runtime | The Windows workflow selects the Qt-distributed MinGW 8.1 toolchain; [GCC 8.1.0 source](https://github.com/gcc-mirror/gcc/tree/releases/gcc-8.1.0) | GPL-3.0 text and GCC Runtime Library Exception 3.1 are included. Other MinGW components retain their own licenses; notices available in the installed toolchain are copied into the portable archive. |

The workflow records its source commit and toolchain in `BUILD-INFO.txt`. Keep these notices with redistributed builds and retain the complete corresponding source and build instructions for the exact revision being distributed. The source checkout and build workflow let users rebuild the application; Qt is deployed as replaceable shared libraries. The packaging job's selected DLLs and copied toolchain notices should be reviewed when changing SDK versions.

No Proxmark3 client, firmware, or seller software is part of the GUI-only archive. A separately supplied backend has its own source, version, dependencies, and license notices.

## 简体中文

本分支保留原项目许可证与署名，并随便携包提供 Qt、GCC 运行库及 QDarkStyleSheet 的相关许可证。Qt 使用动态库形式；源码仓库与工作流保留对应构建方式。`BUILD-INFO.txt` 记录实际构建提交和工具链。更新工具链版本时，需要核对实际打包的动态库及其附带声明。

纯 GUI 包不含 Proxmark3 客户端、固件或卖家软件；用户另行提供的后端具有自己的版本、来源与许可证。
