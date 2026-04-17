# RK3588 Qt AI 标准作业手册

> 适用范围：仅适用于当前仓库 `Qt例程源码`、当前 RK3588 开发板方案、当前 Buildroot 交叉工具链、当前 Qt 5.15.10 SDK。
>
> 本手册的目标不是介绍 Qt 基础知识，而是让另一个 AI 代码工具在接手本仓库时，能够直接按照固定流程完成：
> - 判断应该进入哪个工程入口
> - 正确执行交叉编译
> - 正确验证产物是否面向 RK3588 ARM64
> - 避免进入错误目录或使用错误构建方式
> - 在需要新增/修改应用时沿用本仓库的真实开发策略

---

## 1. 结论先行

如果你是另一个 AI，只需要先记住下面 8 条：

1. 当前仓库真正的 RK3588 板级 Qt 应用开发入口是 `elf_qt/src`，不是仓库根目录下的同名示例目录。
2. 根目录下 `02_watchdog` 到 `11_spi` 这批目录不是完整可独立开发的板级工程副本，单独编译会缺公共库和公共配置。
3. 板级应用的正确编译顺序是：
   - 先编译 `elf_qt/src/libs/libs.pro`
   - 再编译目标应用的 `.pro`
4. 交叉编译必须使用当前 SDK 自带的 `qmake`，不能用宿主机默认的 `qmake`。
5. 当前已验证可用的 SDK 路径是 `/home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot`。
6. 当前已验证的 Qt 版本是 `Qt 5.15.10`，目标架构是 `ARM aarch64`。
7. 板级应用成功编译后的输出目录是 `elf_qt_out/release`，其中：
   - 可执行程序在 `elf_qt_out/release/bin`
   - 动态库在 `elf_qt_out/release/lib`
   - 插件在 `elf_qt_out/release/plugin`
8. 验证是否真的完成交叉编译，不能只看 `make` 是否成功，必须再用 `file` 检查产物是否是 `ARM aarch64`。

---

## 2. 当前项目的真实开发策略

### 2.1 开发模式

当前项目采用的是典型的“宿主机开发 + PC 端交叉编译 + 开发板运行验证”模式：

- 宿主机系统：`Ubuntu 22.04`
- IDE：`Qt Creator 4.7.0`
- 构建工具：`qmake + make`
- 交叉工具链：Buildroot 提供的 `aarch64-buildroot-linux-gnu`
- Qt SDK：当前交叉 SDK 内置 `Qt 5.15.10`
- 目标平台：`RK3588` 开发板，`ARM64 / aarch64`

这意味着：

- 代码主要在 PC 上编写和组织
- 编译不在板子上完成，而是在 PC 上完成交叉编译
- 板子主要用于运行、联调和硬件验证
- 构建系统的正确性依赖于 SDK 中的交叉编译器、sysroot、Qt 头文件和 Qt 库

### 2.2 当前仓库的开发组织方式

当前仓库存在两套看起来相似的目录：

- 仓库根目录下的示例目录
- `elf_qt/src` 下的完整工程目录

它们的角色不同：

#### A. 根目录下的简单独立示例

这些目录可以作为简单示例或最小 Qt 工程单独编译：

- `00_HelloWorld`
- `01_qt_test/01_qfile/test1`
- `01_qt_test/02_qtextstream/test2`
- `01_qt_test/03_qthread/test3`
- `01_qt_test/04_tcp/fortuneclient`
- `01_qt_test/04_tcp/fortuneserver`

这些工程基本不依赖仓库里的公共库，适合作为：

- Qt 交叉环境验证
- 最小工程验证
- 学习 `qmake` 工程结构

#### B. 真正的板级应用完整工程

这些应用的真实开发入口在 `elf_qt/src`：

- `elf_qt/src/02_watchdog`
- `elf_qt/src/03_rtc`
- `elf_qt/src/04_backlight`
- `elf_qt/src/05_wifi`
- `elf_qt/src/06_terminal`
- `elf_qt/src/07_network`
- `elf_qt/src/08_opengl`
- `elf_qt/src/09_musicplayer`
- `elf_qt/src/10_sqlite`
- `elf_qt/src/11_spi`

它们依赖以下公共模块：

- `elf_qt/src/libs/core`
- `elf_qt/src/libs/ui`
- `elf_qt/src/libs/imvirtualkeyboard`
- `elf_qt/src/appout.pri`
- `elf_qt/src/buildout.pri`

因此，针对 RK3588 开发板的真实 Qt 应用开发，应始终优先使用 `elf_qt/src` 这条主线。

---

## 3. 已验证的环境事实

以下事实已经在当前机器上实际核对过：

### 3.1 宿主机环境

- 当前宿主机为 `Ubuntu 22.04`
- `gcc` 版本：`11.4.0`
- `g++` 版本：`11.4.0`
- `Qt Creator 4.7.0` 可正常启动

### 3.2 交叉 SDK 环境

已验证的 SDK 路径：

- `/home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot`

已验证的关键工具：

- `/home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot/bin/qmake`
- `/home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot/bin/aarch64-linux-gcc`
- `/home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot/bin/aarch64-linux-g++`

已验证的 `qmake -v` 结论：

- `QMake version 3.1`
- `Using Qt version 5.15.10`

已验证的交叉编译器目标：

- `Target: aarch64-buildroot-linux-gnu`

### 3.3 已验证的交叉编译结果

实际交叉编译验证结果如下：

- 根目录简单示例：`6/6` 成功
- 根目录板级示例：`10/10` 失败
- `elf_qt/src` 完整板级工程：`17/17` 成功

这里的 `17/17` 包括：

- `elf_qt/src/libs/libs.pro`
- `elf_qt/src/00_HelloWorld/HelloWorld.pro`
- `elf_qt/src/01_qt_test/01_qfile/test1/test1.pro`
- `elf_qt/src/01_qt_test/02_qtextstream/test2/test2.pro`
- `elf_qt/src/01_qt_test/03_qthread/test3/test3_20230927_114445.pro`
- `elf_qt/src/01_qt_test/04_tcp/fortuneclient/fortuneclient.pro`
- `elf_qt/src/01_qt_test/04_tcp/fortuneserver/fortuneserver.pro`
- `elf_qt/src/02_watchdog/watchdog.pro`
- `elf_qt/src/03_rtc/rtc.pro`
- `elf_qt/src/04_backlight/backlight.pro`
- `elf_qt/src/05_wifi/wifi.pro`
- `elf_qt/src/06_terminal/terminal.pro`
- `elf_qt/src/07_network/network.pro`
- `elf_qt/src/08_opengl/opengl.pro`
- `elf_qt/src/09_musicplayer/musicplayer.pro`
- `elf_qt/src/10_sqlite/sqlite.pro`
- `elf_qt/src/11_spi/spi.pro`

