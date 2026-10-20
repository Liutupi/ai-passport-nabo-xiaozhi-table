"""Host tests for NABO DUEL: the C model must behave exactly like the browser prototype.

tests/duel_host/golden/*.json were recorded from game-art/duel/prototype with
tests/duel_host/record_duel_golden.js: for every badge, each key press, received packet and
millisecond tick in order, with the packets it sent, the cues it raised and a state snapshot
every 50 ms. Each test replays one badge's inputs on the C model and compares everything.
Packets go through the C wire encoder and decoder on the way.

The other tests run several C models over a simulated radio (latency, jitter, loss, cuts) and
check the protocol end to end, plus rule tables and the wire format.
"""
import ctypes
import os
import json
import random
import subprocess
import tempfile
import unittest
from pathlib import Path

from host_tools import CC, SHARED_EXPORT

ROOT = Path(__file__).resolve().parent
BOARD = ROOT / 'xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo'
HOST = ROOT / 'tests/duel_host'
SOURCES = [BOARD / 'nabo_duel_model.c', HOST / 'duel_host.c']

temp = tempfile.TemporaryDirectory()
library = Path(temp.name) / 'duel.so'
subprocess.run([CC, '-std=gnu11', '-O2', '-shared', SHARED_EXPORT, '-Wall', '-Wextra', '-Werror',
                '-Wno-missing-field-initializers', '-I', str(BOARD)] + [str(s) for s in SOURCES]
               + ['-o', str(library)], check=True)
lib = ctypes.CDLL(str(library))
U, I = ctypes.c_uint, ctypes.c_int
lib.host_new.argtypes = [I, I, U, I, I, I, I, I]
lib.host_key.argtypes = [I, U, I, I]
lib.host_tick.argtypes = [I, U]
lib.host_recv.argtypes = [I, U, I, ctypes.c_char_p, I, I]
lib.host_out.argtypes = [I, ctypes.c_char_p]
lib.host_take_fx.argtypes = [I, ctypes.POINTER(U), ctypes.POINTER(I), ctypes.POINTER(I), I]
lib.host_snap.argtypes = [I, U, ctypes.POINTER(ctypes.c_longlong)]

TYPES = ['', 'HELLO', 'INVITE', 'ACCEPT', 'DECLINE', 'PING', 'PONG', 'ROUND', 'LATE', 'RESULT', 'NEXT', 'PAUSE',
         'RESUME', 'BEAT', 'BYE', 'ACK']
TYPE_ID = {n: i for i, n in enumerate(TYPES) if n}
WHY = {'no': 0, 'busy': 1, 'cancel': 2, 'quit': 3}
RES = {'none': 0, 'hit': 1, 'foul': 2}
KEYS = {'up': 0, 'down': 1, 'ok': 2}
SCENES = ['cover', 'lobby', 'inviting', 'invited', 'sync', 'duel', 'pause', 'opppause', 'end', 'left']
PHASES = ['wait', 'ready', 'standoff', 'go', 'reveal']
FX = ['tick', 'invite', 'ready', 'standoff', 'fake', 'go', 'foul', 'shot', 'none', 'win', 'lose', 'replay', 'pause',
      'left']
FAKES = ['text', 'twitch', 'tumble']
OUTCOMES = ['win', 'lose', 'replay']
TOASTS = ['', '对方固件版本不同', '对方正在对决', '没有回应', '对方拒绝了', '对方正忙', '对方取消了邀请']
NAME_LEN = 10


def u32(v):
    return int(v) & 0xFFFFFFFF


def canon(p):
    """A prototype packet as the fields the wire carries."""
    t = p['type']
    c = {'type': t, 'v': p.get('v', 1), 's': u32(p.get('s') or 0), 'seq': p.get('seq') or 0, 'ack': p.get('ack') or 0}
    if t == 'HELLO':
        c.update(name=p['name'], busy=int(bool(p['busy'])))
    elif t == 'INVITE':
        c.update(name=p['name'], bo=p['bo'], fake=int(p['fake']))
    elif t == 'ACCEPT':
        c.update(name=p['name'])
    elif t in ('DECLINE', 'BYE'):
        c.update(why=WHY[p['why']])
    elif t == 'PING':
        c.update(id=p['id'], t1=u32(p['t1']), t2=0)
    elif t == 'PONG':
        c.update(id=p['id'], t1=u32(p['t1']), t2=u32(p['t2']))
    elif t == 'ROUND':
        c.update(r=p['r'], a=p['a'], sh=p['sh'], sg=p['sg'], seed=u32(p['seed']), go=u32(p['go']))
    elif t == 'LATE':
        c.update(r=p['r'], a=p['a'])
    elif t == 'RESULT':
        c.update(r=p['r'], a=p['a'], k=RES[p['k']], ms=p['ms'])
    return c


