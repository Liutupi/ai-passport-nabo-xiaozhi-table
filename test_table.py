"""Host tests for NABO TABLE: the C model must play exactly like the browser prototype.

tests/table_host/golden/*.json were recorded from nabo-table/prototype with
tests/table_host/record_golden.js: a config, a seed, every key press and frame advance,
and the prototype's state after each step. Each test replays one trace on the C model.
"""
import ctypes
import json
import subprocess
import tempfile
import unittest
from pathlib import Path

from host_tools import CC, SHARED_EXPORT

ROOT = Path(__file__).resolve().parent
BOARD = ROOT / 'xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo'
HOST = ROOT / 'tests/table_host'
SOURCES = [BOARD / f for f in ('nabo_table_model.c', 'nabo_table_bid.c', 'nabo_table_uc.c', 'nabo_uc_words.c',
                                 'nabo_table_scene_uc.c', 'nabo_table_sprites.c', 'nabo_table_text.c',
                                 'nabo_table_gfx.c', 'nabo_table_shapes.c', 'nabo_table_scene.c',
                                 'nabo_table_scene_play.c', 'nabo_dice_render.c', 'nabo_table_font_12.c',
                                 'nabo_menu_font_16.c', 'nabo_ui_font_18.c', 'nabo_table_font_24.c',
                                 'nabo_mono_font_12.c', 'nabo_poster_font_28.c')]
SOURCES += [HOST / 'table_host.c', HOST / 'render_host.c']
PACK = BOARD / 'table_assets/table_sprites.bin'
KEYS = {'up': 0, 'down': 1, 'ok': 2}
KEY_OK_LONG = 3
CUT_S, SLAM_S = 17 / 12, 15 / 12   # the prototype's sprite lengths for the cut and the slam
EV_MOVE, EV_OK, EV_SETTINGS = 1, 2, 1 << 11

temp = tempfile.TemporaryDirectory()
library = Path(temp.name) / 'table.so'
subprocess.run([CC, '-std=gnu11', '-O2', '-shared', SHARED_EXPORT, '-Wall', '-Wextra', '-Werror',
                '-Wno-missing-field-initializers', '-I', str(HOST), '-I', str(BOARD)] + [str(s) for s in SOURCES]
               + ['-lm', '-o', str(library)], check=True)
lib = ctypes.CDLL(str(library))
lib.host_init.argtypes = [ctypes.c_uint, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                          ctypes.c_int, ctypes.c_float, ctypes.c_float]
lib.host_key.argtypes = [ctypes.c_int]
lib.host_frames.argtypes = [ctypes.c_int]
lib.host_events.restype = ctypes.c_uint
lib.host_snap.argtypes = [ctypes.c_char_p, ctypes.c_int]
lib.host_render_init.argtypes = [ctypes.c_char_p]
lib.host_draw.argtypes = [ctypes.c_int]
SPRITE_BUFFER = lib.host_render_init(str(PACK).encode())
lib.host_uc_word.restype = ctypes.c_char_p


def snap():
    buf = ctypes.create_string_buffer(8192)
    n = lib.host_snap(buf, len(buf))
    assert n < len(buf), 'snapshot truncated'
    return json.loads(buf.value)


def start(cfg, seed):
    lib.host_init(seed, cfg.get('players', 2), cfg.get('ucPlayers', 5), cfg['target'], cfg['dicePer'], cfg['minOpen'], int(cfg['wild']),
                  CUT_S, SLAM_S)


def step(s):
    if s['a'] == 'k':
        lib.host_key(KEYS[s['k']])
    elif s['a'] == 'f':
        lib.host_frames(s['n'])
    else:
        lib.host_key(KEY_OK_LONG)


def same(want, got):
    """Compare snapshots; die angles are floats, so allow one thousandth of a degree."""
    if 'dc' in want and 'dc' in got:
        wy, gy = want['dc'].pop('y'), got['dc'].pop('y')
        if any(abs(a - b) > 1 for wa, ga in zip(wy, gy) for a, b in zip(wa, ga)):
            return False
    return want == got


