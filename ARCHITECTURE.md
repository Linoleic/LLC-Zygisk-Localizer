# LLC-Zygisk-Localizer 技术架构文档

本文档全面梳理 LLC-Zygisk-Localizer 的系统设计、核心模块拓扑、线程与生命周期模型、安全沙箱边界以及自动化测试验证矩阵。

---

## 1. 概述与设计原则

LLC-Zygisk-Localizer 是面向《边狱公司》（*Limbus Company*）Android 客户端的非侵入式本地化注入系统。整体架构遵循以下设计原则：

- **无侵入原版客户端**：不修改游戏 APK、不注入二次打包签名、不架设本地代理或截获 HTTPS 网络，完全在游戏原生 IL2CPP 运行时内生效。
- **两阶段加载与热更新解耦**：将 Zygisk 注入引导（Stage 1）与核心数据引擎（Stage 2）彻底解耦，使得汉化引擎与字典支持热替换安装，免去频繁重启设备的成本。
- **零信任安全边界**：跨应用目录与临时目录加载具备严格的文件所有权、权限位和符号链接安全审查，防止本地提权或恶意注入。
- **高内聚子系统划分**：分离字体加载、UI 文本扫描、数据表替换、剧情观测与状态审计，保证系统具备良好的可维护性与测试覆盖率。

---

## 2. 两阶段加载架构 (Two-Stage Injection)

系统采用分层加载架构，运行时拓扑如下：

```text
[Zygote 进程]
      │ (fork)
      ▼
[游戏应用进程: com.ProjectMoon.LimbusCompany]
      │
      ├─► [Stage 1: arm64-v8a.so (Zygisk 宿主模块)]
      │         │
      │         ├─ 拦截 postAppSpecialize
      │         ├─ 安全路径审计: is_safe_library_candidate (防劫持/权限检查)
      │         └─ 动态 dlopen 候选核心库 (/data/user/0/.../cache/libllc_core.so)
      │
      └─► [Stage 2: libllc_core.so (核心数据引擎)]
                │
                ├─ 解析 IL2CPP 符号表 (il2cpp_api.hpp)
                ├─ 初始化主线程任务分发桥 (main_thread_bridge)
                ├─ 异步加载中文字体 AssetBundle 并完成着色器与材质绑定
                ├─ 挂钩 TextDataSet 数据表与故事剧情
                └─ 监听场景加载，执行 UI 动态翻译与字体统一替换
```

### Stage 1: Zygisk 引导层 (`jni/main.cpp`)
- **注入时机**：在 `postAppSpecialize` 回调中，检查当前进程包名是否匹配 `com.ProjectMoon.LimbusCompany`。若匹配，开启独立监控线程等待 Unity/IL2CPP 核心动态库加载。
- **安全候选库解析 (`is_safe_library_candidate`)**：
  - 优先级依次探测应用内部私有缓存目录、安全暂存目录与模块只读目录。
  - 严格校验候选文件：禁止世界可写（`mode & 0002`）、拒绝符号链接、必须属于 `root (uid=0)` 或当前游戏用户（`geteuid()`），并通过 `realpath` 进行物理路径解析。
- **动态隔离加载**：通过 `dlopen(..., RTLD_NOW)` 将 Stage 2 核心库动态映射入进程空间。

### Stage 2: 核心数据引擎 (`jni/core/`)
- 包含汉化解析、数据表反射修改、字体生命周期管理与 UI 翻译，完全与 Zygisk 专有 API 解耦，可独立热替换。

---

## 3. 核心子系统架构与拓扑

核心引擎按功能划分为四大子系统：

### 3.1 字体与 UI 注入子系统 (Font & UI Injection)
针对 Unity TextMeshPro (TMP) 渲染管线，划分为 4 个高内聚模块：

```text
               ┌───────────────────────────┐
               │    font_injector.hpp      │  <-- 公共门面 API
               └─────────────┬─────────────┘
                             │
       ┌─────────────────────┼─────────────────────┐
       ▼                     ▼                     ▼
┌──────────────┐      ┌──────────────┐      ┌──────────────┐
│ui_overrides  │      │ui_text_      │      │font_         │
│(配置/层级/CJK)│      │replacer      │      │diagnostics   │
│              │      │(UI扫描/更新) │      │(审计/转储)   │
└──────────────┘      └──────────────┘      └──────────────┘
```

1. **`ui_overrides`（规则配置与意图决策）**
   - **数据驱动覆盖**：监控 `static_overrides.json` 与 `custom_overrides.json` 的文件状态与 mtime 变更，自动重新加载。
   - **层级安全保护**：建立白名单与黑名单，严格隔离战斗、大厅、养成等游戏内界面，只允许在标题界面、登录授权和网络重试弹窗中执行直接 UI 替换。
   - **字符与字体分析**：高效判定 UTF-8 文本是否包含 CJK 统一表意字符（`utf8_has_cjk`），并识别游戏内置 CJK 基底字体。
   - **模糊匹配引擎**：内置登录按钮与缺失词条字典，提供基于路径与文本特征的多级回退匹配。

2. **`ui_text_replacer`（UI 文本遍历与组件更新）**
   - **GC 句柄锁定与池化 (`get_pinned_ui_string`)**：为常用 UI 替换字符串创建强引用句柄，杜绝因 Mono/IL2CPP GC 回收导致的内存野指针。
   - **反射写屏障保障**：除调用 `SetText` / `set_text` 外，直接通过 `il2cpp_gc_wbarrier_set_field` 写入 `m_text` 字段，并在修改后调用 `SetAllDirty` 与 `ForceMeshUpdate` 触发网格实时重绘。
   - **字体动态替换**：当文本包含 CJK 字符时，自动将组件字体指向中文字体资产 `g_shs_font`。

