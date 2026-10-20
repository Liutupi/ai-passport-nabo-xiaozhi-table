# NABO 牌桌 · 对决 离线版

**已发布：offline-v1.0.0 · 2026-10-21。** 本分支的正式固件为独立离线版：牌桌（21 点、大话骰、谁是卧底）、双机 ESP-NOW 对决、单人练习和设置。开机即可玩，不含小智，不需要路由器或激活。

[下载离线版固件](https://github.com/Liutupi/ai-passport-nabo-xiaozhi-table/releases/tag/offline-v1.0.0) · [安装与验收说明](docs/RELEASE_offline-v1.0.0.md) · [离线版源码／构建](standalone/) · [验证摘要](validation/offline-v1.0.0.json)

等待声为原 NABO 木鱼的短敲击，节奏逐渐加快，拔枪前停敲；拔枪仍响“叮”。结算按你这台的胜负播放：**赢了吹枪，输了哭脸／被呲**。两台已完成保留设置和成绩的升级及回读校验，用户确认声音和胜负动画正常。

| 你赢了 | 你输了 |
|---|---|
| ![赢了吹枪](docs/images/offline-duel-round-win.png) | ![输了哭脸](docs/images/offline-duel-round-lose.png) |

以上为生产渲染器直出示例，非真机截屏。Release 复用已验收原镜像，应用 SHA256 `fb8af7fe4620104106c8e0fe29a351589ad554b7b3e68540815edb308ee745e3`。每次只接一台，使用 ZIP 内升级脚本可保留兼容分区的设置与成绩；首次整包安装会覆盖写入范围内的原设置，请先备份。

带小智的牌桌固件使用 [v1.6.0](https://github.com/Liutupi/ai-passport-nabo-xiaozhi-table/releases/tag/v1.6.0)，不要把离线包当作小智固件。`xiaozhi-esp32/` 内对决集成仍在开发，离线版验收不代表小智语音和联网恢复回归。

<details>
<summary>小智牌桌版的历史说明</summary>

# AI Passport · NABO × 小智 · 牌桌版

为 **FoloToy AI Passport（ESP32-C3 / 8 MB）**制作的小智定制固件，带「**NABO 牌桌**」：2–4 人的 21 点、大话骰，以及新增的 **3–8 人谁是卧底**。一台工牌传着玩，NABO 当荷官。另有 NABO 待机与对话动画、跳一跳，以及完整的设备设置。

本仓库从 [ai-passport-nabo-xiaozhi-dive](https://github.com/Liutupi/ai-passport-nabo-xiaozhi-dive)（v1.3.1 深潜版）分出，保留完整历史。为给牌桌腾出空间，**移除了深潜、摇骰子、功德木鱼和蓝牙电脑快捷键**。分区表和 NVS 与此前各版兼容，配网、激活和成绩都保留；牌桌的 NABO 动画放在资源分区里，所以从其他版本切换过来时，**应用和资源两个镜像都要写**（刷写工具的 upgrade 模式会自动处理）。

**当前版本：v1.6.0，2026-10-18（新增谁是卧底）。** 应用 SHA256 `de9ff6def9c9e49464d77607aca6a53d4e162f8da387dc2e0f379106101d7327`。本版移除节奏回声，修复牌桌与配网提示同时刷新屏幕时的 SPI 总线争用；使用经过实机串口流程及 10 轮进出压力测试的现成镜像发布，未为发布重新编译。

[下载固件](https://github.com/Liutupi/ai-passport-nabo-xiaozhi-table/releases/latest) · [使用指南](docs/USER_GUIDE.md) · [刷写说明](docs/FLASHING.md) · [编译说明](docs/BUILD.md) · [维护交接](docs/HANDOFF.md) · [验证记录](docs/VALIDATION.md) · [更新记录](CHANGELOG.md)

![谁是卧底：3–8 人设置](docs/images/lcd-undercover-setup.png)
![谁是卧底：结果揭晓](docs/images/lcd-undercover-end.png)

*上两图来自本版真机串口截屏。谁是卧底内置 264 组词，逐人交接看词、轮流描述、投票出局并揭晓身份；3–6 人安排 1 个卧底，7–8 人安排 2 个。*

![NABO 牌桌：封面、21 点轮到自己、21 点开牌、大话骰叫数、大话骰开盅](docs/images/lcd-table.png)

![NABO 牌桌多人：4 人封面、4 人 21 点私密回合与开牌、3 人并列赢家、4 人大话骰开盅](docs/images/lcd-table-multi.png)

*上两图由固件的同一套渲染代码在电脑上按一局录好的对局生成（`test_table.py` 的宿主渲染），真机串口截屏与之一致。*

![NABO 跳一跳实机画面](docs/images/lcd-jump-current.png)

## 已实现

| 功能 | 行为 |
|---|---|
| 小智 + NABO | 224×224 半身原图，待机眨眼，聆听、思考、说话切换动画 |
| 语音打开 | 对话中说“打开牌桌／大话骰／二十一点／谁是卧底／跳一跳／设置” |
| NABO 牌桌 | 2–4 人传着一台工牌玩（封面选人数）：21 点（无明牌，轮流要牌/停牌，一起开牌，最高点并列都得分）和大话骰（两步叫数，1 点万能、叫 1 即斋，错的一方罚 1 分）；换人时出现交接屏，NABO 捂眼不看；21 点先赢满 3 / 5 / 7 局获胜，大话骰先罚满的人输 |
| 跳一跳 | 按住 OK 蓄力、松开起跳；NABO 屈膝摆臂、落地站稳、平台旁跌坐结束；支持连续蓄力 |
| NABO 对决（开发中） | 两台工牌 ESP-NOW 直连的快枪手对决：屏幕反色、「拔！」时按 OK，比反应时间，抢跑判负，有虚晃；也能单人对 NABO 练习。见 [`game-art/duel/`](game-art/duel/) |
| 谁是卧底 | 3–8 人传工牌看词，264 组词，轮流描述、忘词私密查看、投票确认、出局与身份揭晓；人数独立保存 |
| 开机即玩 | 小智还没联网（找不到 Wi-Fi、等待配网）时也能打开本地程序，退出后小智接着启动 |
| 导航 / 设置 | 双击右侧上键返回上一级；长按上键回小智；音量、亮度、休眠、网络等 |

明确的打开命令在语音识别结果到达后由设备直接处理；复杂表达仍可通过 `self.apps.open` MCP 工具执行（`table`）。进入本地程序会暂停小智，长按右侧上键恢复小智。C3 版通过 OK 开始对话，未启用本地唤醒词。

牌桌的 NABO 荷官动画由项目拥有者用 Codex 出图、Grok 图生视频制作，处理成 16 个动作 215 帧，打包为 1.18 MB 的 `table_sprites.bin` 放在资源分区；扑克牌、骰子（沿用摇骰子的立体渲染）、绒布和音效都由程序实时生成。美术规格、镜头清单、起始图、源视频、处理工具和网页原型都在 [`game-art/table/`](game-art/table/)。

## 下载与安装

Release 提供应用 `xiaozhi.bin`、资源 `assets.bin`、包含四个分区镜像的安装 ZIP、从 `0x0` 刷写的完整固件 `…-full.bin`、`manifest.json` 和 `SHA256SUMS.txt`。ZIP 内附使用文档及带设备身份检查、备份和写后校验的刷写工具。

- **本定制版（任一版本）已安装：** 使用 upgrade 模式，写应用 `0x10000` 和资源 `0x400000`，保留配网、激活及用户数据。
- **首次安装：** 使用安装包中的四个独立镜像；先核对芯片、Flash、分区和备份。不会执行整片擦除。

此固件只适配上述工牌，采用单应用自定义分区。官方标准 OTA 镜像与这个布局不兼容。请按[刷写说明](docs/FLASHING.md)操作，不要把 ZIP 当成整片 Flash 备份写入。

## 源码与构建

基于 [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) 的 `4632dc51f0a5ad26e08542e131e6e48da41e4ff3`，完整源码位于 `xiaozhi-esp32/`。本版使用 **ESP-IDF 6.0.2**、ESP32-C3、中文语言资源；组件锁定文件和发行配置一并保留。

```powershell
# 在已初始化的 ESP-IDF 6.0.2 终端中
python tools/build.py
```

宿主回归测试需要 Python 3.11+、GCC/G++ 和 Pillow：

```powershell
python -m pip install -r requirements-dev.txt
python tools/run_tests.py
```

## 交接入口

先阅读 [HANDOFF.md](docs/HANDOFF.md)：它说明源码职责、音频与网络生命周期、已有问题的修复原因、回归测试和下一版发布流程。`validation/` 只保存去除设备标识和私人内容的验证摘要；仓库和 Release 不包含 NVS、设备整片备份、Wi-Fi 信息、账号令牌或对话日志。

源码、解码器、字体及美术/音频素材的来源与许可分别列在 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。NABO 美术与社区音效不因源码的 MIT 许可证而自动变成 MIT 素材。本项目是独立定制项目，与微信及 FoloToy 官方发行无关联。

</details>
