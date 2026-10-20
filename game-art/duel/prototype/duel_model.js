// NABO 对决 · 纯逻辑模型（一台工牌一份）。
// 这是规则和联机协议的标准答案：固件里的 nabo_duel_model.c 要和它逐步一致。
// 不碰屏幕、声音、网络：时间（本机毫秒，整数）作为参数传进来，要发的包放进 out，音效放进 fx。
//   key(t, k, down)        k = 'up' | 'down' | 'ok'，按下沿 / 松开沿
//   recv(t, fromMac, pkt, rssi)
//   tick(t)                宿主每 1 ms 调一次（固件里由 esp_timer 在下一个截止时刻调）
// 所有随机数来自 mulberry32，种子相同结果相同。
(function (root) {
'use strict';

const VER = 1;
const T = {
  READY: 1500,            // 准备阶段
  MIN_DELAY: 1000,        // 对峙时长下限
  MAX_DELAY: 4000,        // 对峙时长上限
  LEAD: 400,              // 房主发 ROUND 到准备开始之间留的余量
  LATE_MIN: 200,          // 客人收到 ROUND 时离拔枪不到这么久 → LATE
  TIMEOUT: 2000,          // 拔枪后多久没按算“没按”
  REVEAL: 3200,           // 每局结果展示（输了：对面拔枪 1.5 s + 得意 1.5 s）
  HELLO: 300, PEER_EXPIRE: 1500,
  BEAT: 250, DEAD: 3000,
  RETX: 60, GIVEUP: 2000,
  PING_N: 8, PING_GAP: 30, PING_WAIT: 120,
  LONG: 1000,             // UP/DOWN 长按暂停
  INVITE_WAIT: 10000,
  LEFT_SHOW: 2200, TOAST: 1800,
  MAX_ATTEMPT: 3,
  FAKE_SHOW: 450,
};
const AI = [
  { name: '新手', mean: 330, sd: 40, foul: 0.05 },
  { name: '老手', mean: 260, sd: 30, foul: 0.03 },
  { name: '神枪', mean: 205, sd: 20, foul: 0.01 },
];
const RELIABLE = new Set(['INVITE', 'ACCEPT', 'DECLINE', 'ROUND', 'LATE', 'RESULT', 'NEXT', 'PAUSE', 'RESUME']);

function mulberry32(a) {
  a >>>= 0;
  return function () {
    a = (a + 0x6D2B79F5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0);
  };
}
// 整数近似正态：12 个均匀数之和（固件里同样做法，不用浮点 log/sqrt）
function normalInt(rng, mean, sd) {
  let s = 0;
  for (let i = 0; i < 12; i++) s += rng() % 1000;
  return mean + Math.floor(((s - 6000 + 6) * sd + 500) / 1000);   // 四舍五入（.5 向上），固件用同样的整数除法
}

// 一局（一次尝试）的时间表，完全由种子和 goAt 决定，两台各算一遍结果相同
function attemptPlan(seed, goAt, round, fakeOn) {
  const rng = mulberry32(seed);
  const delay = T.MIN_DELAY + rng() % (T.MAX_DELAY - T.MIN_DELAY + 1);
  const standoff = goAt - delay, ready = standoff - T.READY;
  const fakes = [];
  if (fakeOn && round >= 2) {
    const n = rng() % 3;
    const lo = standoff + 400, hi = goAt - 350;
    for (let i = 0; i < n && hi > lo; i++)
      fakes.push({ at: lo + rng() % (hi - lo), kind: ['text', 'twitch', 'tumble'][rng() % 3] });
    fakes.sort((a, b) => a.at - b.at);
  }
  return { seed, goAt, delay, standoff, ready, fakes };
}

// 判定：两台各自用 (我, 对手) 调一次，结果互为镜像
function judge(me, opp) {
  if (me.k === 'hit' && opp.k === 'hit') return me.r < opp.r ? 'win' : me.r > opp.r ? 'lose' : 'replay';
  if (me.k === 'hit') return 'win';
  if (opp.k === 'hit') return 'lose';
  if (me.k === 'foul' && opp.k === 'none') return 'lose';
  if (me.k === 'none' && opp.k === 'foul') return 'win';
  return 'replay';   // 都抢跑 / 都没按
}

class DuelBadge {
  constructor(o) {
    this.mac = o.mac;                       // 16 位，显示为 NABO-XXXX
    this.name = 'NABO-' + o.mac.toString(16).toUpperCase().padStart(4, '0');
    this.rng = mulberry32(o.entropy >>> 0);
    this.set = Object.assign({ bo: 3, fake: true, ai: 1 }, o.settings || {});
    this.best = o.best || 0;
    this.suppress = !!o.suppress;           // 脚本测试：不刷新最快纪录
    this.t = 0;
    this.out = [];                          // {to: mac | '*', pkt}
    this.fx = [];                           // 音效 / 演出事件 {t, k}
    this.log = [];
    this.conns = new Map();                 // mac → 可靠传输状态
    this.peers = new Map();                 // 大厅里看到的工牌
    this.link = null;                       // 已配对：{mac, session, host, lastRx, lastBeat}
    this.match = null;
    this.cur = null;                        // 当前这一次尝试
    this.hold = { up: null, down: null };
    this.toast = null;
    this.go('cover');
  }

  // ---------- 外部入口 ----------
  key(t, k, down) {
    this.t = t;
    if (k === 'up' || k === 'down') this.hold[k] = down ? t : null;
    if (!down) return;
    if (k === 'ok' && this.scene === 'duel') { this._press(t); return; }
    const h = this['_key_' + this.scene];
    if (h) h.call(this, k);
  }

  recv(t, from, pkt, rssi) {
    this.t = t;
    if (!pkt || pkt.m !== 'ND') return;
    if (pkt.type === 'HELLO') { this._hello(from, pkt, rssi); return; }
    if (pkt.v !== VER) return;
    const c = this._conn(from);
    if (pkt.type === 'ACK') {
      c.pending = c.pending.filter(p => p.pkt.seq !== pkt.ack);
      if (this.link && this.link.mac === from) this.link.lastRx = t;
      return;
    }
    if (RELIABLE.has(pkt.type)) {
      this._raw(from, { m: 'ND', v: VER, type: 'ACK', s: pkt.s, ack: pkt.seq });
      if (c.seen.includes(pkt.seq)) return;
      c.seen.push(pkt.seq); if (c.seen.length > 16) c.seen.shift();
    }
    // 配对之后，只认这一场 session 的包
    const linked = this.link && this.link.mac === from;
    if (linked) {
      if (pkt.s !== this.link.session) return;
      this.link.lastRx = t;
    } else if (pkt.type !== 'INVITE' && pkt.type !== 'ACCEPT' && pkt.type !== 'DECLINE' && pkt.type !== 'BYE') {
      return;
    }
    const h = this['_pkt_' + pkt.type];
    if (h) h.call(this, from, pkt);
  }

  tick(t) {
    this.t = t;
    // 可靠消息重传
    for (const [mac, c] of this.conns) {
      for (const p of c.pending) {
        if (t - p.first >= T.GIVEUP) { p.dead = true; continue; }
        if (t - p.last >= T.RETX) { p.last = t; p.tries++; this._raw(mac, p.pkt); }
      }
      if (c.pending.some(p => p.dead)) {
        c.pending = c.pending.filter(p => !p.dead);
        if (this.link && this.link.mac === mac) this._lost('lost');
      }
    }
    if (this.toast && t >= this.toast.until) this.toast = null;
    // 长按 UP / DOWN 暂停
    if ((this.scene === 'duel' || this.scene === 'sync') && this.match) {
      for (const k of ['up', 'down'])
        if (this.hold[k] !== null && t - this.hold[k] >= T.LONG) { this.hold[k] = null; this._pause(); }
    }
    // 配对后的心跳和掉线
    if (this.link && !this.match?.solo) {
      if (t - this.link.lastBeat >= T.BEAT) {
        this.link.lastBeat = t;
        this._raw(this.link.mac, this._pk('BEAT', {}));
      }
      if (t - this.link.lastRx >= T.DEAD) this._lost('lost');
    }
    const h = this['_tick_' + this.scene];
    if (h) h.call(this, t);
  }

  // ---------- 场景切换 ----------
  go(scene) { this.scene = scene; this.sceneAt = this.t; this.sel = 0; }
  say(text, ms) { this.toast = { text, until: this.t + (ms || T.TOAST) }; }
  _fx(k, extra) { this.fx.push(Object.assign({ t: this.t, k }, extra)); }
  _note(s) { this.log.push(this.t + ' ' + s); if (this.log.length > 60) this.log.shift(); }

  // ---------- 封面 ----------
  coverItems() { return ['online', 'solo', 'bo', 'fake', 'ai', 'back']; }   // back：回到牌桌封面
  _key_cover(k) {
    const n = this.coverItems().length;
    if (k === 'up') this.sel = (this.sel + n - 1) % n;
    else if (k === 'down') this.sel = (this.sel + 1) % n;
    else {
      const it = this.coverItems()[this.sel];
      if (it === 'online') this._enterLobby();
      else if (it === 'solo') this._startMatch({ solo: true });
      else if (it === 'bo') this.set.bo = this.set.bo === 1 ? 3 : this.set.bo === 3 ? 5 : 1;
      else if (it === 'fake') this.set.fake = !this.set.fake;
      else if (it === 'ai') this.set.ai = (this.set.ai + 1) % AI.length;
      else if (it === 'back') this.exitReq = true;   // 固件：牌桌任务看到后切回牌桌
      this._fx('tick');
    }
  }

  // ---------- 大厅 ----------
  _enterLobby() { this.link = null; this.match = null; this.cur = null; this.go('lobby'); this.helloAt = -1e9; }
  lobbyList() {
    const ps = [...this.peers.values()].sort((a, b) => (a.busy - b.busy) || (b.rssi - a.rssi) || (a.mac - b.mac));
    return ps.slice(0, 4).concat([{ back: true }]);
  }
  _hello(from, pkt, rssi) {
    if (!['lobby', 'inviting', 'invited'].includes(this.scene)) return;
    this.peers.set(from, { mac: from, name: pkt.name, busy: !!pkt.busy, ver: pkt.v, rssi, seen: this.t });
  }
  _broadcastHello(t, busy) {
    if (t - this.helloAt < T.HELLO) return;
    this.helloAt = t;
    this._raw('*', { m: 'ND', v: VER, type: 'HELLO', name: this.name, busy });
  }
  _expirePeers(t) { for (const [m, p] of this.peers) if (t - p.seen > T.PEER_EXPIRE) this.peers.delete(m); }
  _tick_lobby(t) {
    this._broadcastHello(t, false);
    this._expirePeers(t);
    const n = this.lobbyList().length;
    if (this.sel >= n) this.sel = n - 1;
  }
  _key_lobby(k) {
    const list = this.lobbyList(), n = list.length;
    if (k === 'up') this.sel = (this.sel + n - 1) % n;
    else if (k === 'down') this.sel = (this.sel + 1) % n;
    else {
      const p = list[this.sel];
      if (p.back) { this.go('cover'); this.sel = 0; return; }
      if (p.ver !== VER) { this.say('对方固件版本不同'); return; }
      if (p.busy) { this.say('对方正在对决'); return; }
      this.invite = { mac: p.mac, name: p.name, session: this.rng() >>> 0, at: this.t, bo: this.set.bo, fake: this.set.fake };
      this._rel(p.mac, this._pk('INVITE', { name: this.name, bo: this.set.bo, fake: this.set.fake ? 1 : 0 }, this.invite.session));
      this.go('inviting');
      this._fx('tick');
    }
  }
  _tick_inviting(t) {
    this._broadcastHello(t, true);
    this._expirePeers(t);
    if (t - this.invite.at >= T.INVITE_WAIT) { this.say('没有回应'); this._dropConn(this.invite.mac); this._enterLobby(); }
  }
  _key_inviting(k) {
    if (k !== 'up') return;
    this._raw(this.invite.mac, this._pk('BYE', { why: 'cancel' }, this.invite.session));
    this._dropConn(this.invite.mac);
    this._enterLobby();
  }
  _tick_invited(t) {
    this._broadcastHello(t, true);
    this._expirePeers(t);
    if (t - this.invited.at >= T.INVITE_WAIT) this._decline();
  }
  _key_invited(k) {
    if (k === 'ok') this._accept(this.invited);
    else if (k === 'up') this._decline();
  }
  _decline() {
    this._rel(this.invited.mac, this._pk('DECLINE', { why: 'no' }, this.invited.session));
    this._enterLobby();
  }
  _accept(inv) {
    this._rel(inv.mac, this._pk('ACCEPT', { name: this.name }, inv.session));
    this.link = { mac: inv.mac, name: inv.name, session: inv.session, host: false, lastRx: this.t, lastBeat: this.t };
    this._note('配对成功（客人） ' + inv.name);
    this._startMatch({ bo: inv.bo, fake: inv.fake });
  }

  _pkt_INVITE(from, p) {
    const inv = { mac: from, name: p.name, session: p.s, bo: p.bo, fake: !!p.fake, at: this.t };
    if (this.scene === 'inviting' && this.invite.mac === from) {
      // 两人同时互相邀请：MAC 大的当房主，小的直接接受对方的邀请
      if (this.mac > from) return;
      this._dropConn(from, true);
      this._accept(inv);
      return;
    }
    if (this.scene !== 'lobby') {
      this._rel(from, this._pk('DECLINE', { why: 'busy' }, p.s));
      return;
    }
    this.invited = inv;
    this.go('invited');
    this._fx('invite');
  }
  _pkt_ACCEPT(from, p) {
    if (this.scene !== 'inviting' || this.invite.mac !== from || p.s !== this.invite.session) return;
    this.link = { mac: from, name: this.invite.name, session: p.s, host: true, lastRx: this.t, lastBeat: this.t };
    this._note('配对成功（房主） ' + this.invite.name);
    this._startMatch({ bo: this.invite.bo, fake: this.invite.fake });
  }
  _pkt_DECLINE(from, p) {
    if (this.scene !== 'inviting' || this.invite.mac !== from) return;
    this.say(p.why === 'busy' ? '对方正忙' : '对方拒绝了');
    this._dropConn(from);
    this._enterLobby();
  }
  _pkt_BYE(from, p) {
    if (this.scene === 'invited' && this.invited.mac === from) { this.say('对方取消了邀请'); this._enterLobby(); return; }
    if (!this.link || this.link.mac !== from || p.s !== this.link.session) return;
    this._lost(p.why === 'quit' ? 'quit' : 'bye');
  }

  // ---------- 对局 ----------
  _startMatch(o) {
    const solo = !!o.solo;
    const bo = solo ? this.set.bo : o.bo;
    this.match = {
      solo, bo, fake: solo ? this.set.fake : !!o.fake, need: (bo + 1) >> 1,
      me: 0, opp: 0, round: 1, attempt: 1, host: solo || this.link.host,
      offset: 0, rtt: null, last: null, done: false, wantNext: false, oppNext: false,
      oppName: solo ? 'NABO' : this.link.name, ai: AI[this.set.ai],
    };
    this.cur = null;
    this.go('sync');
    if (this.match.host) { this._beginSync(this.t); this.match.issueAt = this.t; }
  }
  _beginSync(t) {
    const m = this.match;
    if (m.solo) { m.syncDone = t; return; }
    m.pings = []; m.pingStart = t; m.pingSent = 0; m.best = null; m.syncDone = null;
  }
  _tick_sync(t) { this._hostTick(t); }
  _hostTick(t) {
    const m = this.match;
    if (!m || !m.host || m.done || this.scene === 'pause' || this.scene === 'opppause') return;
    if (m.syncDone === null && m.pings) {
      if (m.pingSent < T.PING_N && t - m.pingStart >= m.pingSent * T.PING_GAP) {
        this._raw(this.link.mac, this._pk('PING', { id: m.pingSent, t1: t }));
        m.pingSent++;
      }
      if (m.pingSent >= T.PING_N && t - m.pingStart >= (T.PING_N - 1) * T.PING_GAP + T.PING_WAIT) {
        if (m.best) {
          m.offset = m.best.off; m.rtt = m.best.rtt; m.syncDone = t;
          this._note('对时：偏差 ' + m.offset + ' ms，往返 ' + m.rtt + ' ms');
        } else this._beginSync(t);   // 一个 PONG 都没收到，重来（掉线由心跳判断）
      }
    }
    if (m.syncDone !== null && m.issueAt !== undefined && t >= m.issueAt && (!this.cur || this.cur.resolved)) this._issueRound(t);
  }
  _pkt_PING(from, p) { this._raw(from, this._pk('PONG', { id: p.id, t1: p.t1, t2: this.t })); }
  _pkt_PONG(from, p) {
    const m = this.match;
    if (!m || !m.host || m.syncDone !== null || !m.pings) return;
    const rtt = this.t - p.t1, off = p.t2 - Math.floor((p.t1 + this.t + 1) / 2);
    if (!m.best || rtt < m.best.rtt) m.best = { rtt, off };
  }

  _issueRound(t) {
    const m = this.match;
    const seed = this.rng() >>> 0;
    const delay = attemptPlan(seed, 0, m.round, false).delay;
    const ready = Math.max(t + (m.solo ? 300 : T.LEAD), m.revealUntil || 0);
    const goAt = ready + T.READY + delay;
    m.issueAt = undefined;
    if (!m.solo)
      this._rel(this.link.mac, this._pk('ROUND', { r: m.round, a: m.attempt, seed, go: goAt + m.offset, sh: m.me, sg: m.opp }));
    this._beginAttempt(seed, goAt);
  }
  _pkt_ROUND(from, p) {
    const m = this.match;
    if (!m || m.host) return;
    if (m.done) {                                   // 再来一场：房主发来了新一场的第一局
      if (p.r !== 1 || p.a !== 1) return;
      this._startMatch({ bo: m.bo, fake: m.fake });
    }
    const mm = this.match;
    if (p.go - this.t < T.LATE_MIN) {
      this._rel(from, this._pk('LATE', { r: p.r, a: p.a }));
      this._note('ROUND 来晚了，请房主重排');
      return;
    }
    mm.round = p.r; mm.attempt = p.a; mm.me = p.sg; mm.opp = p.sh;   // 比分以房主为准
    if (this.scene === 'pause' || this.scene === 'opppause') return;
    this._beginAttempt(p.seed, p.go);
  }
  _pkt_LATE(from, p) {
    const m = this.match;
    if (!m || !m.host || p.r !== m.round || p.a !== m.attempt) return;
    this.cur = null;
    this._beginSync(this.t); m.issueAt = this.t;
  }

  _beginAttempt(seed, goAt) {
    const m = this.match;
    const plan = attemptPlan(seed, goAt, m.round, m.fake);
    this.cur = Object.assign(plan, { round: m.round, attempt: m.attempt, me: null, opp: null, lockAt: null, goShown: null, fakeIdx: 0, resolved: false });
    if (m.solo) {
      // NABO 的出手也由种子决定
      const r2 = mulberry32(seed ^ 0xA5A5A5A5), ai = m.ai;
      if (r2() % 1000 < ai.foul * 1000) this.cur.aiFoul = plan.standoff + 200 + r2() % Math.max(1, plan.delay - 300);
      else this.cur.aiR = Math.max(140, Math.min(900, normalInt(r2, ai.mean, ai.sd)));
    }
    this.go('duel');
  }
  phase(t) {
    const c = this.cur;
    if (!c) return 'wait';
    if (c.resolved) return 'reveal';
    if (t < c.ready) return 'wait';
    if (t < c.standoff) return 'ready';
    if (t < c.goAt) return 'standoff';
    return 'go';
  }
  _tick_duel(t) {
    const c = this.cur, m = this.match;
    this._hostTick(t);
    if (!c) return;
    if (!c.resolved) {
      if (t >= c.ready && !c.readyFx) { c.readyFx = true; this._fx('ready'); }
      if (t >= c.standoff && !c.standFx) { c.standFx = true; this._fx('standoff'); }
      while (c.fakeIdx < c.fakes.length && t >= c.fakes[c.fakeIdx].at) { this._fx('fake', { kind: c.fakes[c.fakeIdx].kind }); c.fakeIdx++; }
      if (t >= c.goAt && c.goShown === null) { c.goShown = t; this._fx('go'); }   // 固件：esp_timer 在 goAt 发 INVON 并记录时刻
      if (m.solo && !c.opp) {
        if (c.aiFoul !== undefined && t >= c.aiFoul) this._oppResult({ k: 'foul' });
        else if (c.aiR !== undefined && c.goShown !== null && t >= c.goShown + c.aiR) this._oppResult({ k: 'hit', r: c.aiR });
      }
      if (!c.me && c.goShown !== null && t >= c.goShown + T.TIMEOUT) this._lock({ k: 'none' });
      if (!c.me && c.lockAt !== null && t >= c.lockAt) this._lock({ k: 'none' });
    } else if (m.done && t >= m.revealUntil && this.scene === 'duel') {
      this.go('end');
    }
  }
  _press(t) {
    const c = this.cur;
    if (!c || c.me || c.resolved || t < c.standoff) return;
    if (t < c.goAt) { this._lock({ k: 'foul' }); return; }
    if (c.goShown === null) { c.goShown = c.goAt; this._fx('go'); }   // 同一毫秒里按键先于节拍处理
    this._lock({ k: 'hit', r: t - c.goShown });
  }
  _lock(res) {
    const c = this.cur, m = this.match;
    c.me = res;
    this._fx(res.k === 'foul' ? 'foul' : res.k === 'hit' ? 'shot' : 'none');
    if (!m.solo)
      this._rel(this.link.mac, this._pk('RESULT', { r: c.round, a: c.attempt, k: res.k, ms: res.r || 0 }));
    if (c.opp) this._resolve();
  }
  _pkt_RESULT(from, p) {
    const c = this.cur;
    if (!c || p.r !== c.round || p.a !== c.attempt || c.opp) return;
    this._oppResult(p.k === 'hit' ? { k: 'hit', r: p.ms } : { k: p.k });
  }
  _oppResult(res) {
    const c = this.cur;
    c.opp = res;
    if (!c.me) {
      if (res.k === 'foul') this._lock({ k: 'none' });
      else if (res.k === 'hit') {
        // 再按也比对手慢：时钟过了 GO + b 还没按，就锁定为“没按”
        c.lockAt = (c.goShown !== null ? c.goShown : c.goAt) + res.r + 1;
        if (this.t >= c.lockAt) this._lock({ k: 'none' });
      }
      return;
    }
    this._resolve();
  }
  _resolve() {
    const c = this.cur, m = this.match;
    if (c.resolved) return;
    c.resolved = true;
    c.resolvedAt = this.t;
    const out = judge(c.me, c.opp);
    c.outcome = out;
    if (out === 'win') m.me++;
    else if (out === 'lose') m.opp++;
    if (c.me.k === 'hit' && !this.suppress && (!this.best || c.me.r < this.best)) { this.best = c.me.r; c.newBest = true; }
    m.last = c;
    m.revealUntil = this.t + T.REVEAL;
    if (out === 'replay' && m.attempt < T.MAX_ATTEMPT) m.attempt++;
    else { m.round++; m.attempt = 1; }
    if (m.me >= m.need || m.opp >= m.need) m.done = true;
    this._fx(out, { me: c.me, opp: c.opp });
    this._note(`第 ${c.round} 局(${c.attempt}) 我 ${fmtRes(c.me)} / 对手 ${fmtRes(c.opp)} → ${out}`);
    if (m.host && !m.done) {
      // 结果继续显示，房主趁展示时间重新对时
      this._beginSync(this.t + 300);
      m.pingStart = this.t + 300;
      m.issueAt = this.t + 300;
    }
  }

  // ---------- 暂停 ----------
  _pause() {
    const m = this.match;
    this.cur = null;
    this.go('pause');
    if (!m.solo) this._rel(this.link.mac, this._pk('PAUSE', {}));
    this._fx('pause');
  }
  _key_pause(k) {
    if (k === 'up' || k === 'down') { this.sel ^= 1; return; }
    if (this.sel === 0) this._resume(true);
    else this._quit();
  }
  _resume(mine) {
    const m = this.match;
    this.go('duel');
    this.cur = null;
    if (mine && !m.solo) this._rel(this.link.mac, this._pk('RESUME', {}));
    if (m.host) { this._beginSync(this.t); m.issueAt = this.t; }
  }
  _pkt_PAUSE() { if (this.match && !this.match.done) { this.cur = null; this.go('opppause'); } }
  _pkt_RESUME() { if (this.scene === 'opppause') this._resume(false); }
  _quit() {
    const m = this.match;
    if (!m.solo) {
      const bye = this._pk('BYE', { why: 'quit' });
      for (let i = 0; i < 3; i++) this._raw(this.link.mac, bye);
      this._dropConn(this.link.mac);
      this._enterLobby();
    } else { this.match = null; this.go('cover'); }
  }

  // ---------- 整场结束 ----------
  _key_end(k) {
    const m = this.match;
    if (k === 'up' || k === 'down') { if (!m.wantNext) this.sel ^= 1; return; }
    if (m.wantNext) return;
    if (this.sel === 1) { this._quit(); return; }
    if (m.solo) { this._startMatch({ solo: true }); return; }
    m.wantNext = true;
    this._rel(this.link.mac, this._pk('NEXT', {}));
    this._maybeRematch();
  }
  _pkt_NEXT() { if (this.match && this.match.done) { this.match.oppNext = true; this._maybeRematch(); } }
  _maybeRematch() {
    const m = this.match;
    if (m.host && m.wantNext && m.oppNext) { this._startMatch({ bo: m.bo, fake: m.fake }); this.match.issueAt = this.t; }
  }

  // ---------- 掉线 ----------
  _lost(why) {
    if (!this.link) return;
    this._dropConn(this.link.mac);
    this.link = null; this.cur = null;
    this.leftWhy = why;
    this.go('left');
    this._fx('left');
  }
  _tick_left(t) { if (t - this.sceneAt >= T.LEFT_SHOW) this._enterLobby(); }

  // ---------- 传输 ----------
  _conn(mac) {
    let c = this.conns.get(mac);
    if (!c) { c = { txSeq: 0, pending: [], seen: [] }; this.conns.set(mac, c); }
    return c;
  }
  _dropConn(mac, keepSeen) {
    const c = this.conns.get(mac);
    if (!c) return;
    c.pending = [];
    if (!keepSeen) c.seen = c.seen.slice(-16);
  }
  _pk(type, body, session) {
    return Object.assign({ m: 'ND', v: VER, type, s: session !== undefined ? session : (this.link ? this.link.session : 0) }, body);
  }
  _raw(to, pkt) { this.out.push({ to, pkt }); }
  _rel(to, pkt) {
    const c = this._conn(to);
    pkt.seq = (c.txSeq = (c.txSeq + 1) & 0xffff) || (c.txSeq = 1);
    c.pending.push({ pkt, first: this.t, last: this.t, tries: 1 });
    this._raw(to, pkt);
  }
}

function fmtRes(r) {
  if (!r) return '—';
  return r.k === 'hit' ? (r.r / 1000).toFixed(3) + 's' : r.k === 'foul' ? '抢跑' : '没按';
}

const api = { DuelBadge, judge, attemptPlan, mulberry32, normalInt, fmtRes, T, AI, VER };
if (typeof module !== 'undefined' && module.exports) module.exports = api;
else root.NaboDuel = api;
})(typeof window !== 'undefined' ? window : globalThis);
