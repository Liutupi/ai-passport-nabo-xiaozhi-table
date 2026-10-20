# NABO 牌桌 · 对决 · 离线版

不带小智的 AI Passport 固件：开机进首页（NABO 牌桌 / NABO 对决 / 设置），不需要配网、激活或服务器。对决用 ESP-NOW 让两台工牌直连，只在对决打开时启动 Wi-Fi 驱动，退出即关闭。

启动器、设置、深睡和板级支持包来自本地的「NABO 游戏特辑」离线合集（去掉了深潜和跳一跳）；牌桌与对决的源码**直接编译小智版板型目录里的同一批文件**（`main/CMakeLists.txt` 引用 `../xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo/`，`NABO_STANDALONE` 只影响 `nabo_link.cc` 里谁来启动 Wi-Fi），两套固件的游戏逻辑始终一致。精灵包 `table_sprites.bin`、`duel_sprites.bin` 嵌入应用。

分区与 NABO 定制版相同（应用 `0x10000 / 0x3f0000`，资源、文件分区不读不写），所以可以用 `tools/flash_candidate.py` 只写应用、保留 NVS 和资源分区，随时再 upgrade 回小智版。

**正式版：[offline-v1.0.0](https://github.com/Liutupi/ai-passport-nabo-xiaozhi-table/releases/tag/offline-v1.0.0)，2026-10-21。** 两台均完成应用升级、回读和保留数据校验，用户确认木鱼声及赢／输动画正常。发布复用已验收原镜像，没有重新编译，应用 SHA256 为 `fb8af7fe4620104106c8e0fe29a351589ad554b7b3e68540815edb308ee745e3`。完整说明见 [发布说明](../docs/RELEASE_offline-v1.0.0.md)。

等待声使用原 NABO 木鱼的 90 ms 短敲，逐渐加快，拔枪前停止新敲击；保留无风声、清楚的拔枪铃及本机胜负动画（赢吹枪、输哭脸）。

## 构建

ESP-IDF 6.0.2，目标 esp32c3。依赖（LVGL 9.5.0、esp_lvgl_port 2.9.0、button 4.2.0、esp_codec_dev 1.6.2）由组件管理器按 `components/bsp/idf_component.yml` 下载。验收构建的配置及依赖快照为 `configs/offline-v1.0.0.sdkconfig`、`configs/offline-v1.0.0.dependencies.lock`。重新构建的字节可能因时间戳等元数据改变，不能替代 Release 中的已验收镜像。

```bash
cd standalone
idf.py set-target esp32c3
idf.py build
idf.py merge-bin -o nabo-table-duel-offline-merged.bin
python3 tools/verify_firmware.py build
```

## 刷写（一台一台）

```bash
python tools/flash_candidate.py --app build/nabo-table-duel-offline.bin --sha256 <SHA256> \
    --mode upgrade --port COMx --expected-mac aa:bb:cc:dd:ee:ff
```

先完整备份 8 MB 到 `private/device/`，只写 `0x10000` 的应用，再读回全片确认其余字节未变。

Windows 用户可使用 Release ZIP 内 `flash_one.bat`，每次只接一台；发行启动脚本源码保留在 `standalone/tools/flash_one.py` 和 `.bat`。在普通 Python 环境先安装 `python -m pip install -r requirements-flash.txt`。首次完整安装与已有分区的保留数据升级有所不同，详见发布说明。

## 操作

- 首页：上/下选择，OK 进入；设置里有音量、亮度、扬声器、自动休眠。
- 牌桌：与小智版相同；封面长按或双击上键回首页。
- 对决：封面「联机对战 / 单人练习 / 局数 / 虚晃 / 练习对手 / 返回牌桌」；屏幕反色时按 OK；对局中长按上或下 1 秒暂停。

## 许可

启动器代码 MIT；`components/bsp/` 为 FoloToy MIT（`LICENSE.FoloToy`）；字体 OFL 1.1（`main/OFL-*.txt`）；NABO 形象与美术不随 MIT 授权，见仓库 `THIRD_PARTY_NOTICES.md`。
