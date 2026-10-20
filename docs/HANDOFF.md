# 维护交接

## 当前交接状态

**离线正式发布：`offline-v1.0.0`（2026-10-21）。** 两台刷入相同应用 `fb8af7fe4620104106c8e0fe29a351589ad554b7b3e68540815edb308ee745e3`，用户已确认“这次木鱼声可以了，赢了和输了的动画也正常了”。发布使用原验收应用和合并镜像，未重新编译；主入口 `standalone/`，说明见 `docs/RELEASE_offline-v1.0.0.md`，公开验收摘要见 `validation/offline-v1.0.0.json`。以下三轮为修正过程记录，其“尚待刷入”描述属于各自当时状态。小智 `v1.7.0` 集成仍属开发状态，不得把离线验收套用到小智版。

2026-10-20 第三轮音效调整：胜负/双拍心跳版 `da85a383fb70428a2af8c199cfcbfad303b8a0dce86ca5138ae56d4ac714644d` 已逐台刷入两台并完成保留数据校验；用户认为心跳仍不合适，改用已有 NABO 木鱼 PCM。当前等待声为原木鱼 `nabo_muyu_sound.h`（16 kHz / 90 ms，源文件 SHA256 `2856df188461aa819cafbb16c036872ab86461dc3aa2e316503c58933987eef8`），1.25 倍增益，原加速节拍不变；`NDU_CUE_HEART` 保留位值作为等待节拍，内部播放木鱼而非心跳，距 GO 220 ms 不再触发新敲击。胜负动画、联机规则、NVS、板级及原角色素材保持不变；新版尚待刷入实听。

2026-10-20 第二轮实机反馈修正：去沙沙声版 `49c40bbaaa8fc35953cd7f11fef2a88ba946bc4bceaef68d64175c169ed0558b` 已逐台刷入两台，应用/保留区回读、重启与设置一致性均通过；用户反馈胜负动画视角相反、心跳听不到。当前候选把每局/整场/等下一局的动画统一为本机玩家视角：胜利 `smug` 吹枪，失败（含抢跑）`splashed` 哭脸/被呲，脚下光圈用本机颜色。心跳改为 145/175 Hz 双拍及八度泛音，提示铃响时 2.5 ms 淡出并取消延迟第二拍。55 项宿主检查通过，包括 72 个胜负情形的 144 次渲染及 220 个心跳/拔枪重叠情形；新候选的实体听感及画面仍待刷入后验收。

2026-10-20 本地离线版声音调整：用户确认两台旧离线镜像（应用 SHA256 `52a6ff87088fdceb6c350132dc5aa7fc58bea0a4618439467f8dac682522e060`）已刷入并检验成功，反馈等待/拔枪阶段不想要沙沙声。`nt_sound_duel()` 取消风声，虚晃、出手和胜负去掉噪声合成；保留拔枪铃，减轻心跳以避免与铃音叠加削波。原型声音同步；规则、联机和资源未改。新候选的构建、校验和测试记录放在 `table-duel/offline-quiet-audio/` 交付目录；尚未刷入或完成新音效实机听感验收。

**开发中：v1.7.0 NABO 对决**（`duel` 分支），代码与宿主测试完成，尚未真机构建，见下文“NABO 对决”一节。以下为最近一次发布的状态。

当前版本为 **v1.6.0 谁是卧底版**：复用现成实机验证应用，3,975,984 字节，SHA256 `de9ff6def9c9e49464d77607aca6a53d4e162f8da387dc2e0f379106101d7327`。资源 SHA256 `b7230904f977912bdb4b31b21bb47ad105c7e49e4a2f7a162664ba8c43107785`，bootloader、分区表和资源与 v1.5.0 相同。新增 `nabo_table_uc.c`（3–8 人规则）、`nabo_table_scene_uc.c`（画面）和 `nabo_uc_words.c/.h`（264 组词库），原型同步至 `game-art/table/prototype/undercover.js`；词库来源为 `game-art/table/undercover/words.txt`，生成器 `tools/generate_uc_words.py`。本版删除节奏回声固件与相关测试，勿将历史文档中的节奏入口当作本版功能。

**屏幕争用修复：** `NaboDisplay::BeginDirectDraw()` 设置 `panel_borrowed`；LVGL flush 在牌桌持有面板期间只回报完成。仅暂停 LVGL 刷新定时器不够，因为配网状态变更会重新唤醒刷新。不要移除此保护。实机 5/8 人完整流程及 10 轮牌桌进出压力测试已通过，验证范围见 `docs/VALIDATION.md` 和 `validation/v1.6.0.json`；现成镜像与日后干净构建必须分别记录，不能静默替换。

