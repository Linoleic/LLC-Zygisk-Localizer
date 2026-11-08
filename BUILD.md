# 构建与打包

本文档说明如何从源码构建 LLC-Zygisk-Localizer 模块、安装到设备以及运行测试。项目概览见 [README.md](README.md)，模块系统架构与子系统拓扑详见 [ARCHITECTURE.md](ARCHITECTURE.md)。

---

## 环境准备

1. **操作系统**：Windows（PowerShell 5.1 或 PowerShell 7+）
2. **Android NDK**：推荐 `r26` 及以上版本（脚本会自动探测环境变量 `ANDROID_NDK_HOME`、`NDK_ROOT` 或本地 Android SDK 默认路径，详见 [build.ps1](build.ps1)）
3. **Git**：用于记录构建对应的提交和工作区状态
4. **（可选）WSL 与 g++**：用于在 Windows 主机上运行依赖 POSIX 接口的测试用例
5. **JDK 17+ 与 Android SDK Build Tools**：构建随模块分发的 `llc-https.jar`。脚本使用 PATH 中的 `javac`、`jar` 和 SDK 中的 D8；辅助程序使用 Java 8 字节码与最低 API 26。

---

## 版本规则

发布版本与构建身份分别维护：

- **发布版本**：在 [module/module.prop](module/module.prop) 中显式维护 `version=vMAJOR.MINOR.PATCH` 和正整数 `versionCode`。新发布安装包须递增升级码；提交次数不再参与计算，避免历史变更导致升级码倒退。
- **包名版本**：`vMAJOR.MINOR.PATCH-r<Git提交数>`，有未提交改动时追加 `-local`。提交数只用于人类可读的包名标签，不参与升级码计算；升级码始终使用 `module/module.prop` 中显式维护的正整数 `versionCode`。源码中的发布版本保持不变。
- **构建标识**：核心和管理器内嵌完整提交号、本地修改标记、UTC 构建时间和构建 ID。完整构建共享一份生成头文件；`-CoreOnly` 和独立管理器构建也会生成身份信息。不经这些脚本构建时，诊断信息标为 `unknown` / `unidentified`。
- **打包机制**：构建时在 `dist/stage/` 中生成包含包名版本的 `module.prop`，不修改仓库源码。请通过 `build.ps1` 打包，不要直接手动压缩 `module/`。
- **诊断**：`llc-manager status --json` 的 `manager_build` 表示管理器构建，当前进程报告的 `build` 表示运行核心构建；汉化包仍使用独立的资源版本。WebUI 分别展示这些信息，旧核心未提供标识时会明确提示。

---

## 常用构建命令

各构建辅助脚本（如 `build-https.ps1`、`build-info.ps1`、`build-manager.ps1` 及 `ci_release.py`）的详细职责与参数说明详见 [tools/README.md](tools/README.md)。

```powershell
# 1. 完整构建（输出模块刷机 ZIP 至 dist/ 目录）
.\build.ps1

# 2. 仅编译核心库（仅生成 dist/libllc_core.so，用于快速调试或热替换）
.\build.ps1 -CoreOnly

# 3. 完整构建并通过 ADB 安装（调用 ksud module install）
.\build.ps1 -Install

# 4. 指定设备序列号安装（多设备连接时使用）
.\build.ps1 -TargetSerial "<设备序列号>" -Install
```

---

## 运行测试

测试套件架构、覆盖范畴及各测试用例的详细说明详见 [tests/README.md](tests/README.md)。

```powershell
# 运行全部主机测试（C++、Python、Node）
.\tests\run-host-tests.ps1

# 仅运行指定名称的 C++ 测试，跳过脚本测试
.\tests\run-host-tests.ps1 -Filter 'resource_*' -SkipPython -SkipNode

# 运行实机端到端自动化测试（验证字体激活态、Fallback 链、中文 UI 与零豆腐块）
.\tests\run-device-tests.ps1 -TargetSerial "<设备序列号>"
```

### 测试注意事项
- 测试脚本会自动扫描 `tests/` 目录下的用例，新增测试无需修改运行脚本。
- `pack_transaction`、`private_cleanup` 等依赖 POSIX 接口的测试在 Windows 下通过 WSL 的 `g++` 编译运行；未安装 WSL 时会自动跳过。
- `manager_ui`、`resource_manager` 需要可写的 `/data/local/tmp`，可在 Android 或已准备该目录的 WSL 环境运行。
- `resource_download` 还需传入真实汉化包 ZIP 与对应 GitHub Release 元数据 JSON，默认脚本会跳过；可在隔离的 Linux/Android 测试环境单独编译运行，参数顺序为 `<archive.zip> <release-metadata.json>`。

## GitHub Actions

### 使用 GitHub 自行构建

1. Fork 本仓库，在自己的仓库中打开 **Actions**，按 GitHub 提示启用工作流。
2. 选择 **Build → Run workflow**，选择要构建的分支或标签，再点击 **Run workflow**。
3. 等待测试和构建通过，在该次运行页面的 **Artifacts** 中下载 `android-module`。
4. 解压下载的构建产物，取出其中的模块 ZIP，再通过 Root 管理器安装；不要直接安装 GitHub 打包的外层归档。

手动构建不会创建 Release，也无需配置个人令牌。构建产物保留 14 天，下载需要登录 GitHub。

### 工作流与发布

- [build.yml](.github/workflows/build.yml)：推送到 `master`、向 `master` 提交 PR 或手动运行时，Ubuntu 执行主机回归，Windows 构建并校验模块 ZIP。也可供 Release 工作流调用。
- CI 使用 Node 22、Python 3.12、JDK 17，以及 NDK `26.3.11579264`、Build Tools `35.0.0` 和 Android Platform 35。浏览器测试使用 runner 自带 Chrome；Linux 测试目录由工作流预先准备。需要真实上游资源的 `resource_download` 集成测试不在常规 CI 中执行，其余测试由现有入口自动发现。
- 发布前，递增 `module/module.prop` 的版本和 `versionCode`，并在 `CHANGELOG.md` 的 `Unreleased` 下方准备对应版本条目。标题使用 `## [vMAJOR.MINOR.PATCH]`，不写日期，分类使用“新增”“变更”“弃用”“移除”“修复”“安全”中适用的项目。
- [release.yml](.github/workflows/release.yml)：将版本提交推送后，再推送同名标签（例如 `v0.5.1`）。标签触发 Release 工作流，先调用 Build 完成相同的测试与构建；全部通过后创建草稿 Release，上传 ZIP，再公开发布。Release 正文直接提取对应版本章节，保留原文，不附加生成内容或 SHA-256 文件。
- 标签必须与源码版本一致。工作流拒绝覆盖已经存在的 Release；已有草稿导致重跑失败时，先检查草稿及附件，确认后删除草稿再重跑。发布仅使用该任务的 `GITHUB_TOKEN`，无需配置个人令牌。
- Actions 通过不代表完成实机验收。准备公开版本前，仍需对相应提交进行安装与游戏内验证。