### 3.4 已验证的 ARM 产物

已实际验证以下产物是 `ARM aarch64`：

- `elf_qt_out/release/bin/qtdemo_watchdog`
- `elf_qt_out/release/bin/qtdemo_terminal`
- `elf_qt_out/release/plugin/libIM.so`
- 简单示例构建产物 `HelloWorld`
- 简单示例构建产物 `test1`

这说明当前机器上的交叉编译链条已经闭环可用。

---

## 4. 正确的工程入口规则

这是本手册最重要的一节。另一个 AI 如果忽略本节，几乎一定会误判工程结构。

### 4.1 规则一：先判断你面对的是哪类工程

接手用户任务后，先分为两类：

#### 类别 1：简单独立 Qt 示例

如果用户要处理的是：

- `HelloWorld`
- `QFile`
- `QTextStream`
- `QThread`
- `TCP client/server`

则可以直接进入对应 `.pro` 工程单独编译。

#### 类别 2：RK3588 板级业务示例

如果用户要处理的是：

- `watchdog`
- `rtc`
- `backlight`
- `wifi`
- `terminal`
- `network`
- `opengl`
- `musicplayer`
- `sqlite`
- `spi`

则必须优先进入 `elf_qt/src` 下的对应目录，而不是仓库根目录下的同名目录。

### 4.2 规则二：根目录板级目录禁止当作完整工程入口

以下目录虽然存在于仓库根目录，但不能当作完整板级工程入口使用：

- `02_watchdog`
- `03_rtc`
- `04_backlight`
- `05_wifi`
- `06_terminal`
- `07_network`
- `08_opengl`
- `09_musicplayer`
- `10_sqlite`
- `11_spi`

原因如下：

1. 它们的 `.pro` 文件写的是 `include($$PWD/../appout.pri)`，但仓库根目录并不存在这个文件。
2. 它们的源码包含了仓库公共库头文件，例如：
   - `messagebox.h`
   - `basefactory.h`
   - `appwindow.h`
   - `iwatchdog.h`
   - `processhandler.h`
3. 这些头文件和库配置实际位于：
   - `elf_qt/src/appout.pri`
   - `elf_qt/src/buildout.pri`
   - `elf_qt/src/libs/core`
   - `elf_qt/src/libs/ui`

### 4.3 规则三：板级应用必须走公共库体系

`elf_qt/src` 下的板级应用不是各自完全独立的小项目，而是建立在公共框架之上的应用：

- `core` 提供通用能力
- `ui` 提供窗口、消息框、事件处理等 UI 基础设施
- `imvirtualkeyboard` 提供输入法/插件相关能力

因此对板级应用做任何开发时，都要把“公共库 + 应用”的整体结构一起考虑。

---

## 5. 目录与职责总表

### 5.1 核心目录

- `QT编程.txt`
  - 官方/项目文档总入口
- `00_HelloWorld`
  - 根目录最小 Qt Widgets 示例
- `01_qt_test`
  - 根目录一组基础 Qt 示例
- `02_watchdog` ~ `11_spi`
  - 根目录板级示例副本，不推荐作为真实工程入口
- `elf_qt/src`
  - 当前仓库真正的完整开发入口
- `elf_qt/src/libs`
  - 公共库与插件工程
- `elf_qt_out`
  - 编译输出目录

### 5.2 完整工程内部关键文件

- `elf_qt/src/buildout.pri`
  - 定义输出目录、安装路径、运行时 rpath
- `elf_qt/src/appout.pri`
  - 板级应用统一引入的公共构建配置
- `elf_qt/src/libs/libs.pro`
  - 公共库总入口
- `elf_qt/src/libs/core/core.pro`
  - `core` 动态库工程
- `elf_qt/src/libs/ui/ui.pro`
  - `ui` 动态库工程
- `elf_qt/src/plugin.pri`
  - 插件输出规则

### 5.3 编译产物目录

默认情况下，当前仓库的完整工程产物在：

- `elf_qt_out/release/bin`
- `elf_qt_out/release/lib`
- `elf_qt_out/release/plugin`

已观察到的实际产物包括：

- `elf_qt_out/release/bin/qtdemo_watchdog`
- `elf_qt_out/release/bin/qtdemo_rtc`
- `elf_qt_out/release/bin/qtdemo_backlight`
- `elf_qt_out/release/bin/qtdemo_wifi`
- `elf_qt_out/release/bin/qtdemo_terminal`
- `elf_qt_out/release/bin/qtdemo_network`
- `elf_qt_out/release/bin/qtdemo_opengl`
- `elf_qt_out/release/bin/qtdemo_musicplayer`
- `elf_qt_out/release/bin/qtdemo_sqlite`
- `elf_qt_out/release/bin/qtdemo_spi`
- `elf_qt_out/release/lib/libcore.so`
- `elf_qt_out/release/lib/libui.so`
- `elf_qt_out/release/plugin/libIM.so`

---

## 6. 文档章节与工程目录映射

下面这张表是“文档 -> 仓库 -> 可执行文件”的固定映射表。

| 文档主题 | 根目录副本 | 正确工程入口 | 目标程序名 |
|---|---|---|---|
| HelloWorld | `00_HelloWorld` | `elf_qt/src/00_HelloWorld` | `HelloWorld` |
| QFile 示例 | `01_qt_test/01_qfile/test1` | `elf_qt/src/01_qt_test/01_qfile/test1` | `test1` |
| QTextStream 示例 | `01_qt_test/02_qtextstream/test2` | `elf_qt/src/01_qt_test/02_qtextstream/test2` | `test2` |
| QThread 示例 | `01_qt_test/03_qthread/test3` | `elf_qt/src/01_qt_test/03_qthread/test3` | `test3` |
| TCP Client | `01_qt_test/04_tcp/fortuneclient` | `elf_qt/src/01_qt_test/04_tcp/fortuneclient` | `fortuneclient` |
| TCP Server | `01_qt_test/04_tcp/fortuneserver` | `elf_qt/src/01_qt_test/04_tcp/fortuneserver` | `fortuneserver` |
| watchdog | `02_watchdog` | `elf_qt/src/02_watchdog` | `qtdemo_watchdog` |
| rtc | `03_rtc` | `elf_qt/src/03_rtc` | `qtdemo_rtc` |
| backlight | `04_backlight` | `elf_qt/src/04_backlight` | `qtdemo_backlight` |
| wifi | `05_wifi` | `elf_qt/src/05_wifi` | `qtdemo_wifi` |
| terminal | `06_terminal` | `elf_qt/src/06_terminal` | `qtdemo_terminal` |
| network | `07_network` | `elf_qt/src/07_network` | `qtdemo_network` |
| opengl | `08_opengl` | `elf_qt/src/08_opengl` | `qtdemo_opengl` |
| musicplayer | `09_musicplayer` | `elf_qt/src/09_musicplayer` | `qtdemo_musicplayer` |
| sqlite | `10_sqlite` | `elf_qt/src/10_sqlite` | `qtdemo_sqlite` |
| spi | `11_spi` | `elf_qt/src/11_spi` | `qtdemo_spi` |

