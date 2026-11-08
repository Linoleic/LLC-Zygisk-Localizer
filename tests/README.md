# 自动化回归与单元测试套件 (tests)

本目录包含 LLC-Zygisk-Localizer 的完整自动化测试套件。测试涵盖底层 C++ 引擎逻辑、Android 模块安装/卸载脚本、HTTPS 网络辅助组件以及前端 WebUI 界面交互。

所有测试均可脱离真实 Android 手机，在开发者 PC（Windows/WSL 或 Linux 宿主机）及 GitHub Actions CI 环境中高效运行。

---

## 目录结构

```text
tests/
├── run-host-tests.ps1            # 自动化测试统一调度脚本（离线宿主）
├── run-device-tests.ps1          # 自动化端到端测试调度脚本（Android 实机）
│
├── archive_test.cpp              # ZIP 与 7z/LZMA 解压引擎测试
├── config_test.cpp               # 配置持久化与字体/语言配置测试
├── content_tables_test.cpp       # 平铺文本表与内存字符串检索测试
├── first_activation_test.cpp     # 首次激活与应用 UID 权限传递测试
├── font_lifecycle_test.cpp       # Unity 对象原生池/GC与字体Fallback注入测试
├── json_loader_test.cpp          # 底层 JSON 解析器与大型字典容错测试
├── manager_ui_test.cpp           # 管理器状态暴露契约测试
├── pack_transaction_test.cpp     # 资源包更新事务原子性与回滚测试
├── reload_test.cpp               # 动态热重载管理器测试
├── resource_download_test.cpp    # 资源下载、分段与校验测试
├── resource_manager_test.cpp     # 管理器全生命周期与重启判定测试
├── status_diagnostics_test.cpp   # 诊断信息收集与日志测试
├── story_test.cpp                # 剧情拦截匹配与回退测试
├── structured_content_test.cpp   # il2cpp 反射模拟与结构化对象覆盖测试
├── translation_pack_test.cpp     # 汉化包目录索引与元数据测试
├── update_plan_test.cpp          # 更新策略决策算法测试
│
├── test_customize.py             # customize.sh 安装脚本逻辑测试
├── test_uninstall.py             # uninstall.sh 卸载清理测试
├── test_https_fetch.py           # Java HTTPS 辅助模块测试
├── test_ci_release.py            # CI 发布校验工具自身的单测
│
├── webui_bridge_test.mjs         # WebUI 通信桥接层测试
├── webui_model_test.mjs          # WebUI ViewModel 状态机测试
├── webui_browser_test.mjs        # WebUI 模拟 DOM 完整渲染测试
│
├── https/
│   └── HttpsFixture.java         # HTTPS 辅助测试夹具
└── stubs/                        # 宿主机编译桩代码（模拟 Android 日志与系统头文件）
    ├── dlfcn.h
    └── android/log.h
```

---

## 运行测试

### 1. 运行全部测试

在仓库根目录下运行调度脚本：

```powershell
.\tests\run-host-tests.ps1
```

该脚本将自动按序执行：
1. 自动发现并编译运行所有 `tests/*_test.cpp`。
2. 运行所有 Python 系统测试 `tests/test_*.py`。
3. 调用 Node.js 测试运行器执行所有 `tests/*_test.mjs`。

### 2. 常用参数与过滤

```powershell
# 仅运行指定名称模式的测试（通配符匹配）
.\tests\run-host-tests.ps1 -Filter 'resource_*'

# 仅运行 C++ 单元测试，跳过 Python 与 Node.js
.\tests\run-host-tests.ps1 -SkipPython -SkipNode

# 仅运行 WebUI 前端测试
.\tests\run-host-tests.ps1 -Filter '*webui*' -SkipPython

# 指定自定义编译器或解释器路径
.\tests\run-host-tests.ps1 -Compiler "g++" -Python "python" -Node "node"
```

---

## 测试用例分类与详解

### 一、C++ 单元测试 (`*_test.cpp`)

所有 C++ 测试通过 C++17 编译。依赖 POSIX 接口的测试在 Windows 下会自动借助 WSL 的 `g++` 编译执行；纯数据逻辑测试则直接由宿主机编译器执行。

