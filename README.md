# LLC-Zygisk-Localizer

《边狱公司》（Limbus Company）Android 客户端的运行时简体中文本地化原型。目标进程为 `com.ProjectMoon.LimbusCompany`，当前构建仅输出 `arm64-v8a`。

模块通过 Zygisk 加载原生核心，读取本地汉化 JSON 和字体 AssetBundle，修改 IL2CPP 文本数据及 TextMeshPro 字体回退列表。项目仍处于开发验证阶段，尚未完成独立安装、版本适配和稳定性验收。

本文依据 2026-10-28 的本地源码、脚本、发布物检查和已部署设备验证整理。“已实现”表示存在对应代码，不代表已通过完整验收。实机已确认部分 UI、人格名称和序章剧情中文生效，同时两次冷启动复现技能/被动等内容漏译，详见[实机验证记录](docs/verification/2026-10-28-device/REPORT.md)。历史 README 中未获证据支持的性能和稳定性结论仍不作为能力承诺。

## 当前状态

| 项目 | 当前情况 |
| --- | --- |
| 源码构建 | 本次接手审查中，两层源码按现有参数在临时目录编译通过，无编译警告 |
| 构建工具 | Windows PowerShell；脚本使用 NDK `26.3.11579264`、目标 API 26、C++17 |
| 运行环境 | 已测设备：Android 16 / API 36、4 KB 页面、SELinux Enforcing、KernelSU + Zygisk Next 1.5.2；其他组合待验证 |
| 游戏适配 | 已测游戏 1.117.1 / versionCode 485，Unity 6000.3.12f1；剧情若干偏移已由实机反射核对，完整布局依据仍待补齐 |
| 外部资源 | 汉化 JSON、剧情文件和 `tmpchinesefont` 未包含在当前项目或 v0.2.0 ZIP 中 |
| 独立安装 | 安装脚本未准备完整的核心可访问路径，开发部署依赖 ADB 复制文件 |
| 发布物 | v0.2.0 ZIP 内核心与目录内核心不同；模块元数据仍为 `v0.1.0-probe` |
| 实机验证 | 两次冷启动及登录成功；UI/人格与序章剧情抽样生效，技能/被动/语音等存在漏译；运行中 Stage 1 与磁盘实现的日志不一致 |
| 版本管理 | 已建立本地 Git 仓库，分支为 `master`；初始快照包含当前源码和文档，不包含此前开发历史 |

编译目标 API 26 不代表已经验证 Android 8.0 及以上兼容性。当前资料不足以确认 Android 最低版本、所有 Root 方案或 GLES/Vulkan 组合的支持范围。

## 架构与源码

```text
jni/main.cpp
  preAppSpecialize：按 nice_name 匹配目标进程
  postAppSpecialize：创建 Stage 1 加载线程
    轮询 /proc/self/maps，等待 libil2cpp.so（约 60 秒超时）
    再等待 500 ms，依次尝试 dlopen 加载 libllc_core.so
    查找 llc_core_init 并调用

jni/core/core_main.cpp
  llc_core_init：解析 IL2CPP 导出符号，创建核心工作线程
    加载本地 JSON
    等待运行时，调用 il2cpp_thread_attach
    尝试异步加载字体包、修正材质并注入 fallback
    每轮等待约 250 ms，检查文本数据、剧情和诊断触发器
```

`il2cpp_thread_attach` 注册运行时线程，不会将代码调度至 Unity 主线程。目前 Unity 对象操作与数据补丁均由工作线程发起，线程安全问题尚未修复。

### Stage 1 加载路径

Stage 1 源码为 `jni/main.cpp`，产物为 `module/zygisk/arm64-v8a.so`。它只匹配主进程的 nice_name，未按 UID 区分 Android 用户或工作资料。核心候选路径依次为：

1. `/data/data/com.ProjectMoon.LimbusCompany/cache/libllc_core.so`
2. `/data/user/0/com.ProjectMoon.LimbusCompany/cache/libllc_core.so`
3. `/data/local/tmp/llc/libllc_core.so`
4. `/data/adb/modules/llc_zygisk/libllc_core.so`