def encode(c):
    """Wire bytes for a canonical packet (mirrors nd_encode)."""
    name = lambda s: s.encode().ljust(NAME_LEN, b'\0')[:NAME_LEN]
    b = bytearray(b'ND') + bytes([c['v'], TYPE_ID[c['type']]]) + c['s'].to_bytes(4, 'little') \
        + c['seq'].to_bytes(2, 'little') + c['ack'].to_bytes(2, 'little') + b'\0'
    t = c['type']
    if t == 'HELLO':
        b += name(c['name']) + bytes([c['busy']])
    elif t == 'INVITE':
        b += name(c['name']) + bytes([c['bo'], c['fake']])
    elif t == 'ACCEPT':
        b += name(c['name'])
    elif t in ('DECLINE', 'BYE'):
        b += bytes([c['why']])
    elif t in ('PING', 'PONG'):
        b += bytes([c['id']]) + c['t1'].to_bytes(4, 'little') + c['t2'].to_bytes(4, 'little')
    elif t == 'ROUND':
        b += bytes([c['r'], c['a'], c['sh'], c['sg']]) + c['seed'].to_bytes(4, 'little') + c['go'].to_bytes(4, 'little')
    elif t == 'LATE':
        b += bytes([c['r'], c['a']])
    elif t == 'RESULT':
        b += bytes([c['r'], c['a'], c['k']]) + c['ms'].to_bytes(2, 'little')
    return bytes(b)


def decode(b):
    """Canonical packet from the C encoder's bytes."""
    assert b[:2] == b'ND', b
    t = TYPES[b[3]]
    c = {'type': t, 'v': b[2], 's': int.from_bytes(b[4:8], 'little'), 'seq': int.from_bytes(b[8:10], 'little'),
         'ack': int.from_bytes(b[10:12], 'little')}
    q = b[13:]
    name = lambda x: x.split(b'\0')[0].decode()
    if t == 'HELLO':
        c.update(name=name(q[:NAME_LEN]), busy=q[NAME_LEN])
    elif t == 'INVITE':
        c.update(name=name(q[:NAME_LEN]), bo=q[NAME_LEN], fake=q[NAME_LEN + 1])
    elif t == 'ACCEPT':
        c.update(name=name(q[:NAME_LEN]))
    elif t in ('DECLINE', 'BYE'):
        c.update(why=q[0])
    elif t in ('PING', 'PONG'):
        c.update(id=q[0], t1=int.from_bytes(q[1:5], 'little'), t2=int.from_bytes(q[5:9], 'little'))
    elif t == 'ROUND':
        c.update(r=q[0], a=q[1], sh=q[2], sg=q[3], seed=int.from_bytes(q[4:8], 'little'), go=int.from_bytes(q[8:12], 'little'))
    elif t == 'LATE':
        c.update(r=q[0], a=q[1])
    elif t == 'RESULT':
        c.update(r=q[0], a=q[1], k=q[2], ms=int.from_bytes(q[3:5], 'little'))
    return c


def take_out(i):
    n = lib.host_take_out(i)
    assert n >= 0, 'outbox overflow'
    out = []
    buf = ctypes.create_string_buffer(64)
    for k in range(n):
        ln = lib.host_out(k, buf)
        to = lib.host_out_to(k)
        out.append(('*' if to < 0 else to, decode(buf.raw[:ln])))
    return out


def take_fx(i):
    t, k, a = (U * 16)(), (I * 16)(), (I * 16)()
    n = lib.host_take_fx(i, t, k, a, 16)
    return [[t[j], FX[k[j]], FAKES[a[j]] if FX[k[j]] == 'fake' else ''] for j in range(n)]


def snap(i, t):
    v = (ctypes.c_longlong * 23)()
    lib.host_snap(i, t, v)
    return list(v)


def proto_snap(s):
    """The prototype snapshot in the same layout as host_snap (peer count excluded)."""
    sc = SCENES.index(s['sc'])
    ph = PHASES.index(s['ph']) if s['ph'] else -1
    m = s['m'] or [None] * 6   # no match: the C fields are stale
    v = [sc, s['sel'], ph, int(s['m'] is not None)] + m
    if s['c']:
        me, opp, resolved, go_at, outcome = s['c']
        v += [1, int(me is not None), me[0] if me else 0, me[1] if me else 0, int(opp is not None),
              opp[0] if opp else 0, opp[1] if opp else 0, resolved, u32(go_at),
              OUTCOMES.index(outcome) if outcome else -1]
    else:
        v += [0, None, None, None, None, None, None, None, None, None]
    v += [TOASTS.index(s['toast']), s['best']]
    return v


