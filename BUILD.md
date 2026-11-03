# 构建与打包

本文档说明如何从源码构建 LLC-Zygisk-Localizer 模块、安装到设备，以及运行测试。项目概览见 [README.md](README.md)。

## 前置要求
1. **Windows PowerShell**（PowerShell 5.1 或 PowerShell 7+）
2. **Android NDK**：推荐 `r26` 及以上版本（脚本支持环境变量或本地 SDK 自动探测，详见 [build.ps1](build.ps1)）
3. **Git**：用于动态版本推导

## 自动化版本机制
打包脚本采用 **Git Revision 动态版本化**：
- **版本号格式**：`v0.4.0-r<Commit数>`（例如第 65 次提交对应 `v0.4.0-r65`，`versionCode=465`）。
- **本地修改标记**：若工作区存在未提交的修改，版本号将自动追加 `-local`（如 `v0.4.0-r65-local`），方便区分调试包与正式提交包。
- **自动同步**：构建时在暂存目录 `dist/stage/` 中生成带真实版本号的 `module.prop` 并据此打包，不会修改仓库中的 [module/module.prop](module/module.prop)；该文件的 `version=dev`、`versionCode=0` 仅为占位符，请通过 `build.ps1` 打包，不要直接压缩 `module/` 目录安装。

## 常用构建命令

```powershell
# 1. 完整构建（输出刷机 ZIP 至 dist/ 目录，不触及设备）
.\build.ps1

# 2. 仅编译 Stage 2 核心库（输出 dist/libllc_core.so）
.\build.ps1 -CoreOnly

# 3. 完整构建并通过 ADB 安装（调用 ksud module install）
.\build.ps1 -Install

# 4. 指定特定 ADB 序列号安装
.\build.ps1 -TargetSerial "<设备序列号>" -Install
```

## 运行测试

```powershell
# 运行全部主机测试（C++、Python、Node）
.\tests\run-host-tests.ps1

# 仅运行名称匹配的 C++ 测试，并跳过 Python / Node 测试
.\tests\run-host-tests.ps1 -Filter 'resource_*' -SkipPython -SkipNode
```

- 测试从 `tests/` 目录自动发现，新增测试文件无需修改脚本。
- `pack_transaction`、`private_cleanup` 等依赖 POSIX 接口的测试在 Windows 上通过 WSL 的 `g++` 运行；未安装 WSL 时会被跳过并给出原因。
- `manager_ui`、`resource_manager` 需要可写的 `/data/local/tmp`，`resource_download` 需要真实的发布包参数，三者仅适合在设备上运行，主机上会被跳过。