前两个路径在主用户环境下一般指向同一应用目录。缓存核心优先于模块内核心，升级 ZIP 后可能仍使用旧缓存；清缓存则可能删除核心。验证时必须记录实际加载路径和核心哈希。

加载发生在应用沙箱建立后，不能假定应用能访问模块目录或临时目录。当前没有使用 companion 或预先保存可用文件描述符的完整加载方案。仅刷入 ZIP，与运行电脑端部署脚本后的环境不等价。

### Stage 2 文件职责

| 文件（相对 `jni/core/`） | 职责 |
| --- | --- |
| `core_main.cpp` | 核心入口、TextDataSet 查找、补丁时序及诊断触发 |
| `il2cpp_api.hpp` | IL2CPP 导出解析、类与方法查找 |
| `json_loader.cpp` | UI、内容、语音、章节及剧情 JSON 加载 |
| `storage.cpp` / `storage.hpp` | 全局翻译映射及手写 JSON 提取辅助函数 |
| `json_models.hpp` | 本地翻译数据结构 |
| `data_patcher.cpp` | 遍历字典和列表，按 ID 替换文本引用 |
| `font_injector.cpp` | 字体包加载、材质处理、fallback 追加、文本/字体转储 |
| `story_hook.cpp` | 轮询活动剧情实例、触发剧情补丁及反射检查 |
| `And64InlineHook.cpp` | 仍参与编译；本次审查未发现活动业务代码调用其 Hook 安装接口 |

`jni/core/legacy/` 保存旧实现，包括原生 Hook 路径，不在当前脚本的编译列表中。`story_hook` 文件名及日志中的 `A64 Hooked` 不能证明已安装 Hook；`is_story_hook_installed()` 实际只返回 watcher 初始化标记。

### 当前实现边界

- 字体通过 `LoadFromFileAsync`、`LoadAllAssetsAsync` 加载，按名称中的 `SourceHanSans` 选择资产。fallback 用列表 `Add` 追加，不保证最高优先级；后续新加载字体也未得到持续扫描保证。
- 内容补丁覆盖 UI、人格、技能、被动、状态、关键词、E.G.O、角色、语音、放映室、横幅、章节和关卡等数据。实际覆盖取决于外部资源、对象布局及加载时序。
- 剧情沿 `GlobalGameManager → _story → _activeStoryManager → _readingScenario` 查找，再读取对应 JSON。约 250 ms 的轮询不能保证在首句显示前完成，也没有补丁后刷新已显示台词的完整流程。
- 当前业务路径以对象数据修改为主。这不能作为免检测、账号安全、长期稳定或“零开销”的保证。

## 目录与外部资源

```text
LLC-Zygisk-Localizer/
├── README.md
├── build.ps1                     # 编译、打包、ADB 部署；默认也会部署
├── config.json                   # 当前代码和脚本未读取
├── jni/
│   ├── main.cpp                  # Stage 1
│   ├── zygisk.hpp                # Zygisk API
│   └── core/                     # Stage 2，含 legacy/ 历史实现
├── module/
│   ├── module.prop
│   ├── customize.sh              # 安装提示及文件权限设置
│   ├── META-INF/com/google/android/
│   ├── zygisk/arm64-v8a.so
│   └── libllc_core.so
└── dist/
    ├── libllc_core.so
    ├── LLC-Zygisk-Localizer-v0.1.0-probe.zip
    └── LLC-Zygisk-Localizer-v0.2.0.zip
```

UI/内容词库固定读取：

```text
/data/user/0/com.ProjectMoon.LimbusCompany/files/Lang/LLC_zh-CN/
```

字体包固定读取：

```text
/data/user/0/com.ProjectMoon.LimbusCompany/files/tmpchinesefont
```

剧情文件名为 `<剧情ID>.json`，依次查找以下目录；内部目录另有一次大写 ID 回退查找：

```text
/data/data/com.ProjectMoon.LimbusCompany/files/Lang/LLC_zh-CN/StoryData/
/data/user/0/com.ProjectMoon.LimbusCompany/files/Lang/LLC_zh-CN/StoryData/
/sdcard/Android/data/com.ProjectMoon.LimbusCompany/files/Lang/LLC_zh-CN/StoryData/
```