| 测试文件 | 测试范畴与核心验证点 |
| :--- | :--- |
| **`archive_test.cpp`** | ZIP 与 LZMA（7z）解压缩。验证解压完整性、CRC 校验、以及防范路径穿透攻击（如 Zip Slip、以斜杠或驱动器盘符开头的非法路径）。 |
| **`config_test.cpp`** | 配置文件读写与序列化。验证字体设定、语言切换、静态覆盖规则以及持久化更新。 |
| **`content_tables_test.cpp`** | 平铺数据表性能与安全性。验证在大量文本条目下的字符串哈希计算、内存偏移计算与极低开销的键值检索。 |
| **`first_activation_test.cpp`** | 首次释放激活流程。模拟游戏应用在 Root 初始化后的权限继承与安全访问上下文。 |
| **`json_loader_test.cpp`** | JSON 文本加载器。验证残缺 JSON、类型不匹配、畸形数据下的解析容错与异常安全保证。 |
| **`manager_ui_test.cpp`** | 管理器与 WebUI 接口契约。验证 `llc-manager-native status --json` 生成的状态模型结构。 |
| **`pack_transaction_test.cpp`** | 事务原子性。模拟写入中断、掉电或临时文件异常，验证更新失败时能干净回滚至原版本，绝不留下半解压状态。 |
| **`reload_test.cpp`** | 运行时热重载。验证本地汉化包文件变更时，核心能否在不杀死游戏进程的前提下平滑刷新内存词表。 |
| **`resource_download_test.cpp`** | 资源下载与断点处理。验证大文件哈希校验及元数据对齐（集成用例默认跳过，需提供实际包体验证）。 |
| **`resource_manager_test.cpp`** | 管理器状态机。测试 `.boot_id` 匹配逻辑、重启状态判定（`is_reboot_required`）、清理残留指令及包体回滚。 |
| **`status_diagnostics_test.cpp`** | 运行时健康指标。测试核心注入状态收集、进程标识、Managed Fields 与异常排查日志格式。 |
| **`story_test.cpp`** | 剧情脚本本地化。验证对剧情对话编号（Story ID）的安全校验、文本覆写规则以及遇到缺失文本时的游戏原版降级。 |
| **`structured_content_test.cpp`** | il2cpp 运行时内存映射。通过模拟 il2cpp 的类型元数据和内存偏移，验证技能、人格、被动等复杂游戏实体的字段替换。 |
| **`translation_pack_test.cpp`** | 汉化包结构规范。测试官方与镜像汉化包的目录组织、字体包关联以及全局哈希验证。 |
| **`update_plan_test.cpp`** | 更新策略规划。验证版本比对（语义化与版本戳）、更新检查缓存有效时间及网络故障时的镜像降级决策。 |

---

### 二、Python 系统级测试 (`test_*.py`)

主要覆盖跨模块脚本、安装包逻辑及网络通信层：

1. **`test_customize.py`**
   - **验证重点**：Magisk / KernelSU / APatch 安装脚本 `module/customize.sh` 的逻辑。
   - **核心断言**：
     - **全新安装**：清除旧标识，正确提示重启设备。
     - **热更新升级**：精准继承上一次启动的 `.boot_id`，不生成重启标识，确保 WebUI 状态保持为无需重启，并输出清晰的操作提示文案。

2. **`test_uninstall.py`**
   - **验证重点**：卸载脚本 `module/uninstall.sh`。
   - **核心断言**：卸载时必须干净清理游戏目录下的注入缓存、释放的 `libllc_core.so` 副本以及临时文件，确保游戏完全还原至官方原始纯净状态。

3. **`test_https_fetch.py`**
   - **验证重点**：`manager/https/HttpsFetch.java` 的网络调用逻辑。
   - **核心断言**：在本地启动 HTTP/HTTPS 测试服务，验证 Java 运行时对 TLS、重定向、分块传输以及文件流保存的稳定性。

4. **`test_ci_release.py`**
   - **验证重点**：`tools/ci_release.py` 自动化质检工具自身。
   - **核心断言**：验证对合规 CHANGELOG 的准确解析、非法换行符的捕获、畸形 ZIP 包与非法 ELF 架构的拦截率。

---

### 三、Node.js 前端 WebUI 测试 (`webui_*_test.mjs`)

通过 Node.js 内置的 `--test` 框架运行，无需启动浏览器进程：

1. **`webui_bridge_test.mjs`**
   - **验证重点**：前端与底层管理器的桥接通信协议（`webroot/bridge.mjs`）。
   - **核心断言**：支持 KernelSU `ksu.exec` 执行模式与离线调试 Mock 模式，确保超时、错误码能够被正确捕获和处理。

2. **`webui_model_test.mjs`**
   - **验证重点**：前端数据模型与业务逻辑（`webroot/view-model.mjs`）。
   - **核心断言**：数据单位转换（B/KB/MB）、ISO 时间格式化、版本新旧比较、状态机转换逻辑。

3. **`webui_browser_test.mjs`**
   - **验证重点**：完整 DOM 渲染与用户交互行为（`webroot/app.mjs`）。
   - **核心断言**：在模拟 DOM 中测试标签页切换（资源/字体/设置/杂项）、按钮激活与禁用逻辑、下载进度条百分比展示、Toast 弹窗以及重启提示横幅的呈现。

---

### 四、辅助桩代码与夹具

- **`tests/stubs/android/log.h`**：提供 `__android_log_print` 的宿主机实现（转为 `printf` 输出），让使用 Android Log API 的 C++ 代码能在非 Android 主机上直接编译。
- **`tests/stubs/dlfcn.h`**：提供动态符号加载函数的空实现桩。
- **`tests/https/HttpsFixture.java`**：为 Java HTTPS 测试提供模拟响应与本地打桩支持。
