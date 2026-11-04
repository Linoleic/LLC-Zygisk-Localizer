# 构建与打包

本文档说明如何从源码构建 LLC-Zygisk-Localizer 模块、安装到设备以及运行测试。项目概览见 [README.md](README.md)。

---

## 环境准备

1. **操作系统**：Windows（PowerShell 5.1 或 PowerShell 7+）
2. **Android NDK**：推荐 `r26` 及以上版本（脚本会自动探测环境变量 `ANDROID_NDK_HOME`、`NDK_ROOT` 或本地 Android SDK 默认路径，详见 [build.ps1](build.ps1)）
3. **Git**：用于推导构建版本号
4. **（可选）WSL 与 g++**：用于在 Windows 主机上运行依赖 POSIX 接口的测试用例
5. **JDK 17+ 与 Android SDK Build Tools**：构建随模块分发的 `llc-https.jar`。脚本使用 PATH 中的 `javac`、`jar` 和 SDK 中的 D8；辅助程序使用 Java 8 字节码与最低 API 26。

---

## 版本规则

打包脚本基于 **Git 提交信息** 自动推导版本：

- **版本号格式**：`v0.4.1-r<提交数>`（例如第 77 次提交生成 `v0.4.1-r77`，`versionCode=487`）。
- **本地修改标记**：若工作区存在未提交改动，版本号会自动追加 `-local`（如 `v0.4.0-r65-local`），方便区分调试版本与正式提交版本。
- **打包机制**：构建时会在暂存目录 `dist/stage/` 中生成包含真实版本的 `module.prop` 并据此打包，不会修改仓库源码；仓库中的 [module/module.prop](module/module.prop) 仅为模板占位符，请通过 `build.ps1` 打包，不要直接手动压缩 `module/` 目录。

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
