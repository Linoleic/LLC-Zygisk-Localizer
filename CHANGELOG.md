# 更新日志

本文档记录各版本的重要变更。
格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循[语义化版本 2.0.0](https://semver.org/lang/zh-CN/)。

## [Unreleased]

## [v0.4.1]

### 修复

- 修复设置界面与服务条款/隐私政策文本异常：为托管字符串池引入 IL2CPP GC 根句柄（`il2cpp_gchandle_new` / `il2cpp_gchandle_get_target` / `il2cpp_gchandle_free`）常驻保护，彻底解决场景切换卸载导致托管内存被回收污染、开关丢失“关闭”字样、底部确认/取消按钮变为韩文及条款标题显示资源路径的问题。
- 补全托管字符串池去重与 GC 句柄生命周期的完整宿主单元测试覆盖。

## [v0.4.0]

### 新增

- 完整适配底层全部 49 张数据表，实现游戏主界面、剧情对话、战斗、人格技能、E.G.O 饰品、镜像迷宫与语音的全面本地化。
- 内置 WebUI 管理器（支持 KernelSU / APatch / Magisk），提供汉化资源包自动校验、热下载、回退与版本切换功能。
- 支持自定义中文字体注入与多语言共存策略。
- 采用方案 A（基于 `LoginUIText.json` 动态映射）优化标题与登录界面的本地化，移除冗余笛卡尔积配置。

### 优化

- 优化冷启动性能，使用自适应轮询替代静态延时；引入 IL2CPP 反射缓存与动态避让。
- 优化模块打包逻辑，构建时通过暂存目录生成动态 `module.prop`，保持源码目录干净。

[Unreleased]: https://github.com/Linoleic/LLC-Zygisk-Localizer/compare/v0.4.1...HEAD
[v0.4.1]: https://github.com/Linoleic/LLC-Zygisk-Localizer/compare/v0.4.0...v0.4.1
[v0.4.0]: https://github.com/Linoleic/LLC-Zygisk-Localizer/releases/tag/v0.4.0
