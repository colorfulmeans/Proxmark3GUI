# Proxmark3GUI — 离线分支

本分支基于 [wh201906/Proxmark3GUI](https://github.com/wh201906/Proxmark3GUI) 的原生 Qt 界面，增加本地资源管理和明确的本地后端调用边界。无需卖家账号，不接入卖家服务或云端卡片分析，不包含卖家可执行文件。

**当前状态：已实现源码改造，物理设备兼容性仍待验证。** Windows 工作流生成包含 Qt 运行依赖的 **GUI-only 便携包**，不包含 Proxmark3 客户端或固件。编译成功或单元测试通过，不等于某个设备、固件或 RRG 版本已经适配完成。

本地 Linux / Qt 5.15.2 已完成 GUI 编译，四个 QtTest 工程合计 **196 项通过**。Windows 工作流尚未远程运行，其打包产物和 Windows 上的实际运行仍待验证。

- [离线使用与编译说明（中英双语）](../../OFFLINE.md)
- [功能迁移状态表](../../MIGRATION_STATUS.md)
- [Windows Qt 5.15.2 与 Linux Qt 5 编译测试工作流](../../.github/workflows/build.yml)
- [English](../../README.md)

GUI 只接受经过审核的本地客户端命令子集。这不是操作系统级禁网沙箱，用户选择的原生客户端也必须来源可信。本分支没有宣称与卖家软件全部功能等价，也没有宣称某个当前 RRG 版本已验证适配。

工作流配置为测试成功后生成 `Proxmark3GUI-offline-windows-x64-GUI-only.zip` 及 SHA-256 文件。使用前检查实际运行结果和提交号；存在工作流文件不代表远程构建已经成功。

保留上游 [LGPL-2.1 许可证](../../LICENSE) 与原有署名。本分支与硬件卖家及 RfidResearchGroup 无隶属关系。

## 上游历史说明

下方保留原始 README，供了解项目历史和界面。其下载链接、下载量、附带客户端的发布说明、构建命令、截图和教程描述的是上游历史发布版本，不代表本分支的产物，也不覆盖 [OFFLINE.md](../../OFFLINE.md) 的说明。下载上游 release 不会获得本分支改动；旧教程中的部分环境脚本设置在此分支不可用。

[![downloads](https://img.shields.io/github/downloads/wh201906/Proxmark3GUI/total?label=GitHub%E4%B8%8B%E8%BD%BD%E9%87%8F)](https://github.com/wh201906/Proxmark3GUI/releases)  
[![downloads](https://img.shields.io/sourceforge/dt/proxmark3gui.svg?label=SourceForge%E4%B8%8B%E8%BD%BD%E9%87%8F)](https://sourceforge.net/projects/proxmark3gui/)  

一个自制的[Proxmark3](https://github.com/Proxmark/proxmark3) GUI，可在Windows/Linux/macOS系统下运行，支持[官方版](https://github.com/Proxmark/proxmark3)/[冰人版](https://github.com/RfidResearchGroup/proxmark3)客户端和固件  

(目前[官方版](https://github.com/Proxmark/proxmark3)已停更超过一年，推荐使用[冰人版](https://github.com/RfidResearchGroup/proxmark3)固件和客户端)

[English](../../README.md)

***

## 功能

+ 快速查找可用端口并连接
+ 支持直接输入PM3命令(官方版/冰人版)
+ 有针对于Mifare卡（IC卡）的图形界面
    + 支持不同大小的卡片(MINI, 1K, 2K, 4K)
    + 支持编辑Mifare扇区数据
    + 支持读取全卡/选中扇区，读卡逻辑更加智能
    + 支持写入全卡/选中扇区
    + 可以打开二进制/文本格式的扇区数据文件
    + 分析访问控制位（Access Bits）
    + 支持UID卡操作（UID快速读写，UFUID锁卡）
+ 支持部分低频命令
+ 自定义UI界面，各选项卡可拆分组合
+ ...

***

## 预览图
![preview](preview_zh_CN.png)  

[更多预览](../preview/previews.md)  

***

## 关于预编译Windows客户端

一位国外大佬 [Gator96100](https://github.com/Gator96100) 做了个 [ProxSpace](https://github.com/Gator96100/ProxSpace) 以便在Windows平台上编译PM3固件和客户端，他还把自己编译好的客户端放到了[网站](https://www.proxmarkbuilds.org/)上  
release页面中有含客户端的GUI。这个GUI也可以搭配你自己的客户端使用  
(本来打算在CSDN下载里面放几个最新版客户端的，结果不能把下载币改为0)  
感谢大佬  

***

## 下载Windows版本
[release](https://github.com/wh201906/Proxmark3GUI/releases) 页面当中包含了编译好的Windows程序，解压后即可使用  
`Vx.x.x-win64.7z`是不带客户端的纯GUI程序，可配合已有的客户端使用  
`Vx.x.x-win64-xxxxxxx.7z`包含了对应的客户端，打开`Vx.x.x-win64-xxxxxxx\GUI\Proxmark3GUI.exe`即可使用  

SourceForge平台上也可下载  
[![Download Proxmark3GUI](https://a.fsdn.com/con/app/sf-download-button)](https://sourceforge.net/projects/proxmark3gui/files/latest/download)  

## 在Linux系统下编译
```
cd ~
sudo apt-get update
sudo apt-get install git build-essential
sudo apt-get install qt5-default libqt5serialport5-dev 
git clone https://github.com/wh201906/Proxmark3GUI.git --depth=1
cd Proxmark3GUI
mkdir build && cd build
qmake ../src
make -j4 && make clean
./Proxmark3GUI
```

## 在macOS系统下编译
```
cd ~
brew update
brew install qt@5
brew link qt5 --force
git clone https://github.com/wh201906/Proxmark3GUI.git --depth=1
cd Proxmark3GUI
mkdir build && cd build
qmake ../src
make -j4 && make clean
open Proxmark3GUI.app
```

> 为使GUI在macOS下正确连接设备，需要更改一些设置

![macOS_settings](macOS_settings.png)

***
## 教程
[1.快速上手](../tutorial/Quickstart/quickstart_zh_CN.md)  
[2.使用ProxSpace编译的客户端](../tutorial/Work_With_ProxSpace/work_with_proxspace_zh_CN.md)  
[3.编辑Mifare(IC)卡数据](../tutorial/Edit_Mifare_Classic_data/Edit_Mifare_Classic_data_zh_CN.md)（无需PM3硬件）  
***

## 更新日志
[更新日志](../CHANGELOG/CHANGELOG_zh_CN.md)
