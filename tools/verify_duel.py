"""Drive NABO DUEL on real badges over the USB serial console (development firmware).

    python tools/verify_duel.py OUT_DIR [PORT ...]

Without ports it uses every Espressif USB serial device it finds. With one badge: a practice
match against NABO to the end, then back to the table and home. With two: they find each other
in the lobby, one invites, the other accepts, a whole online match is played (each badge fires
when its own status says the screen flipped), and both results must mirror. Then ten rounds of
open duel -> lobby -> back -> home with the heap and Wi-Fi reconnection after each.

Test mode (>table.duel.test) keeps the badge's record and saved settings untouched. Screens are
saved as PNG from >table.capture. Logs stay in OUT_DIR (keep it out of git: it can hold network
details). Exit code 1 on any reset, assert or failed check.
"""
import json
import re
import struct
import sys
import threading
import time
import zlib
from pathlib import Path

import serial
from serial.tools import list_ports

PROBLEMS = ('Guru Meditation', 'abort()', 'rst:0x', 'Backtrace', 'assert failed', 'did not stop', 'bus_lock',
            'stack overflow', 'not enough memory')
SCENE = {0: 'cover', 1: 'lobby', 2: 'inviting', 3: 'invited', 4: 'sync', 5: 'duel', 6: 'pause', 7: 'opppause',
         8: 'end', 9: 'left'}


class Badge:
    def __init__(self, port, out):
        self.port, self.out = port, out
        self.s = serial.Serial(port, 115200, timeout=0.05)
        self.buf, self.lock = [], threading.Lock()
        self.stop = False
        self.reader = threading.Thread(target=self.read, daemon=True)
        self.reader.start()

    def read(self):
        while not self.stop:
            line = self.s.readline()
            if line:
                with self.lock:
                    self.buf.append(line.decode('utf-8', 'replace').rstrip())

    def mark(self):
        with self.lock:
            return len(self.buf)

    def lines(self, since=0):
        with self.lock:
            return self.buf[since:]

    def cmd(self, c, wait=0.3):
        self.s.write((c + '\n').encode())
        self.s.flush()
        time.sleep(wait)

    def wait_for(self, pattern, since, timeout):
        end = time.time() + timeout
        while time.time() < end:
            for t in self.lines(since):
                m = re.search(pattern, t)
                if m:
                    return m
            time.sleep(0.02)
        return None

    def status(self):
        for _ in range(3):
            n = self.mark()
            self.cmd('>table.status', 0)
            m = self.wait_for(r'@table (\{.*\})', n, 1.5)
            if m:
                return json.loads(m.group(1))
        raise RuntimeError(f'{self.port}: no table status')

    def apps(self):
        n = self.mark()
        self.cmd('>apps.status', 0)
        m = self.wait_for(r'@apps (\{.*\})', n, 2)
        return json.loads(m.group(1)) if m else {}

    def press(self, k, wait=0.3):
        self.cmd('>table.press=' + k, wait)

    def duel(self):
        st = self.status()
        if 'duel' not in st:
            raise RuntimeError(f'{self.port}: not in the duel: {st}')
        return st['duel']

    def capture(self, name):
        n = self.mark()
        self.cmd('>table.capture', 0)
        if not self.wait_for(r'^@cap end', n, 30):
            print(self.port, 'capture timed out', name)
            return
        rows = {}
        for t in self.lines(n):
            p = t.split(' ')
            if len(p) == 5 and p[0] == '@cap' and p[1].isdigit():
                y, x, w, hexs = int(p[1]), int(p[2]), int(p[3]), p[4]
                rows.setdefault(y, {})
                for i in range(w):
                    v = int(hexs[4 * i:4 * i + 4], 16)
                    rows[y][x + i] = v
        raw = bytearray()
        for y in range(320):
            raw.append(0)
            for x in range(240):
                v = rows.get(y, {}).get(x, 0)
                raw += bytes(((v >> 11) << 3, ((v >> 5) & 63) << 2, (v & 31) << 3))
        chunk = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
        png = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 240, 320, 8, 2, 0, 0, 0))
               + chunk(b'IDAT', zlib.compress(bytes(raw))) + chunk(b'IEND', b''))
        (self.out / f'{self.tag}-{name}.png').write_bytes(png)
        print(self.port, 'captured', name, flush=True)

    def problems(self):
        return [t for t in self.lines() if any(k in t for k in PROBLEMS)]

    def close(self):
        self.stop = True
        time.sleep(0.1)
        (self.out / f'{self.tag}-serial.log').write_text('\n'.join(t for t in self.lines() if not t.startswith('@cap ')),
                                                          'utf-8')
        self.s.close()


def wait_scene(b, scenes, timeout, poll=0.15):
    end = time.time() + timeout
    while time.time() < end:
        d = b.duel()
        if SCENE.get(d['scene']) in scenes:
            return d
        time.sleep(poll)
    raise RuntimeError(f'{b.port}: never reached {scenes}, last {d}')


def open_duel(b):
    b.cmd('>table.duel.test', 0.3)
    b.cmd('>apps.open=duel', 5)
    d = wait_scene(b, {'cover'}, 10)
    assert d['radio'] == 1, f'{b.port}: ESP-NOW did not start {d}'
    return d