---

## 7. AI 标准作业流程（SOP）

以下流程是给另一个 AI 的固定执行顺序。除非用户明确要求，否则不要跳步。

### 7.1 接手任务后的第 1 步：识别任务类型

先回答以下问题：

1. 用户是要编译已有示例，还是修改已有示例，还是新增示例？
2. 用户涉及的是“简单独立示例”，还是“板级业务示例”？
3. 用户是否明确要求通过 Qt Creator 操作，还是允许命令行构建？

然后按以下规则选择入口：

- 若是简单独立示例：可直接进入目标目录的 `.pro`
- 若是板级业务示例：必须进入 `elf_qt/src`

### 7.2 接手任务后的第 2 步：确认交叉链路未被切换

无论用户说环境已经正常，都要再次确认下列事实：

- `qmake` 来自 SDK，不是宿主机
- 编译器目标是 `aarch64-buildroot-linux-gnu`
- Qt 版本是当前 SDK 的 `Qt 5.15.10`

建议的确认命令：

```bash
/home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot/bin/qmake -v
/home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot/bin/aarch64-linux-g++ -v
```

如果这两项不对，停止继续构建，先修正环境。

### 7.3 接手任务后的第 3 步：选择正确的构建方式

本仓库推荐以下两种构建方式：

#### 方式 A：Qt Creator + 正确 Kit

适用于：

- 用户明确要求图形界面操作
- 用户需要调试 Kit、Qt Version、Compilers 配置

要求：

- Qt Creator 中的 `Compilers` 必须是 ARM 交叉编译器
- `Qt Versions` 必须是 SDK 的 `qmake`
- `Kits` 必须绑定到 ARM 交叉环境，不得使用 `Desktop`

#### 方式 B：命令行 `qmake + make`

适用于：

- 批量构建
- AI 自动化验证
- CI/脚本化过程

当前仓库的自动化验证优先推荐命令行方式。

### 7.4 构建现有板级应用的标准命令

#### 步骤 1：先构建公共库

推荐在单独构建目录中执行：

```bash
mkdir -p /tmp/build-rk3588-libs
cd /tmp/build-rk3588-libs
/home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot/bin/qmake /home/elf/workspace/QTtest/Qt例程源码/elf_qt/src/libs/libs.pro
make -j4
```

说明：

- `libs.pro` 会按顺序构建 `core`、`ui`、`imvirtualkeyboard`
- 板级应用依赖这些公共库
- 如果公共库未构建，后续应用大概率链接失败或运行时缺少依赖

#### 步骤 2：再构建目标应用

以 `watchdog` 为例：

```bash
mkdir -p /tmp/build-rk3588-watchdog
cd /tmp/build-rk3588-watchdog
/home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot/bin/qmake /home/elf/workspace/QTtest/Qt例程源码/elf_qt/src/02_watchdog/watchdog.pro
make -j4
```

以 `terminal` 为例：

```bash
mkdir -p /tmp/build-rk3588-terminal
cd /tmp/build-rk3588-terminal
/home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot/bin/qmake /home/elf/workspace/QTtest/Qt例程源码/elf_qt/src/06_terminal/terminal.pro
make -j4
```

#### 步骤 3：确认产物位置

即使在 `/tmp` 中构建，产物仍会按 `buildout.pri` 的规则落到仓库输出目录：

- `elf_qt_out/release/bin`
- `elf_qt_out/release/lib`
- `elf_qt_out/release/plugin`

原因是 `elf_qt/src/buildout.pri` 已固定输出目录为：

```qmake
APP_OUT_DIR=$${APP_SRC_PATH}/../../elf_qt_out
```

### 7.5 构建简单独立示例的标准命令

以 `HelloWorld` 为例：

```bash
mkdir -p /tmp/build-rk3588-helloworld
cd /tmp/build-rk3588-helloworld
/home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot/bin/qmake /home/elf/workspace/QTtest/Qt例程源码/00_HelloWorld/HelloWorld.pro
make -j4
file HelloWorld
```

以 `QFile` 示例 `test1` 为例：

```bash
mkdir -p /tmp/build-rk3588-test1
cd /tmp/build-rk3588-test1
/home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot/bin/qmake /home/elf/workspace/QTtest/Qt例程源码/01_qt_test/01_qfile/test1/test1.pro
make -j4
file test1
```

### 7.6 交叉编译成功的判定标准

只有同时满足以下条件，才允许对用户声称“交叉编译成功”：

1. `qmake` 和 `make` 退出码为 `0`
2. 产物存在于预期位置
3. `file <binary>` 显示目标架构为 `ARM aarch64`

正确示例：

```bash
file elf_qt_out/release/bin/qtdemo_watchdog
```

期望结果应包含类似字样：

```text
ELF 64-bit ... ARM aarch64 ...
```

如果结果是 `x86_64`，说明不是有效的板级交叉编译。

---

## 8. 板级应用为什么必须先编公共库

本项目的板级应用并不是“单目录自洽”的简单工程，而是共享统一框架。

例如 `watchdog` 的源代码中直接引用了：

- `messagebox.h`
- `basefactory.h`
- `appwindow.h`
- `iwatchdog.h`

这些都不在 `elf_qt/src/02_watchdog` 自己目录里，而在公共库中。

### 8.1 关键依赖关系

- `elf_qt/src/appout.pri`
  - 统一加入 `-lcore -lui`
  - 统一加入公共头文件搜索路径
- `elf_qt/src/buildout.pri`
  - 统一定义输出目录、安装路径、rpath
- `elf_qt/src/libs/core`
  - 提供串口、配置、资源加载、工厂、进程等基础能力
