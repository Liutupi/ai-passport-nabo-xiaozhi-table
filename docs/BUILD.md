# 编译和测试

## 固定环境

- Python 3.11+，Git，ESP-IDF **6.0.2**；目标 `esp32c3`。
- 上游源码提交：`4632dc51f0a5ad26e08542e131e6e48da41e4ff3`。
- 本版源码：`xiaozhi-esp32/`；组件版本：其中的 `dependencies.lock`。
- 已刷入镜像的完整配置：`configs/release-v1.0.0.sdkconfig`。

先安装/初始化 ESP-IDF。Windows 使用安装器提供的 ESP-IDF PowerShell；Linux/macOS 先执行 IDF 的 `export.sh`。确认 `idf.py --version` 显示 6.0.2，且 `IDF_PATH` 已设置。IDF 5.x 不支持，其他 6.x 版本未替代本版验证环境。

## 构建

在仓库根目录：

```powershell
python tools/build.py
```

工具在独立的 `xiaozhi-esp32/build-release/` 中生成配置和应用，以发行配置作为默认值；不依赖作者电脑的绝对路径。输出 `xiaozhi.bin`、bootloader、partition table、generated assets。已经安装定制版的设备不能直接用 `idf.py flash` 升级，请使用校验工具。
Windows 构建启用 Ninja response files，以适应含空格、较深目录的链接命令长度限制。

需要调整配置时，可使用上游入口：

```powershell
cd xiaozhi-esp32
python scripts/build.py folotoy/ai-passport-nabo --name ai-passport-nabo --language zh-CN
```

这个入口会更新本地 sdkconfig 和 build 状态；调整后需重新确认芯片、板型、分区、语言、音频任务栈及蓝牙配置。

## 已包含的资源

NABO RLE、平台/角色精灵、跳跃 PCM、牌桌精灵包 `table_assets/table_sprites.bin`、中文字体均在板型目录，干净检出即可构建，不需要作者的旧 Muse 项目、私有素材路径或移植脚本。

**对决素材：** `table_assets/duel_sprites.bin` 和 `nabo_duel_sprite_ids.h` 由 `python3 tools/generate_duel_sprites.py` 生成，输入是 `game-art/duel/sprites/large/<动作>/NN.png`（从 `game-art/duel/art/` 验收图集切出的 256×256 透明帧）。按 0.85 倍缩放、每个动作 64 色、第 0 帧关键帧其余差分，逐帧解码回来核对；总大小超过 500 KB 会报错。同时生成原型用的 `game-art/duel/prototype/assets/nabo_duel_sprites.js`（解码后的 RGB565 帧）。资源分区 3 MB，v1.6.0 用了 2,537,295 字节，加上对决约 3.03 MB。

**字体：** `python tools/generate_fonts.py --converter lv_font_conv --sans SourceHanSansCN-Medium.otf --archivo ArchivoBlack-Regular.ttf --mono SpaceMono-Bold.ttf`（lv_font_conv 1.5.3；源字体 SHA256 记在 `game-art/fonts.json`：思源黑体取自 adobe-fonts/source-han-sans 的 release 分支 `SubsetOTF/CN/`，另两个取自 google/fonts 的 `ofl/`）。

**牌桌素材：** `table_assets/table_sprites.bin` 和 `nabo_table_sprite_ids.h` 由 `python3 tools/generate_table_sprites.py` 生成，输入是 `game-art/table/sprites/`（先运行 `python3 game-art/table/tools/clip_to_sprites.py` 从 `game-art/table/art/video/` 的图生视频抽帧生成，需要 ffmpeg）。每个动作一套 64 色调色板；大图每帧都是关键帧，直接从 Flash 解码；小图第 0 帧是关键帧，之后是差分帧。脚本会逐帧解码回来比对像素和颜色误差。构建时 CMake 通过 `DEFAULT_ASSETS_EXTRA_FILES` 把 `table_assets/` 打进 `generated_assets.bin`，运行时用 `Assets::GetAssetData("table_sprites.bin")` 原地读取。需要 Pillow、NumPy。

**字体：** 新增界面中文后重新生成。源字体不入库，SHA256 记录在 `game-art/fonts.json`：

```sh
python3 tools/generate_fonts.py --converter path/to/lv_font_conv \
    --sans SourceHanSansCN-Medium.otf --archivo ArchivoBlack-Regular.ttf --mono SpaceMono-Bold.ttf
```

字体来源：Source Han Sans（adobe-fonts/source-han-sans 的 release 分支 `SubsetOTF/CN/`）、Archivo Black 与 Space Mono（google/fonts 的 `ofl/`），lv_font_conv 1.5.3（npm）。下载后先核对 SHA256 与 fonts.json 一致。

`prepare_nabo.py`、`port_apps.py` 是早期移植生成器，未放进发行仓库；它们会覆盖后续手工修复，不能用来维护本版。修改资源要保留来源/许可并更新对应格式、偏移、大小和校验记录。

## 宿主测试

```powershell
python -m pip install -r requirements-dev.txt
python tools/run_tests.py
```

GCC/G++ 必须在 PATH 中，或设置 `CC`、`CXX`。Windows 可用 MinGW/w64devkit；Linux 使用系统 GCC。测试直接编译生产 C/C++：语音指令案例、PCM 分块极值/尾块、跳一跳与节奏回声回归、牌桌（按浏览器原型录下的 6 条对局逐步对拍、规则用例、增量画面与整屏重绘逐像素比对）、立体骰子渲染，另有上游宿主测试。

宿主测试不刷工牌。通过后仍须在真实硬件验证对话、音频、按键和程序切换。CI 不替代这一步。

## 可复现性的范围

v1.0.0 发布的是已经验收的原镜像，SHA256 见 Release 清单。源文件、SDK 配置与组件锁可复现功能，但重新编译的时间戳、路径和 ESP-IDF 版本字符串可能影响字节；不能将重新构建的不同 SHA256 自动认作同一份已验收镜像。