def compare_snap(c, p):
    """None in the prototype layout means 'no attempt': the C fields there are stale, skip them."""
    return all(b is None or a == b for a, b in zip(c, p))


class GoldenReplay(unittest.TestCase):
    def replay(self, path):
        data = json.loads(path.read_text())
        for idx, tr in enumerate(data['badges']):
            s = tr['settings']
            lib.host_new(0, tr['mac'], u32(tr['entropy']), s['bo'], int(s['fake']), s['ai'], tr['best'], int(tr['suppress']))
            for n, st in enumerate(tr['steps']):
                where = f"{path.name} badge {tr['mac']:#06x} step {n} ({st['a']} t={st['t']})"
                if st['a'] == 'T':
                    for t in range(st['t'], st['t'] + st['n']):
                        lib.host_tick(0, u32(t))
                        out, fx = take_out(0), take_fx(0)
                        self.assertEqual((out, fx), ([], []), f'{where}: unexpected output at t={t}')
                    continue
                if st['a'] == 'k':
                    lib.host_set_sel(0, st['sel'])
                    lib.host_key(0, u32(st['t']), KEYS[st['k']], st['down'])
                elif st['a'] == 'r':
                    self.assertTrue(lib.host_recv(0, u32(st['t']), st['from'], encode(canon(st['pkt'])),
                                                  len(encode(canon(st['pkt']))), st['rssi']), where)
                else:
                    lib.host_tick(0, u32(st['t']))
                want = [(o['to'], canon(o['p'])) for o in st['out']]
                self.assertEqual(take_out(0), want, f'{where}: packets')
                self.assertEqual(take_fx(0), [[u32(f[0]), f[1], f[2]] for f in st['fx']], f'{where}: cues')
                if 's' in st:
                    c, p = snap(0, u32(st['t']))[:22], proto_snap(st['s'])
                    self.assertTrue(compare_snap(c, p), f'{where}: state\n C  {c}\n JS {p}')


for _path in sorted((HOST / 'golden').glob('*.json')):
    setattr(GoldenReplay, 'test_' + _path.stem, lambda self, p=_path: self.replay(p))


# ---------- C models over a simulated radio ----------

class Air:
    """Several C badges, each with its own clock, joined by a lossy link (1 ms steps)."""

    def __init__(self, seed=1, latency=3, jitter=2, loss=0.0):
        self.rng = random.Random(seed)
        self.g = 0
        self.nodes = []
        self.queue = []
        self.latency, self.jitter, self.loss = latency, jitter, loss
        self.fx = []
        self.n = 0

    def add(self, mac, off=0, bo=3, fake=True, ai=1, entropy=None, bot=None, rssi=-45):
        i = len(self.nodes)
        lib.host_new(i, mac, entropy if entropy is not None else mac * 2654435761 & 0xFFFFFFFF, bo, int(fake), ai, 0, 1)
        self.nodes.append(dict(i=i, mac=mac, off=off, antenna=True, bot=bot, rssi=rssi, fx=[]))
        return self.nodes[-1]

    def local(self, n):
        return (self.g + n['off']) & 0xFFFFFFFF

    def key(self, n, k, down=True):
        lib.host_key(n['i'], self.local(n), KEYS[k], int(down))
        self.flush(n)

    def tap(self, n, k):
        self.key(n, k, True)
        self.key(n, k, False)

    def flush(self, n):
        for to, pkt in take_out(n['i']):
            n['fx'] += take_fx(n['i'])
            targets = [m for m in self.nodes if m is not n] if to == '*' else [m for m in self.nodes if m['mac'] == to]
            for m in targets:
                if not n['antenna'] or not m['antenna'] or self.rng.random() < self.loss:
                    continue
                lat = max(1, round(self.latency + (self.rng.random() * 2 - 1) * self.jitter))
                self.n += 1
                self.queue.append((self.g + lat, self.n, m, n['mac'], encode(pkt)))
        n['fx'] += take_fx(n['i'])

    def step(self, ms):
        for _ in range(ms):
            self.g += 1
            due = sorted((q for q in self.queue if q[0] <= self.g), key=lambda q: (q[0], q[1]))
            self.queue = [q for q in self.queue if q[0] > self.g]
            for _, _, m, frm, wire in due:
                if m['antenna']:
                    lib.host_recv(m['i'], self.local(m), frm, wire, len(wire), m['rssi'])
                    self.flush(m)
            for n in self.nodes:
                if n['bot']:
                    n['bot'](self, n)
                lib.host_tick(n['i'], self.local(n))
                self.flush(n)

    def state(self, n):
        return snap(n['i'], self.local(n))