- `elf_qt/src/libs/ui`
  - 提供 `AppWindow`、`MessageBox`、事件处理等 UI 基础设施

### 8.2 典型误判

错误做法：

- 直接进入根目录 `02_watchdog`
- 执行 `qmake` + `make`
- 看到找不到 `messagebox.h`
- 误以为是交叉环境坏了

实际根因：

- 不是交叉环境坏了
- 是工程入口错了
- 你进的是“不完整副本”，不是完整工程

---

## 9. 当前仓库中的关键构建规则

### 9.1 `buildout.pri` 的作用

`elf_qt/src/buildout.pri` 负责三件核心事情：

1. 固定输出目录到 `elf_qt_out`
2. 定义应用和库的安装目标路径
3. 设置运行时 rpath

关键规则可概括为：

- `TEMPLATE=lib` 的产物目标路径偏向 `QT_SYSROOT/usr/lib`
- `TEMPLATE=app` 的产物目标路径偏向 `QT_SYSROOT/usr/bin`
- 实际编译输出目录固定在仓库内的 `elf_qt_out`

这意味着：

- `make` 会把最终产物放到仓库输出目录
- 如果执行 `make install`，更偏向“安装到 SDK/sysroot 的 staging 区”
- 这不是直接部署到实体开发板，而是安装到交叉环境视角下的目标文件系统位置

这里最后一条是基于 `buildout.pri` 内容得出的工程推断。

### 9.2 `appout.pri` 的作用

`elf_qt/src/appout.pri` 负责板级应用统一接入公共库：

- `LIBS += -lcore -lui`
- `INCLUDEPATH` 指向 `libs/core`
- `INCLUDEPATH` 指向 `libs/core/interface`
- `INCLUDEPATH` 指向 `libs/ui`

因此，新建板级应用时，若希望遵循当前仓库策略，就必须引入这个文件。

### 9.3 `libs.pro` 的作用

`elf_qt/src/libs/libs.pro` 是板级应用公共依赖的总入口：

- 顺序构建 `core`
- 顺序构建 `ui`
- 顺序构建 `imvirtualkeyboard`

任何板级应用在首次构建或公共库改动后，都应优先重编这里。

---

## 10. 运行时资源与部署策略

### 10.1 代码中的运行时资源路径规则

从 `elf_qt/src/libs/core/basepath.cpp` 可知：

- 资源默认路径：`应用目录/data/rc`
- 配置默认路径：`应用目录/data/conf.txt`
- 缓存默认路径：`应用目录/data/cache`

也就是说，应用运行时默认认为：

- 可执行程序旁边存在 `data/`
- `data/` 下有 `rc/`
- `data/` 下可能有 `conf.txt`

### 10.2 文档中的部署方式

`QT编程.txt` 中多处提到：

- 把可执行文件拷到开发板
- 某些例程还会把 `/usr/bin/data` 一并复制到运行目录附近

这与代码的默认逻辑是一致的：应用运行时需要外部资源目录。

### 10.3 当前仓库的现实情况

当前仓库中没有完整的顶层 `resource/` 目录快照，但源码明确存在运行时资源加载逻辑。

因此必须记住：

- “能编译成功” 不等于 “运行时资源完整”
- 如果程序在板子上运行时报图标、配置、资源缺失，需要优先检查 `data/` 目录是否与可执行文件一起部署

### 10.4 动态库与插件部署

当前构建结果表明，完整工程会生成：

- `elf_qt_out/release/lib/libcore.so`
- `elf_qt_out/release/lib/libui.so`
- `elf_qt_out/release/plugin/libIM.so`

如果在板上运行非系统预装路径中的应用，必须同时考虑：

- 可执行文件是否能找到 `libcore.so`
- 可执行文件是否能找到 `libui.so`
- 若启用了相关输入法/插件能力，插件路径是否也已正确部署

幸运的是，`buildout.pri` 已设置了相对 `rpath`，例如：

- `$$ORIGIN`
- `$$ORIGIN/lib`
- `$$ORIGIN/../lib`

这说明当前工程更倾向于支持“程序和库相对目录协同部署”。

---

## 11. 新增板级应用的标准做法

如果用户要在当前仓库中新增一个面向 RK3588 的板级 Qt 应用，必须遵循下面规则。

### 11.1 新应用放置位置

新应用应放在：

- `elf_qt/src/<新目录>`

不要只在仓库根目录创建新目录。

### 11.2 新应用 `.pro` 的基本原则

新应用 `.pro` 应模仿现有板级应用：

- `TEMPLATE = app`
- 按需声明 `QT += ...`
- `include($$PWD/../appout.pri)`
- 指定 `SOURCES` / `HEADERS` / `FORMS`
- 指定 `TARGET = <可执行程序名>`

最小结构示意：

```qmake
TEMPLATE = app

QT += gui widgets serialport
include($$PWD/../appout.pri)

HEADERS += \
    $$PWD/mywidget.h

SOURCES += \
    $$PWD/main.cpp \
    $$PWD/mywidget.cpp

FORMS += \
    $$PWD/mywidget.ui

TARGET = qtdemo_myapp
```

### 11.3 新应用的 `main.cpp` 基本模式

现有板级应用普遍遵循这类初始化顺序：

1. 应用启动前全局初始化
2. 创建 `QApplication`
3. 加载资源目录
4. 初始化公共 UI 框架
5. 创建业务窗口或部件

也就是说，新应用如果要完全融入当前体系，应该复用：

- `AppWindow`
- `ResourceLoader`
- `BasePath`
- 公共 UI/消息框/事件处理

### 11.4 新应用的构建顺序

任何新应用加入后，推荐构建顺序仍然是：

1. 构建 `elf_qt/src/libs/libs.pro`
2. 构建新应用 `.pro`
3. 用 `file` 验证架构

### 11.5 新应用的目标命名规则

建议沿用当前项目命名模式：

- `qtdemo_<feature>`

例如：

- `qtdemo_watchdog`
- `qtdemo_rtc`
- `qtdemo_backlight`

这样可以和当前仓库现有产物保持一致。

---

## 12. 修改已有应用时的标准策略

### 12.1 修改应用界面逻辑

如果只是修改某个应用的：

- `.ui`
- 交互逻辑
- 局部业务代码

则通常只需：

1. 确认目标应用位于 `elf_qt/src`
2. 构建公共库（若改动影响公共接口则必须）
3. 重新构建该应用
4. 验证产物架构

### 12.2 修改公共库

如果改动的是：

