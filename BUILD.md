# 构建与打包

本文档说明如何从源码构建 LLC-Zygisk-Localizer 模块、安装到设备以及运行测试。项目概览见 [README.md](README.md)。

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
- **包名版本**：`v0.4.1-g<12位提交号>`，有未提交改动时追加 `-local`。源码中的发布版本保持不变。
- **构建标识**：核心和管理器内嵌完整提交号、本地修改标记、UTC 构建时间和构建 ID。完整构建共享一份生成头文件；`-CoreOnly` 和独立管理器构建也会生成身份信息。不经这些脚本构建时，诊断信息标为 `unknown` / `unidentified`。
- **打包机制**：构建时在 `dist/stage/` 中生成包含包名版本的 `module.prop`，不修改仓库源码。请通过 `build.ps1` 打包，不要直接手动压缩 `module/`。
- **诊断**：`llc-manager status --json` 的 `manager_build` 表示管理器构建，当前进程报告的 `build` 表示运行核心构建；汉化包仍使用独立的资源版本。WebUI 分别展示这些信息，旧核心未提供标识时会明确提示。

---

## 常用构建命令

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

```powershell
# 运行全部主机测试（C++、Python、Node）
.\tests\run-host-tests.ps1

# 仅运行指定名称的 C++ 测试，跳过脚本测试
.\tests\run-host-tests.ps1 -Filter 'resource_*' -SkipPython -SkipNode
```

### 测试注意事项
- 测试脚本会自动扫描 `tests/` 目录下的用例，新增测试无需修改运行脚本。
- `pack_transaction`、`private_cleanup` 等依赖 POSIX 接口的测试在 Windows 下通过 WSL 的 `g++` 编译运行；未安装 WSL 时会自动跳过。
- `manager_ui`、`resource_manager` 需要设备上可写的 `/data/local/tmp`，`resource_download` 需要真实的发布包参数，这部分用例仅适合在设备上运行，主机测试中会自动跳过。
