// 模拟空中环境：几台工牌、各自的本机时钟（偏差 + 漂移）、ESP-NOW 链路（延迟、抖动、丢包、断开）。
// 网页原型和宿主测试共用。全局时间按 1 ms 步进。
(function (root) {
'use strict';
const D = (typeof module !== 'undefined' && module.exports) ? require('./duel_model.js') : root.NaboDuel;

class Air {
  constructor(o) {
    o = o || {};
    this.rng = D.mulberry32(o.seed || 1);
    this.g = 0;                                   // 全局时间 ms
    this.nodes = [];                              // {badge, off, ppm, antenna, rssiTo}
    this.queue = [];                              // {at, to, from, pkt}
    this.net = Object.assign({ latency: 3, jitter: 2, loss: 0 }, o.net || {});
    this.stats = { sent: 0, lost: 0, delivered: 0 };
    this.trace = [];
    this.onPacket = null;
  }
  add(badge, o) {
    o = o || {};
    const n = { badge, off: o.off || 0, ppm: o.ppm || 0, antenna: true, rssi: o.rssi || -45, bot: o.bot || null };
    this.nodes.push(n);
    return n;
  }
  local(n) { return Math.floor(this.g + n.off + this.g * n.ppm / 1e6); }
  node(mac) { return this.nodes.find(n => n.badge.mac === mac); }
  rand() { return this.rng() / 4294967296; }

  key(n, k, down) { n.badge.key(this.local(n), k, down); this._flush(n); }

  step(ms) {
    for (let i = 0; i < ms; i++) {
      this.g++;
      // 先投递这一毫秒到达的包，再走节拍
      const due = this.queue.filter(q => q.at <= this.g);
      if (due.length) {
        this.queue = this.queue.filter(q => q.at > this.g);
        due.sort((a, b) => a.at - b.at || a.n - b.n);
        for (const q of due) {
          const n = this.node(q.to);
          if (!n || !n.antenna) { this.stats.lost++; continue; }
          this.stats.delivered++;
          n.badge.recv(this.local(n), q.from, q.pkt, Math.min(n.rssi, this.node(q.from) ? this.node(q.from).rssi : -90) + Math.round(this.rand() * 4 - 2));
          this._flush(n);
        }
      }
      for (const n of this.nodes) {
        if (n.bot) n.bot(this, n);
        n.badge.tick(this.local(n));
        this._flush(n);
      }
    }
  }
  _flush(n) {
    const b = n.badge;
    while (b.out.length) {
      const { to, pkt } = b.out.shift();
      const targets = to === '*' ? this.nodes.filter(m => m !== n) : [this.node(to)].filter(Boolean);
      for (const m of targets) {
        this.stats.sent++;
        const wire = JSON.parse(JSON.stringify(pkt));
        if (this.onPacket) this.onPacket(this.g, b.mac, m.badge.mac, wire);
        if (!n.antenna || !m.antenna || this.rand() < this.net.loss) { this.stats.lost++; continue; }
        const lat = Math.max(1, Math.round(this.net.latency + (this.rand() * 2 - 1) * this.net.jitter));
        this.queue.push({ at: this.g + lat, to: m.badge.mac, from: b.mac, pkt: wire, n: this._n = (this._n || 0) + 1 });
      }
    }
  }
}

// 机器人玩家：自动接受邀请、拔枪（反应时间可设）、整场结束后再来一场
function makeBot(o) {
  o = Object.assign({ mean: 280, sd: 40, foul: 0, rematch: true, invite: null, seed: 7 }, o || {});
  const rng = D.mulberry32(o.seed);
  const st = { plan: null, pressAt: null, waitUntil: -Infinity, attemptKey: null };
  return function (air, n) {
    const b = n.badge, t = air.local(n);
    if (t < st.waitUntil) return;
    const press = k => { air.key(n, k, true); air.key(n, k, false); st.waitUntil = t + 200; };
    if (b.scene === 'cover' && o.invite !== false) { b.sel = 0; press('ok'); return; }
    if (b.scene === 'lobby' && o.invite) {
      const list = b.lobbyList(), i = list.findIndex(p => p.mac === o.invite);
      if (i >= 0) { b.sel = i; press('ok'); }
      return;
    }
    if (b.scene === 'invited') { st.waitUntil = t + 600; if (st.seenInvite) press('ok'); st.seenInvite = true; return; }
    st.seenInvite = false;
    if (b.scene === 'end' && o.rematch && !b.match.wantNext) { if (t - b.sceneAt > 1500) { b.sel = 0; press('ok'); } return; }
    if (b.scene !== 'duel' || !b.cur) return;
    const c = b.cur, key = c.seed + ':' + c.goAt;
    if (st.attemptKey !== key) {
      st.attemptKey = key;
      if (rng() % 1000 < o.foul * 1000) st.pressAt = c.standoff + 100 + rng() % Math.max(1, c.delay - 200);
      else st.pressAt = c.goAt + Math.max(120, D.normalInt(rng, o.mean, o.sd));
    }
    if (!c.me && !c.resolved && t >= st.pressAt) { air.key(n, 'ok', true); air.key(n, 'ok', false); }
  };
}

const api = { Air, makeBot };
if (typeof module !== 'undefined' && module.exports) module.exports = api;
else Object.assign(root.NaboDuel, api);
})(typeof window !== 'undefined' ? window : globalThis);
