// 宿主场景测试：两台模型 + 模拟链路，跑完整场次并检查双方结果一致
const D = require('../prototype/duel_model.js');
const { Air, makeBot } = require('../prototype/sim.js');
let fails = 0;
const ok = (c, m) => { if (!c) { fails++; console.log('  FAIL', m); } };

function run(name, o) {
  const air = new Air({ seed: o.seed || 1, net: o.net });
  const A = new D.DuelBadge({ mac: 0x3F2A, entropy: 11, settings: { bo: o.bo || 3, fake: true }, suppress: true });
  const B = new D.DuelBadge({ mac: 0x1B07, entropy: 22, suppress: true });
  const na = air.add(A, { off: 0, bot: makeBot({ invite: 0x1B07, mean: o.ma || 250, sd: 30, foul: o.fa || 0, seed: 3, rematch: false }) });
  const nb = air.add(B, { off: o.off ?? 987654, ppm: o.ppm ?? 40, bot: makeBot({ mean: o.mb || 280, sd: 30, foul: o.fb || 0, seed: 5, rematch: false }) });
  let syncErr = [];
  const log = [];
  A.fx = new Proxy([], { set(t, k, v) { if (v && v.k && ['win','lose','replay'].includes(v.k)) log.push(['A', v.k]); t[k] = v; return true; } });
  B.fx = new Proxy([], { set(t, k, v) { if (v && v.k && ['win','lose','replay'].includes(v.k)) log.push(['B', v.k]); if (v && v.k === 'go') syncErr.push(air.g); t[k] = v; return true; } });
  let aGo = [];
  const origA = A._fx.bind(A);
  A._fx = (k, e) => { if (k === 'go') aGo.push(air.g); origA(k, e); };
  if (o.cut) air.step(o.cut.at), air.node(0x1B07).antenna = false;
  air.step(o.ms || 60000);
  const pairs = [];
  for (let i = 0; i + 1 < log.length; i += 2) pairs.push(log[i][1] + '/' + log[i + 1][1]);
  const mirror = { win: 'lose', lose: 'win', replay: 'replay' };
  const consistent = log.filter(x => x[0] === 'A').every((x, i) => { const y = log.filter(z => z[0] === 'B')[i]; return !y || mirror[x[1]] === y[1]; });
  const errs = aGo.map((g, i) => syncErr[i] !== undefined ? syncErr[i] - g : null).filter(x => x !== null);
  console.log(`${name}: A ${A.scene} ${A.match ? A.match.me + ':' + A.match.opp : '-'} | B ${B.scene} ${B.match ? B.match.me + ':' + B.match.opp : '-'} | 局 ${log.length/2} | 拔枪时刻差(全局ms) ${JSON.stringify(errs)} | 丢包 ${air.stats.lost}/${air.stats.sent}`);
  return { A, B, consistent, errs, air };
}

let r = run('普通链路', {});
ok(r.consistent, '双方结果不镜像'); ok(r.A.scene === 'end' && r.B.scene === 'end', '没打完'); ok(r.errs.every(e => Math.abs(e) <= 3), '拔枪不同步');
ok(r.A.match.me === r.B.match.opp && r.A.match.opp === r.B.match.me, '比分不一致');
ok(r.A.match.me === 2, 'A 更快应赢 2 局');

r = run('高延迟抖动+20%丢包', { net: { latency: 40, jitter: 30, loss: 0.2 }, seed: 9 });
ok(r.consistent, '高丢包结果不一致'); ok(r.A.scene === 'end' && r.B.scene === 'end', '高丢包没打完');
ok(r.errs.every(e => Math.abs(e) <= 35), '高延迟下拔枪偏差过大');

r = run('B 爱抢跑', { fb: 0.6, bo: 5 });
ok(r.consistent, '抢跑结果不一致'); ok(r.A.scene === 'end', '抢跑场没打完');

r = run('都抢跑', { fa: 1, fb: 1, bo: 1, ms: 40000 });
ok(r.consistent, '都抢跑不一致');

r = run('五局三胜 B 更快', { bo: 5, ma: 320, mb: 220, seed: 4 });
ok(r.B.match.me === 3 && r.B.scene === 'end', 'B 应赢 3 局');

r = run('中途断线', { cut: { at: 15000 }, ms: 25000 });
ok(['lobby', 'left'].includes(r.A.scene), 'A 没发现掉线: ' + r.A.scene);

// 规则表
const J = D.judge, H = r => ({ k: 'hit', r }), F = { k: 'foul' }, N = { k: 'none' };
ok(J(H(200), H(250)) === 'win' && J(H(250), H(200)) === 'lose' && J(H(200), H(200)) === 'replay', 'hit/hit');
ok(J(H(900), F) === 'win' && J(H(900), N) === 'win' && J(F, N) === 'lose' && J(N, F) === 'win', '抢跑/没按');
ok(J(F, F) === 'replay' && J(N, N) === 'replay', '双方无效');

// 单人练习
{
  const air = new Air({ seed: 3 });
  const A = new D.DuelBadge({ mac: 1, entropy: 5, settings: { bo: 3, ai: 2 }, suppress: true });
  air.add(A, { bot: makeBot({ invite: false, mean: 230, sd: 30, seed: 8, rematch: false }) });
  A.sel = 1; air.key(air.nodes[0], 'ok', true);
  air.step(40000);
  console.log('单人练习:', A.scene, A.match.me + ':' + A.match.opp);
  ok(A.scene === 'end', '单人没打完');
}
console.log(fails ? `${fails} 项失败` : '全部通过');
process.exit(fails ? 1 : 0);