def bot(mean=260, sd=30, foul=0.0, invite=None, rematch=True, seed=1):
    rng = random.Random(seed)
    st = {'key': None, 'press': None, 'wait': 0}

    def act(air, n):
        if air.g < st['wait']:
            return
        s = air.state(n)
        scene = SCENES[s[0]]
        if scene == 'cover' and invite is not False:
            lib.host_set_sel(n['i'], 0); air.tap(n, 'ok'); st['wait'] = air.g + 200
        elif scene == 'lobby' and invite:
            for r in range(4):
                if lib.host_lobby_row(n['i'], r) == invite:
                    lib.host_set_sel(n['i'], r); air.tap(n, 'ok'); st['wait'] = air.g + 200
        elif scene == 'invited':
            air.tap(n, 'ok'); st['wait'] = air.g + 200
        elif scene == 'end' and rematch and s[0] == SCENES.index('end'):
            lib.host_set_sel(n['i'], 0); air.tap(n, 'ok'); st['wait'] = air.g + 2000
        elif scene == 'duel' and s[10]:
            go_at = s[18]
            if st['key'] != go_at:
                st['key'] = go_at
                if rng.random() < foul:
                    st['press'] = air.g + max(0, (go_at - air.local(n)) - rng.randint(200, 900))
                else:
                    st['press'] = air.g + (go_at - air.local(n)) + max(120, int(rng.gauss(mean, sd)))
            if not s[11] and not s[17] and air.g >= st['press']:
                air.tap(n, 'ok')
    return act


def outcomes(n):
    return [f[1] for f in n['fx'] if f[1] in ('win', 'lose', 'replay')]


class Protocol(unittest.TestCase):
    def match(self, **net):
        air = Air(**net)
        a = air.add(0x3F2A, off=5000, bot=bot(mean=240, invite=0x1B07, rematch=False, seed=2))
        b = air.add(0x1B07, off=0xFFFF0000, bot=bot(mean=300, rematch=False, seed=3))   # clock wraps during the match
        air.step(60000)
        return air, a, b

    def check_consistent(self, a, b):
        mirror = {'win': 'lose', 'lose': 'win', 'replay': 'replay'}
        oa, ob = outcomes(a), outcomes(b)
        self.assertGreater(len(oa), 0)
        self.assertEqual([mirror[x] for x in oa[:len(ob)]], ob[:len(oa)])

    def test_clean_link(self):
        air, a, b = self.match()
        sa, sb = air.state(a), air.state(b)
        self.assertEqual(SCENES[sa[0]], 'end')
        self.assertEqual(SCENES[sb[0]], 'end')
        self.assertEqual((sa[4], sa[5]), (sb[5], sb[4]))
        self.assertEqual(sa[4], 2, 'the faster badge wins two out of three')
        self.check_consistent(a, b)
        # Both screens flip at the same instant: go cues within 2 ms of each other on the global clock.
        ga = [f[0] - a['off'] for f in a['fx'] if f[1] == 'go']
        gb = [(f[0] - b['off']) & 0xFFFFFFFF for f in b['fx'] if f[1] == 'go']
        self.assertEqual(len(ga), len(gb))
        for x, y in zip(ga, gb):
            self.assertLessEqual(abs(((x - y + 2 ** 31) & 0xFFFFFFFF) - 2 ** 31), 2)

    def test_lossy_link(self):
        air, a, b = self.match(seed=5, latency=30, jitter=20, loss=0.2)
        self.assertEqual(SCENES[air.state(a)[0]], 'end')
        self.assertEqual(SCENES[air.state(b)[0]], 'end')
        self.check_consistent(a, b)

    def test_cut_returns_to_lobby(self):
        air = Air()
        a = air.add(0x3F2A, bot=bot(invite=0x1B07, seed=2))
        b = air.add(0x1B07, off=123456, bot=bot(seed=3))
        air.step(6000)
        self.assertEqual(SCENES[air.state(a)[0]], 'duel')
        b['antenna'] = False
        air.step(4000)
        self.assertIn(SCENES[air.state(a)[0]], ('left', 'lobby'))
        self.assertIn('left', [f[1] for f in a['fx']])

    def test_late_round_is_rescheduled(self):
        # The guest only hears the ROUND when it is almost time to draw: it answers LATE and the
        # host schedules the attempt again.
        air = Air()
        a = air.add(0x3F2A, bot=bot(invite=0x1B07, seed=2))
        b = air.add(0x1B07, off=777, bot=bot(seed=3))
        air.step(300)   # paired, syncing
        held = []
        orig = air.flush

        def delay_round(n):
            outs = take_out(n['i'])
            n['fx'] += take_fx(n['i'])
            for to, pkt in outs:
                if pkt['type'] == 'ROUND' and not held:
                    held.append(1)
                    go_local = pkt['go']
                    lat = ((go_local - air.local(b)) & 0xFFFFFFFF) - 100   # arrives 100 ms before the draw
                    air.queue.append((air.g + lat, -1, b, n['mac'], encode(pkt)))
                    continue
                targets = [m for m in air.nodes if m is not n] if to == '*' else [m for m in air.nodes if m['mac'] == to]
                for m in targets:
                    air.n += 1
                    air.queue.append((air.g + 2, air.n, m, n['mac'], encode(pkt)))
        air.flush = delay_round
        air.step(8000)
        air.flush = orig
        air.step(20000)
        self.assertTrue(held)
        self.assertGreater(len(outcomes(a)), 0, 'play continued after LATE')
        self.check_consistent(a, b)

    def test_three_badges(self):
        air = Air(seed=4)
        a = air.add(0x3F2A, bot=bot(invite=0x08C5, rematch=False, seed=2), rssi=-42)
        b = air.add(0x1B07, off=50, bot=bot(invite=0x08C5, rematch=False, seed=3), rssi=-46)
        c = air.add(0x08C5, off=900, bot=bot(rematch=False, seed=4), rssi=-60)
        air.step(30000)
        scenes = sorted(SCENES[air.state(n)[0]] for n in (a, b, c))
        self.assertEqual(scenes.count('end'), 2, scenes)
        self.assertEqual(scenes.count('lobby'), 1, scenes)

    def test_solo_practice(self):
        air = Air()
        a = air.add(0x3F2A, bo=5, ai=2, bot=bot(invite=False, mean=230, rematch=False, seed=9))
        lib.host_set_sel(0, 1)
        air.tap(a, 'ok')
        air.step(60000)
        s = air.state(a)
        self.assertEqual(SCENES[s[0]], 'end')
        self.assertEqual(max(s[4], s[5]), 3)


