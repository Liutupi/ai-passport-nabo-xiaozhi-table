// Records golden traces from the NABO DUEL prototype (game-art/duel/prototype/duel_model.js + sim.js).
// Run with node; test_duel.py replays each badge's inputs on the C model and compares every packet,
// cue and state snapshot.
//   node tests/duel_host/record_duel_golden.js
const path = require('path');
const fs = require('fs');
const D = require(path.join(__dirname, '../../game-art/duel/prototype/duel_model.js'));
const { Air, makeBot } = require(path.join(__dirname, '../../game-art/duel/prototype/sim.js'));

const PH = ['wait', 'ready', 'standoff', 'go', 'reveal'];
const RES = r => (r ? [r.k === 'hit' ? 1 : r.k === 'foul' ? 2 : 0, r.k === 'hit' ? r.r : 0] : null);

function snap(b, t) {
  const m = b.match, c = b.cur;
  return {
    t, sc: b.scene, sel: b.sel, ph: b.scene === 'duel' ? b.phase(t) : '',
    m: m ? [m.me, m.opp, m.round, m.attempt, Number(m.done), Number(m.host)] : null,
    c: c ? [RES(c.me), RES(c.opp), Number(!!c.resolved), c.goAt, c.outcome || ''] : null,
    toast: b.toast ? b.toast.text : '', best: b.best,
  };
}

function record(name, o) {
  const air = new Air({ seed: o.seed || 1, net: o.net });
  const badges = o.badges.map(x => new D.DuelBadge({ mac: x.mac, entropy: x.entropy, settings: x.settings, suppress: x.suppress !== false, best: x.best || 0 }));
  const traces = badges.map((b, i) => ({ mac: o.badges[i].mac, entropy: o.badges[i].entropy, settings: b.set, best: b.best, suppress: b.suppress, steps: [] }));
  const nodes = badges.map((b, i) => air.add(b, { off: o.badges[i].off || 0, ppm: o.badges[i].ppm || 0, rssi: o.badges[i].rssi || -45, bot: o.badges[i].bot ? makeBot(o.badges[i].bot) : null }));
  badges.forEach((b, i) => {
    const tr = traces[i].steps;
    const k0 = b.key.bind(b), r0 = b.recv.bind(b), t0 = b.tick.bind(b);
    const drain = () => {
      const out = b.out.map(x => ({ to: x.to, p: x.pkt }));
      const fx = b.fx.splice(0).map(f => [f.t, f.k, f.kind || '']);
      return { out, fx };
    };
    // out is drained by Air._flush right after each call, so read it before that happens.
    // sel: the row before the key (bots and scripts pick lobby rows by setting it directly).
    b.key = (t, k, down) => { const sel = b.sel; k0(t, k, down); const d = drain(); tr.push({ a: 'k', t, k, down: Number(down), sel, ...d }); };
    b.recv = (t, from, pkt, rssi) => { r0(t, from, pkt, rssi); const d = drain(); tr.push({ a: 'r', t, from, pkt, rssi, ...d }); };
    // Every tick is recorded; runs of ticks with nothing to report collapse into {a: 'T', t, n}.
    b.tick = t => {
      t0(t);
      const d = drain();
      const s = t % 50 === 0 ? snap(b, t) : null;
      const last = tr[tr.length - 1];
      if (d.out.length || d.fx.length || s) tr.push({ a: 't', t, ...d, ...(s ? { s } : {}) });
      else if (last && last.a === 'T' && last.t + last.n === t) last.n++;
      else tr.push({ a: 'T', t, n: 1 });
    };
  });
  for (const ev of (o.script || []).sort((a, b) => a.at - b.at)) {
    air.step(ev.at - air.g);
    const n = nodes[ev.badge];
    if (ev.key) air.key(n, ev.key, ev.down);
    if (ev.antenna !== undefined) n.antenna = ev.antenna;
    if (ev.sel !== undefined) n.badge.sel = ev.sel;   // the scripted player picks a row directly
  }
  air.step((o.ms || 40000) - air.g);
  for (const tr of traces) tr.endT = undefined;
  const file = path.join(__dirname, 'golden', name + '.json');
  fs.writeFileSync(file, JSON.stringify({ name, badges: traces.map((t, i) => ({ ...t, off: o.badges[i].off || 0 })) }));
  const sum = badges.map(b => `${b.name} ${b.scene} ${b.match ? b.match.me + ':' + b.match.opp : '-'}`).join(' | ');
  console.log(`${name}: ${sum} | steps ${traces.map(t => t.steps.length).join('/')}`);
}

