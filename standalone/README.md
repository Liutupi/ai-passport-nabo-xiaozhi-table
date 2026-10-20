# NABO 牌桌 · 对决 · 离线版

不带小智的 AI Passport 固件：开机进首页（NABO 牌桌 / NABO 对决 / 设置），不需要配网、激活或服务器。对决用 ESP-NOW 让两台工牌直连，只在对决打开时启动 Wi-Fi 驱动，退出即关闭。

启动器、设置、深睡和板级支持包来自本地的「NABO 游戏特辑」离线合集（去掉了深潜和跳一跳）；牌桌与对决的源码**直接编译小智版板型目录里的同一批文件**（`main/CMakeLists.txt` 引用 `../xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo/`，`NABO_STANDALONE` 只影响 `nabo_link.cc` 里谁来启动 Wi-Fi），两套固件的游戏逻辑始终一致。精灵包 `table_sprites.bin`、`duel_sprites.bin` 嵌入应用。

分区与 NABO 定制版相同（应用 `0x10000 / 0x3f0000`，资源、文件分区不读不写），所以可以用 `tools/flash_candidate.py` 只写应用、保留 NVS 和资源分区，随时再 upgrade 回小智版。

## 构建

ESP-IDF 6.0.2，目标 esp32c3。依赖（LVGL 9.5.0、esp_lvgl_port 2.9.0、button 4.2.0、esp_codec_dev 1.6.2）由组件管理器按 `components/bsp/idf_component.yml` 下载。

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

## 操作

- 首页：上/下选择，OK 进入；设置里有音量、亮度、扬声器、自动休眠。
- 牌桌：与小智版相同；封面长按或双击上键回首页。
- 对决：封面「联机对战 / 单人练习 / 局数 / 虚晃 / 练习对手 / 返回牌桌」；屏幕反色时按 OK；对局中长按上或下 1 秒暂停。

## 许可

启动器代码 MIT；`components/bsp/` 为 FoloToy MIT（`LICENSE.FoloToy`）；字体 OFL 1.1（`main/OFL-*.txt`）；NABO 形象与美术不随 MIT 授权，见仓库 `THIRD_PARTY_NOTICES.md`。