class Rules(unittest.TestCase):
    def test_judge_table(self):
        H, F, N = 1, 2, 0
        cases = [((H, 200, H, 250), 0), ((H, 250, H, 200), 1), ((H, 200, H, 200), 2), ((H, 900, F, 0), 0),
                 ((H, 900, N, 0), 0), ((F, 0, N, 0), 1), ((N, 0, F, 0), 0), ((F, 0, F, 0), 2), ((N, 0, N, 0), 2),
                 ((F, 0, H, 300), 1), ((N, 0, H, 300), 1)]
        for args, want in cases:
            self.assertEqual(lib.host_judge(*args), want, args)

    def test_wire_round_trip(self):
        samples = [
            {'type': 'HELLO', 'v': 1, 's': 0, 'seq': 0, 'ack': 0, 'name': 'NABO-3F2A', 'busy': 1},
            {'type': 'INVITE', 'v': 1, 's': 0xDEADBEEF, 'seq': 7, 'ack': 0, 'name': 'NABO-1B07', 'bo': 5, 'fake': 0},
            {'type': 'ROUND', 'v': 1, 's': 5, 'seq': 65535, 'ack': 0, 'r': 3, 'a': 2, 'sh': 1, 'sg': 2,
             'seed': 0xFFFFFFFF, 'go': 4000000000},
            {'type': 'RESULT', 'v': 1, 's': 5, 'seq': 9, 'ack': 0, 'r': 1, 'a': 1, 'k': 1, 'ms': 213},
            {'type': 'PONG', 'v': 1, 's': 5, 'seq': 0, 'ack': 0, 'id': 7, 't1': 123, 't2': 4294967295},
            {'type': 'ACK', 'v': 1, 's': 5, 'seq': 0, 'ack': 42},
        ]
        lib.host_new(3, 0x0001, 1, 3, 1, 1, 0, 1)
        for c in samples:
            wire = encode(c)
            self.assertLessEqual(len(wire), 40)
            self.assertEqual(lib.host_recv(3, 1, 0x0002, wire, len(wire), -40), 1, c['type'])
        # Malformed input is dropped, never crashes.
        for bad in [b'', b'ND', b'XX' + bytes(30), encode(samples[3])[:15], encode(samples[1])[:-1] + bytes([0])]:
            lib.host_recv(3, 1, 0x0002, bad, len(bad), -40)
        bad_bo = bytearray(encode(samples[1]))
        bad_bo[13 + NAME_LEN] = 4
        self.assertEqual(lib.host_recv(3, 1, 0x0002, bytes(bad_bo), len(bad_bo), -40), 0)



