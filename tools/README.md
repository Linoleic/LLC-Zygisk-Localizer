# 构建与发布工具 (tools)

本目录包含 LLC-Zygisk-Localizer 的核心辅助工具与脚本，主要用于 Android 本地二进制构建、构建元信息生成以及 GitHub Actions CI 自动化发布校验。

---

## 目录结构

```text
tools/
├── build-https.ps1      # Android DEX (llc-https.jar) 编译脚本
├── build-info.ps1       # 构建元数据与版本头文件生成脚本
├── build-manager.ps1    # 原生管理器 (llc-manager-native) 交叉编译脚本
├── ci_release.py        # CI 发布校验与更新日志提取工具
├── localization_audit.cpp # 离线核对游戏原文和汉化包
└── run_localization_audit.py # 记录输入指纹并比较两次审计
```

## 离线汉化审计

使用支持 C++17 的宿主编译器和 Python 3.11+。将游戏原文导出和 LLC 汉化包解压到各自的语言目录，传入包含数据表 JSON 和 `StoryData/` 的那一级。

```powershell
g++ -std=c++17 tools/localization_audit.cpp -o dist/localization-audit.exe
python tools/run_localization_audit.py dist/localization-audit.exe 'D:/exports/current' 'D:/packs/LLC_zh-CN' -o dist/localization-audit.json
```

新版原文可通过 `--previous-original 'D:/exports/previous'` 进行比较。传入 `--baseline dist/previous-audit.json` 可同时比较两份审计报告，输出新增、已补齐、持续存在、待复核的条目，以及文件状态变化。判断缺口已补齐时要求两次报告使用相同的原文快照；原文变化后消失的缺口列为待复核。脚本会记录各输入目录的 JSON 文件数和 SHA-256 指纹。

工具递归发现 JSON 文件，逐文件核对 `StoryData` 和模块共用规则识别的单字段表。报告记录缺译、解析后无法匹配的字段和重复 ID 的行号。其余文件列入资源清单，提供文件状态和新旧原文结构变化。单字段表的规则在 `jni/core/string_tables.inc` 维护一次，模块和审计同步使用。导出缺失或文件结构异常时，先检查文件状态再解读字段数量。

无 ID 剧情以导出行序检查候选匹配，实机行序仍由模块验证。角色名与阵营名可能来自游戏运行时的资源回退，报告会标出这些待回读字段。首句显示时序和对话记录请用实机回读验证。

## 剧情运行时审计

游戏打开后，在测试设备上通过现有的 `llc_story_check` 触发文件开启审计。等日志出现 `[LLC-STORY-AUDIT] enabled` 后，再进入要测的剧情。

```powershell
adb shell "su -c 'echo audit-start > /data/user/0/com.ProjectMoon.LimbusCompany/files/llc_story_check'"
# 回放目标剧情。停在首句时打开对话记录，然后读取该条记录。
adb shell "su -c 'echo audit-history > /data/user/0/com.ProjectMoon.LimbusCompany/files/llc_story_check'"
# 等待日志报告 history observed 或 history not observed 后结束采样。
adb shell "su -c 'echo audit-stop > /data/user/0/com.ProjectMoon.LimbusCompany/files/llc_story_check'"
adb pull /sdcard/Android/data/com.ProjectMoon.LimbusCompany/files/llc_story_audit.json dist/
```

结束后等日志出现 `[LLC-STORY-AUDIT] stopped` 再拉取报告。报告记录播放回调结束时的字段和普通对话刷新成功后的屏幕字段。对话记录只在本次会话采到一条首句、当前对白仍是这条首句、且记录页只有一个可见条目时核对正文和说话人。多条记录可继续用 `llc_dump` 手工回读。每次游戏启动默认关闭，热重载汉化时结束并导出当前审计。

阶段状态包括 `equal`、`different`、`mixed`、`not_applicable`（无预期译文且实读为空）、`no_expected_translation`（有显示值但无法得到预期译文）、`unavailable`（采样时无法读取或对齐）和 `not_observed`（本次未采样该阶段）。判断上游缺译时，请结合离线报告。

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
