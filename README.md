# LLC-Zygisk-Localizer

[![Platform](https://img.shields.io/badge/Platform-Android%20%28arm64--v8a%29-blue.svg)](https://developer.android.com)
[![Injection](https://img.shields.io/badge/Injection-Zygisk-success.svg)](https://github.com/topjohnwu/zygisk-module-sample)
[![Architecture](https://img.shields.io/badge/Architecture-Data--Only%20Modular-orange.svg)](#核心架构特性)
[![License](https://img.shields.io/badge/License-MIT%20%2F%20Academic-lightgrey.svg)](#免责声明)

《边狱公司》（*Limbus Company*，包名：`com.ProjectMoon.LimbusCompany`）Android 客户端的高性能、模块化运行时简体中文本地化引擎。

本项目通过 Zygisk 机制在应用启动早期注入原生运行时，采用 **100% 内存纯数据热修（Data-Only Modular Architecture）**，在不修改游戏 APK 安装包、不修改 `.text` 代码段的前提下，动态接管与热替换 Unity IL2CPP 托管堆中的文本字典、剧情节点与 TextMeshPro 字体回退链。

---

## 核心架构特性

### 1. 100% 纯数据模块化热修（Zero Inline Hooks）
- **绝不篡改代码段**：彻底摒弃任何针对 Native/IL2CPP 方法的 Inline Hook（如 `A64HookFunction`、`Dobby` 或直接修改内存指令），从原理上规避 AppSealing 反作弊系统的内存完整性校验（彻底解决错误码 `30010` 封禁与闪退问题）。
- **托管堆反射与安全遍历**：仅在 Unity 托管堆内通过安全的反射与类型遍历定位 `TextDataSet`、`Dictionary<string, string>`、`List<T>` 等结构，执行原子级字符串引用替换。
- **只读静态反射与指针有效性校验**：场景切换（如标题页进入大厅）过程中，彻底杜绝后台线程随意触发 `il2cpp_runtime_invoke` 的危险行为，全程引入安全指针边界检验（`> 0x10000`）与类名比对，确保跨场景高负载下零崩溃。
- **GC 安全写屏障**：所有托管对象字段修改均严格应用 IL2CPP GC 写屏障（Write Barrier），杜绝增量垃圾回收导致的野指针与堆破坏。

### 2. 引擎与汉化资源彻底解耦
- **轻量化模块**：刷机包仅 ~1-2 MB，只包含 Zygisk 注入入口、核心动态库与 WebUI 控制面板。
- **免重启热更新**：完整文本资产（`LimbusLocalize_Babel`）存储于应用私有目录，由内置的独立原生管理器 `llc-manager` 从官方上游 Release 或 CDN 镜像获取。
- **原子事务与安全回滚**：支持 SHA-256 全文件完整性校验、原子级版本激活与即时回滚；更新汉化只需冷启动游戏，**无需重启手机系统**。

### 3. 全局文本与界面覆盖
- **65+ 运行期数据表**：全量覆盖角色技能、被动、状态关键词、E.G.O 饰品、但丁笔记、播报员语音、镜像迷宫主题卡包、章节信息及关卡描述。
- **剧情多线程同步**：通过 Unity 主线程调度桥（`UnitySynchronizationContext.Post`）与台词身份比对，动态修正剧情对话正文、地点与说话人缓存。
- **原生登录界面提前适配**：内置 `title_overrides.json`，在远程词库加载就绪前，提前汉化标题公告、网络重试及 4 字规整的“游客登录”与“用户中心”按钮。

### 4. 统一字体渲染引擎
- **动态 TMP Fallback 注入**：启动时自动加载思源黑体（`SourceHanSans`）AssetBundle（`tmpchinesefont`），注入到所有已加载字体资源的 Fallback 链顶层。
- **图形管线兼容修复**：针对 Android Vulkan / GLES 图形管线，自动校准字体材质属性与 Shader Keyword，防止缺字、方框（Tofu）或渲染花屏。
- **持久化备份机制**：内置字体自动暂存保护，跨游戏更新与缓存清理后自动恢复字体包。

### 5. 语言动态共存与策略跟随
- 核心工作线程实时感知游戏内部的语言设置变动。支持 `跟随游戏设置`、`强制中文` 与 `恢复原版` 策略；当玩家切换为日文/英文时，自动卸载内存修改并休眠，保持多语言无缝共存。

---

## 系统架构

```mermaid
flowchart TD
    subgraph System["Android 系统环境"]
        Zygote["Zygote 进程"] -->|"fork()"| App["Limbus Company 进程"]
    end

    subgraph Stage1["Stage 1: Zygisk 注入入口"]
        Arm64["zygisk/arm64-v8a.so"] -->|"maps 监控"| WaitIl2cpp["等待 libil2cpp.so 加载"]
        WaitIl2cpp -->|"dlopen()"| LoadStage2["加载 libllc_core.so"]
    end

    subgraph Stage2["Stage 2: 纯数据原生核心"]
        CoreInit["llc_core_init"] --> Worker["核心工作线程"]
        Worker --> JsonLoader["JSON 词库加载器"]
        Worker --> FontEngine["TMP 字体与材质引擎"]
        Worker --> Patcher["纯数据内存热修器"]
        Worker --> StoryEngine["剧情状态与主线程调度"]
    end

    subgraph GameHeap["Unity IL2CPP 托管堆"]
        TDS["TextDataSet / 本地化字典"]
        Fonts["TextMeshPro 字体对象"]
        UIList["_uiList / _loginUIList"]
        Story["StoryManager / 场景台词"]
    end

    subgraph Storage["存储与更新管理"]
        Manager["llc-manager CLI"] <--> WebUI["KernelSU / Magisk WebUI"]
        WebUI <--> Remote["LLC 官方 Release / CDN"]
        Manager --> LocalPacks["/data/data/.../files/Lang/"]
    end

    App --> Stage1
    LoadStage2 --> Stage2
    JsonLoader -.-> LocalPacks
    Patcher ==>|"GC 写屏障原子替换"| TDS
    Patcher ==>|"覆盖标题与登录按钮"| UIList
    FontEngine ==>|"注入 Fallback 列表"| Fonts
    StoryEngine ==>|"主线程 Post 同步"| Story
```

---

## 运行环境要求

| 项目 | 要求 / 支持范围 |
| --- | --- |
| **操作系统** | Android 10 ~ Android 16+（API Level 26+） |
| **CPU 架构** | `arm64-v8a`（64 位 IL2CPP 运行时） |
| **内存分页** | 完美兼容 4 KB 与 16 KB 页面（Android 15+） |
| **Root 方案** | KernelSU / KernelSU-Next / SukiSU / APatch / Magisk |
| **Zygisk 环境** | 启用现代 Zygisk 实现（推荐 Zygisk Next 1.1.0+） |
| **目标游戏** | 《边狱公司》（*Limbus Company*，Google Play / TapTap 等 arm64 发行版） |

---

## 目录结构

```text
LLC-Zygisk-Localizer/
├── build.ps1                 # 自动化编译、动态版本同步与安装脚本
├── .gitignore                # 排除构建临时文件与调试证据
├── jni/
│   ├── main.cpp              # Stage 1: Zygisk 进程过滤与核心加载器
│   ├── zygisk.hpp            # Zygisk C++ API 规范头文件
│   └── core/                 # Stage 2: 纯数据热修核心引擎源码
│       ├── core_main.cpp     # 核心主循环、多语言调度与生命周期监控
│       ├── data_patcher.cpp  # IL2CPP 字典与列表内存热替换
│       ├── font_injector.cpp # TMP 字体 AssetBundle 加载与材质修复
│       ├── story_hook.cpp    # 剧情安全反射与主线程缓存调度
│       ├── json_loader.cpp   # 本地化 JSON 解析与数据装载
│       └── storage.cpp       # 词库存储管理与路径解析
├── module/                   # Magisk / KernelSU 刷机模块模板
│   ├── module.prop           # 模块元数据（由构建脚本动态更新）
│   ├── customize.sh          # 刷机安装脚本与核心暂存同步
│   ├── config/
│   │   ├── title_overrides.json # 标题与登录界面纯数据预置词条
│   │   └── compatibility.json   # 兼容性策略配置
│   ├── bin/llc-manager       # 独立本地化资源管理 CLI
│   └── webroot/              # KernelSU / Magisk WebUI 前端面板
├── tools/                    # 开发与维护工具集（资源打包、状态核验脚本）
├── tests/                    # 单元测试与主机模拟测试用例
└── docs/                     # 详细技术规范与验证报告
```

---

## 构建与打包

### 前置要求
1. **Windows PowerShell**（支持 PowerShell 5.1 / PowerShell 7+）
2. **Android NDK**：推荐 `r26` 及以上版本（脚本支持环境变量或本地 SDK 自动探测，详见 [build.ps1](build.ps1)）
3. **Git**：用于动态版本推导

### 自动化版本机制
打包脚本采用 **Git Revision 动态版本化**：
- **版本号格式**：`v0.4.0-r<Commit数>`（例如当前第 62 次提交对应 `v0.4.0-r62`，`versionCode=462`）。
- **未提交代码自动标记**：若工作区存在未提交的修改，版本号将自动追加 `-local`（如 `v0.4.0-r62-local`），方便区分调试包与正式发布包。
- **自动同步**：构建时自动将最新版本、作者及信息写回 [module/module.prop](module/module.prop)。

### 常用构建命令

```powershell
# 1. 完整构建（输出刷机 ZIP 至 dist/ 目录，不触及设备）
.\build.ps1

# 2. 仅编译 Stage 2 核心库（快速迭代 C++ 代码，输出 dist/libllc_core.so）
.\build.ps1 -CoreOnly

# 3. 完整构建并通过 ADB 直接推送到手机安装（调用 ksud module install）
.\build.ps1 -Install

# 4. 指定特定 ADB 序列号安装
.\build.ps1 -TargetSerial "<设备序列号>" -Install
```

---

## 模块管理与 WebUI

在支持 WebUI 的管理器（如 KernelSU / SukiSU）中打开本模块卡片，或通过 Root Shell 执行 `llc-manager`：

### WebUI 功能
- **状态看板**：实时查看当前磁盘汉化版本、游戏进程加载版本、内存命中词条数与覆盖率报告。
- **在线更新与切换**：一键检查官方 GitHub Release / 镜像源，支持后台下载校验、原子切换与上一版本即时回滚。
- **字体与语言策略**：在线选择语言跟随模式，管理自定义字体暂存。
- **本地代理设置**：支持为资源下载设置专用 HTTP/SOCKS 代理（如 `127.0.0.1:7890`），不影响系统全局网络。

### 原生 CLI 常用命令
```sh
# 检查更新（输出 JSON 报告）
/data/adb/modules/llc_zygisk/bin/llc-manager check

# 查看当前资源与运行进程状态
/data/adb/modules/llc_zygisk/bin/llc-manager status --json

# 激活新下载的候选版本
/data/adb/modules/llc_zygisk/bin/llc-manager activate <版本号>

# 一键回滚到上一汉化包
/data/adb/modules/llc_zygisk/bin/llc-manager rollback
```

---

## 运行排错与诊断

当需要收集日志或定位特定文本匹配情况时，可使用以下诊断工具：

### 1. ADB 实时日志过滤
```powershell
adb logcat -s LLC_Zygisk LLC_CORE
```
- `LLC_Zygisk`：展示 Stage 1 注入、进程匹配与 Stage 2 加载日志。
- `LLC_CORE`：展示字体注入、数据表遍历、剧情主线程同步与报错日志。

### 2. 触发运行时内存转储（Dump）
游戏在运行状态下，通过向外部私有目录写入触发文件可即时导出当前状态：
```powershell
# 导出当前已同步的全部文本表条目至 llc_text_dump.txt
adb shell "su -c 'touch /sdcard/Android/data/com.ProjectMoon.LimbusCompany/files/llc_dump'"

# 导出当前字体回退列表与材质状态至 llc_font_dump.txt
adb shell "su -c 'touch /sdcard/Android/data/com.ProjectMoon.LimbusCompany/files/llc_font'"

# 导出当前剧情活动场景与文本检查报告至 llc_story_state.json
adb shell "su -c 'touch /sdcard/Android/data/com.ProjectMoon.LimbusCompany/files/llc_story_check'"
```

---

## 分支模型与提交规范

- **分支规范**：
  - `master`：稳定主分支，仅合入经过完整实机验证的代码与正式版本。
  - `dev`：日常活跃开发分支，用于新特性开发与热修复。
- **提交规范**：
  - 遵循纯英文 [Conventional Commits](https://www.conventionalcommits.org/) 格式，例如：
    - `feat(patcher): add in-place dictionary synchronization`
    - `fix(core): prevent scene transition SIGSEGV by removing background invoke`
    - `chore(build): synchronize module metadata and automate revision versioning`

---

## 作者与致谢

- **[Linoleic](https://github.com/Linoleic)**：Zygisk 注入框架研发、100% 纯数据内存热修架构设计、Android 跨场景防崩溃重构、WebUI 控制面板、自定义字体引擎及移动端自动化构建体系。
- **[LLC Team (LimbusLocalize)](https://github.com/LocalizeLimbusCompany/LocalizeLimbusCompany)**：全部上游剧情、UI、人格与技能的高质量中文翻译资产及持续本地化维护。

---

## 免责声明

1. 本项目作为开源技术原型，仅供逆向工程研究、Android 原生注入技术探讨与学术交流使用。
2. 本项目严禁用于任何商业牟利行为。
3. 《边狱公司》（*Limbus Company*）及其所有美术素材、游戏文本和知识产权均归 **Project Moon** 所有。
4. 使用本模块所产生的一切后果由使用者自行承担。