普通内容和 UI 未实现相同的外部存储回退。`config.json` 虽有 `CurrentLanguage`，但当前没有读取逻辑，修改它不会改变语言或路径。非主用户、应用分身及工作资料目录尚未适配。

当前脚本不负责下载或部署这些资源。资源来源、版本/哈希、目录树、代表性 JSON、字体包构建方式和使用许可均需补充，不能仅凭模块 ZIP 复现完整汉化环境。

## 构建与开发部署

### 版本管理范围

Git 跟踪源码、构建脚本、模块安装骨架、配置及文档，包括实机验证的 `REPORT.md`。`dist/`、编译得到的 `.so`/`.o`/`.a`、ZIP 包以及实机原始日志、截图和资源摘录由 `.gitignore` 排除，仍保留在本地。

验证报告中指向原始证据的相对链接仅在保留这些附件的本地目录可用，克隆仓库不会得到原始附件。`.gitattributes` 将文本检出为 LF，避免 Windows 的全局换行设置影响 Android 安装脚本。当前未配置远程仓库。

### 工具要求

现有脚本写死了本机 Windows 路径下的 NDK 和 ADB：

```text
C:\Users\user\AppData\Local\Android\Sdk\ndk\26.3.11579264\toolchains\llvm\prebuilt\windows-x86_64\bin\aarch64-linux-android26-clang++.cmd
C:\Users\user\AppData\Local\Android\Sdk\platform-tools\adb.exe
```

脚本还预设了无线 ADB 序列号。换机器需调整工具路径，换设备需显式传入 `-TargetSerial`。当前未提供 Linux 构建脚本，也未声明已验证的 PowerShell 最低版本。

### 仅编译，不部署

`build.ps1` 没有只编译、不部署开关。在项目根目录执行以下命令，可沿用现有参数，将产物输出至新建临时目录：

```powershell
$ErrorActionPreference = 'Stop'
$ProjectRoot = (Get-Location).Path
$BuildOutput = Join-Path $env:TEMP ('llc-build-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $BuildOutput | Out-Null
$Clang = 'C:\Users\user\AppData\Local\Android\Sdk\ndk\26.3.11579264\toolchains\llvm\prebuilt\windows-x86_64\bin\aarch64-linux-android26-clang++.cmd'
$CoreDir = Join-Path $ProjectRoot 'jni\core'
$Sources = @(
    'storage.cpp', 'json_loader.cpp', 'font_injector.cpp', 'data_patcher.cpp',
    'And64InlineHook.cpp', 'story_hook.cpp', 'core_main.cpp'
) | ForEach-Object { Join-Path $CoreDir $_ }

& $Clang -shared -fPIC -std=c++17 -O3 -Wall -Wextra -I $CoreDir $Sources -llog -ldl -o (Join-Path $BuildOutput 'libllc_core.so')
if ($LASTEXITCODE -ne 0) { throw 'Stage 2 compilation failed' }
& $Clang -shared -fPIC -std=c++17 -O3 -Wall -Wextra -I (Join-Path $ProjectRoot 'jni') (Join-Path $ProjectRoot 'jni\main.cpp') -llog -ldl -o (Join-Path $BuildOutput 'arm64-v8a.so')
if ($LASTEXITCODE -ne 0) { throw 'Stage 1 compilation failed' }
$BuildOutput
```

这只复现编译结果，不修复运行库依赖和 16 KB 对齐问题，也不验证 Android 加载与运行。

### 现有脚本实际行为

| 调用方式 | 编译/打包 | 对设备的操作 |
| --- | --- | --- |
| 默认 | 编译两层，更新 `module/`，生成 v0.2.0 ZIP | 推送 ZIP；复制核心到临时目录、应用缓存及模块目录 |
| `-CoreOnly` | 只编译核心，更新 `dist/` 和 `module/`，不重新打 ZIP | 仍会复制核心到设备 |
| `-Install` | 默认流程 | 额外调用 KernelSU 的 `ksud module install`，直接复制待更新模块到活动模块目录 |