class Replay(unittest.TestCase):
    def replay(self, name):
        trace = json.loads((HOST / 'golden' / name).read_text())
        start(trace['cfg'], trace['seed'])
        for i, (s, want) in enumerate(zip(trace['steps'], trace['snaps'])):
            step(s)
            got = snap()
            if not same(dict(want), got):
                self.fail(f'{name}: step {i} {s} differs\n want {want}\n  got {got}')

    def test_traces(self):
        names = sorted(p.name for p in (HOST / 'golden').glob('t*.json'))
        self.assertGreaterEqual(len(names), 13)
        players = {json.loads((HOST / 'golden' / n).read_text())['cfg']['players'] for n in names}
        self.assertEqual(players, {2, 3, 4})
        for name in names:
            with self.subTest(trace=name):
                self.replay(name)


class Render(unittest.TestCase):
    def test_dirty_rectangles_match_full_redraws(self):
        # Replay a whole trace one frame at a time; every incremental frame must equal a full redraw.
        trace = json.loads((HOST / 'golden' / 't1_default.json').read_text())
        start(trace['cfg'], trace['seed'])
        frames = 0
        for s in trace['steps']:
            n = s['n'] if s['a'] == 'f' else 1
            if s['a'] != 'f':
                step(s)
            for _ in range(n):
                lib.host_frames(1) if s['a'] == 'f' else None
                self.assertGreaterEqual(lib.host_draw(1), 0, f'frame {frames}: dirty redraw differs')
                frames += 1
        self.assertGreater(frames, 1000)

    def test_ram_budget(self):
        self.assertGreater(SPRITE_BUFFER, 0, 'sprite pack failed to load')
        self.assertLessEqual(SPRITE_BUFFER, 6 * 1024)   # one small-NABO index buffer


class Rules(unittest.TestCase):
    def setUp(self):
        start({'target': 3, 'dicePer': 5, 'minOpen': 2, 'wild': True}, 42)

    def test_target_cycles_and_asks_to_persist(self):
        lib.host_key(KEYS['up'])            # menu wraps to the target item
        lib.host_events()
        lib.host_key(KEYS['ok'])
        self.assertEqual(snap()['target'], 5)
        self.assertTrue(lib.host_events() & EV_SETTINGS)

    def test_pause_only_in_play(self):
        lib.host_key(KEY_OK_LONG)
        self.assertEqual(snap()['pause'], -1)      # not on the cover
        lib.host_key(KEYS['ok'])                   # start 21 points
        lib.host_key(KEY_OK_LONG)
        self.assertEqual(snap()['pause'], 0)
        lib.host_frames(600)                       # paused: the deal does not advance
        self.assertEqual(snap()['bj']['d'], 0)

    def test_first_bid_never_offers_open(self):
        lib.host_key(KEYS['down'])
        lib.host_key(KEYS['ok'])                   # liar's dice
        lib.host_frames(200)
        lib.host_key(KEYS['ok'])                   # take the hand-off
        s = snap()['dc']
        self.assertNotIn(0, s['q'])
        self.assertEqual(s['q'][0], 2)             # smallest bid uses min_open

    def test_ones_are_wild_until_someone_bids_ones(self):
        lib.host_key(KEYS['down'])
        lib.host_key(KEYS['ok'])                   # liar's dice, 5 dice each
        for p, row in enumerate([[1, 1, 3, 4, 5], [1, 2, 2, 6, 6]]):
            for i, v in enumerate(row):
                lib.host_set_dice(p, i, v)
        self.assertEqual(lib.host_count(2), 5)     # two 2s plus three wild 1s
        self.assertEqual(lib.host_count(1), 3)     # ones only count themselves
        lib.host_add_bid(0, 3, 1)                  # a bid on ones: zhai, ones stop being wild
        self.assertEqual(lib.host_count(2), 2)
        self.assertEqual(lib.host_count(1), 3)



def start_game(players, game, **cfg):
    """Cover -> pick the player count -> start 21 points (game 0) or liar's dice (game 1)."""
    c = {'target': 3, 'dicePer': 5, 'minOpen': 2, 'wild': True, 'players': players}
    c.update(cfg)
    start(c, 42)
    for _ in range(game):
        lib.host_key(KEYS['down'])
    lib.host_key(KEYS['ok'])


