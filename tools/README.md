# 构建与发布工具 (tools)

本目录包含 LLC-Zygisk-Localizer 的核心辅助工具与脚本，主要用于 Android 本地二进制构建、构建元信息生成以及 GitHub Actions CI 自动化发布校验。

---

## 目录结构

```text
tools/
├── build-https.ps1      # Android DEX (llc-https.jar) 编译脚本
├── build-info.ps1       # 构建元数据与版本头文件生成脚本
├── build-manager.ps1    # 原生管理器 (llc-manager-native) 交叉编译脚本
└── ci_release.py        # CI 发布校验与更新日志提取工具
```

---

## 工具详解

### 1. `build-https.ps1` (HTTPS 原生辅助编译)

- **定位**：将 Java 源码编译为 Android 专用的 DEX 文件 `llc-https.jar`。
- **作用**：部分 Android 系统（或精简 Root 环境）未内置 `curl` 命令。该工具产出的 `llc-https.jar` 允许模块在运行时直接借助 Android 系统的 JVM 和原生 TLS 栈执行安全的 HTTPS 请求，用于查询 GitHub Release API 及下载汉化包。
- **前置依赖**：
  - JDK 17+（需提供 `javac` 和 `jar`）
  - Android SDK（需提供 `build-tools` 中的 `d8` 以及对应平台的 `android.jar`）
- **编译流程**：
  1. `javac --release 8` 编译 `manager/https/HttpsFetch.java` 为 Java 8 class 字节码。
  2. `jar` 打包为临时 `classes.jar`。
  3. `d8 --release --min-api 26` 将 class 转换为 Android `classes.dex`。
  4. 最终归档输出至 `module/bin/llc-https.jar`。
- **调用方式**：
  ```powershell
  .\tools\build-https.ps1 [-OutputPath "<自定义输出路径.jar>"]
  ```

---

### 2. `build-info.ps1` (构建元信息生成器)

- **定位**：在编译前提取版本与源码提交信息，注入 C++ 预处理宏。
- **作用**：
  - 读取 `module/module.prop` 校验版本号格式（`vMAJOR.MINOR.PATCH`）及正整数 `versionCode`。
  - 获取 Git HEAD 提交哈希、提交计数、工作区脏状态（Dirty）以及 UTC 构建时间戳。
  - 生成 `llc_build_generated.hpp` 头文件，定义宏：
    - `LLC_RELEASE_VERSION`：基础发布版本（如 `"v0.6.2"`）。
    - `LLC_VERSION_CODE`：版本升级码（如 `602`）。
    - `LLC_GIT_REVISION`：完整 40 位 Git 提交哈希。
    - `LLC_BUILD_DIRTY`：工作区是否有未提交修改（`1` 或 `0`）。
    - `LLC_BUILD_ID`：短哈希与构建时间戳标识。
    - `LLC_BUILT_AT`：ISO-8601 UTC 时间字符串。
- **调用方式**：
  ```powershell
  .\tools\build-info.ps1 -OutputDirectory "<头文件输出目录>"
  ```

---

### 3. `build-manager.ps1` (原生管理器交叉编译)

- **定位**：使用 Android NDK 编译 arm64-v8a 架构的原生独立可执行文件 `llc-manager-native`。
- **作用**：
  - 自动探测系统中的 Android NDK Clang 编译器（`aarch64-linux-android26-clang++.cmd`）。
  - 调用 `build-info.ps1` 生成构建版本头文件。
  - 调用 `build-https.ps1` 同步更新配套的 `llc-https.jar`。
  - 静态编译链接 `manager/resource_manager.cpp`、LZMA 归档模块（`third_party/lzma/lzma_all.cpp`）、底层 JSON 解析器与存储模块，生成不依赖外部运行时的高性能管理程序。
- **调用方式**：
  ```powershell
  # 默认输出至 module/bin/llc-manager-native
  .\tools\build-manager.ps1

  # 指定自定义编译器与输出路径
  .\tools\build-manager.ps1 -Compiler "<clang++路径>" -OutputPath "<输出路径>"
  ```

---

### 4. `ci_release.py` (CI 发布校验与打包质检)

- **定位**：GitHub Actions 发布工作流专用（也可本地调用检验打包规范）。
- **主要功能**：
  1. **更新日志提取**：从 `CHANGELOG.md` 中严格提取对应版本的中文日志，校验其必须遵循 Keep a Changelog 规范，分类仅限“新增、变更、弃用、移除、修复、安全”，作为 Release 正文。
  2. **版本一致性检验**：确保 Git Tag、`module.prop` 声明的 `version` 和 `versionCode` 完全匹配。
  3. **ZIP 格式与安全性质检**：
     - 禁止路径穿透字符（如 `..`、绝对路径、反斜杠或盘符冒号）。
     - 禁止文件名大小写重复条目。
     - 校验全包 CRC 完整性。
  4. **二进制架构与元数据检验**：
     - 验证 `zygisk/arm64-v8a.so`、`libllc_core.so`、`bin/llc-manager-native` 必须为合法的 64 位 ARM ELF 二进制（ELF Machine `0xb7`）。
     - 校验二进制中是否内嵌了与当前构建一致的 Git 提交号。
     - 检验 `llc-https.jar` 内的 `classes.dex` 结构有效。
     - 检验必要许可证及安装说明文件（`LICENSE`、`THIRD_PARTY_NOTICES.md` 等）完整且非空。
     - 强制要求所有 `.sh` Shell 脚本与可执行脚本必须采用 Linux LF 换行符。
- **调用方式**：
  ```bash
  # 验证发布输入并提取 Release Notes
  python tools/ci_release.py --tag v0.6.2 --output dist/release-notes.md

  # 深度质检已打包好的 ZIP 刷机包
  python tools/ci_release.py --tag v0.6.2 --package dist/LLC-Zygisk-Localizer-v0.6.2-r118.zip --revision <40位GitCommitHash>
  ```