确认目标设备后，开发部署命令为：

```powershell
$Serial = '替换为 adb devices 中的目标序列号'
powershell -ExecutionPolicy Bypass -File .\build.ps1 -TargetSerial $Serial
# 仅更新核心：追加 -CoreOnly
# 调用 KernelSU 安装流程：追加 -Install
```

`-Install` 没有对应的 Magisk/APatch 管理器命令。默认流程和 `-CoreOnly` 都会改写设备文件，不应当作离线构建命令。

复制 Stage 2 不会替换现有游戏进程已映射的核心。更新前应结束游戏，部署后再启动；Stage 1 更新按现有工作流需重启设备，再核对加载日志。

```powershell
adb -s $Serial shell am force-stop com.ProjectMoon.LimbusCompany
# 此后执行选定的构建/部署命令，再手动启动游戏。
```

脚本未完整检查打包和 ADB 退出码，远端复制命令末尾还有 `; true`。控制台“成功”字样不能替代设备文件、哈希、属主、SELinux 上下文和实际加载路径检查。

## 诊断方法

游戏运行且核心进入 watcher 循环后使用。`$Serial` 为选定设备序列号，实际访问权限取决于设备环境。

```powershell
adb -s $Serial logcat -d -s LLC_Zygisk LLC_CORE
```

Stage 1 使用 `LLC_Zygisk`；当前核心日志宏使用 `LLC_CORE`，包括剧情日志。优先检查核心来源、入口调用、IL2CPP 符号解析、资源加载错误及补丁返回结果。

### 文件触发

代码支持应用内部和外部 `files/` 下的触发文件。下面使用外部目录；通常每约 1 秒检查一次，处理后删除触发文件。

```powershell
adb -s $Serial shell "su -c 'touch /sdcard/Android/data/com.ProjectMoon.LimbusCompany/files/llc_dump'"
# 确认日志中的触发及输出后读取，避免读到上次结果。
adb -s $Serial shell "su -c 'cat /sdcard/Android/data/com.ProjectMoon.LimbusCompany/files/llc_text_dump.txt'"
```

| 触发文件 | 输出 |
| --- | --- |
| `llc_dump` | 外部 `files/llc_text_dump.txt` |
| `llc_font` | 外部 `files/llc_font_dump.txt` |
| `llc_story` | 外部 `files/llc_story_inspect.txt` |
| `llc_audit` | logcat 审计报告 |

### 属性触发

```powershell
adb -s $Serial shell "su -c 'setprop debug.llc.dump 1'"
# 再次触发时改用不同的非零值，例如 2。
```

其他属性为 `debug.llc.font`、`debug.llc.story` 和 `debug.llc.audit`。重复写相同非零值不会再触发；初值会在首次诊断检查时缓存，启动前预设属性不保证触发。需要确定触发时可使用文件方式。

### 结果解释

- `entries loaded & patched` 当前主要来自运行时字典数量，不是逐项成功汉化计数。
- `Story Hook ACTIVE` 只表示 watcher 初始化，不能证明剧情替换成功。
- fallback 记录不能证明字形、材质、换行和遮挡均正常，仍需画面验收。
- 文本转储辅助定位，不能替代剧情首句、切场景、技能不同等级和真实渲染检查。
- 历史 README 的 15 套字体、9228 条 UI、992 个剧情等数字缺少当前资源清单及原始日志，暂不列为验收结果。

## 已知问题与后续顺序

以下问题尚未修复。P1 优先于功能扩展；条件性问题需确认目标环境后验证。

