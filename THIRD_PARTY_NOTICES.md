# 来源与许可

## XiaoZhi

基于 <https://github.com/78/xiaozhi-esp32> 提交 `4632dc51f0a5ad26e08542e131e6e48da41e4ff3`，上游 MIT 许可证完整保留在根目录及 `xiaozhi-esp32/LICENSE`。新增/修改的项目代码沿用 MIT；下列第三方内容按其各自条款。

## Helix MP3（仅 v1.2.0 及更早版本）

v1.2.0 及更早版本的网络电台包含 Helix MP3 解码器（RealNetworks，RPSL/RCSL）。`feat/dive` 起已移除电台和全部 Helix 源码；旧版源码与许可仍保留在对应标签和发行包中。

## 字体与组件

界面中文字体 `nabo_menu_font_16` / `nabo_ui_font_18` / `nabo_table_font_12` / `nabo_table_font_24` 子集自思源黑体 Source Han Sans CN Medium（Copyright 2014-2025 Adobe，保留字体名 “Source”，OFL 1.1，全文 `OFL-SourceHanSans.txt`）；子集以 nabo_* 命名，不使用保留字体名。v1.1.0 及更早版本使用 LXGW WenKai（OFL 1.1，`OFL-LXGW.txt` 保留以对应旧版源码）。节奏回声的海报标题字体 `nabo_poster_font_28/48` 子集自 Archivo Black（Copyright 2017 The Archivo Black Project Authors，OFL 1.1，全文 `OFL-ArchivoBlack.txt`），小号标签字体 `nabo_mono_font_12` 子集自 Space Mono Bold（Copyright 2016 The Space Mono Project Authors，OFL 1.1，全文 `OFL-SpaceMono.txt`）。字体由 `tools/generate_fonts.py` 用 lv_font_conv 1.5.3 生成，来源校验和记录在 `game-art/fonts.json`；源 TTF 不入库。ESP-IDF、LVGL、Espressif/其他 IDF 组件分别保留其上游许可，通过 `main/idf_component.yml` 与 `dependencies.lock` 解析；MIT 不覆盖这些依赖的独立条款。

## NABO 和游戏资源

NABO 美术来自项目拥有者提供的原图与此前 NABO 工牌资源。医院、书房、播客、木鱼、咖啡、靶心平台为本定制项目制作；游戏机制和等距画面参考微信《跳一跳》。没有取得或使用微信原版源码。

蓄力、起跳、正中 PCM 参考项目拥有者指定的“新空气”AI Passport 社区版本，来源 <https://ai-passport.folotoy.cn/plays/778/> 的公开固件；来源/样本校验和见 `game-art/original-sound-reference.json`。落地/跌落及淡化蓄力变体在本地制作。社区页面没有为这些声音提供已确认的独立开源许可，因此**不将它们声明为 MIT 或公有领域**。素材权利与源码许可分开；再分发/替换素材时须保留来源并核实适用条款。

NABO 牌桌的 NABO 荷官造型、起始图和动作视频由项目拥有者使用 Codex（出图）和 Grok Imagine（图生视频）为本项目制作，NABO 形象权利同上文 NABO 美术，**不因源码的 MIT 许可证而成为 MIT 素材**；`table_assets/table_sprites.bin` 由这些视频抽帧生成。21 点和大话骰为公共规则的传统游戏；界面、扑克牌与骰盅绘制、交接流程和合成音效为本项目原创实现，立体骰子沿用本项目此前摇骰子程序的渲染器。

v1.3.x 深潜版的 NABO 潜水帧见 <https://github.com/Liutupi/nabo-dive>；深潜已在本版移除。

节奏回声（ECHO CLUB）的“先听示范、再拍回去”玩法受 AI Passport 社区《节奏闯关 · COPYCAT CLUB》启发；界面、关卡节奏型、合成音色和全部代码均为本项目原创实现，未使用该作品的源码、图像或声音。

本项目不包含微信客户端、社区整片固件、私人账号数据或他人的设备备份。

对决等待木鱼音效复用项目拥有者此前 NABO 木鱼程序的原创合成 PCM（`nabo_muyu_sound.h`），16 kHz、1440 采样；原文件 SHA256 `2856df188461aa819cafbb16c036872ab86461dc3aa2e316503c58933987eef8`。浏览器版本由 `tools/generate_duel_wait_audio.py` 从同一采样生成；未取用第三方木鱼录音。