# ---------- runtime glue and renderer ----------

RSOURCES = [BOARD / f for f in (
    'nabo_duel_model.c', 'nabo_duel.c', 'nabo_table_scene_duel.c', 'nabo_table_model.c', 'nabo_table_bid.c',
    'nabo_table_uc.c', 'nabo_uc_words.c', 'nabo_table_scene_uc.c', 'nabo_table_sprites.c', 'nabo_table_text.c',
    'nabo_table_gfx.c', 'nabo_table_shapes.c', 'nabo_table_scene.c', 'nabo_table_scene_play.c', 'nabo_dice_render.c',
    'nabo_table_font_12.c', 'nabo_menu_font_16.c', 'nabo_ui_font_18.c', 'nabo_table_font_24.c', 'nabo_mono_font_12.c',
    'nabo_poster_font_28.c', 'nabo_duel_font_56.c')] + [HOST / 'duel_render_host.c']
rlibrary = Path(temp.name) / 'duel_render.so'
subprocess.run([CC, '-std=gnu11', '-O2', '-shared', SHARED_EXPORT, '-Wall', '-Wextra', '-Werror',
                '-Wno-missing-field-initializers', '-I', str(ROOT / 'tests/table_host'), '-I', str(BOARD)]
               + [str(s) for s in RSOURCES] + ['-lm', '-o', str(rlibrary)], check=True)
r = ctypes.CDLL(str(rlibrary))
r.rh_init.argtypes = [ctypes.c_char_p]
r.rh_new.argtypes = [I, I, U, I, I, I, U]
r.rh_key.argtypes = [I, I, I, U]
r.rh_rx.argtypes = [I, I, ctypes.c_char_p, I, I, U]
r.rh_advance.argtypes = [I, U]
r.rh_go_pending.argtypes = [I, ctypes.POINTER(U)]
r.rh_flipped.argtypes = [I, U]
r.rh_state.argtypes = [I, ctypes.POINTER(ctypes.c_longlong)]
r.rh_out.argtypes = [I, ctypes.c_char_p]
r.rh_cues.restype = U
r.rh_frame.argtypes = [ctypes.POINTER(ctypes.c_uint16)]
DUEL_PACK = BOARD / 'table_assets/duel_sprites.bin'
DUEL_BUFFER = r.rh_init(str(DUEL_PACK).encode())
SHOTS = Path(os.environ.get('DUEL_SHOTS') or (Path(temp.name) / 'shots'))   # DUEL_SHOTS=dir keeps the screenshots
LAG = 30


def rstate(i):
    v = (ctypes.c_longlong * 16)()
    r.rh_state(i, v)
    keys = 'scene phase me opp round has_cur me_has me_k me_r opp_has opp_k opp_r resolved go_at go_shown ticked'
    return dict(zip(keys.split(), list(v)))


def frame_png(path):
    import zlib, struct
    buf = (ctypes.c_uint16 * (240 * 320))()
    r.rh_frame(buf)
    raw = bytearray()
    for y in range(320):
        raw.append(0)
        for x in range(240):
            v = buf[y * 240 + x]
            raw += bytes(((v >> 11) << 3, ((v >> 5) & 63) << 2, (v & 31) << 3))
    chunk = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 240, 320, 8, 2, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(bytes(raw))) + chunk(b'IEND', b''))


class Badge:
    """One badge running the glue as nabo_table.c does: frames every 33 ms, the draw flipped by
    a timer at go_at (+flip_delay), PING answered and stamped in the radio layer."""

    def __init__(self, i, mac, off=0, bot=None, flip_delay=1, **kw):
        self.i, self.mac, self.off, self.bot, self.flip_delay = i, mac, off, bot, flip_delay
        self.next_frame = 0
        self.flips = []
        self.shots = {}
        r.rh_new(i, mac, kw.get('entropy', mac * 40503 & 0xFFFFFFFF), kw.get('bo', 3), int(kw.get('fake', True)),
                 kw.get('ai', 1), self.off & 0xFFFFFFFF)

    def local(self, g):
        return (g + self.off) & 0xFFFFFFFF


