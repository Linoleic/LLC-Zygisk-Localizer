# Third-Party Notices

This project redistributes the following third-party source files. Their original
licenses and copyright notices are reproduced or referenced below.

## nlohmann/json

- Location: `jni/third_party/nlohmann/json.hpp`
- Version: 3.11.3
- Source: <https://github.com/nlohmann/json>
- Copyright: (c) 2013-2023 Niels Lohmann
- License: MIT (full text in `jni/third_party/nlohmann/LICENSE.MIT`)

## Zygisk module API header

- Location: `jni/zygisk.hpp`
- API version: 5
- Source: <https://github.com/topjohnwu/zygisk-module-sample>
- Copyright: 2022-2023 John "topjohnwu" Wu
- License: ISC-style permissive license (full text in the header of the file itself)
- The header is used unmodified, as required by its own notice.

## Runtime resources (not distributed in this repository)

The Simplified Chinese translation data is **not** included in this repository.
It is downloaded by the user at runtime from the upstream project
[LocalizeLimbusCompany](https://github.com/LocalizeLimbusCompany/LocalizeLimbusCompany)
and remains subject to that project's own license and terms.
The MIT license of this repository covers only the code in this repository.

The preset login/title strings in `module/config/title_overrides.json` are original
content of this project (previously hardcoded in the source, later moved into a
configuration file so users can customize them) and are covered by the same MIT license.

## Trademarks

*Limbus Company* and all related names, art and text are the property of
Project Moon. This project is unofficial and is not affiliated with or endorsed
by Project Moon.