def bj_round(hands):
    ints = ctypes.c_int * 12
    for p, ranks in enumerate(hands):
        lib.host_set_hand(p, ints(*ranks), len(ranks))
    lib.host_bj_reveal()
    return snap()


class MultiPlayer(unittest.TestCase):
    def test_player_count_cycles_and_asks_to_persist(self):
        start({'target': 3, 'dicePer': 5, 'minOpen': 2, 'wild': True}, 1)
        for _ in range(4):
            lib.host_key(KEYS['down'])             # the player-count item
        lib.host_events()
        seen = []
        for _ in range(3):
            lib.host_key(KEYS['ok'])
            seen.append(snap()['players'])
            self.assertTrue(lib.host_events() & EV_SETTINGS)
        self.assertEqual(seen, [3, 4, 2])

    def test_deal_goes_round_from_the_starter(self):
        start_game(4, 0)
        lib.host_frames(600)
        s = snap()
        self.assertEqual(s['np'], 4)
        self.assertEqual([len(h) for h in s['bj']['h']], [2, 2, 2, 2])
        self.assertEqual(s['ho'], 0)                    # round 1: player 1 starts

    def test_turns_pass_to_the_next_player_still_drawing(self):
        start_game(3, 0)
        lib.host_frames(600)
        lib.host_key(KEYS['ok'])                        # player 1 takes the badge
        lib.host_key(KEYS['down'])
        lib.host_key(KEYS['ok'])                        # stands
        self.assertEqual(snap()['ho'], 1)
        lib.host_key(KEYS['ok'])
        lib.host_key(KEYS['down'])
        lib.host_key(KEYS['ok'])                        # player 2 stands
        self.assertEqual(snap()['ho'], 2)
        lib.host_key(KEYS['ok'])
        lib.host_key(KEYS['ok'])                        # player 3 hits ...
        lib.host_key(KEYS['ok'])                        # ... and, alone now, keeps the badge
        s = snap()
        self.assertEqual((s['sc'], s['turn']), (3, 2))

    def test_tied_highest_totals_all_score(self):
        start_game(3, 0)
        s = bj_round([[10, 10], [7, 13, 3], [10, 8]])   # 20, 20, 18
        self.assertEqual(s['w'], 0b011)
        self.assertEqual(s['score'], [1, 1, 0])

    def test_everyone_on_the_same_total_or_bust_is_a_tie(self):
        start_game(2, 0)
        s = bj_round([[10, 9], [9, 10]])                # two players, 19 each: no score, as before
        self.assertEqual((s['w'], s['score']), (0, [0, 0]))
        start_game(4, 0)
        s = bj_round([[10, 10, 5]] * 4)                 # all bust
        self.assertEqual((s['w'], s['score']), (0, [0, 0, 0, 0]))

    def test_a_shared_lead_at_the_target_plays_on(self):
        start_game(3, 0)
        lib.host_set_score(0, 2)
        lib.host_set_score(1, 2)
        s = bj_round([[10, 10], [10, 10], [10, 5]])
        self.assertEqual(s['score'], [3, 3, 0])
        self.assertFalse(lib.host_match_over())         # 3 : 3 is not decided yet
        s = bj_round([[10, 10], [10, 8], [10, 5]])
        self.assertTrue(lib.host_match_over())

    def test_liars_dice_penalises_whoever_was_wrong(self):
        start_game(3, 1)
        lib.host_frames(200)
        lib.host_key(KEYS['ok'])                        # player 1 takes the badge
        s = snap()['dc']
        self.assertEqual(s['q'][0], 3)                  # three players: the opening bid is at least 3
        self.assertEqual(s['q'][-1], 15)                # up to every die on the table
        lib.host_key(KEYS['ok'])
        lib.host_key(KEYS['ok'])                        # bid 3 of the lowest face
        self.assertEqual(snap()['ho'], 1)
        lib.host_key(KEYS['ok'])                        # player 2 ...
        lib.host_key(KEYS['down'])
        lib.host_key(KEYS['ok'])                        # ... opens
        lib.host_frames(120)
        s = snap()
        q, f = s['dc']['b'][-1][1:]
        held = lib.host_count(f) >= q
        loser = 1 if held else 0
        self.assertEqual(s['lo'], loser)
        self.assertEqual(s['score'], [int(loser == 0), int(loser == 1), 0])
        lib.host_key(KEYS['ok'])                        # next round: the loser calls first
        lib.host_frames(200)
        self.assertEqual(snap()['ho'], loser)

    def test_liars_dice_ends_at_the_penalty_limit(self):
        start_game(4, 1)
        lib.host_set_score(0, 2)
        lib.host_set_score(1, 2)                        # whoever loses this open reaches 3
        lib.host_frames(200)
        lib.host_key(KEYS['ok'])
        lib.host_key(KEYS['ok'])
        lib.host_key(KEYS['ok'])                        # player 1 bids
        lib.host_key(KEYS['ok'])
        lib.host_key(KEYS['down'])
        lib.host_key(KEYS['ok'])                        # player 2 opens
        lib.host_frames(120)
        s = snap()
        self.assertEqual(max(s['score']), 3)
        self.assertTrue(lib.host_match_over())
        lib.host_key(KEYS['ok'])
        self.assertEqual(snap()['sc'], 8)               # the end screen