- `libs/core`
- `libs/ui`
- `buildout.pri`
- `appout.pri`

则必须认为影响范围扩大，建议：

1. 重新构建 `elf_qt/src/libs/libs.pro`
2. 重新构建所有受影响应用，至少重构建直接依赖应用
3. 抽查多个产物做 `file` 验证

### 12.3 修改构建配置

如果改动：

- `qmake` 配置
- 交叉编译工具链路径
- `Qt Version`
- `Kit`

则修改后第一个动作不应该是“继续写业务代码”，而应该是：

1. 重新构建 `HelloWorld`
2. 用 `file` 验证 `ARM aarch64`
3. 再构建一个板级应用，例如 `qtdemo_watchdog`

这样可以快速判断问题是在环境侧还是工程侧。

---

## 13. 典型错误与诊断规则

### 13.1 错误：找不到 `messagebox.h` / `appwindow.h` / `basefactory.h`

含义：

- 你大概率进入了根目录下的不完整板级目录

应对：

- 切换到 `elf_qt/src/<对应应用>`
- 先编 `elf_qt/src/libs/libs.pro`

### 13.2 错误：`Cannot read .../appout.pri`

含义：

- 工程引用了 `../appout.pri`
- 但当前目录层级没有这个文件

根因：

- 使用了错误的工程入口

### 13.3 错误：编译成功但产物是 `x86_64`

含义：

- 你用了宿主机 `qmake`
- 或 Qt Creator 的 `Desktop` Kit

应对：

- 强制切回 SDK 自带 `qmake`
- 强制检查 `Qt Versions` 与 `Kits`

### 13.4 错误：板上运行时报资源缺失

含义：

- 编译已成功
- 但运行所需 `data/` 目录未完整部署

应对：

- 检查应用目录下是否有 `data/rc`
- 检查配置文件是否到位

### 13.5 错误：链接阶段找不到公共库

含义：

- 公共库未先构建
- 或输出目录内容被清理

应对：

- 先重建 `elf_qt/src/libs/libs.pro`
- 再重建目标应用

---

## 14. 给另一个 AI 的固定执行清单

接手本仓库任务时，按下面清单逐项执行。

### 14.1 任务前清单

- [ ] 识别用户任务属于“简单示例”还是“板级应用”
- [ ] 若是板级应用，强制切换视角到 `elf_qt/src`
- [ ] 确认 SDK 路径仍是 `/home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot`
- [ ] 确认 `qmake -v` 仍显示 `Qt 5.15.10`
- [ ] 确认交叉编译器目标仍是 `aarch64-buildroot-linux-gnu`

### 14.2 板级应用编译清单

- [ ] 先构建 `elf_qt/src/libs/libs.pro`
- [ ] 再构建目标应用 `.pro`
- [ ] 检查产物是否出现在 `elf_qt_out/release/bin`
- [ ] 用 `file` 验证是否为 `ARM aarch64`

### 14.3 根目录工程使用清单

- [ ] 仅将根目录 `00_HelloWorld` 与 `01_qt_test` 用作最小示例或环境验证
- [ ] 不把根目录 `02_watchdog` 到 `11_spi` 当作完整板级工程

### 14.4 部署/运行前清单

- [ ] 检查可执行文件是否已准备好
- [ ] 检查相关动态库是否已准备好
- [ ] 检查 `data/` 资源目录是否需要随程序部署
- [ ] 检查目标板路径布局是否与运行时代码预期一致

---

## 15. 最简操作模板

如果另一个 AI 时间很紧，只允许执行最少步骤，那么按下面模板做：

### 15.1 编译一个 RK3588 板级应用

```bash
mkdir -p /tmp/build-rk3588-libs
cd /tmp/build-rk3588-libs
/home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot/bin/qmake /home/elf/workspace/QTtest/Qt例程源码/elf_qt/src/libs/libs.pro
make -j4

mkdir -p /tmp/build-rk3588-watchdog
cd /tmp/build-rk3588-watchdog
/home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot/bin/qmake /home/elf/workspace/QTtest/Qt例程源码/elf_qt/src/02_watchdog/watchdog.pro
make -j4

file /home/elf/workspace/QTtest/Qt例程源码/elf_qt_out/release/bin/qtdemo_watchdog
```

### 15.2 验证一个最小 Qt 交叉工程

```bash
mkdir -p /tmp/build-rk3588-helloworld
cd /tmp/build-rk3588-helloworld
/home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot/bin/qmake /home/elf/workspace/QTtest/Qt例程源码/00_HelloWorld/HelloWorld.pro
make -j4
file HelloWorld
```

---

## 16. 最终操作准则

最后再重复一次本仓库的最高优先级准则：

1. 面向 RK3588 板级开发时，优先使用 `elf_qt/src`
2. 先编公共库，再编应用
3. 交叉编译必须使用 SDK 自带 `qmake`
4. 板端联调优先走 `ssh + 板端日志 + 本地桌面会话环境复用`，不要先猜

---

## 17. SSH 调试技巧与经验

这一节专门沉淀“宿主机通过 SSH 调试 RK3588 Qt 程序”的实战经验。  
重点不是泛泛而谈 SSH 基础，而是针对当前仓库、当前板端 Ubuntu 图形会话、当前 Qt/UI/视频方案，告诉另一个 AI 应该怎么高效排查问题。

### 17.1 先明确：SSH 只是进入板子，不等于进入图形会话

通过 SSH 登录板子后，默认得到的是一个 shell 会话，不等于板端当前正在显示桌面的图形会话。

这会直接影响：

- `health-ui` 能不能真正显示到板端桌面
- `gnome-screenshot` / `ffmpeg -f x11grab` 能不能抓到图
- X11 模拟点击是否有效
- 某些 Qt 程序是否能正常初始化 `xcb`

因此，凡是涉及“UI 是否显示”“板端桌面有没有打开某个窗口”“自动点击界面按钮”之类的问题，都不要只在 SSH 默认环境里启动程序。

### 17.2 当前板端图形会话的已验证关键环境

当前项目实际调通过的 RK3588 板端本地桌面环境是：

```bash
DISPLAY=:1
XAUTHORITY=/run/user/1000/gdm/Xauthority
DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus
```

因此，凡是需要让 `health-ui` 真正显示到板端桌面的调试命令，优先带上：

```bash
export DISPLAY=:1
export XAUTHORITY=/run/user/1000/gdm/Xauthority
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus
```

如果不带这些变量，常见现象是：