| 编号 | 优先级 | 问题与影响 | 入口 |
| --- | --- | --- | --- |
| A01 | P1，部署兼容风险 | 两层均依赖 `libc++_shared.so`，包内未提供完整方案；现有进程已映射游戏提供的运行库，Stage 2 可运行，当前磁盘 Stage 1 重启后仍待验证 | `build.ps1`、ELF 依赖、实机记录 |
| A02 | P1 | 加载依赖开发部署留下的缓存/临时文件；沙箱访问、清缓存及升级流程未闭环 | `jni/main.cpp`、安装脚本 |
| A03 | P1 | 工作线程直接调用 Unity API，并与游戏并发读写数据 | `core_main.cpp`、`font_injector.cpp`、`data_patcher.cpp` |
| A04 | P1，条件性 | 裸指针写托管引用绕过写屏障；启用增量 GC 时存在存活判断风险 | `data_patcher.cpp`；GC 模式待确认 |
| A05 | P1，适配风险 | 对象/字典布局硬编码，部分字段或键被尝试解释为字符串指针；缺少版本与类型校验 | 数据、剧情及核心调度代码 |
| A06 | P2，实机两次复现 | 人格就绪时其他表仍为 0，但全量补丁已标记完成；后加载的技能/被动等出现英文漏译 | `all_content_patched`、实机事件日志 |
| A07 | P2 | 剧情补丁失败也缓存最后实例，同一实例就绪后不再重试 | `story_hook.cpp` |
| A08 | P2，已复现 | JSON 不支持完整 Unicode 转义，并依赖字段顺序，合法输入可能丢失文本 | `storage.cpp`、`json_loader.cpp` |
| A09 | P2 | 打包/部署错误未完整检查，命令失败仍可能打印成功 | `build.ps1` |
| A10 | P2，兼容性 | 两层库 LOAD 段为 4 KB 对齐，未完成 16 KB 页面适配 | ELF、构建参数 |
| A11 | P2 | ZIP、目录内核心和元数据版本不一致，难以对应实测版本 | 下方发布物快照 |
| A12 | P2，实机发现 | 运行中 Stage 1 日志与磁盘/源码字符串不一致，疑似此前版本仍驻留 Zygote；重启游戏后差异仍存在 | 实机验证记录 |

已取得一套设备与游戏基线。当前功能修复优先处理已复现的内容补丁时序；同时核对驻留 Stage 1，补齐独立加载、主线程与安全字段访问，再处理其他重试和 JSON 问题，统一发布物并扩展实机验收。