const A = (bot, extra) => Object.assign({ mac: 0x3F2A, entropy: 11, off: 5000, rssi: -42, settings: { bo: 3, fake: true, ai: 1 }, bot }, extra);
const B = (bot, extra) => Object.assign({ mac: 0x1B07, entropy: 22, off: 987654, ppm: 40, rssi: -46, bot }, extra);

record('d1_normal', { badges: [A({ invite: 0x1B07, mean: 250, sd: 30, seed: 3, rematch: false }), B({ mean: 280, sd: 30, seed: 5, rematch: false })], ms: 40000 });
record('d2_lossy', { seed: 9, net: { latency: 40, jitter: 30, loss: 0.2 }, badges: [A({ invite: 0x1B07, mean: 250, sd: 30, seed: 3, rematch: false }), B({ mean: 280, sd: 30, seed: 5, rematch: false })], ms: 50000 });
record('d3_fouls', { badges: [A({ invite: 0x1B07, mean: 250, sd: 40, seed: 7, foul: 0.3, rematch: true }, { settings: { bo: 5, fake: true, ai: 1 } }), B({ mean: 260, sd: 40, seed: 8, foul: 0.4, rematch: true })], ms: 90000 });
record('d4_both_foul', { badges: [A({ invite: 0x1B07, foul: 1, seed: 1, rematch: false }, { settings: { bo: 1, fake: true, ai: 1 } }), B({ foul: 1, seed: 2, rematch: false })], ms: 40000 });
record('d5_solo', { badges: [A({ invite: false, mean: 240, sd: 40, foul: 0.1, seed: 4, rematch: true }, { settings: { bo: 5, fake: true, ai: 2 } })],
  script: [{ at: 5, badge: 0, sel: 1 }, { at: 6, badge: 0, key: 'ok', down: true }, { at: 7, badge: 0, key: 'ok', down: false }], ms: 60000 });
record('d6_cut', { badges: [A({ invite: 0x1B07, mean: 250, sd: 30, seed: 3 }), B({ mean: 280, sd: 30, seed: 5 })],
  script: [{ at: 14000, badge: 1, antenna: false }, { at: 22000, badge: 1, antenna: true }], ms: 32000 });
record('d7_pause', { badges: [A({ invite: 0x1B07, mean: 250, sd: 30, seed: 3, rematch: false }), B({ mean: 280, sd: 30, seed: 5, rematch: false })],
  script: [{ at: 9000, badge: 1, key: 'down', down: true }, { at: 10300, badge: 1, key: 'down', down: false }, { at: 12000, badge: 1, key: 'ok', down: true }, { at: 12020, badge: 1, key: 'ok', down: false }], ms: 40000 });
record('d8_three', { badges: [A({ invite: 0x08C5, mean: 250, sd: 30, seed: 3, rematch: false }), B({ invite: 0x08C5, mean: 280, sd: 30, seed: 5, rematch: false }),
  { mac: 0x08C5, entropy: 33, off: 77777, rssi: -60, bot: { mean: 300, sd: 30, seed: 6, rematch: false } }], ms: 40000 });
record('d9_mutual', { badges: [A({ invite: 0x1B07, mean: 250, sd: 30, seed: 3, rematch: true }), B({ invite: 0x3F2A, mean: 280, sd: 30, seed: 5, rematch: true })], ms: 60000 });
record('d10_quit', { badges: [A({ invite: 0x1B07, mean: 250, sd: 30, seed: 3 }), B(null)],
  script: [{ at: 600, badge: 1, key: 'ok', down: true }, { at: 610, badge: 1, key: 'ok', down: false },
    { at: 2500, badge: 1, key: 'ok', down: true }, { at: 2510, badge: 1, key: 'ok', down: false },
    { at: 9000, badge: 1, key: 'up', down: true }, { at: 10200, badge: 1, key: 'up', down: false },
    { at: 10500, badge: 1, key: 'down', down: true }, { at: 10510, badge: 1, key: 'down', down: false },
    { at: 10800, badge: 1, key: 'ok', down: true }, { at: 10810, badge: 1, key: 'ok', down: false }], ms: 20000 });