- 程序进程存在，但板端桌面没窗口
- 程序启动失败，只在日志里报 `xcb` / display 相关错误
- 你以为“UI 黑屏”，实际是 UI 根本没进入正确桌面会话

### 17.3 调试前先分清三种启动方式

在板端调试本项目时，建议先明确自己到底要哪一种：

#### A. 只测后端

适用于：

- 测 `healthd`
- 测 `health-videod`
- 测 IPC socket
- 测摄像头拍照、录像、预览流

推荐：

```bash
cd /home/elf/rk3588_bundle
./scripts/start.sh --backend-only
```

这样可以避免 UI 干扰，先单独验证：

- `run/rk_health_station.sock`
- `run/rk_video.sock`
- `logs/healthd.log`
- `logs/health-videod.log`

#### B. 测 UI 但不要求桌面交互

适用于：

- 简单 smoke test
- 看程序是否能启动并进入事件循环
- 验证本地 IPC 是否能连通

可用：

```bash
./bin/health-ui --smoke-test
```

#### C. 测板端真实桌面显示

适用于：

- 验证 Qt 窗口是否真的显示
- 验证 Video 页面有没有真实画面
- 自动点按钮、截屏、抓桌面

这时必须带 17.2 的桌面环境变量。

### 17.4 先看日志，再看结论

SSH 调试时，不要先凭现象猜结论。  
本项目里最有价值的第一手证据通常是 bundle 日志：

- `logs/healthd.log`
- `logs/health-videod.log`
- `logs/health-ui.log`

板端启动失败时的标准动作：

```bash
cd /home/elf/rk3588_bundle
./scripts/status.sh
tail -n 80 logs/health-ui.log
tail -n 80 logs/health-videod.log
tail -n 80 logs/healthd.log
```

经验：

- `status.sh` 先看“进程是否活着、socket 是否存在”
- 再看对应日志最后几十行
- 没看日志前，不要说“程序没启动”“摄像头坏了”“Qt 坏了”

### 17.5 优先验证 socket，不要上来就怀疑业务逻辑

当前仓库本地 IPC 很关键：

- 主后端 socket：`run/rk_health_station.sock`
- 视频后端 socket：`run/rk_video.sock`

调试规则：

1. 如果 UI 页面没有数据，先看 socket 是否存在
2. 如果 socket 不存在，优先检查对应 daemon 是否启动成功
3. 如果 socket 存在，再继续看协议请求、页面逻辑、数据面

简单检查：

```bash
cd /home/elf/rk3588_bundle
./scripts/status.sh
ls -l run/
```

### 17.6 摄像问题必须分成“控制面”和“数据面”两层排查

这是这次视频联调里最重要的经验之一。

#### 控制面

控制面关注：

- `health-ui` 是否连上 `rk_video.sock`
- `get_status` 是否返回成功
- `preview_url` 是否正确
- `camera_state` 是否为 `Previewing`

如果控制面正常，说明：

- UI 与 `health-videod` 的控制链路是通的
- 页面不是纯逻辑没刷新

#### 数据面

数据面关注：

- 预览流本身是否真的有数据
- UI 是否真的在消费流
- 流格式是否适合 UI 当前消费方式

经验结论：

- “拍照、录像正常，但 UI 没画面” 不代表摄像头坏
- 它经常意味着：控制面正常，但 UI 数据面消费失败

因此遇到视频问题时，不要把“有无画面”直接等同于“摄像头好坏”。

### 17.7 调试视频时，先用板端独立消费者证明流是好的

如果怀疑视频预览有问题，优先不要先改 UI。  
先在板端用独立命令证明流本身是否可消费。

例如之前验证过的板端思路：

- 后端启动预览
- 再用独立的 GStreamer / Python 消费者读取预览流
- 若独立消费者能拿到数据，说明问题更可能在 UI 侧

这一步的价值非常高，因为它能快速把问题分成两类：

1. 后端根本没出流
2. 后端出流正常，但 UI 消费失败

### 17.8 遇到 UI 问题时，尽量先做“后台启动 + 前台取证”

推荐模式：

1. 后端后台启动
2. UI 单独启动，并把 stdout/stderr 重定向到临时日志
3. 自动执行点击或截屏
4. 结束后统一回收进程

示意：

```bash
cd /home/elf/rk3588_bundle
./scripts/start.sh --backend-only >/tmp/backend.log 2>&1
./bin/health-ui >/tmp/health-ui-debug.log 2>&1 &
UI_PID=$!
sleep 5
# 这里做点击 / 截图 / 状态检查
kill $UI_PID
./scripts/stop.sh
```

这样做的好处：

- UI 和后端日志分离
- 取证更清楚
- 不容易把一次失败调试污染成“状态不明”

### 17.9 如果要自动点板端 UI，先确认真实窗口几何信息

SSH 调试里，模拟点击最容易犯的错误是“按自己猜的坐标点”。  
正确做法是先取窗口几何，再计算点击坐标。

可用工具：

```bash
xwininfo -name "RK Health UI"
```

重点字段：

- `Absolute upper-left X`
- `Absolute upper-left Y`
- `Width`
- `Height`

然后再用 X11/XTest 或其它方式去点。  
否则很容易出现：

- 你以为点到了 `Video`
- 实际点的是窗口空白处
- 最后误判为“按钮没响应”“页面逻辑错了”

### 17.10 不要让旧的调试进程污染新结论

这次联调里一个非常真实的问题是：

- 之前留下的 `gst-launch` 预览消费者还在占用或抢同一个端口
- 新一轮 UI 调试时，现象被旧进程污染

经验规则：

每次开始新一轮板端调试前，优先清理旧进程：

```bash
pkill -f health-ui || true
pkill -f health-videod || true
pkill -f 'gst-launch-1.0 -q udpsrc port=5602' || true
```

然后再重新：

- `./scripts/stop.sh`
- `./scripts/start.sh ...`

否则很容易得到假的结论。

### 17.11 一键脚本能用，但调试时不要完全依赖一键脚本

`start_all.sh` / `stop_all.sh` 适合：

- 快速拉起完整系统
- 回归验证
- 交付式运行

但在深度调试时，建议拆开使用：

- `start.sh --backend-only`
- 手动启动 `health-ui`
- 单独看临时日志

原因：

- 一键脚本更适合运行，不适合精细化取证
- UI 问题、摄像问题、显示问题往往需要单独拉 UI 才容易定位

### 17.12 当前视频方案下的 SSH 调试重点

当前在线方案不是旧的本地 `UDP/H264`，而是：