def to_table_cover(b):
    """From the duel cover: UP wraps to 返回牌桌, OK leaves the duel."""
    b.press('up')
    b.press('ok', 1.0)
    st = b.status()
    assert 'duel' not in st and st['scene'] == 0, f'{b.port}: not back on the table cover: {st}'


def play(badges, timeout=180, captures=True):
    """Fire on every badge when its status shows the draw; return the final duel blocks."""
    shot = set()
    end = time.time() + timeout
    fired = {b.port: None for b in badges}
    while time.time() < end:
        done = 0
        for b in badges:
            d = b.duel()
            scene, phase = SCENE.get(d['scene']), d['phase']
            key = (d['round'], d['attempt'])
            if scene == 'duel' and phase == 3 and d['my'][0] < 0 and fired[b.port] != key:
                b.press('ok', 0)
                fired[b.port] = key
                if captures and 'go' not in shot:
                    shot.add('go')
                    b.capture('go')
            if captures and scene == 'duel' and phase == 4 and 'reveal' not in shot:
                shot.add('reveal')
                b.capture('reveal')
            if captures and scene == 'duel' and phase == 2 and 'standoff' not in shot:
                shot.add('standoff')
                b.capture('standoff')
            if scene == 'end':
                done += 1
        if done == len(badges):
            break
        time.sleep(0.05)
    out = [b.duel() for b in badges]
    if captures:
        badges[0].capture('end')
    return out


def back_home(b, wifi_timeout=25):
    b.cmd('>apps.open=home', 3)
    end = time.time() + wifi_timeout
    a = {}
    while time.time() < end:
        a = b.apps()
        if a.get('wifi'):
            break
        time.sleep(1)
    return a


def main():
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    ports = sys.argv[2:] or [p.device for p in list_ports.comports() if p.vid == 0x303A]
    if not ports:
        raise SystemExit('No AI Passport found on USB.')
    badges = [Badge(p, out) for p in ports[:2]]
    for i, b in enumerate(badges):
        b.tag = 'AB'[i]
    time.sleep(2)
    results = {'ports': ports[:2]}
    ok = True
    try:
        # ---- practice on the first badge ----
        a = badges[0]
        open_duel(a)
        a.capture('cover')
        a.press('down')
        a.press('ok', 1.0)                       # 单人练习
        d = play([a], captures=len(badges) == 1)[0]
        results['practice'] = d
        assert SCENE[d['scene']] == 'end' and max(d['me'], d['opp']) >= 2, d
        a.press('down')
        a.press('ok', 1.0)                       # 回封面
        to_table_cover(a)
        results['practice_home'] = back_home(a)
        # ---- online ----
        if len(badges) == 2:
            a, b = badges
            for x in badges:
                open_duel(x)
                x.press('ok', 0.5)               # 联机对战 -> lobby
            time.sleep(2.5)
            da = a.duel()
            assert da['peers'] >= 1, f'A sees no badge in the lobby: {da}'
            a.capture('lobby')
            a.press('ok', 1.0)                   # invite the first row
            wait_scene(b, {'invited'}, 5)
            b.capture('invited')
            b.press('ok', 1.0)
            wait_scene(a, {'sync', 'duel'}, 5)
            ra, rb = play([a, b])
            results['online'] = {'A': ra, 'B': rb}
            assert SCENE[ra['scene']] == 'end' and SCENE[rb['scene']] == 'end', (ra, rb)
            assert (ra['me'], ra['opp']) == (rb['opp'], rb['me']), f'scores do not mirror: {ra} {rb}'
            print('ONLINE rtt/offset', ra['rtt'], ra['offset'], rb['rtt'], rb['offset'], flush=True)
            for x in badges:
                x.press('down')
                x.press('ok', 1.5)               # 回大厅
                wait_scene(x, {'lobby'}, 5)
                x.press('up')                    # the last row: 返回
                x.press('ok', 0.8)
                wait_scene(x, {'cover'}, 5)
                to_table_cover(x)
                results['online_home_' + x.tag] = back_home(x)
        # ---- open / close cycles ----
        cycles = []
        for i in range(10):
            x = badges[i % len(badges)]
            open_duel(x)
            x.press('ok', 1.5)                   # lobby: radio busy
            x.press('up')
            x.press('ok', 0.8)                   # back to the duel cover
            to_table_cover(x)
            a_status = back_home(x)
            cycles.append({'port': x.port, 'heap': a_status.get('heap'), 'wifi': a_status.get('wifi')})
            print('cycle', i + 1, cycles[-1], flush=True)
        results['cycles'] = cycles
        bad_wifi = [c for c in cycles if not c['wifi']]
        if bad_wifi:
            print('Wi-Fi did not come back after', len(bad_wifi), 'cycles', flush=True)
            ok = False
    except Exception as e:   # keep the logs
        print('FAILED:', e, flush=True)
        results['error'] = str(e)
        ok = False
    for x in badges:
        heap = [t for t in x.lines() if 'NABO-Table' in t and ('free=' in t or 'heap=' in t)]
        results['heap_' + x.tag] = heap[-12:]
        bad = x.problems()
        results['problems_' + x.tag] = bad
        if bad:
            ok = False
        x.close()
    (out / 'results.json').write_text(json.dumps(results, indent=2, ensure_ascii=False), 'utf-8')
    print('RESULT', 'OK' if ok else 'FAIL', flush=True)
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