3. **`font_injector`（字体底层加载与 Fallback 注入）**
   - **AssetBundle 异步加载**：从文件加载中文字体 AssetBundle，并在主线程绑定 Android Vulkan 兼容着色器与图集纹理。
   - **资产防回收固化**：对加载出的字体资产应用 `HideFlags.DontUnloadUnusedAsset (32) | DontSave (20) = 52`，并注册 `DontDestroyOnLoad`，彻底解决游戏切场景或视频播放时触发 `Resources.UnloadUnusedAssets()` 导致中文字体被销毁、进而退化为“口”字豆腐块的缺陷。
   - **动态 Fallback 链维护与 CJK 剔除**：将 `g_shs_font` 注入至游戏中所有加载的 `TMP_FontAsset` Fallback 列表索引 0（高优先级），并通过 `strip_cjk_from_font` 剔除原版日文字体中的残缺 CJK 字形，实现标准中文字形呈现。

4. **`font_diagnostics`（字体与文本深度审计）**
   - 提供 `dump_all_fonts_status` 与 `dump_active_tmp_texts`，转储运行时所有已加载字体、Fallback 拓扑及当前界面活跃文本的语言分类统计（ZH、EN、KO、NUM/SYM）。

### 3.2 数据表拦截与结构化补丁 (Data Patcher)
- **`data_patcher`**：挂钩核心数据结构反序列化流程，拦截 `TextDataSet` 及技能、人格、被动、饰品等字典。
- **`json_loader` & `storage`**：读取汉化包内的 JSON 数据源，构建极低哈希冲突的内存查找表，支持高效键值覆盖与安全类型回退。
- **`managed_fields`**：在不依赖固定内存偏移的前提下，通过反射遍历类继承树安全定位特定字段并执行覆写。

### 3.3 剧情对话拦截系统 (Story Hook)
- **`story_hook` & `story_sync`**：针对剧情剧场模式与对话框文本，依据剧情编号进行精准翻译覆写。
- **`story_refresh_guard`**：针对场景跳转与重复进出，提供防抖机制与降级安全策略，防止剧情文本错乱。

### 3.4 线程模型与主线程桥接 (Main Thread Bridge)
- **`main_thread_bridge`**：Unity 引擎的大量原生 API（如 `Resources.FindObjectsOfTypeAll`、材质着色器绑定、网格刷新等）均受限于 Unity 引擎主线程约束。
- 核心引擎设计了无锁/轻量级同步任务队列，所有由后台工作线程触发的 UI 刷新或诊断转储均通过任务分发至主线程安全执行，避免跨线程调用引发的 SIGSEGV 崩溃。

---

## 4. 安全防护与沙箱边界模型

针对 Android 环境下跨应用、跨用户以及 Root 权限运行的风险，系统构建了多层防护：

1. **严格的属主与权限审查**
   - 临时文件、缓存模块以及覆盖配置在加载前均经过权限判定。
   - 仅信任 `uid == 0`（系统/Root）或 `uid == geteuid()`（游戏进程拥有者）创建的文件。
   - 彻底拒绝包含世界可写位（`0002`）的文件与目录，防止同设备上的其他非特权应用实施符号链接攻击或内容劫持。

2. **规范路径与符号链接拦截**
   - 在关键路径（如 `/data/local/tmp`）使用 `lstat` 检测符号链接。
   - 通过 `realpath` 展开实际规范路径，防止利用目录软链接绕过层级检查。

3. **安全权限目录规范**
   - 运行时生成的目录与文件一律采用安全的 `0755` 目录权限与 `0644`/`0755` 文件权限，杜绝宽泛的 `0777` 授权。

---

## 5. 资源管理与前端交互 (Manager & WebUI)

```text
[Root 管理器 (KernelSU / APatch / Magisk)]
                   │
                   ▼ (HTTP/WebUI)
             [webroot/ 前端]
        (HTML / CSS / Vanilla ES6)
                   │
                   ▼ (ksu.exec / JSON-RPC)
        [manager/ 后台服务与 CLI]
      (llc-manager / llc-manager-native)
                   │
         ┌─────────┴─────────┐
         ▼                   ▼
    [网络下载与解压]      [状态标记与配置]
    (llc-https.jar)   (.boot_id / reboot 状态判定)
```

- **`manager/`**：包含原生编译的 `llc-manager-native` 与脚本包装器，负责汉化包的哈希校验、解压缩、版本回滚、下载网络探测及设备重启判定。
- **`webroot/`**：纯原生、无第三方打包依赖的 ES6 前端应用，通过 Root 管理器桥接运行，向用户展示直观的安装状态、字体设置、版本回滚与加载诊断面板。

---

## 6. 测试与验证矩阵

系统采用“宿主离线测试 + 实机端到端测试”双重防线：

| 测试层级 | 执行入口 | 涵盖用例 | 核心验证范畴 |
| :--- | :--- | :--- | :--- |
| **宿主测试 (Host)** | `tests/run-host-tests.ps1` | 24 套套件 (C++, Python, Node.js) | 解压引擎、配置解析、字符与 CJK 判定、字段反射、JSON 容错、权限安全边界、内存模型、WebUI 通信协议 |
| **实机测试 (Device)**| `tests/run-device-tests.ps1`| 4 项端到端深度断言 | 1. 字体资产与 Fallback 链审计<br>2. 活跃 UI 文本中文字符及方块“口”字豆腐块排查<br>3. Logcat 运行时缺字告警与崩溃检测<br>4. 进程存活与稳定性验证 |

两套测试矩阵共同保障了代码在经过重构、模块解耦或底层依赖变更后，仍能保持绝对的稳定性和兼容性。