- `health-videod` 输出本地 `TCP + MJPEG`
- `health-ui` 用 `QTcpSocket` 消费预览

因此视频调试的关注点已经变成：

- `preview_url` 是否是 `tcp://127.0.0.1:5602?transport=tcp_mjpeg&boundary=rkpreview`
- `health-videod` 是否起了 `tcpserversink`
- `health-ui` 是否成功连到 TCP 端口
- UI 日志里是否出现 `preview socket connected`
- UI 日志里是否持续出现 `frame rendered`

这是当前判断“预览真的工作了”的最直接依据。

### 17.13 当你怀疑是运行库问题时，优先区分“编译成功”和“板端可运行”

这次项目里还验证过一个很重要的事实：

- 交叉编译成功，不代表板端一定能运行
- bundle 里打进去的 Qt / ICU / libstdc++ 可能和板端 Ubuntu 的 glibc 不兼容

经验规则：

1. 先确认二进制是不是 `ARM aarch64`
2. 再确认板端运行时到底在用 bundle 库还是系统库
3. 如果板端能跑历史版本但新版本不能跑，不要先怀疑业务逻辑，先看运行时库冲突

### 17.14 SSH 调试的最小高效模板

如果另一个 AI 时间很紧，只允许用最少步骤排板端问题，推荐下面模板：

#### 模板 A：后端是否正常

```bash
cd /home/elf/rk3588_bundle
./scripts/start.sh --backend-only
./scripts/status.sh
tail -n 80 logs/health-videod.log
tail -n 80 logs/healthd.log
```

#### 模板 B：UI 是否真的进了板端桌面

```bash
export DISPLAY=:1
export XAUTHORITY=/run/user/1000/gdm/Xauthority
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus

cd /home/elf/rk3588_bundle
./bin/health-ui >/tmp/health-ui-debug.log 2>&1
```

#### 模板 C：视频页面有没有真正出画面

```bash
grep -n 'preview socket connected\|frame rendered\|preview error text' /tmp/health-ui-debug.log
tail -n 80 logs/health-videod.log
```

### 17.15 给另一个 AI 的一句话原则

通过 SSH 调试 RK3588 Qt 程序时，最容易犯的错误不是“不会写命令”，而是：

- 没进入真正的图形会话
- 没区分控制面和数据面
- 没看日志就先下结论
- 被上一次残留进程污染了现象

只要始终坚持下面四件事，调试效率会高很多：

1. 先确认会话环境
2. 先确认进程 / socket / 日志
3. 先证明流或数据链路本身是否正常
4. 每轮调试前清理旧状态

如果另一个 AI 严格执行这四条，基本就不会在当前仓库上走错方向。


## 附录：SSH连接板卡进行Qt应用上板运行调试的经验与教训

本附录结合一次实际的 `RK3588 + Ubuntu 22.04.5 LTS` 板卡调试过程，总结通过SSH连接开发板后，
如何完成Qt应用交叉编译、上传、启动和定位问题。这里的核心结论是：正确流程不是“在板子上临时编译”，
而是“宿主机交叉编译 + SSH上传产物 + 板端运行验证 + 日志定位问题”。

### 一、推荐的标准流程

1. 在宿主机完成交叉编译，生成目标板可运行的ARM64产物；
2. 使用 `file` 命令确认产物架构确实是 `ARM aarch64`；
3. 通过 `rsync` 或 `scp` 将完整运行目录上传到板端；
4. 在板端通过SSH进入运行目录，先停旧进程，再启动新版本；
5. 优先查看状态脚本、日志文件、socket文件和监听端口，而不是只凭“命令执行了”来判断成功。

如果项目已经具备bundle目录，推荐直接传整个bundle，而不要只拷贝某个单独二进制文件。这样更容易保
证二进制、脚本、插件和依赖库的一致性。

补充一条已验证经验：如果目标板磁盘空间紧张，不一定每次都要整体替换整个bundle。也可以在保留原目
录结构的前提下，只增量同步被修改过的 `bin/`、`scripts/`、`assets/` 或少量私有运行库，但前提是目
录层级和脚本约定不能被破坏。

### 二、强烈建议使用bundle方式上板

在实际调试中，推荐把板端运行目录组织成如下结构：

```
rk3588_bundle/
├── bin/
├── lib/
│   └── app/
├── assets/
│   └── models/
├── model/
├── plugins/
├── scripts/
├── logs/
├── run/
└── data/
```

这种方式的好处是：

1. `bin/` 中保存可执行文件；
2. `lib/` 中可放bundle模式下需要的完整运行库；
3. `lib/app/` 中只放系统没有、但应用私有必需的库，例如 `librknnrt.so`；
4. `assets/models/` 中保存 `.rknn`、`.onnx` 等模型文件；
5. `model/` 中保存模型运行时会按相对路径读取的辅助文件，例如标签文件；
6. `scripts/` 中统一管理 `start.sh`、`stop.sh`、`status.sh`；
7. `logs/` 保存运行日志；
8. `run/` 保存pid和本地socket；
9. `data/` 保存SQLite等运行数据；
10. 整个目录可整体替换、整体备份、整体清理，调试时非常直观。

### 三、上板前必须先验证架构

很多“程序上板不能运行”的根因，并不是代码错了，而是把宿主机 `x86-64` 产物误传到了开发板。因此上
板前必须执行：

```
file healthd
file health-ui
```

只有当输出中出现 `ARM aarch64` 时，才能说明这个产物是为RK3588这类64位ARM开发板准备的。如
果输出是 `x86-64`，即使上传成功，也不可能在板子上正常运行。

### 四、SSH适合做部署和调试，但不等于自动拥有图形桌面会话

这是本次调试中非常关键的一条经验。

通过SSH登录开发板后，虽然可以执行命令，但这个SSH shell默认通常不带图形桌面会话环境变量。也就
是说，即使板端已经登录桌面，SSH里也可能看不到：

- `DISPLAY`
- `WAYLAND_DISPLAY`
- `XDG_RUNTIME_DIR`
- `XAUTHORITY`
- `DBUS_SESSION_BUS_ADDRESS`

这会直接影响Qt GUI程序启动。典型现象是：

1. 后端程序可以正常启动；
2. UI程序在SSH中启动失败；
3. 失败并不是Qt程序本身坏了，而是图形会话环境没有传给当前shell。

因此，如果要通过SSH远程拉起Qt界面程序，建议先确认板端当前是否真的有图形会话，再把对应环境变量
显式导出后再执行Qt应用。

### 五、不接显示器并不一定意味着UI无法运行