上一版 Release 为 **v1.5.0 牌桌多人版**（本仓库），应用 SHA256 `849a1ce6468c9f927ba94e951f74c5dbf1a9c1ec0da06bbdc8d63187e1f43e05`：牌桌支持 2–4 人（21 点并列最高都得分，大话骰改为罚分制），应用分区剩约 337 KB，bootloader、分区表沿用 v1.4.0。上一版 **v1.4.0 牌桌版**，应用 SHA256 `b9502231ad4d8b2056e91c567858826fbc91c30fa25dcdb60024c08892e97f4b`：新增 NABO 牌桌（21 点、大话骰），移除深潜、摇骰子、功德木鱼和蓝牙电脑快捷键（含 `nabo_pc.c`）；小智没联网时也能打开本地程序。应用分区剩约 340 KB。**资源分区随之改变**（多了 1.18 MB 的 `table_sprites.bin`，共 2.54 MB），所以发行包不再复用 v1.0.0 的资源镜像，upgrade 模式写应用和资源。牌桌的结构与注意事项见下文“NABO 牌桌”一节。

v1.3.x 深潜版：应用 4,107,808 字节，SHA256 `7cd8589aff5aa7eacf66bdff93a9cd5accc19613aa9add2ce80504507ac0a45a`（v1.3.0）；新增深潜，移除网络电台，在 [ai-passport-nabo-xiaozhi-dive](https://github.com/Liutupi/ai-passport-nabo-xiaozhi-dive) 维护。

以下为 v1.2.0 及更早的交接状态。

最新 Release 为 **v1.2.0**，应用 4,053,008 字节，SHA256 `2c05a52efc76a38597c0a632082c913f8af0acaf6d357f889a58b92829a3c984`：海报风菜单（`nabo_menu_view.cc`）与思源黑体 4-bit 字体。应用分区只剩约 75 KB，新增大块资源前先评估字体子集或改用压缩字体。截图脚本测试只发方向键，不要在设置页发 OK（会改用户设置）。

v1.1.0：应用 3,945,968 字节，SHA256 `5c0d5117bc69ee0fd7115e0a73ce2c12df6e409ccd3a6e08ebe134ef39394620`。新增节奏回声、菜单重设计和菜单面板按需分配；验证范围见 [VALIDATION.md](VALIDATION.md)。发行包的 bootloader、分区表和资源镜像直接复用 v1.0.0 Release（与实机逐字节一致），只替换应用：本机重新构建的 bootloader/资源与已部署字节不同，若把它们打进包，`flash_release.py --mode upgrade` 会对现有设备拒绝写入。

下面是 v1.0.0 的原始交接状态。

首个定制 Release 为 **v1.0.0**，应用 3,901,088 字节，SHA256：

```text
b18f915d0295181609b225b475511e1e8a5e60d950b2390fc33191e7c0278d40
```

这是已刷入并获得“摇骰子、木鱼、网络电台三个都能打开”反馈的镜像。此前连续对话内存修正版获得“连续对话正常”；连续蓄力和音量获得“连跳和音量都合适”。各项证据对应的镜像与限制见 [VALIDATION.md](VALIDATION.md)，不能把此前全部测试说成对最新镜像又完整执行了一遍。

## 代码职责

目录前缀 `xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo/`：

| 文件 | 维护职责 |
|---|---|
| ai_passport_board.cc / config.h | 硬件初始化、ADC 按键、双击/长按、语音回调；引脚保持原工牌布局 |
| nabo_display.cc / muse_pixel.c | 224×224 NABO 动画和流式行解码，避免整帧内存分配 |
| nabo_apps.cc | 工具菜单、页面状态、导航、任务/协议所有权、音频切换、设备侧 MCP |
| nabo_voice_commands.c | 有界无堆分配的明确语音命令解析；拒绝否定、询问、多程序歧义 |
| nabo_jump_model.c / nabo_jump_rig.c | 游戏物理、平台和人体动作；保留落地/镜头期间的输入 |
| nabo_jump_sound.c / jump_sounds.bin | PCM 音效、蓄力渐变与切换过渡 |
| nabo_table.c | 牌桌任务（30 fps esp_timer 节拍）、原始按键沿（UP/DOWN 连发、OK 松开生效、长按 2 秒暂停）、NVS 抢局与人数设置（`table_target` / `table_players`）、`>table.*` 串口诊断 |
| nabo_table_model.c / nabo_table_bid.c | 牌桌规则（2–4 人）：封面、交接、暂停、21 点、大话骰（罚分制）；纯逻辑，与浏览器原型逐步对拍（test_table.py） |
| nabo_table_gfx.c / shapes.c / scene*.c | 显示列表 + 脏矩形，8 行条带合成；绒布、牌、骰盅、文字、精灵；只用整数 |
| nabo_table_sprites.c | 从资源分区原地读取 table_sprites.bin，大图逐行解码、小图关键帧+差分 |
| nabo_table_sound.c | 现场合成：骰子共振器（复用 nabo_dice_render.c）、滤波噪声纸牌声、铃声 |
| nabo_dice_render.c | 立体骰子渲染与敲击声；摇骰子程序已删，牌桌用 nabo_dice_pose / nabo_dice_die_row |
| nabo_rhythm_model.c / nabo_rhythm_sound.c | 节奏回声关卡、判定（16 kHz 采样时钟）、合成音色；宿主测试 test_rhythm.py |
| nabo_rhythm_play.c | 音频 worker 与 LVGL 之间的歌曲时钟锚点、音效队列、延迟补偿 |
| nabo_rhythm_ui.c / nabo_rhythm_stage.c | 选关/结算与演奏画面；对象只在程序打开时存在 |
| nabo_*_font_*.c | 由 tools/generate_fonts.py 生成；新增中文界面文字后重新生成，字形不足会显示缺字 |
| nabo_duel_model.c/.h | NABO 对决规则与联机协议（大厅、邀请、对时、局、判定、暂停、掉线、线格式），纯逻辑，与 `game-art/duel/prototype/duel_model.js` 逐毫秒对拍（test_duel.py） |
| nabo_duel.c/.h | 运行时胶水：带时间戳的按键/收包按时间排进模型节拍（模型滞后 30 ms），拔枪闸门（屏幕反色前不让模型越过 go_at），音效位，PING 打戳与 PONG 快速应答的纯函数 |
| nabo_link.cc/.h | ESP-NOW：停/启小智 Wi-Fi 站点、固定信道 1、收包回调里直接回 PONG、发 PING 时写入发送时刻 |
| nabo_table_scene_duel.c | 对决画面（黄昏小镇 NTG_DUSK 背景 + 对手 NABO + 文字），时间用模型时间 |

核心层只通过 `Board::HandleRecognizedSpeech()` 可选接口调用板型，不能在 `application.cc` 中硬编码 NABO 程序关键词。STT 回调通过 `Application::Schedule()` 在主任务显示文字并处理打开指令。其他板型默认不处理该接口。

## 不要退回的修复

**小智内存：** C3 没有 PSRAM。旧 ES8311 整帧双声道临时 vector 和过长队列造成 `std::bad_alloc`，包括长回答后的启动提示循环崩溃。现在用 `stereo_frames.h` 每次 160 帧、640 字节栈缓冲转换；NABO 的 PCM 队列 1 帧，压缩收发缓冲 600 ms。保持 Opus **24 KiB 栈**：真实语音曾使用近 20 KiB，不能根据待机栈水位随意缩小。其他板型的队列默认不变。

**程序切换：** 本地程序持有音频时停止小智，返回前完成本地音频退出；主任务回收已完成的协议初始化任务后再恢复 AudioService，不能叠加约 8 KiB 的已完成任务栈。避免在 LVGL 显示锁内阻塞等待音频任务。

**BLE：** 电脑快捷键与 `nabo_pc.c` 已在 v1.4.0 删除，固件不再启动蓝牙。若将来恢复：先断开、停止 host、清空 GATT 属性，再 NimBLE deinit/释放内存，并用真实已配对电脑验证退出。

**游戏输入与画面：** 落地、镜头移动甚至飞行末段可缓冲下一次蓄力；保留第一次按下时间和提前松开的时长。不能等画面停止才接受按键。失败动作在平台旁结束，不让 NABO 整体飞出屏幕。已把绘制范围缩到有效扫描行和角色/阴影局部区域；避免恢复逐像素全场景检查。

**声音：** 本地程序（牌桌、跳一跳、节奏回声）用 16 kHz 双声道 I2S、4×160 帧 DMA。小智 24 kHz 与本地音效的所有权和采样率不同，要先完成退出再切换。蓄力 gain 0.65、15 ms 淡入淡出、约 8 ms 声音切换过渡已被接受。界面 100% 需保留扬声器余量，不能直接恢复满幅。

**按键：** 右侧上键双击回上一级，单击原功能、长按直接回小智。页面父级要保留，特别是设置编辑、状态页、电脑分组。左侧电源键不是可读取的 MCU 按键。

**菜单内存：** LVGL 使用系统堆（`CONFIG_LV_USE_CLIB_MALLOC`）。菜单面板在进入菜单页时创建、回到 NABO 或进入游戏时删除，节奏回声同理。不要改回开机常驻：v1.1.0 首次常驻版本在开机 MQTT 发布工具列表时出现 `esp-aes: Failed to allocate memory`，改为按需后待机空闲堆约 30 KB → 36 KB。

**节奏判定：** 判定以 OK/下键的 press-down 时间戳为准（下键按下由板型 `OnPressDown` 转发），通过 worker 写 I2S 前记录的锚点换算到歌曲采样位置。默认补偿 42 ms，可用 `>rhythm.latency=` 调整；`>rhythm.best.reset` 清除成绩，脚本测试后应执行，避免留下假成绩。

## NABO 牌桌

**屏幕所有权：** 与此前的深潜相同，不经过 LVGL。`change_page` 在 LVGL 任务里 `BeginDirectDraw()` 后调用 `nabo_table_open()`；离开时 `nabo_table_close()` 等最后一个条带发完再 `EndDirectDraw()`。

**按键：** 牌桌直接读三个键的按下/松开（板型里 UP 新增了 `OnPressDown/OnPressUp` → `nabo_apps_up_edge`）。对局中屏蔽双击上键（返回上一级）和长按上键（回小智），只有牌桌封面上才生效，避免按住上键翻叫数时被踢出去。中途退出走暂停菜单。

**内存：** 打开时分配显示列表等约 18 KB（多人版把显示列表、骰子和扑克牌上限加大，原来约 12 KB）、两条 3.8 KB 条带、5.3 KB 精灵缓冲、6 KB 任务栈。实测打开后空闲堆约 65 KB（配网模式下），最大连续块约 34 KB。大图帧直接从 Flash 解码，不占内存。

**多人：** 人数在开局时固定到 `nt_t.np`，所有按玩家循环的地方都只到 `np`。规则改动（并列都得分、罚分制、加赛）见 `game-art/table/DESIGN.md` 第 9 节。改规则时**先改浏览器原型**，再用 `tests/table_host/golden_server.py` + `record_golden.js` 重新录对拍轨迹（Playwright 打开原型页面后注入脚本，调用 `recordTrace`），C 模型必须逐步一致。渲染器每一帧超出显示列表、骰子、扑克牌或字形池上限时会置 `ntg_t.overflow`，新画面要用四人六骰的轨迹检查一遍。

**开机即玩：** `application.cc`（NABO 板型分支）允许在 starting / wifi_configuring 状态 `BeginLocalApp()`，保持开机状态不变；期间的联网事件只记录下来，`EndLocalApp()` 时继续开机流程（若已联网就补发一次联网事件去激活）。已在没有可用 Wi-Fi 的工牌上验证；"游戏中途联网、退出后激活"尚未在真机验证。

**素材流程：** `game-art/table/`：起始图 → 图生视频（`art/video/`）→ `game-art/table/tools/clip_to_sprites.py` 抽帧抠图 → `tools/generate_table_sprites.py` 打包。改了素材要重新构建资源镜像，并用 upgrade（写应用+资源）刷写。

**画面速度：** 平时每帧 1.5–6.5 ms，28–30 fps；换画面时整屏重画 55–180 ms，会有一下停顿，后续可优化。不要把渲染代码放进 IRAM（深潜试过，开机即 abort 循环）。

## NABO 对决（v1.7.0 开发中）

**入口：** 牌桌封面第四项「对决 · 联机」。牌桌模型只抛出 `NT_EV_DUEL`；`nabo_table.c` 进入对决模式：分配 `nabo_duel_t`（约 5 KB）、把精灵缓冲换成对决包需要的 21.7 KB（牌桌的小缓冲先释放）、共用的精灵播放器指向 `duel_sprites.bin`、启动 ESP-NOW。对决封面「返回牌桌」置 `exit_req`，任务里 `duel_leave()` 全部还原。牌桌关闭时任务退出前也会调用 `duel_leave()`。

**不要改动的设计：**
- **拔枪靠硬件反色。** go_at 到达时 esp_timer 叫醒任务，任务先发 INVOFF（面板本来就靠 INVON 显示正常颜色），再 `ndu_flipped()` 记下时刻；反应时间从这一刻算起。反色前的按键在胶水层改到 go_at − 1，必然算抢跑。不要改成画一帧白屏（整屏重画 55–180 ms，两台不一致）。
- **对时不经过模型节拍。** 房主模型发的 PING 由 `nabo_link_send` 在发出瞬间改写 t1；客人的 PING 在收包回调里由 `ndu_ping_reply` 立刻回 PONG，不交给模型。若把 PONG 改回由模型回复，帧与 30 ms 滞后会让偏差错几十毫秒。
- **模型滞后 30 ms。** 按键时间戳来自按键回调，经主任务 Schedule 才到牌桌环形队列；滞后保证它们按自己的时间进入模型。来得更晚的输入被推到下一毫秒。
- **Wi-Fi：** `WifiManager::StopStation()` 只 `esp_wifi_stop` 不反初始化，ESP-NOW 再 `esp_wifi_start` 用 STA 接口；退出时 `esp_wifi_stop` 后 `StartStation()`。期间的断开/连上事件由 `application.cc` 记录，离开本地程序时照常激活。配网模式下不停热点，走 AP 接口（未验证）。
- 对决里 UP/DOWN 长按 1 秒暂停（由模型判断），所以 `nabo_table_on_cover()` 在对决中返回 false，长按上键不会退回小智。

**协议与规则改动：** 先改 `game-art/duel/prototype/duel_model.js`，再 `node tests/duel_host/record_duel_golden.js` 重录轨迹，`test_duel.py` 必须逐毫秒一致。

**NVS：** `nabo_ui` 命名空间新增 `duel_bo`、`duel_fake`、`duel_ai`、`duel_best`（u16 ms），只在变化时写。

**串口：** `>apps.open=duel` 直达对决封面；`>table.status` 的 `duel` 段有场景、阶段、比分、双方结果、往返/偏差、收发计数、是否反色；`>table.duel.test` 开启测试模式（不写纪录和设置）；`>table.press=up|down|ok` 在对决里变成一次按下+松开。

**待真机验证：** 构建大小、两台联机（对时偏差、拔枪同时性）、丢包环境、实体按键手感、声音、退出对决后 Wi-Fi 重连与小智对话、对决打开时的空闲堆（预计比牌桌少约 21 KB）、10 轮进出。

## 诊断与回归

开发固件保留串口命令，如 `>apps.status`、`>apps.open=home`、`>apps.open=jump`。它们用于定位页面和资源状态；使用实体按键/真实语音做最终验收。游戏诊断必须打开测试抑制模式，避免自动操作刷新用户最高分。

主页 `VoiceMemory` 每约 5 秒打印 heap、largest、Opus 栈余量和音频计数，只在主页工作，不干扰游戏绘制。记录长回答、打断、返回主页后再对话，检查 abort/assert、栈和最大连续内存块。日志可能包含 ASR/TTS、网络信息，只保存到被 Git 忽略的 `private/`，公开问题仅用脱敏堆栈和统计。

按影响运行测试：语音解析/PCM/游戏（跳一跳、节奏回声、牌桌）生产 C 测试，以及上游宿主测试。修改音频/生命周期必须加真实连续对话与程序来回切换；修改导航需验证所有父页面。不要反复运行不相关测试替代硬件验证。

## 下一版发布流程

1. 从本仓库的 `v1.0.0` 标签开始，保持板型身份 `ai-passport-nabo` 和分区接口。
2. 读取源码中的 AGENTS.md，修改最窄职责层，保留第三方许可。改动编译配置时更新锁定/配置并记录理由。
3. 宿主测试、目标构建、相应实机回归；给每份设备验证摘要记录具体应用 SHA256。
4. 打包四个**构建输出镜像**，从不打包已配网设备的整片 Flash、NVS 或 private。upgrade 模式写应用和资源（牌桌素材在资源分区）。
5. 更新 CHANGELOG、交接、验证边界、manifest 和 SHA256SUMS；标签与源码提交绑定。
6. 创建 Release 后下载公开资产重新核对 SHA256 和 ZIP 成员，确认文档/许可证随包。

## 已知边界

自定义单应用分区不能使用标准 OTA。网络语音需要小智服务。仅此 ESP32-C3 工牌经实机验证，其他上游板型保留源码但不是本项目的兼容承诺。首版不保证任意网络环境永久无重连，也不承诺全阶段 60 FPS；用户实际接受的连跳和声音、测量范围均在验证记录中。