技术依据：[Zygisk C++ 运行库要求](https://github.com/topjohnwu/zygisk-module-sample#c-stl)、[Zygisk API 与沙箱限制](https://github.com/topjohnwu/zygisk-module-sample/blob/master/module/jni/zygisk.hpp)、[Unity 主线程要求](https://docs.unity3d.com/6000.0/Documentation/Manual/async-awaitable-continuations.html)、[增量 GC 写屏障](https://docs.unity3d.com/cn/2021.2/Manual/performance-incremental-garbage-collection.html)、[Android 16 KB 页面适配](https://developer.android.com/guide/practices/page-sizes)。这些资料说明通用约束，不代替本游戏版本实测。

## 验证记录

### 2026-10-28 接手审查

- 未修改运行时代码，未向设备推送、安装或执行调试命令。
- 按现有 NDK 和 `-shared -fPIC -std=c++17 -O3 -Wall -Wextra` 参数在临时目录编译两层，均通过，无编译警告。
- 使用 `llvm-readelf` 检查依赖和对齐；检查 ZIP 清单、元数据及核心 SHA-256。
- 用 Windows 主机编译现有 `storage.cpp`、`json_loader.cpp` 的最小复现程序，仅为 Android 日志及动态加载声明提供主机桩，未调用 Unity/IL2CPP API。
- 本轮文档修订再次核对源码路径、脚本行为及下列发布物哈希。
- 文档中的 6 段 PowerShell 示例通过语法解析；实际运行了“仅编译，不部署”示例，两层编译通过。其余涉及设备的示例未执行。

| 解析输入 | 预期 | 实际 |
| --- | --- | --- |
| `{"content":"\u4e2d\u6587"}` | `中文` | `u4e2du6587` |
| 剧情 `content` 位于 `id` 前 | 保留内容 | 内容为空；放在 `id` 后的对照输入正常 |
| 技能等级 `name` 位于 `coinlist` 后 | 保留名称 | 名称为空 |

上述结果不能证明 Android 动态加载、偏移、视觉效果或运行稳定性。

### 发布物快照

哈希仅对应本次检查，重新构建后需更新。

| 位置 | 核心大小（字节） | 核心 SHA-256 |
| --- | --- | --- |
| v0.2.0 ZIP 内 `libllc_core.so` | 111240 | `2c5a20494ec1f00cb85bd30ea2c33157e98602c91a05c8e2ebc11ffafa29cd92` |
| `module/libllc_core.so` | 254784 | `998b05b9541ebd23e4a5f9d99b6b14744e5628fa43bd3ce16ab179cb1fcbf1a5` |
| `dist/libllc_core.so` | 254784 | `998b05b9541ebd23e4a5f9d99b6b14744e5628fa43bd3ce16ab179cb1fcbf1a5` |

ZIP 内 `module.prop` 为 `id=llc_zygisk`、`version=v0.1.0-probe`、`versionCode=100`。ZIP 文件名不能作为核心版本依据。

### 2026-10-28 已部署设备验证

通过 ADB 在现有测试设备完成两次应用冷启动、登录、人格/技能/被动详情检查及序章 0-1 回放，未重新部署或重启设备。Stage 2 与当前源码重编译产物哈希一致，使用应用缓存核心加载。字体转储中 14 个非中文字体均含中文 fallback；序章 `S001B` 日志匹配 92/92 行，前两句、地点和说话人抽样显示中文。

两次均在技能、被动、状态等表仍为 0 时打印全量完成，稍后这些表已经填充却没有重试；画面与文本转储确认技能/被动、角色语音字幕及章节说明仍有英文。运行中的 Stage 1 日志与当前磁盘实现不一致，不能将现有启动结果视为当前 Stage 1 随系统重启后的验证。完整基线、时序、截图及证据见[实机验证记录](docs/verification/2026-10-28-device/REPORT.md)。短时观察未见进程崩溃，尚未完成长期与 GC 压力测试。

### 后续实机验收

每次记录游戏版本/构建标识、`libil2cpp.so` 标识、设备和系统版本、页面大小、Root/Zygisk 版本、模块/核心/资源哈希、操作步骤、预期、实际结果及证据位置。

至少覆盖：没有预存核心的干净安装；资源缺失或延迟就绪；清缓存与核心升级；连续冷启动；登录到主界面；技能各等级及状态词条；剧情首句与重进；切场景和字体渲染；长时间运行及 GC 压力；目标范围内的 16 KB 页面设备。失败时保留 tombstone/logcat 和对应二进制。

## 接手待确认事项

这些信息无法仅从当前源码和目录恢复，需上一位维护者补充。其余实现细节可从源码继续调查。

1. **测试基线与最后可用版本**：游戏版本/安装来源、设备和 Android 版本、Root/Zygisk 实现及版本、SELinux 状态、页面大小、最后成功核心的哈希。若另有 Git 仓库，请给地址、分支和提交。
2. **资源与字体复现**：汉化包来源、版本/哈希、获取方式、目录树及代表性 JSON；字体包的 Unity/TMP 版本、构建目标、图形 API、构建工程/步骤及使用许可。是否依赖其他模块先安装资源？
3. **布局依据**：TextDataSet、字典 Entry、技能等级及剧情偏移来自哪个游戏版本？是否保留 dump、元数据、反射输出和字段类型？更新游戏后如何重新确认？
4. **成功范围与证据**：哪些界面、技能等级和剧情确实验证过？字体、首句、长时间运行及重复冷启动分别验证到什么程度？请给日志/转储/画面位置与复现步骤，不只提供总条目数。
5. **设备上的额外操作**：是否改过 SELinux、文件上下文/权限、Zygisk 配置或其他模块？核心实际从哪里加载？`libc++_shared.so` 由谁提供？是否做过无电脑端预部署的干净安装？
6. **故障与取舍记录**：切换到轮询前后的崩溃、退出或检测日志对应哪些版本和修改？此前“防封/稳定”结论属于推测还是有原始记录？是否有未合入当前目录的修复？

补齐前继续按“原型、兼容范围待验证”维护，不把历史描述转写为已验证结论。
