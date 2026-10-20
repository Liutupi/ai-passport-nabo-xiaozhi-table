"""Flash the NABO 牌桌 · 对决 offline firmware onto ONE AI Passport (app only).

Plug in exactly one badge and run flash_one.bat. The badge is identified by its MAC, fully
backed up to private/device/, then only the application at 0x10000 is written and every other
byte (bootloader, NVS settings, assets, files) is read back to prove it did not change.
"""
import re
import subprocess
import sys
import time
from pathlib import Path

import serial
from serial.tools import list_ports

HERE = Path(__file__).resolve().parent
APP = HERE / 'nabo-table-duel-offline.bin'
APP_SHA256 = 'fb8af7fe4620104106c8e0fe29a351589ad554b7b3e68540815edb308ee745e3'

ports = [p.device for p in list_ports.comports() if p.vid == 0x303A]
if len(ports) != 1:
    sys.exit(f'请只插一台工牌（现在找到 {len(ports)} 台：{ports}）')
port = ports[0]
out = subprocess.run([sys.executable, '-m', 'esptool', '--port', port, 'read-mac'], capture_output=True, text=True)
m = re.search(r'MAC: ([0-9a-fA-F:]{17})', out.stdout + out.stderr)
if not m:
    sys.exit('读不到 MAC：' + (out.stdout + out.stderr)[-400:])
mac = m.group(1).lower()
print('工牌', port, mac, flush=True)
subprocess.run([sys.executable, str(HERE / 'flash_candidate.py'), '--app', str(APP), '--sha256', APP_SHA256,
                '--mode', 'upgrade', '--port', port, '--expected-mac', mac,
                '--private-dir', str(HERE / 'private' / 'device')], check=True, cwd=HERE)
print('等工牌重启……', flush=True)
time.sleep(6)
for _ in range(10):
    try:
        s = serial.Serial(port, 115200, timeout=0.3)
        break
    except Exception:
        time.sleep(1)
else:
    sys.exit('刷写成功，但重启后串口没回来；请看工牌屏幕是否进了首页')
s.write(b'>games.status\n')
end = time.time() + 5
while time.time() < end:
    line = s.readline().decode('utf-8', 'replace').strip()
    if line.startswith('@games'):
        print('启动正常：', line, flush=True)
        print('完成。拔下这台，插下一台再运行。', flush=True)
        sys.exit(0)
sys.exit('刷写成功，但没收到状态回复；请看工牌屏幕')