UC_MENU = 2


def uc_start(n, seed=5):
    """Cover -> who is the undercover -> n players -> deal; every player looks at their word."""
    start({'target': 3, 'dicePer': 5, 'minOpen': 2, 'wild': True, 'players': 2, 'ucPlayers': 5}, seed)
    for _ in range(UC_MENU):
        lib.host_key(KEYS['down'])
    lib.host_key(KEYS['ok'])
    while snap()['uc']['cu'] < n:
        lib.host_key(KEYS['up'])
    while snap()['uc']['cu'] > n:
        lib.host_key(KEYS['down'])
    lib.host_events()
    lib.host_key(KEYS['ok'])
    for _ in range(n):
        lib.host_key(KEYS['ok'])        # take the badge
        lib.host_key(KEYS['ok'])        # cover the word
    return snap()


def uc_vote(player):
    """From the describing screen, vote the given player out."""
    s = snap()
    assert s['sc'] == 11, s
    lib.host_key(KEYS['ok'])            # "start voting"
    alive = [p for p, a in enumerate(snap()['uc']['al']) if a]
    for _ in range(alive.index(player)):
        lib.host_key(KEYS['down'])
    lib.host_key(KEYS['ok'])            # pick
    lib.host_key(KEYS['ok'])            # confirm
    return snap()