本次实际验证表明：板端虽然没有连接显示器，但系统中仍然存在活动的GNOME/Xorg图形会话，因此Qt UI
依然可以被成功拉起。也就是说：

1. “没接显示器”不等于“没有图形会话”；
2. 是否能启动UI，不能只靠猜测；
3. 应该通过进程状态、日志、socket连接情况来确认。

如果系统已经启动图形桌面，通常可以从图形进程环境中提取有效变量，例如：

```
DISPLAY=:1
XDG_RUNTIME_DIR=/run/user/1000
XAUTHORITY=/run/user/1000/gdm/Xauthority
DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus,...
```

然后再启动Qt GUI应用。

### 六、遇到启动失败时，先看日志，不要先猜

本次调试的一个重要教训是：应用启动失败后，第一时间查看日志，往往比盲目修改脚本更有效。建议的检
查顺序如下：

1. `status.sh` 查看进程状态；
2. 查看后端日志，例如 `logs/healthd.log`；
3. 查看前端日志，例如 `logs/health-ui.log`；
4. 查看 `run/` 目录中的pid和socket文件；
5. 查看监听端口是否已经建立。

对于本地IPC程序，socket文件是否成功创建往往是关键线索；对于网络服务程序，则应确认端口是否真正处
于监听状态。

### 七、实际踩到的根因：板端glibc版本低于bundle中部分库的要求

本次上板中，后端最初启动失败，日志中出现了类似下面的信息：

```
GLIBC_2.38 not found
GLIBC_2.36 not found
```

这说明问题不是业务代码逻辑错误，而是运行时库版本不兼容。更具体地说：

1. 交叉编译bundle时打入的某些Qt/ICU/libstdc++库来自Buildroot sysroot；
2. 这些库依赖更高版本的glibc；
3. 板端Ubuntu 22.04的glibc版本较低，导致程序刚启动就失败。

这个问题的教训是：

1. bundle里“自带库”并不一定总比系统库更安全；
2. 如果目标板本身就是Ubuntu，并且已经安装了匹配的Qt运行库，有时应优先使用系统库；
3. 一旦出现 `GLIBC_x.xx not found`，应优先从“运行时库兼容性”方向排查，而不是先怀疑业务代码。

还要进一步区分两类库：

1. Qt/ICU/libstdc++/glib 这类“系统也有且强依赖glibc ABI”的公共运行库，Ubuntu板上通常更适合优先
   使用系统库；
2. `librknnrt.so` 这类“应用专有、系统未必自带”的私有库，则可以单独随bundle放在 `lib/app/` 中，再
   通过 `LD_LIBRARY_PATH` 只补这部分最小依赖。

这次实际验证表明，这种“系统Qt + 私有AI运行库”混合方式，比“整包Buildroot运行库全量覆盖”更稳妥。

### 八、启动脚本应支持区分“使用bundle库”还是“使用系统库”

通过这次调试，启动脚本最好支持至少两种模式：

1. `bundle` 模式：优先使用bundle中的 `lib/` 和 `plugins/`；
2. `system` 模式：优先使用板端系统自带运行库；
3. `auto` 模式：根据目标板系统自动选择更合适的运行方式。

对于Ubuntu/Debian类开发板，如果系统已经带有可用的Qt运行环境，通常可以优先考虑系统库模式。这样
可以避免把不兼容的交叉sysroot运行库强行带到目标板上。

补充一条非常重要的落地规则：

1. `system` 模式不等于完全清空 `LD_LIBRARY_PATH`；
2. 更推荐的做法是：不再把 bundle 的整包 `lib/` 和 `plugins/` 强行前置，但允许把 `lib/app/` 这类应
   用私有库目录加入 `LD_LIBRARY_PATH`；
3. 这样可以继续复用板端Ubuntu自带Qt，同时又不丢失RKNN等系统未提供的运行时依赖。

也就是说，真正稳定的 `system` 模式应理解为：

- Qt/ICU/libstdc++ 走系统；
- RKNN等应用私有库走bundle内最小补充；
- 插件路径只在确实需要bundle内Qt插件时再显式启用。

### 九、远程停进程时要谨慎，避免误杀自己的SSH命令

这也是一个很实际的教训。如果远程执行：

```
pkill -f /path/to/program
```

有时可能会把当前SSH里包含该字符串的shell命令也匹配进去，导致远程命令自己把自己“杀掉”。因此更
稳妥的做法是：

```
pkill -x healthd
pkill -x health-ui
```

或者使用更明确的pid文件管理机制，而不是模糊匹配整条命令行。

### 十、推荐保留的板端运行检查项

每次重新部署后，建议至少检查以下项目：

1. `healthd` 是否在运行；
2. `health-videod` 是否在运行；
3. 如果启用了AI链路，`health-falld` 是否在运行；
4. 本地主socket是否存在；
5. 视频socket、analysis socket、fall socket 是否存在；
6. TCP端口是否监听成功；
7. 数据库文件是否已经创建；
8. 日志中是否出现 `bootstrap complete`、`entering event loop` 等成功标志；
9. UI是否已经成功连上后端IPC；
10. 如存在 `health-falld`，建议再主动查询一次运行时状态，确认至少包含：
    - `input_connected=true`
    - `pose_model_ready=true`
    - `last_frame_ts` / `last_infer_ts` 已更新

对于只做后端验证的场景，可以优先使用：

```
./scripts/start.sh --backend-only
```

这样可以先把“后端是否可运行”和“图形桌面是否可用”两个问题拆开验证，减少误判。

### 十一、建议长期固化为标准作业

针对Qt应用的上板运行调试，建议在团队内部固定如下标准：

1. 宿主机交叉编译；
2. 上板前用 `file` 校验架构；
3. 用 `rsync` 上传完整bundle；
4. 用 `start.sh` / `stop.sh` / `status.sh` 管理进程；
5. 用日志和socket/端口状态判断程序是否真正启动；
6. 对Ubuntu板卡，重点关注图形会话环境与glibc兼容性问题。

再补一条经过实际验证的新经验：

7. 对于新增的板端AI守护进程，不必默认退回“板上原生编译”。如果交叉编译产物本身没问题，并且启动脚
   本正确区分了“系统公共库”和“bundle私有库”，那么新增服务同样可以沿用“宿主机交叉编译 + 板端
   system运行时直跑”的旧方案。

把这些经验固化下来后，后续再通过SSH连接板卡调试Qt程序时，效率会明显提高，也更容易避免“命令执行
成功但程序实际上没有跑起来”这一类常见误判。
