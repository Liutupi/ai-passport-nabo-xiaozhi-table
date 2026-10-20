"""Exercise the production PCM mixer: quiet waiting and audible, hiss-free duel cues."""
import ctypes
import math
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

from host_tools import CC, SHARED_EXPORT, unload_library

ROOT = Path(__file__).resolve().parent
BOARD = ROOT / 'xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo'
SR = 16000
WIND, WAIT, FAKE, GO, SHOT, FOUL, WIN, LOSE = [1 << i for i in range(3, 11)]


class DuelSoundTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        folder = Path(cls.temp.name)
        wrapper = folder / 'audio_host.c'
        wrapper.write_text('''#include "nabo_table_sound.h"
#include "nabo_table_model.h"
static nt_sound_t sound;
void reset(unsigned seed) { nt_sound_reset(&sound); sound.seed = seed; }
void cue(unsigned bits) { nt_sound_duel(&sound, bits, 6000); }
void fill(short *out, unsigned n) { nt_sound_fill(&sound, out, n); }
void card(void) { nt_sound_events(&sound, NT_EV_CARD); }
''')
        library = folder / 'audio.dll'
        subprocess.run([CC, '-std=gnu11', '-O2', '-shared', SHARED_EXPORT,
                        '-Wall', '-Wextra', '-Werror', '-I', str(BOARD),
                        str(BOARD / 'nabo_table_sound.c'),
                        str(BOARD / 'nabo_dice_render.c'), str(wrapper),
                        '-o', str(library)], check=True)
        cls.lib = ctypes.CDLL(str(library))
        cls.lib.reset.argtypes = [ctypes.c_uint]
        cls.lib.cue.argtypes = [ctypes.c_uint]
        cls.lib.fill.argtypes = [ctypes.POINTER(ctypes.c_int16), ctypes.c_uint]

    @classmethod
    def tearDownClass(cls):
        unload_library(cls.lib._handle)
        cls.temp.cleanup()

    def render(self, cue, seed=1, block=320, frames=SR):
        self.lib.reset(seed)
        self.lib.cue(cue)
        samples = []
        while len(samples) < frames:
            count = min(block, frames - len(samples))
            out = (ctypes.c_int16 * count)()
            self.lib.fill(out, count)
            samples.extend(out)
        return samples

    def test_waiting_is_silent_for_full_standoff(self):
        self.assertFalse(any(self.render(WIND, frames=SR * 7)))

    def test_wind_does_not_mask_draw_bell(self):
        self.assertEqual(self.render(GO), self.render(WIND | GO))

    def test_tonal_cues_are_audible_without_random_hiss_or_clipping(self):
        for cue in (WAIT, FAKE, GO, SHOT, FOUL, WIN, LOSE):
            with self.subTest(cue=cue):
                pcm = self.render(cue)
                self.assertGreater(max(map(abs, pcm)), 500)
                self.assertLess(max(map(abs, pcm)), 32767)
                self.assertEqual(pcm, self.render(cue, seed=0xABCDEF))
                self.assertFalse(any(pcm[-1600:]))

    def test_mixer_is_independent_of_audio_chunk_size(self):
        for cue in (GO | SHOT, WAIT, WIN, LOSE):
            with self.subTest(cue=cue):
                self.assertEqual(self.render(cue), self.render(cue, block=173))

    def test_wait_knock_and_draw_bell_have_mixing_headroom(self):
        # A sound already queued must still yield cleanly when GO is mixed.
        for elapsed_ms in range(1, 221):
            with self.subTest(elapsed_ms=elapsed_ms):
                self.lib.reset(1)
                self.lib.cue(WAIT)
                before = (ctypes.c_int16 * (elapsed_ms * 16))()
                self.lib.fill(before, len(before))
                self.lib.cue(GO)
                out = (ctypes.c_int16 * 3200)()
                self.lib.fill(out, len(out))
                self.assertLess(max(map(abs, out)), 32767)
                bell = self.render(GO)
                self.assertEqual(list(out)[80:], bell[80:3200], 'wait knock must yield to the draw bell')

    def test_wait_uses_the_original_short_wooden_knock(self):
        pcm = self.render(WAIT)
        body = (BOARD / 'nabo_muyu_sound.h').read_text('utf-8').split('= {', 1)[1].split('}', 1)[0]
        original = [int(value) for value in re.findall(r'-?\d+', body)]
        self.assertEqual(len(original), 1440)
        self.assertEqual(pcm[:1400], [value * 320 >> 8 for value in original[:1400]])
        self.assertFalse(any(pcm[1440:]), 'one cue is one 90 ms knock, with no delayed heartbeat')
        self.assertGreater(math.sqrt(sum(x*x for x in pcm[:640]) / 640), 3000)

    def test_table_card_sound_remains_audible(self):
        self.lib.reset(1)
        self.lib.card()
        out = (ctypes.c_int16 * 3200)()
        self.lib.fill(out, len(out))
        self.assertGreater(max(map(abs, out)), 500)


if __name__ == '__main__':
    unittest.main()