class GlueAir:
    def __init__(self, latency=2, jitter=1, seed=3):
        self.g, self.badges, self.queue, self.n = 0, [], [], 0
        self.rng = random.Random(seed)
        self.latency, self.jitter = latency, jitter
        self.pending_keys = []

    def add(self, b):
        self.badges.append(b)
        return b

    def press(self, b, k, at_g, down=True):
        self.pending_keys.append((at_g, b, k, down))

    def send(self, b, wire, to):
        targets = [x for x in self.badges if x is not b] if to < 0 else [x for x in self.badges if x.mac == to]
        for x in targets:
            self.n += 1
            lat = max(1, round(self.latency + (self.rng.random() * 2 - 1) * self.jitter))
            self.queue.append((self.g + lat, self.n, x, b.mac, wire))

    def step(self, ms, render_every=0, shots=None):
        for _ in range(ms):
            self.g += 1
            for at, b, k, down in [p for p in self.pending_keys if p[0] <= self.g]:
                r.rh_key(b.i, KEYS[k], int(down), b.local(at))
            self.pending_keys = [p for p in self.pending_keys if p[0] > self.g]
            due = sorted((q for q in self.queue if q[0] <= self.g), key=lambda q: (q[0], q[1]))
            self.queue = [q for q in self.queue if q[0] > self.g]
            for _, _, x, frm, wire in due:
                st = rstate(x.i)
                reply = x.ping_reply(wire)   # radio layer: PING answered at once, never reaches the model
                if reply is not None:
                    if reply:
                        self.send(x, reply, frm)
                    continue
                r.rh_rx(x.i, frm, wire, len(wire), -45, x.local(self.g))
            for b in self.badges:
                go = U()
                if r.rh_go_pending(b.i, ctypes.byref(go)) and b.local(self.g) == ((go.value + b.flip_delay) & 0xFFFFFFFF):
                    r.rh_flipped(b.i, b.local(self.g))   # the timer fired: panel inverted now
                    b.flips.append(self.g)
                    if b.bot:
                        b.bot(self, b)
                if self.g >= b.next_frame:
                    b.next_frame = self.g + 33
                    r.rh_advance(b.i, b.local(self.g))
                    for k in range(r.rh_take_out(b.i)):
                        wire = ctypes.create_string_buffer(64)
                        ln = r.rh_out(k, wire)
                        data = bytearray(wire.raw[:ln])
                        if data[3] == TYPE_ID['PING']:
                            data[14:18] = b.local(self.g).to_bytes(4, 'little')   # ndu_stamp_ping at send
                        self.send(b, bytes(data), r.rh_out_to(k))
                    if render_every and (self.g // 33) % render_every == 0:
                        res = r.rh_draw(b.i, 1)
                        assert res >= 0, f'render {"mismatch" if res == -1 else "overflow"} at g={self.g}'
                        st = rstate(b.i)
                        if shots is not None:
                            name = f"{b.i}_{SCENES[st['scene']]}_{PHASES[st['phase']] if st['phase'] >= 0 else ''}"
                            if name not in shots:
                                shots[name] = SHOTS / f'{name}.png'
                                frame_png(shots[name])

    def session_of(self, b):
        return None


def badge_ping_reply(air, b):
    def reply(wire):
        if wire[3] != TYPE_ID['PING']:
            return None
        # The guest answers with its own receive time, in the session it is linked to (any, here).
        c = decode(wire)
        pong = dict(c, type='PONG', seq=0, ack=0, t2=b.local(air.g))
        return encode(pong)
    return reply


def react(ms):
    def act(air, b):
        air.press(b, 'ok', air.g + ms)
        air.press(b, 'ok', air.g + ms + 40, False)
    return act


class Runtime(unittest.TestCase):
    def test_sprite_buffer_budget(self):
        self.assertGreater(DUEL_BUFFER, 0, 'duel sprite pack failed to load')
        self.assertLessEqual(DUEL_BUFFER, 22 * 1024)   # one index buffer for the biggest animation
        self.assertLess(DUEL_PACK.stat().st_size, 500 * 1024)

    def solo(self, reaction=230, flip_delay=1, early=None):
        air = GlueAir()
        b = air.add(Badge(0, 0x3F2A, off=1000, bot=None if early else react(reaction), flip_delay=flip_delay, bo=5, ai=0))
        b.ping_reply = lambda w: None
        air.step(100)
        r.rh_set_sel(0, 1)
        air.press(b, 'ok', air.g + 1)
        air.press(b, 'ok', air.g + 20, False)
        if early is not None:
            # Press between go_at and the flip: never a reaction.
            def watch(air_, b_):
                pass
            b.bot = watch
        return air, b

    def test_solo_reaction_counts_from_the_flip(self):
        air, b = self.solo(reaction=230, flip_delay=7)
        hits = []
        for _ in range(60):
            air.step(500)
            st = rstate(0)
            if st['me_has'] and st['me_k'] == 1 and (not hits or hits[-1][1] != st['go_at']):
                hits.append((st['me_r'], st['go_at'], st['go_shown']))
            if SCENES[st['scene']] == 'end':
                break
        self.assertTrue(hits)
        for ms, go_at, shown in hits:
            self.assertEqual(ms, 230)
            self.assertEqual((shown - go_at) & 0xFFFFFFFF, 7, 'reaction counts from the actual flip')
        self.assertEqual(SCENES[rstate(0)['scene']], 'end')

    def test_press_before_the_flip_is_a_foul(self):
        air = GlueAir()
        b = air.add(Badge(0, 0x3F2A, off=50, flip_delay=40, bo=1, ai=0))
        b.ping_reply = lambda w: None
        air.step(100)
        r.rh_set_sel(0, 1)
        air.press(b, 'ok', air.g + 1)
        air.press(b, 'ok', air.g + 20, False)
        air.step(200)
        go = U()
        for _ in range(200):
            air.step(10)
            if r.rh_go_pending(0, ctypes.byref(go)):
                break
        target_g = ((go.value - b.off) & 0xFFFFFFFF) + 10   # 10 ms after go_at, 30 ms before the flip
        air.press(b, 'ok', target_g)
        air.press(b, 'ok', target_g + 30, False)
        air.step(target_g - air.g + 200)
        st = rstate(0)
        self.assertTrue(st['me_has'])
        self.assertEqual(st['me_k'], 2, 'pressed before the screen flipped: foul')

    def test_online_through_the_glue(self):
        # Badges with different clocks, 33 ms frames, a model 30 ms behind, the draw flipped by a
        # timer: both screens still flip within 2 ms, and every result agrees.
        air = GlueAir(latency=2, jitter=1)
        a = air.add(Badge(0, 0x3F2A, off=7000, bot=react(240), flip_delay=1))
        b = air.add(Badge(1, 0x1B07, off=0x7FFF0000, bot=react(290), flip_delay=1))
        a.ping_reply = badge_ping_reply(air, a)
        b.ping_reply = badge_ping_reply(air, b)
        air.step(100)
        air.press(a, 'ok', air.g + 1); air.press(a, 'ok', air.g + 20, False)    # A: online
        air.press(b, 'ok', air.g + 1); air.press(b, 'ok', air.g + 20, False)    # B: online
        air.step(1500)
        air.press(a, 'ok', air.g + 1); air.press(a, 'ok', air.g + 20, False)    # A invites B (row 0)
        air.step(600)
        self.assertEqual(SCENES[rstate(1)['scene']], 'invited')
        air.press(b, 'ok', air.g + 1); air.press(b, 'ok', air.g + 20, False)    # B accepts
        shots = {}
        air.step(40000, render_every=1, shots=shots)
        sa, sb = rstate(0), rstate(1)
        self.assertEqual(SCENES[sa['scene']], 'end')
        self.assertEqual(SCENES[sb['scene']], 'end')
        self.assertEqual((sa['me'], sa['opp']), (2, 0))
        self.assertEqual((sb['me'], sb['opp']), (0, 2))
        self.assertEqual(len(a.flips), len(b.flips))
        for x, y in zip(a.flips, b.flips):
            self.assertLessEqual(abs(x - y), 2, f'draw flips {x} vs {y}')
        self.assertGreaterEqual(len(shots), 8)
        Runtime.shots = shots

    def test_every_screen_renders(self):
        # Cover, lobby with peers, practice to the end: no overflow, dirty rects == full redraw.
        air = GlueAir()
        b = air.add(Badge(0, 0x3F2A, off=100, bot=react(200), bo=3, ai=2))
        b.ping_reply = lambda w: None
        shots = {}
        air.step(300, render_every=1, shots=shots)
        for sel in range(6):
            r.rh_set_sel(0, sel)
            air.step(66, render_every=1)
        r.rh_set_sel(0, 1)
        air.press(b, 'ok', air.g + 1); air.press(b, 'ok', air.g + 20, False)
        air.step(40000, render_every=1, shots=shots)
        self.assertEqual(SCENES[rstate(0)['scene']], 'end')
        r.rh_set_sel(0, 5)


if __name__ == '__main__':
    unittest.main(verbosity=1)
