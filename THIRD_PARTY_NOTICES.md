# 第三方组件与许可声明 (Third-Party Notices)

本项目引用或重新分发了以下第三方开源文件。其原始版权声明与许可协议说明如下：

## nlohmann/json

- 路径：`jni/third_party/nlohmann/json.hpp`
- 版本：3.11.3
- 来源：<https://github.com/nlohmann/json>
- 版权所有：(c) 2013-2023 Niels Lohmann
- 许可协议：MIT（完整文本见 `jni/third_party/nlohmann/LICENSE.MIT`）

## Zygisk 模块 API 头文件

- 路径：`jni/zygisk.hpp`
- API 版本：5
- 来源：<https://github.com/topjohnwu/zygisk-module-sample>
- 版权所有：2022-2023 John "topjohnwu" Wu
- 许可协议：ISC 宽容型协议（完整文本见该文件头部）
- 说明：按其自身声明要求，原样保留未做修改。

## 运行时资源说明（未随本仓库分发）

简体中文汉化词库数据**不包含**在本仓库中。词库由用户在运行时从上游开源项目 [LocalizeLimbusCompany](https://github.com/LocalizeLimbusCompany/LocalizeLimbusCompany) 自动下载，并遵守该项目自身的开源许可与条款。本仓库的 MIT 协议仅覆盖本仓库自有的源代码。

`module/config/static_overrides.json` 中的预置静态文本属于本项目的原创内容（原硬编码于源码中，后抽离为配置文件便于自定义），同样受本仓库的 MIT 协议约束。

## 商标与知识产权声明

《边狱公司》（*Limbus Company*）及其相关的一切名称、美术素材与游戏文本知识产权均归 **Project Moon** 所有。本项目为非官方项目，与 Project Moon 无任何关联，亦未获得其官方认可。