class Undercover(unittest.TestCase):
    def test_undercover_count_and_words(self):
        for n in range(3, 9):
            s = uc_start(n, seed=n)
            u = s['uc']
            self.assertEqual(sum(u['uc']), 2 if n >= 7 else 1, n)
            self.assertEqual(s['sc'], 11)                        # describing
            self.assertTrue(u['al'][u['sp']])                    # someone still in starts
            words = {lib.host_uc_word(p) for p in range(n)}
            self.assertEqual(len(words), 2)                      # two words, split by role
            uc_words = {lib.host_uc_word(p) for p in range(n) if u['uc'][p]}
            self.assertEqual(len(uc_words), 1)

    def test_setup_asks_to_persist_the_player_count(self):
        start({'target': 3, 'dicePer': 5, 'minOpen': 2, 'wild': True, 'ucPlayers': 5}, 1)
        for _ in range(UC_MENU):
            lib.host_key(KEYS['down'])
        lib.host_key(KEYS['ok'])
        lib.host_key(KEYS['up'])
        lib.host_events()
        lib.host_key(KEYS['ok'])
        self.assertTrue(lib.host_events() & EV_SETTINGS)
        self.assertEqual(snap()['ucp'], 6)

    def test_civilians_win_when_the_undercover_is_out(self):
        s = uc_start(5)
        spy = s['uc']['uc'].index(1)
        s = uc_vote(spy)
        self.assertEqual((s['sc'], s['uc']['over'], s['uc']['won']), (13, 1, 0))
        lib.host_key(KEYS['ok'])
        self.assertEqual(snap()['sc'], 14)

    def test_undercover_wins_when_as_many_as_civilians(self):
        s = uc_start(3)
        civ = s['uc']['uc'].index(0)
        s = uc_vote(civ)                                         # 1 undercover vs 1 civilian
        self.assertEqual((s['uc']['over'], s['uc']['won']), (1, 1))
        s = uc_start(7)                                          # 2 undercovers
        civs = [p for p, x in enumerate(s['uc']['uc']) if not x]
        for i, p in enumerate(civs[:3]):
            s = uc_vote(p)
            if i < 2:
                self.assertEqual(s['uc']['over'], 0)
                lib.host_key(KEYS['ok'])                         # next round
        self.assertEqual((s['uc']['over'], s['uc']['won']), (1, 1))   # 2 vs 2

    def test_tie_and_forgot_change_nothing(self):
        s = uc_start(6)
        before = s['uc']['al']
        lib.host_key(KEYS['ok'])                                 # vote screen
        for _ in range(6):
            lib.host_key(KEYS['down'])                           # to "tie, describe again"
        lib.host_key(KEYS['ok'])
        s = snap()
        self.assertEqual((s['sc'], s['uc']['rd'], s['uc']['al']), (11, 2, before))
        lib.host_key(KEYS['down'])                               # "someone forgot"
        lib.host_key(KEYS['ok'])
        lib.host_key(KEYS['down'])
        lib.host_key(KEYS['ok'])                                 # player 2
        self.assertEqual((snap()['sc'], snap()['ho']), (1, 1))
        lib.host_key(KEYS['ok'])
        self.assertEqual((snap()['sc'], snap()['turn']), (10, 1))
        lib.host_key(KEYS['ok'])
        self.assertEqual((snap()['sc'], snap()['uc']['al']), (11, before))

    def test_pairs_do_not_repeat_soon(self):
        uc_start(4, seed=3)
        seen = []
        for _ in range(30):
            seen.append(snap()['uc']['pair'])
            while snap()['sc'] != 14:                            # vote out the undercover
                if snap()['sc'] == 13:
                    lib.host_key(KEYS['ok'])
                    continue
                uc_vote(snap()['uc']['uc'].index(1))
            lib.host_key(KEYS['ok'])                             # play again
            for _ in range(4):
                lib.host_key(KEYS['ok'])
                lib.host_key(KEYS['ok'])
        for i in range(len(seen)):
            self.assertNotIn(seen[i], seen[max(0, i - 23):i])

    def test_every_word_has_glyphs(self):
        import re
        font = (BOARD / 'nabo_table_font_24.c').read_text('utf-8')
        have = {chr(int(h, 16)) for h in re.findall(r'/\* U\+([0-9A-F]+) ', font)}
        sys_path = ROOT / 'tools'
        import sys
        sys.path.insert(0, str(sys_path))
        from generate_uc_words import load_pairs
        missing = {c for a, b in load_pairs() for c in a + b if c not in have}
        self.assertEqual(missing, set(), 'regenerate fonts: python tools/generate_fonts.py ...')

    def test_eight_players_fit_the_renderer(self):
        lib.host_overflow.restype = ctypes.c_int
        for name in ('t12_uc_eight.json', 't6_long.json'):
            trace = json.loads((HOST / 'golden' / name).read_text())
            start(trace['cfg'], trace['seed'])
            for s in trace['steps']:
                n = s['n'] if s['a'] == 'f' else 1
                if s['a'] != 'f':
                    step(s)
                for _ in range(n):
                    if s['a'] == 'f':
                        lib.host_frames(1)
                    lib.host_draw(0)
                    self.assertFalse(lib.host_overflow(), f'{name}: display list overflow')



class UndercoverEntry(unittest.TestCase):
    def test_voice_start_returns_to_the_undercover_item(self):
        start({'target': 3, 'dicePer': 5, 'minOpen': 2, 'wild': True}, 5)
        self.assertEqual(snap()['menu'], 0)
        lib.host_uc_direct()                       # "打开谁是卧底": straight to its setup screen
        lib.host_key(KEY_OK_LONG)
        lib.host_key(KEYS['down'])
        lib.host_key(KEYS['ok'])                   # pause -> back to the cover
        s = snap()
        self.assertEqual((s['sc'], s['menu']), (0, 2))
        lib.host_key(KEYS['ok'])                   # OK on the cover opens the undercover setup again
        self.assertEqual(snap()['sc'], 9)

if __name__ == '__main__':
    unittest.main()
