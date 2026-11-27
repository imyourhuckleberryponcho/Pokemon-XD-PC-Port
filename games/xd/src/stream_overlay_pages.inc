const char* const kPage_native[] = {
R"PG0(<!DOCTYPE html>
<!-- Pokemon XD PC port: stream overlay page, look: Native XD. OBS: Sources > + > Browser, tick "Local file", pick this file, width 1000, height 260.
  The game's pictures (ui/) and letters (font/) next to it are made from your own disc the first time the overlay runs.
  Data: party.js (window.XDParty({...})), format version 2 (docs/STREAM_OVERLAY.md). No internet address anywhere in this page.
  The program rewrites overlay_native.html only while its first line is its own version marker; to make your own look copy this
  file to my_overlay.html (any other name) and change the copy: the program never touches other files. -->
<html lang="en"><head><meta charset="utf-8"><title>Party - Native XD</title>
<style>
@font-face{font-family:XD0;src:url(font/xd0.ttf)}
@font-face{font-family:XD1;src:url(font/xd1.ttf)}
@font-face{font-family:XD2;src:url(font/xd2.ttf)}
@font-face{font-family:XD3;src:url(font/xd3.ttf)}
html,body{margin:0;background:transparent;overflow:hidden;height:100%}
*{box-sizing:border-box}
body{font-family:XD1,'Segoe UI','Yu Gothic UI','Noto Sans JP',sans-serif;color:#fff}
.n{font-family:XD3,'Segoe UI','Yu Gothic UI','Noto Sans JP',sans-serif}
.tx{text-shadow:1.2px 1.2px 0 #000,0 0 2px rgba(0,0,0,.7)}
.fit{display:inline-block;white-space:nowrap;transform-origin:0 50%}
.hpb{position:relative;background:#000}
.hpb b{position:absolute;inset:2px;background:linear-gradient(#4a4a4a 82%,#737373 82%)}
.hpb i,.hpb u{position:absolute;left:2px;top:2px;bottom:2px;width:calc((100% - 4px)*var(--hp));transition:width .6s linear}
.hpb u{background:#ffd9d0;transition:width .5s ease-out .7s}
.hi .hpb i{background:linear-gradient(#10ff42 0 8%,#7bff94 8% 17%,#00e729 17% 83%,#00d629 83% 91%,#00b521 91%)}
.mid .hpb i{background:linear-gradient(#ffff10 0 8%,#ffff7b 8% 17%,#e7e700 17% 83%,#d6d600 83% 91%,#b5b500 91%)}
.lo .hpb i{background:linear-gradient(#e75a4a 0 8%,#f7a494 8% 17%,#d63a20 17% 83%,#bd2921 83% 91%,#9c2121 91%)}
.heart{position:relative;background:#000}
.heart b{position:absolute;inset:1.5px;background:#3a2545}
.heart i{position:absolute;left:1.5px;top:1.5px;bottom:1.5px;width:calc((100% - 3px)*var(--heart));background:linear-gradient(#ff9cff 0 16%,#ce10ff 16% 72%,#9400bd 72%);transition:width .8s}
.heart s{position:absolute;inset:1.5px;background:linear-gradient(90deg,transparent calc(100% - 1.5px),#000 0) 0 0/20% 100%;right:calc(1.5px - 1px)}
.st{background:center/contain no-repeat}
[data-st=poison] .st{background-image:url(ui/st_poison.png)}
[data-st=burn] .st{background-image:url(ui/st_burn.png)}
[data-st=paralysis] .st{background-image:url(ui/st_paralysis.png)}
[data-st=sleep] .st{background-image:url(ui/st_sleep.png)}
[data-st=freeze] .st{background-image:url(ui/st_freeze.png)}
[data-st=fainted] .st{background-image:url(ui/st_fainted.png)}
.star{position:absolute;display:none;width:16px;height:16px;background:radial-gradient(closest-side,#fff 0 18%,transparent 19%),conic-gradient(from 0deg,#ffe97a,#fff6c8,#ffd23a,#fff6c8,#ffe97a);
  clip-path:polygon(50% 0,60% 40%,100% 50%,60% 60%,50% 100%,40% 60%,0 50%,40% 40%);filter:drop-shadow(0 0 2px #a86b00);animation:glint 4s ease-in-out infinite}
.shiny .star{display:block}
@keyframes glint{0%,60%,100%{transform:scale(.85)}70%{transform:scale(1.3) rotate(20deg)}80%{transform:scale(.9)}90%{transform:scale(1.15)}}
.g{font-size:.82em;margin-left:.28em;display:inline-block}
.g.male{color:#6cb6ff}.g.female{color:#ff8fbd}
.exp{position:relative;background:#000}
.exp b{position:absolute;inset:1.5px;background:#3a3a3a}
.exp i{position:absolute;left:1.5px;top:1.5px;bottom:1.5px;width:calc((100% - 3px)*var(--exp));background:linear-gradient(#7fc0ff 0 16%,#1073ff 16% 72%,#004ac6 72%);transition:width .8s}
body.nopieces .plate,body.nopieces .hud{background:rgba(24,24,30,.82)!important;border-image:none!important;border-radius:14px}
</style>
<style>
#team{position:absolute;left:4px;right:4px;bottom:4px;display:grid;grid-template-columns:repeat(3,1fr);gap:6px 7px;justify-items:start}
.m{position:relative;zoom:.932;width:350px;height:64px;filter:drop-shadow(0 2px 3px rgba(0,0,0,.45))}
.plate,.ring,.hole,.hud,.lip,.hring,.hring2{position:absolute;pointer-events:none}
.plate{inset:0;border:solid transparent;border-width:0 30px 0 62px;border-image:url(ui/small_silver.png) 0 90 0 186 fill/0 30px 0 62px;}
.shadow .plate{border-image-source:url(ui/small_shadow.png)} .faint .plate{border-image-source:url(ui/small_faint.png)}
.hole{left:9px;top:10px;width:44px;height:44px;border-radius:50%;background:#000;overflow:hidden}
.hole img{width:100%;height:100%;display:block}
.hole:empty{background:#000 url(ui/ball_ok.png) center/58% no-repeat}

.faint .hole img{filter:grayscale(1) brightness(.62)}
.ring{display:none}
.nm{position:absolute;left:67px;top:8px;width:128px;font-size:18px;line-height:22px;overflow:visible}
.lv{position:absolute;left:204px;top:8px;font-size:17px;line-height:22px;white-space:pre}
.lv::before{content:'Lv ';font:15px XD1}
.st{position:absolute;right:20px;top:12px;width:49px;height:14px}
.hp{position:absolute;left:64px;top:34px;width:174px;height:17px}
.hp::before{content:'';position:absolute;left:0;top:0;width:29px;height:17px;background:#000 url(ui/hp2.png) 1px -1px/28px 18px no-repeat}
.hp .hpb{position:absolute;left:29px;right:0;top:0;bottom:0}
.num{position:absolute;right:22px;top:34px;font-size:17px;line-height:19px;white-space:pre}
.heart{position:absolute;left:90px;top:50px;width:148px;height:7px;display:none}
.shadow .heart{display:block} .shadow .hp,.shadow .num{top:31px}
.star{left:46px;top:6px;width:17px;height:17px}
.faint .nm,.faint .lv,.faint .num{color:#c9b3ba}
.hud,.lip,.hring,.hring2,.hh,.exp,.xl{display:none}
#balls{position:absolute;left:8px;top:0;display:none;gap:3px}
#balls i{width:22px;height:22px;background:center/contain no-repeat url(ui/ball_none.png)}
.low .hp{animation:low 1.8s ease-in-out infinite}
@keyframes low{0%,100%{box-shadow:0 0 0 0 rgba(255,70,40,0)}50%{box-shadow:0 0 0 2px rgba(255,86,50,.95),0 0 9px 2px rgba(255,60,30,.6)}}
.shadow:not(.faint)::before{content:'';position:absolute;inset:-3px -2px -3px -3px;border-radius:34px 14px 14px 34px;box-shadow:0 0 8px 2px rgba(122,30,200,.7);animation:smo 3.2s ease-in-out infinite}
@keyframes smo{50%{opacity:.35}}
.hit .hole{animation:hit .5s steps(2) 1}@keyframes hit{50%{filter:brightness(2.6)}}
.sent{animation:sent .45s ease-out}@keyframes sent{0%{transform:translateY(18px);opacity:0}}
.battle #team{display:flex;flex-wrap:wrap;gap:6px 7px;align-content:flex-end;inset:4px}
.battle .m:not(.on){order:2;width:259px;zoom:.932}
.battle .m:not(.on) .num{display:none}
.battle .m:not(.on) .nm{width:118px} .battle .m:not(.on) .lv{left:auto;right:22px}
.battle .m:not(.on) .hp{width:176px}
.battle .m:not(.on) .heart{width:150px}
.battle .m:not(.on)[data-st]:not([data-st=none]) .lv{display:none} .battle .m:not(.on) .st{right:22px}
.battle .m.on{order:1;zoom:1.72;width:222px;height:53px;margin:0 6px 1px 2px}
.battle .m.on.shadow{height:59px}
.battle .m.on:not(.shadow){margin-bottom:7px}
.battle .m.on .plate{display:none}
.battle .m.on .hud{display:block;left:0;top:0;width:220px;height:52px;background:url(ui/hud_silver.png) 0 0/220px 52px no-repeat}
.battle .m.on.shadow .hud{background-image:url(ui/hud_shadow.png)} .battle .m.on.faint .hud{background-image:url(ui/hud_faint.png)}
.battle .m.on.shadow .lip{display:block;left:0;top:36px;width:180px;height:22px;background:url(ui/lip_shadow.png) 0 0/180px 22px no-repeat}
.battle .m.on .hole{left:172.5px;top:4.5px;width:43px;height:43px}
.battle .m.on .nm{left:15px;top:4px;width:98px;line-height:20px}
.battle .m.on .lv{left:auto;right:56px;top:4px;line-height:20px;font-size:16px}
.battle .m.on .lv::before{font-size:13.5px}
.battle .m.on .hp{left:13px;top:22px;width:153px;height:12px}
.battle .m.on .hp::before{width:25px;height:12px;background:#000 url(ui/hplabel.png) center/26px 12px no-repeat}
.battle .m.on .hp .hpb{left:24px}
.battle .m.on .num{right:auto;left:38px;top:33px;font-size:15px;line-height:15px}
.battle .m.on .st{right:58px;top:34.5px;width:42px;height:12px}
.battle .m.on .heart{left:30px;top:47.5px;width:134px;height:6.5px}
.battle .m.on.hasexp:not(.shadow) .lip{display:block;left:0;top:36px;width:180px;height:22px;background:url(ui/lip_silver.png) 0 0/180px 22px no-repeat}
.battle .m.on.hasexp.faint:not(.shadow) .lip{background-image:url(ui/lip_faint.png)}
.battle .m.on.hasexp:not(.shadow) .exp{display:block;position:absolute;left:38px;top:47.5px;width:126px;height:6.5px}
.battle .m.on.hasexp:not(.shadow) .xl{display:block;position:absolute;left:11px;top:44.5px;width:24px;height:10px;background:url(ui/explabel.png) center/24px 10px no-repeat}
.battle .m.on.shadow .hh{display:block;position:absolute;left:14px;top:46px;width:10px;height:9px;background:#ff8cff;clip-path:path('M5 9 L.4 4.2 A2.6 2.6 0 0 1 5 1.6 A2.6 2.6 0 0 1 9.6 4.2 Z');}
.battle .m.on .star{left:166px;top:2px;width:13px;height:13px}
.battle .m.on.shadow:not(.faint)::before{inset:-2px;border-radius:14px 27px 27px 14px;box-shadow:0 0 5px 1.5px rgba(122,30,200,.75)}
.battle #balls{display:flex;left:9px;bottom:176px;top:auto}
.side #team{left:8px;right:auto;bottom:auto;top:8px;display:flex;flex-direction:column;flex-wrap:nowrap;gap:9px;align-content:flex-start;inset:8px auto auto 8px}
.side .m,.side.battle .m:not(.on){zoom:1;width:350px;margin:0 0 0 14px}
.side.battle .m:not(.on){order:2}
.side.battle .m:not(.on) .num{display:block} .side.battle .m:not(.on) .nm{width:128px} .side.battle .m:not(.on) .lv{left:204px;right:auto;display:block!important}
.side.battle .m:not(.on) .hp{width:174px} .side.battle .m:not(.on) .heart{width:148px} .side.battle .m:not(.on) .st{right:20px}
.side.battle .m.on{zoom:1.66;margin:0 0 2px 0}
.side.battle #balls{left:10px;top:6px;bottom:auto}
.side.battle #team{top:34px}
</style></head>
<body><div id="balls"></div><div id="team"></div>
<script>
(function () {
  var q = new URLSearchParams(location.search), dn = q.get('data');
  var src = dn && /^[A-Za-z0-9_-]+$/.test(dn) ? 'data/' + dn + '.js' : 'party.js';
  var last = null, prev = {}, els = {};
  var XD = window.XD = {
    q: q,
    el: function (t, c, x) { var e = document.createElement(t); if (c) e.className = c; if (x !== undefined) e.textContent = x; return e; },
    hpClass: function (m) { var p = m.hp / Math.max(1, m.max_hp); return p > .5 ? 'hi' : p > .2 ? 'mid' : 'lo'; },
    fainted: function (m) { return !m.egg && (m.status === 'fainted' || m.hp <= 0); },
    name: function (m) { return m.egg ? 'EGG' : (m.nickname || m.name || ''); },
    small: function (m) { return m.small || m.face || ''; },
    setName: function (span, m) {
      span.textContent = XD.name(m);
      if (!m.egg && (m.gender === 'male' || m.gender === 'female')) span.append(XD.el('span', 'g ' + m.gender, m.gender === 'male' ? '\u2642' : '\u2640'));
    },
    expFraction: function (m) {
      var a = m.exp_level_start, b = m.exp_next;
      if (m.egg || typeof m.exp !== 'number' || typeof a !== 'number' || typeof b !== 'number' || b <= a) return -1;
      return Math.max(0, Math.min(1, (m.exp - a) / (b - a)));
    },
    STATUS: { poison: 'poison', bad_poison: 'poison', burn: 'burn', paralysis: 'paralysis', sleep: 'sleep', freeze: 'freeze', fainted: 'fainted' },
    fit: function (e) { e.style.transform = ''; var cs = getComputedStyle(e.parentNode), w = e.parentNode.clientWidth - parseFloat(cs.paddingLeft) - parseFloat(cs.paddingRight), s = e.scrollWidth; if (s > w && w > 0) e.style.transform = 'scaleX(' + Math.max(.45, w / s) + ')'; },
    flash: function (e, c, ms) { e.classList.remove(c); void e.offsetWidth; e.classList.add(c); setTimeout(function () { e.classList.remove(c); }, ms || 900); }
  };
  window.XDParty = function (d) {
    var t = JSON.stringify(d); if (t === last) return; last = t;
    var root = XD.root, party = (d && d.party) || [], seen = {};
    document.body.classList.toggle('battle', !!(d && d.in_battle));
    document.body.dataset.n = party.length;
    document.body.dataset.out = party.filter(function (m) { return m.on_field; }).length;
    party.forEach(function (m) {
      var k = m.slot, e = els[k], old = prev[k], sp = m.species_id + '/' + m.shiny + '/' + m.egg + '/' + (m.still || '') + '/' + (m.idle ? m.idle.src : '') + '/' + (m.small || m.face || ''); seen[k] = 1;
      if (!e || e.dataset.sp !== sp) { if (e) e.remove(); e = els[k] = XD.build(m); e.dataset.sp = sp; root.append(e); old = null; }
      var tg = { on: !!m.on_field, faint: XD.fainted(m), shadow: !!m.shadow, shiny: !!m.shiny, low: !XD.fainted(m) && !m.egg && m.hp / Math.max(1, m.max_hp) <= .2 };
      Object.keys(tg).forEach(function (c) { e.classList.toggle(c, tg[c]); });
      ['hi', 'mid', 'lo'].forEach(function (c) { e.classList.toggle(c, XD.hpClass(m) === c); });
      var xf = XD.expFraction(m); e.classList.toggle('hasexp', xf >= 0);
      e.dataset.st = XD.STATUS[m.status] || 'none';
      e.style.setProperty('--hp', Math.max(0, Math.min(1, m.hp / Math.max(1, m.max_hp))));
      e.style.setProperty('--heart', m.heart_max > 0 ? m.heart / m.heart_max : 0);
      e.style.setProperty('--exp', xf < 0 ? 0 : xf);
      XD.update(e, m, party, d);
      if (old) {
        if (m.hp < old.hp) XD.flash(e, 'hit'); else if (m.hp > old.hp) XD.flash(e, 'heal', 1200);
        if (m.level > old.level) XD.flash(e, 'lvup', 2600);
        if (m.on_field && !old.on_field) XD.flash(e, 'sent', 1000);
      }
      prev[k] = JSON.parse(JSON.stringify(m));
    });
    Object.keys(els).forEach(function (k) { if (!seen[k]) { els[k].remove(); delete els[k]; delete prev[k]; } });
    if (XD.layout) XD.layout(party, d, els);
    XD.refit();
  };
  XD.refit = function () { document.querySelectorAll('.fit').forEach(XD.fit); };
  var rt = null; document.addEventListener('transitionend', function () { clearTimeout(rt); rt = setTimeout(XD.refit, 30); });
  XD.start = function () {
    var tries = 0;
    function pieces() { var p = new Image(); p.onload = function () { if (document.body.classList.contains('nopieces')) location.reload(); }; p.onerror = function () { document.body.classList.add('nopieces'); }; p.src = 'ui/small_silver.png?t=' + Date.now(); }
    pieces();
    function poll() { var o = document.getElementById('feed'); if (o) o.remove(); var s = document.createElement('script'); s.id = 'feed'; s.charset = 'utf-8'; s.src = src + (src.indexOf('?') < 0 ? '?t=' : '&t=') + Date.now(); document.head.append(s); }
    var f = document.fonts ? Promise.all(['XD0', 'XD1', 'XD2', 'XD3'].map(function (n) { return document.fonts.load('18px ' + n); })) : Promise.resolve();
    f.catch(function () {}).then(function () { if (q.get('hold')) return; poll(); if (!dn) setInterval(function () { poll(); if (document.body.classList.contains('nopieces') && ++tries % 5 === 0) pieces(); }, 1000); document.fonts && document.fonts.addEventListener && document.fonts.addEventListener('loadingdone', XD.refit); });
  };
})();
</script>
<script>
XD.root = document.getElementById('team');
if (XD.q.get('shape') === 'side') document.body.classList.add('side');
XD.build = function (m) {
  var e = XD.el('div', 'm'), E = XD.el;
  var hole = E('div', 'hole');
  if (XD.small(m)) { var i = E('img'); i.alt = ''; i.src = XD.small(m); i.onerror = function () { this.remove(); }; hole.append(i); }
  var nm = E('div', 'nm n tx'); e._n = E('span', 'fit'); nm.append(e._n);
  e._lv = E('div', 'lv n tx'); e._num = E('div', 'num n tx');
  var hp = E('div', 'hp'), b = E('div', 'hpb'); b.append(E('b'), E('u'), E('i')); hp.append(b);
  var h = E('div', 'heart'); h.append(E('b'), E('i'), E('s'));
  var x = E('div', 'exp'); x.append(E('b'), E('i'));
  e.append(hole, E('div', 'plate'), E('div', 'hud'), E('div', 'lip'), nm, e._lv, hp, e._num, h, E('div', 'hh'), E('div', 'xl'), x, E('div', 'st'), E('div', 'star'));
  return e;
};
XD.update = function (e, m) {
  XD.setName(e._n, m); e._lv.textContent = m.egg ? '' : m.level; e._num.textContent = m.egg ? '' : m.hp + ' / ' + m.max_hp;
};
XD.layout = function (p) {
  var b = document.getElementById('balls'); b.replaceChildren();
  p.forEach(function (m) { var i = XD.el('i'); i.style.backgroundImage = 'url(ui/ball_' + (XD.fainted(m) ? 'dead' : m.shadow ? 'shadow' : m.status !== 'none' ? 'bad' : 'ok') + '.png)'; b.append(i); });
};
XD.start();
</script></body></html>
)PG1",
nullptr};
const char* const kPage_showcase[] = {
R"PG0(<!DOCTYPE html>
<!-- Pokemon XD PC port: stream overlay page, look: Showcase. OBS: Sources > + > Browser, tick "Local file", pick this file, width 1000, height 260.
  The game's pictures (ui/) and letters (font/) next to it are made from your own disc the first time the overlay runs.
  Data: party.js (window.XDParty({...})), format version 2 (docs/STREAM_OVERLAY.md). No internet address anywhere in this page.
  The program rewrites overlay_showcase.html only while its first line is its own version marker; to make your own look copy this
  file to my_overlay.html (any other name) and change the copy: the program never touches other files. -->
<html lang="en"><head><meta charset="utf-8"><title>Party - Showcase</title>
<style>
@font-face{font-family:XD0;src:url(font/xd0.ttf)}
@font-face{font-family:XD1;src:url(font/xd1.ttf)}
@font-face{font-family:XD2;src:url(font/xd2.ttf)}
@font-face{font-family:XD3;src:url(font/xd3.ttf)}
html,body{margin:0;background:transparent;overflow:hidden;height:100%}
*{box-sizing:border-box}
body{font-family:XD1,'Segoe UI','Yu Gothic UI','Noto Sans JP',sans-serif;color:#fff}
.n{font-family:XD3,'Segoe UI','Yu Gothic UI','Noto Sans JP',sans-serif}
.tx{text-shadow:1.2px 1.2px 0 #000,0 0 2px rgba(0,0,0,.7)}
.fit{display:inline-block;white-space:nowrap;transform-origin:0 50%}
.hpb{position:relative;background:#000}
.hpb b{position:absolute;inset:2px;background:linear-gradient(#4a4a4a 82%,#737373 82%)}
.hpb i,.hpb u{position:absolute;left:2px;top:2px;bottom:2px;width:calc((100% - 4px)*var(--hp));transition:width .6s linear}
.hpb u{background:#ffd9d0;transition:width .5s ease-out .7s}
.hi .hpb i{background:linear-gradient(#10ff42 0 8%,#7bff94 8% 17%,#00e729 17% 83%,#00d629 83% 91%,#00b521 91%)}
.mid .hpb i{background:linear-gradient(#ffff10 0 8%,#ffff7b 8% 17%,#e7e700 17% 83%,#d6d600 83% 91%,#b5b500 91%)}
.lo .hpb i{background:linear-gradient(#e75a4a 0 8%,#f7a494 8% 17%,#d63a20 17% 83%,#bd2921 83% 91%,#9c2121 91%)}
.heart{position:relative;background:#000}
.heart b{position:absolute;inset:1.5px;background:#3a2545}
.heart i{position:absolute;left:1.5px;top:1.5px;bottom:1.5px;width:calc((100% - 3px)*var(--heart));background:linear-gradient(#ff9cff 0 16%,#ce10ff 16% 72%,#9400bd 72%);transition:width .8s}
.heart s{position:absolute;inset:1.5px;background:linear-gradient(90deg,transparent calc(100% - 1.5px),#000 0) 0 0/20% 100%;right:calc(1.5px - 1px)}
.st{background:center/contain no-repeat}
[data-st=poison] .st{background-image:url(ui/st_poison.png)}
[data-st=burn] .st{background-image:url(ui/st_burn.png)}
[data-st=paralysis] .st{background-image:url(ui/st_paralysis.png)}
[data-st=sleep] .st{background-image:url(ui/st_sleep.png)}
[data-st=freeze] .st{background-image:url(ui/st_freeze.png)}
[data-st=fainted] .st{background-image:url(ui/st_fainted.png)}
.star{position:absolute;display:none;width:16px;height:16px;background:radial-gradient(closest-side,#fff 0 18%,transparent 19%),conic-gradient(from 0deg,#ffe97a,#fff6c8,#ffd23a,#fff6c8,#ffe97a);
  clip-path:polygon(50% 0,60% 40%,100% 50%,60% 60%,50% 100%,40% 60%,0 50%,40% 40%);filter:drop-shadow(0 0 2px #a86b00);animation:glint 4s ease-in-out infinite}
.shiny .star{display:block}
@keyframes glint{0%,60%,100%{transform:scale(.85)}70%{transform:scale(1.3) rotate(20deg)}80%{transform:scale(.9)}90%{transform:scale(1.15)}}
.g{font-size:.82em;margin-left:.28em;display:inline-block}
.g.male{color:#6cb6ff}.g.female{color:#ff8fbd}
.exp{position:relative;background:#000}
.exp b{position:absolute;inset:1.5px;background:#3a3a3a}
.exp i{position:absolute;left:1.5px;top:1.5px;bottom:1.5px;width:calc((100% - 3px)*var(--exp));background:linear-gradient(#7fc0ff 0 16%,#1073ff 16% 72%,#004ac6 72%);transition:width .8s}
body.nopieces .plate,body.nopieces .hud{background:rgba(24,24,30,.82)!important;border-image:none!important;border-radius:14px}
</style>
<style>
#team{position:absolute;left:0;right:0;bottom:0;top:0;display:flex;justify-content:center;padding:0 8px}
.m{position:relative;flex:0 0 164px;height:100%;transition:flex-basis .5s}
.rail{position:absolute;left:0;right:0;bottom:4px;height:62px;background:url(ui/railmid_silver.png) 0 0/100% 100%;z-index:3}
.m:first-child .rail{left:-4px;border-radius:26px 0 0 26px}
.m:last-child .rail{right:-4px;border-radius:0 26px 26px 0}
.rail{box-shadow:0 2px 5px rgba(0,0,0,.5), inset 0 0 0 1.5px #000;overflow:hidden}
.m+.m .rail{box-shadow:0 2px 5px rgba(0,0,0,.5), inset 0 1.5px 0 #000, inset 0 -1.5px 0 #000, inset 1px 0 0 rgba(0,0,0,.45)}
.shadow .rail{background-image:url(ui/railmid_shadow.png)} .faint .rail{background-image:url(ui/railmid_faint.png)}
.win{position:absolute;left:8px;right:8px;top:6px;bottom:6px;background:linear-gradient(#2b2b2b,#161616);border-radius:9px;box-shadow:inset 0 0 0 1px #0c0c0c, 0 1px 0 rgba(255,255,255,.25)}
.m:first-child .win{left:13px} .m:last-child .win{right:13px}
.nm{position:absolute;left:9px;right:9px;top:3px;height:24px;font-size:21px;line-height:24px}
.lv{position:absolute;left:9px;top:27px;font-size:18px;line-height:18px;white-space:pre}
.lv::before{content:'Lv ';font:13px XD1}
.hp{position:absolute;left:58px;right:9px;top:29px;height:14px}
.hp .hpb{position:absolute;inset:0}
.heart{position:absolute;left:9px;right:9px;bottom:2px;height:6px;display:none}
.shadow .heart{display:block;left:58px} .shadow .hp{top:26px;height:13px}
.num{display:none}
.st{position:absolute;left:7px;top:29px;width:49px;height:14px}
[data-st]:not([data-st=none]) .lv{display:none}
.stand{position:absolute;left:50%;bottom:53px;width:124px;height:30px;margin-left:-62px;border-radius:50%;z-index:1;
  background:radial-gradient(ellipse at 50% 42%,#3a3a3a 0 52%,#151515 54% 57%,transparent 58%),linear-gradient(90deg,#7d7d7d,#efefef 30%,#9d9d9d 55%,#e2e2e2 78%,#777);
  box-shadow:0 7px 0 -1px #4c4c4c,0 7px 0 0 #000,0 0 0 1.5px #000;transition:all .45s}
.shadow .stand{background:radial-gradient(ellipse at 50% 42%,#3a1a52 0 52%,#150a1e 54% 57%,transparent 58%),linear-gradient(90deg,#3f2160,#8a55b8 30%,#51307a 55%,#7d4bab 78%,#3a1d58);box-shadow:0 7px 0 -1px #2a1440,0 7px 0 0 #000,0 0 0 1.5px #000}
.hold{position:absolute;left:50%;bottom:67px;width:0;height:0;z-index:2;transform-origin:0 0;transition:transform .5s cubic-bezier(.2,.9,.3,1.2),filter .5s}
.mdl{position:absolute;left:0;bottom:0;transform:translateX(-50%)}
.spr{position:absolute;inset:0;background-repeat:no-repeat;background-size:var(--tw) 100%;-webkit-mask-repeat:no-repeat;mask-repeat:no-repeat;-webkit-mask-size:var(--tw) 100%;mask-size:var(--tw) 100%}
.spr.anim{animation:play var(--d) steps(var(--f)) infinite}
.spr.aura.anim{animation:play var(--d) steps(var(--f)) infinite,rise 2.4s linear infinite}
@keyframes play{to{background-position-x:calc(-1*var(--tw));-webkit-mask-position-x:calc(-1*var(--tw));mask-position-x:calc(-1*var(--tw))}}
.body{filter:drop-shadow(0 0 1px rgba(0,0,0,.75)) drop-shadow(0 2px 2px rgba(0,0,0,.35))}
.mdl.face img{position:absolute;left:0;bottom:2px;width:84px;height:84px;margin-left:-42px;border-radius:50%;box-shadow:0 0 0 2px #000,0 0 0 5px #9a9a9a,0 0 0 6.5px #000}
.mdl.face{width:0;height:0;transform:none}
.battle .m.on .mdl.face img{transform:scale(.85);transform-origin:50% 100%}
.mdl.face:empty{width:0;height:0}
.mdl.face:empty::after{content:'';position:absolute;left:-17px;bottom:4px;width:34px;height:34px;background:url(ui/ball_ok.png) center/contain no-repeat;filter:drop-shadow(0 2px 2px rgba(0,0,0,.5))}
.nopic .star{display:none}
.faint .mdl{transform:translate(-50%,36%)}
.aura{background:linear-gradient(#d46bff,#7a14d8 55%,#3d0a80);filter:blur(7px);opacity:0;animation:rise 2.4s linear infinite;transform-origin:50% 100%}
.a2{animation-delay:-.8s,-.8s!important}.a3{animation-delay:-1.6s,-1.6s!important}
.aura.anim.a2{animation-delay:0s,-.8s!important}.aura.anim.a3{animation-delay:0s,-1.6s!important}
@keyframes rise{0%{transform:scale(1.02,1.0);opacity:0}22%{opacity:.8}100%{transform:translateY(-9px) scale(1.07,1.15);opacity:0}}
.shadow .body{filter:drop-shadow(0 0 1px #2a0050) drop-shadow(0 0 5px #8a20e0) brightness(.93) saturate(.92)}
.haze{display:none;position:absolute;left:50%;bottom:58px;width:150px;height:56px;margin-left:-75px;background:radial-gradient(closest-side,rgba(150,50,255,.75),rgba(100,20,190,.35) 55%,transparent);z-index:1;animation:haze 3s ease-in-out infinite}
.shadow:not(.faint) .haze{display:block}@keyframes haze{50%{transform:scale(1.12,.86);opacity:.65}}
.faint .aura{display:none}
.clip{position:absolute;left:-40px;right:-40px;top:-40px;bottom:0;z-index:2;pointer-events:none}
.faint .clip{bottom:64px;overflow:hidden}.faint .hold{bottom:3px;transform:scale(.86)!important;filter:grayscale(1) brightness(.5) contrast(.9)}
.faint .stand{opacity:.85;filter:brightness(.5)}
.faint .nm,.faint .num{color:#c9b3ba}
.battle .m:not(.on) .hold{transform:scale(.84)} .battle .m:not(.on):not(.faint) .hold{filter:brightness(.86)}
.battle .m.on{z-index:5;flex-basis:200px} .battle .m:not(.on){flex-basis:142px}
.battle[data-out="1"] .m.on{flex-basis:216px}.battle[data-out="1"] .m:not(.on){flex-basis:152px}
.battle .m.on .hold{transform:scale(1.16)}
.battle .m.on .stand{width:158px;margin-left:-79px;height:36px;bottom:52px}
.battle .m.on .stand::after{content:'';position:absolute;inset:-5px;border-radius:50%;border:3px solid #fff;box-shadow:0 0 8px #fff8c8,inset 0 0 8px #fff8c8;animation:ring 2.2s ease-in-out infinite}
@keyframes ring{50%{opacity:.45}}
.battle .m.on .win{box-shadow:0 0 0 2.5px #fff,0 0 0 4px #000}
.battle .m.on .num{display:block;position:absolute;right:9px;top:29px;font-size:15px;line-height:15px;white-space:pre}
.battle .m.on .nm{right:62px}
.battle .m.on .lv{left:auto;right:9px;top:3px;line-height:24px}
.battle .m.on .hp{left:9px;right:84px}
.battle .m.on.shadow .heart{left:9px;right:84px}
.battle .m.on .st{left:auto;right:7px;top:8px}
.low .hp{animation:low 1.8s ease-in-out infinite}
)PG0",
R"PG1(@keyframes low{0%,100%{box-shadow:0 0 0 0 rgba(255,70,40,0)}50%{box-shadow:0 0 0 2px rgba(255,86,50,.95),0 0 9px 2px rgba(255,60,30,.6)}}
.hit .mdl{animation:shake .55s linear 1}
.hit .body::after,.lvup .body::after{content:''}
.hit .body{animation-name:play,hitflash}.hit .body{animation:play var(--d) steps(var(--f)) infinite,hitflash .6s steps(2) 1}
@keyframes hitflash{0%,50%{filter:brightness(3.4) saturate(.2)}25%,100%{filter:none}}
@keyframes shake{0%,100%{transform:translateX(-50%)}15%{transform:translateX(calc(-50% - 7px))}35%{transform:translateX(calc(-50% + 6px))}55%{transform:translateX(calc(-50% - 4px))}75%{transform:translateX(calc(-50% + 3px))}}
.fx{position:absolute;left:50%;bottom:62px;width:150px;height:150px;margin-left:-75px;z-index:4;pointer-events:none;opacity:0}
.lvup .fx{animation:lvb 1.5s ease-out 1;background:radial-gradient(closest-side,transparent 62%,rgba(255,226,110,.95) 70%,transparent 84%)}
@keyframes lvb{0%{opacity:0;transform:scale(.3,.1) translateY(60px)}25%{opacity:1}100%{opacity:0;transform:scale(1.2,.5) translateY(-90px)}}
.up{position:absolute;left:-20px;right:-20px;top:2px;text-align:center;z-index:6;font-size:24px;color:#ffe15a;opacity:0;-webkit-text-stroke:0;text-shadow:0 0 3px #000,2px 2px 0 #000,-1px -1px 0 #000,1px -1px 0 #000,-1px 1px 0 #000}
.lvup .up{animation:upw 2.4s ease-out 1}@keyframes upw{0%{opacity:0;transform:translateY(24px)}14%{opacity:1;transform:none}75%{opacity:1}100%{opacity:0;transform:translateY(-8px)}}
.heal .fx{animation:lvb 1.1s ease-out 1;background:radial-gradient(closest-side,transparent 60%,rgba(110,255,160,.85) 70%,transparent 86%)}
.sent .hold{animation:sent .7s ease-out 1}@keyframes sent{0%{transform:scale(.1);filter:brightness(6)}60%{filter:brightness(2)}100%{transform:scale(1.2)}}
.star{left:8px;top:auto;bottom:128px;width:22px;height:22px;z-index:4}
.star.s2{left:auto;right:14px;bottom:100px;width:14px;height:14px;animation-delay:-1.7s}
[data-st=poison] .body{filter:drop-shadow(0 0 1px #000) drop-shadow(0 0 4px #c040ff)}
[data-st=burn] .body{filter:drop-shadow(0 0 1px #000) drop-shadow(0 0 4px #ff7030)}
[data-st=freeze] .body{filter:drop-shadow(0 0 1px #000) drop-shadow(0 0 4px #8fe8ff) brightness(1.08)}
[data-st=paralysis] .body{filter:drop-shadow(0 0 1px #000) drop-shadow(0 0 4px #ffe13a)}
[data-st=sleep] .body{filter:drop-shadow(0 0 1px #000) brightness(.75) saturate(.8)}
.side #team{flex-direction:column;justify-content:flex-start;align-items:stretch;padding:8px 10px;left:0;right:auto;width:420px}
.side .m,.side.battle .m:not(.on),.side.battle .m.on{flex:0 0 161px;transition:none}
.side.battle .m.on{flex-basis:184px}.side.battle .m:not(.on){flex-basis:148px}
.side .m .rail,.side .m:first-child .rail,.side .m:last-child .rail{left:130px;right:0;bottom:22px;height:74px;border-radius:12px 37px 37px 12px;box-shadow:0 2px 5px rgba(0,0,0,.5),inset 0 0 0 1.5px #000}
.side .win,.side .m:first-child .win,.side .m:last-child .win{left:24px;right:22px;top:7px;bottom:7px}
.side .stand{left:74px;bottom:12px}.side .hold{left:74px;bottom:26px}.side .haze{left:74px;bottom:14px}.side .fx{left:74px;bottom:20px}
.side .clip{left:0;right:0}
.side .faint .clip{bottom:44px}.side .faint .hold{bottom:0}
.side .nm,.side.battle .m.on .nm{font-size:23px;right:72px;top:5px;left:12px}.side .lv{font-size:19px;top:5px;left:auto;right:12px;line-height:24px}.side .hp,.side .shadow .hp,.side.battle .m.on .hp{top:32px;height:14px;left:12px;right:12px}
.side .num,.side.battle .m.on .num{display:block;position:absolute;right:12px;left:auto;top:46px;font-size:15px;line-height:14px}
.side .shadow .heart,.side.battle .shadow.on .heart{left:12px;right:100px;bottom:5px;height:7px}
.side .st{top:10px;left:auto;right:12px}
.side .m .rail{left:142px!important}
.side .star{left:12px;bottom:120px}.side .star.s2{left:128px;right:auto;bottom:88px}
.side.battle .m.on .hold{transform:scale(1.04)}.side.battle .m.on .stand{width:150px;margin-left:-75px}
.side .stand,.side .hold,.side .haze,.side .fx{left:88px!important}
.side .up{left:0;right:auto;width:150px;bottom:110px}
</style></head>
<body><div id="team"></div>
<script>
(function () {
  var q = new URLSearchParams(location.search), dn = q.get('data');
  var src = dn && /^[A-Za-z0-9_-]+$/.test(dn) ? 'data/' + dn + '.js' : 'party.js';
  var last = null, prev = {}, els = {};
  var XD = window.XD = {
    q: q,
    el: function (t, c, x) { var e = document.createElement(t); if (c) e.className = c; if (x !== undefined) e.textContent = x; return e; },
    hpClass: function (m) { var p = m.hp / Math.max(1, m.max_hp); return p > .5 ? 'hi' : p > .2 ? 'mid' : 'lo'; },
    fainted: function (m) { return !m.egg && (m.status === 'fainted' || m.hp <= 0); },
    name: function (m) { return m.egg ? 'EGG' : (m.nickname || m.name || ''); },
    small: function (m) { return m.small || m.face || ''; },
    setName: function (span, m) {
      span.textContent = XD.name(m);
      if (!m.egg && (m.gender === 'male' || m.gender === 'female')) span.append(XD.el('span', 'g ' + m.gender, m.gender === 'male' ? '\u2642' : '\u2640'));
    },
    expFraction: function (m) {
      var a = m.exp_level_start, b = m.exp_next;
      if (m.egg || typeof m.exp !== 'number' || typeof a !== 'number' || typeof b !== 'number' || b <= a) return -1;
      return Math.max(0, Math.min(1, (m.exp - a) / (b - a)));
    },
    STATUS: { poison: 'poison', bad_poison: 'poison', burn: 'burn', paralysis: 'paralysis', sleep: 'sleep', freeze: 'freeze', fainted: 'fainted' },
    fit: function (e) { e.style.transform = ''; var cs = getComputedStyle(e.parentNode), w = e.parentNode.clientWidth - parseFloat(cs.paddingLeft) - parseFloat(cs.paddingRight), s = e.scrollWidth; if (s > w && w > 0) e.style.transform = 'scaleX(' + Math.max(.45, w / s) + ')'; },
    flash: function (e, c, ms) { e.classList.remove(c); void e.offsetWidth; e.classList.add(c); setTimeout(function () { e.classList.remove(c); }, ms || 900); }
  };
  window.XDParty = function (d) {
    var t = JSON.stringify(d); if (t === last) return; last = t;
    var root = XD.root, party = (d && d.party) || [], seen = {};
    document.body.classList.toggle('battle', !!(d && d.in_battle));
    document.body.dataset.n = party.length;
    document.body.dataset.out = party.filter(function (m) { return m.on_field; }).length;
    party.forEach(function (m) {
      var k = m.slot, e = els[k], old = prev[k], sp = m.species_id + '/' + m.shiny + '/' + m.egg + '/' + (m.still || '') + '/' + (m.idle ? m.idle.src : '') + '/' + (m.small || m.face || ''); seen[k] = 1;
      if (!e || e.dataset.sp !== sp) { if (e) e.remove(); e = els[k] = XD.build(m); e.dataset.sp = sp; root.append(e); old = null; }
      var tg = { on: !!m.on_field, faint: XD.fainted(m), shadow: !!m.shadow, shiny: !!m.shiny, low: !XD.fainted(m) && !m.egg && m.hp / Math.max(1, m.max_hp) <= .2 };
      Object.keys(tg).forEach(function (c) { e.classList.toggle(c, tg[c]); });
      ['hi', 'mid', 'lo'].forEach(function (c) { e.classList.toggle(c, XD.hpClass(m) === c); });
      var xf = XD.expFraction(m); e.classList.toggle('hasexp', xf >= 0);
      e.dataset.st = XD.STATUS[m.status] || 'none';
      e.style.setProperty('--hp', Math.max(0, Math.min(1, m.hp / Math.max(1, m.max_hp))));
      e.style.setProperty('--heart', m.heart_max > 0 ? m.heart / m.heart_max : 0);
      e.style.setProperty('--exp', xf < 0 ? 0 : xf);
      XD.update(e, m, party, d);
      if (old) {
        if (m.hp < old.hp) XD.flash(e, 'hit'); else if (m.hp > old.hp) XD.flash(e, 'heal', 1200);
        if (m.level > old.level) XD.flash(e, 'lvup', 2600);
        if (m.on_field && !old.on_field) XD.flash(e, 'sent', 1000);
      }
      prev[k] = JSON.parse(JSON.stringify(m));
    });
    Object.keys(els).forEach(function (k) { if (!seen[k]) { els[k].remove(); delete els[k]; delete prev[k]; } });
    if (XD.layout) XD.layout(party, d, els);
    XD.refit();
  };
  XD.refit = function () { document.querySelectorAll('.fit').forEach(XD.fit); };
  var rt = null; document.addEventListener('transitionend', function () { clearTimeout(rt); rt = setTimeout(XD.refit, 30); });
  XD.start = function () {
    var tries = 0;
    function pieces() { var p = new Image(); p.onload = function () { if (document.body.classList.contains('nopieces')) location.reload(); }; p.onerror = function () { document.body.classList.add('nopieces'); }; p.src = 'ui/small_silver.png?t=' + Date.now(); }
    pieces();
    function poll() { var o = document.getElementById('feed'); if (o) o.remove(); var s = document.createElement('script'); s.id = 'feed'; s.charset = 'utf-8'; s.src = src + (src.indexOf('?') < 0 ? '?t=' : '&t=') + Date.now(); document.head.append(s); }
    var f = document.fonts ? Promise.all(['XD0', 'XD1', 'XD2', 'XD3'].map(function (n) { return document.fonts.load('18px ' + n); })) : Promise.resolve();
    f.catch(function () {}).then(function () { if (q.get('hold')) return; poll(); if (!dn) setInterval(function () { poll(); if (document.body.classList.contains('nopieces') && ++tries % 5 === 0) pieces(); }, 1000); document.fonts && document.fonts.addEventListener && document.fonts.addEventListener('loadingdone', XD.refit); });
  };
})();
</script><script>
XD.modelOf = function (m) {
  var T = window.XDModels || {}, t = T[m.species_id + (m.shiny ? 's' : '')];
  if (t) return t;
  if (m.idle && m.idle.src && m.idle.frames > 0 && m.idle.w > 0 && m.idle.h > 0) return { src: m.idle.src, w: m.idle.w, h: m.idle.h, frames: m.idle.frames, fps: m.idle.fps || 15, r: 11 };
  if (m.still && m.still_w > 0 && m.still_h > 0) return { src: m.still, w: m.still_w, h: m.still_h, frames: 1, fps: 1, r: 11 };
  return null;
};
XD.makeModel = function (m) {
  var E = XD.el, box = E('div', 'mdl'), M = XD.q.get('nomodels') ? null : XD.modelOf(m);
  if (!M) { box.classList.add('face'); if (XD.small(m)) { var f = E('img'); f.alt = ''; f.src = XD.small(m); f.onerror = function () { this.remove(); }; box.append(f); } return box; }
  box._M = M;
  ['aura a1', 'aura a2', 'aura a3', 'body'].forEach(function (c) {
    if (c !== 'body' && !m.shadow) return;
    var s = E('div', 'spr ' + c), img = 'url(' + M.src + ')';
    if (c === 'body') s.style.backgroundImage = img; else { s.style.webkitMaskImage = img; s.style.maskImage = img; }
    s.style.setProperty('--f', M.frames); s.style.setProperty('--d', (M.frames / M.fps) + 's');
    if (M.frames > 1) s.classList.add('anim');
    box.append(s);
  });
  return box;
};
XD.sizeModel = function (box, maxW, maxH, mass) {
  var M = box._M; if (!M) return;
  var rel = Math.max(.82, Math.min(1.12, Math.pow(M.r / 11, .13)));
  var k = Math.sqrt(mass / (M.w * M.h)) * rel; k = Math.min(k, maxW / M.w, maxH / M.h);
  var w = M.w * k, h = M.h * k; box.style.width = w + 'px'; box.style.height = h + 'px';
  box.style.setProperty('--w', w + 'px'); box.style.setProperty('--tw', (w * M.frames) + 'px');
};
</script>
<script>
XD.root = document.getElementById('team');
var side = XD.q.get('shape') === 'side'; if (side) document.body.classList.add('side');
XD.build = function (m) {
  var E = XD.el, e = E('div', 'm');
  var clip = E('div', 'clip'), hold = E('div', 'hold'); e._mdl = XD.makeModel(m); if (!e._mdl._M && !XD.small(m)) e.classList.add('nopic'); hold.append(e._mdl); clip.append(hold);
  if (side) XD.sizeModel(e._mdl, 160, 128, 12000); else XD.sizeModel(e._mdl, 190, 146, 17500);
  var rail = E('div', 'rail'), win = E('div', 'win'), nm = E('div', 'nm n tx'); e._n = E('span', 'fit'); nm.append(e._n);
  e._lv = E('div', 'lv n tx'); e._num = E('div', 'num n tx'); e._up = E('div', 'up n');
  var hp = E('div', 'hp'), b = E('div', 'hpb'); b.append(E('b'), E('u'), E('i')); hp.append(b);
  var h = E('div', 'heart'); h.append(E('b'), E('i'), E('s'));
  win.append(nm, e._lv, hp, e._num, h, E('div', 'st')); rail.append(win);
  e.append(E('div', 'stand'), E('div', 'haze'), clip, rail, E('div', 'fx'), e._up, E('div', 'star'), E('div', 'star s2'));
  return e;
};
XD.update = function (e, m) {
  XD.setName(e._n, m); e._lv.textContent = m.egg ? '' : m.level; e._num.textContent = m.egg ? '' : m.hp + ' / ' + m.max_hp;
  e._up.textContent = 'Lv ' + m.level + '!';
};
XD.start();
</script></body></html>
)PG2",
nullptr};
const char* const kPage_belt[] = {
R"PG0(<!DOCTYPE html>
<!-- Pokemon XD PC port: stream overlay page, look: Battle belt. OBS: Sources > + > Browser, tick "Local file", pick this file, width 1000, height 260.
  The game's pictures (ui/) and letters (font/) next to it are made from your own disc the first time the overlay runs.
  Data: party.js (window.XDParty({...})), format version 2 (docs/STREAM_OVERLAY.md). No internet address anywhere in this page.
  The program rewrites overlay_belt.html only while its first line is its own version marker; to make your own look copy this
  file to my_overlay.html (any other name) and change the copy: the program never touches other files. -->
<html lang="en"><head><meta charset="utf-8"><title>Party - Battle belt</title>
<style>
@font-face{font-family:XD0;src:url(font/xd0.ttf)}
@font-face{font-family:XD1;src:url(font/xd1.ttf)}
@font-face{font-family:XD2;src:url(font/xd2.ttf)}
@font-face{font-family:XD3;src:url(font/xd3.ttf)}
html,body{margin:0;background:transparent;overflow:hidden;height:100%}
*{box-sizing:border-box}
body{font-family:XD1,'Segoe UI','Yu Gothic UI','Noto Sans JP',sans-serif;color:#fff}
.n{font-family:XD3,'Segoe UI','Yu Gothic UI','Noto Sans JP',sans-serif}
.tx{text-shadow:1.2px 1.2px 0 #000,0 0 2px rgba(0,0,0,.7)}
.fit{display:inline-block;white-space:nowrap;transform-origin:0 50%}
.hpb{position:relative;background:#000}
.hpb b{position:absolute;inset:2px;background:linear-gradient(#4a4a4a 82%,#737373 82%)}
.hpb i,.hpb u{position:absolute;left:2px;top:2px;bottom:2px;width:calc((100% - 4px)*var(--hp));transition:width .6s linear}
.hpb u{background:#ffd9d0;transition:width .5s ease-out .7s}
.hi .hpb i{background:linear-gradient(#10ff42 0 8%,#7bff94 8% 17%,#00e729 17% 83%,#00d629 83% 91%,#00b521 91%)}
.mid .hpb i{background:linear-gradient(#ffff10 0 8%,#ffff7b 8% 17%,#e7e700 17% 83%,#d6d600 83% 91%,#b5b500 91%)}
.lo .hpb i{background:linear-gradient(#e75a4a 0 8%,#f7a494 8% 17%,#d63a20 17% 83%,#bd2921 83% 91%,#9c2121 91%)}
.heart{position:relative;background:#000}
.heart b{position:absolute;inset:1.5px;background:#3a2545}
.heart i{position:absolute;left:1.5px;top:1.5px;bottom:1.5px;width:calc((100% - 3px)*var(--heart));background:linear-gradient(#ff9cff 0 16%,#ce10ff 16% 72%,#9400bd 72%);transition:width .8s}
.heart s{position:absolute;inset:1.5px;background:linear-gradient(90deg,transparent calc(100% - 1.5px),#000 0) 0 0/20% 100%;right:calc(1.5px - 1px)}
.st{background:center/contain no-repeat}
[data-st=poison] .st{background-image:url(ui/st_poison.png)}
[data-st=burn] .st{background-image:url(ui/st_burn.png)}
[data-st=paralysis] .st{background-image:url(ui/st_paralysis.png)}
[data-st=sleep] .st{background-image:url(ui/st_sleep.png)}
[data-st=freeze] .st{background-image:url(ui/st_freeze.png)}
[data-st=fainted] .st{background-image:url(ui/st_fainted.png)}
.star{position:absolute;display:none;width:16px;height:16px;background:radial-gradient(closest-side,#fff 0 18%,transparent 19%),conic-gradient(from 0deg,#ffe97a,#fff6c8,#ffd23a,#fff6c8,#ffe97a);
  clip-path:polygon(50% 0,60% 40%,100% 50%,60% 60%,50% 100%,40% 60%,0 50%,40% 40%);filter:drop-shadow(0 0 2px #a86b00);animation:glint 4s ease-in-out infinite}
.shiny .star{display:block}
@keyframes glint{0%,60%,100%{transform:scale(.85)}70%{transform:scale(1.3) rotate(20deg)}80%{transform:scale(.9)}90%{transform:scale(1.15)}}
.g{font-size:.82em;margin-left:.28em;display:inline-block}
.g.male{color:#6cb6ff}.g.female{color:#ff8fbd}
.exp{position:relative;background:#000}
.exp b{position:absolute;inset:1.5px;background:#3a3a3a}
.exp i{position:absolute;left:1.5px;top:1.5px;bottom:1.5px;width:calc((100% - 3px)*var(--exp));background:linear-gradient(#7fc0ff 0 16%,#1073ff 16% 72%,#004ac6 72%);transition:width .8s}
body.nopieces .plate,body.nopieces .hud{background:rgba(24,24,30,.82)!important;border-image:none!important;border-radius:14px}
</style>
<style>
#team{position:absolute;left:4px;right:4px;bottom:4px;height:252px;display:flex;align-items:flex-end;gap:4px}
.m{position:relative;flex:0 0 162px;height:58px;transition:flex-basis .45s cubic-bezier(.3,.9,.4,1),height .45s}
.plate{position:absolute;inset:0;border:solid transparent;border-width:0 24px 0 56px;border-image:url(ui/small_silver.png) 0 90 0 186 fill/0 24px 0 56px}
.shadow .plate{border-image-source:url(ui/small_shadow.png)}.faint .plate{border-image-source:url(ui/small_faint.png)}
.plate{filter:drop-shadow(0 2px 3px rgba(0,0,0,.5))}
.hole{position:absolute;left:8px;top:9px;width:40px;height:40px;border-radius:50%;background:#000;overflow:hidden}
.hole img{width:100%;height:100%;display:block}
.hole:empty{background:#000 url(ui/ball_ok.png) center/58% no-repeat}

.faint .hole img{filter:grayscale(1) brightness(.6)}
.nm{position:absolute;left:60px;right:20px;top:6px;height:22px;font-size:18px;line-height:22px}
.lv{position:absolute;left:60px;top:29px;font-size:16px;line-height:18px;white-space:pre}.lv::before{content:'Lv';font:11px XD1;margin-right:2px}
.hp{position:absolute;left:98px;right:16px;top:32px;height:12px}.hpb{position:absolute;inset:0}
.heart{position:absolute;left:98px;right:16px;top:45px;height:6px;display:none}.shadow .heart{display:block}.shadow .hp{top:30px}
.st{position:absolute;left:58px;top:31px;width:38px;height:13px}[data-st]:not([data-st=none]) .lv{visibility:hidden}
.num,.hud,.lip,.hh,.pop,.ringbg,.under,.back,.exp,.xl{display:none}
.star{left:40px;top:1px;width:18px;height:18px;z-index:9}
.low .hp{animation:low 1.8s ease-in-out infinite}
@keyframes low{0%,100%{box-shadow:0 0 0 0 rgba(255,70,40,0)}50%{box-shadow:0 0 0 2px rgba(255,86,50,.95),0 0 9px 2px rgba(255,60,30,.6)}}
.shadow:not(.faint)::before{content:'';position:absolute;inset:-1px;border-radius:30px 12px 12px 30px;box-shadow:0 0 7px 2px rgba(122,30,200,.75)}
.battle .m.on{order:1;flex-basis:300px;height:71px;margin-right:12px;z-index:4}
.battle .m.on.shadow{margin-bottom:8px}
.battle .m.on .plate,.battle .m.on .hole{display:none}
.battle .m.on .hud{display:block;position:absolute;left:0;bottom:0;width:300px;height:71px;background:url(ui/hud_silver.png) 0 0/100% 100%;z-index:1;filter:drop-shadow(0 2px 3px rgba(0,0,0,.5))}
.battle .m.on.shadow .hud{background-image:url(ui/hud_shadow.png)}
.battle .m.on.hasexp:not(.shadow) .lip{display:block;position:absolute;left:0;bottom:-8px;width:245.5px;height:30px;background:url(ui/lip_silver.png) 0 0/100% 100%;z-index:1}
.battle .m.on.hasexp.faint:not(.shadow) .lip{background-image:url(ui/lip_faint.png)}
.battle .m.on.hasexp:not(.shadow) .exp{display:block;position:absolute;left:52px;bottom:-3px;right:76px;height:8px;z-index:2}
.battle .m.on.hasexp:not(.shadow) .xl{display:block;position:absolute;left:18px;bottom:-4px;width:30px;height:12px;background:url(ui/explabel.png) center/30px 12px no-repeat;z-index:2}
.battle .m.on.hasexp:not(.shadow){margin-bottom:8px}
.battle .m.on.shadow .lip{display:block;position:absolute;left:0;bottom:-8px;width:245.5px;height:30px;background:url(ui/lip_shadow.png) 0 0/100% 100%;z-index:1}
.battle .m.on.shadow .hh{display:block;position:absolute;left:18px;bottom:-5px;width:12px;height:11px;background:#ff8cff;z-index:2;clip-path:path('M6 11 L.5 5.2 A3.1 3.1 0 0 1 6 1.9 A3.1 3.1 0 0 1 11.5 5.2 Z')}
.battle .m.on.shadow::before{inset:auto;left:-2px;bottom:-10px;width:304px;height:83px;border-radius:18px 38px 38px 18px;box-shadow:0 0 8px 2px rgba(122,30,200,.7)}
.battle .m.on .back{display:block;position:absolute;left:236.5px;bottom:7.5px;width:56px;height:56px;border-radius:50%;background:radial-gradient(circle at 50% 35%,#4e4e58,#0b0b0e 75%);z-index:0}
.battle .m.on.shadow .back{background:radial-gradient(circle at 50% 35%,#7b2fc4,#12041f 78%)}
.battle .m.on .nm{left:20px;right:96px;top:auto;bottom:43px;height:24px;font-size:24px;line-height:24px;z-index:2}
.battle .m.on .hp{left:18px;right:73px;top:auto;bottom:25px;height:16px;z-index:2}
.battle .m.on .hp::before{content:'';position:absolute;left:0;top:0;width:34px;height:16px;background:#000 url(ui/hplabel.png) center/34px 15px no-repeat}
.battle .m.on .hp .hpb{left:33px}
.battle .m.on .num{display:block;position:absolute;left:40px;bottom:5px;font-size:20px;line-height:20px;z-index:2;white-space:pre}
.battle .m.on .lv{left:auto;right:78px;top:auto;bottom:5px;font-size:20px;line-height:20px;z-index:2;visibility:visible}.battle .m.on .lv::before{font:15px XD1;margin-right:4px}
.battle .m.on[data-st]:not([data-st=none]) .lv{visibility:hidden}
.battle .m.on .st{left:auto;right:76px;top:auto;bottom:7px;width:54px;height:16px;z-index:2}
.battle .m.on .heart{left:41px;right:76px;top:auto;bottom:-3px;height:8px;z-index:2}
.battle .m.on .star{left:auto;right:6px;top:-6px}
.battle .m.on .pop{display:block;position:absolute;left:140.5px;bottom:7.5px;width:248px;height:190px;z-index:3;overflow:hidden;
  -webkit-mask:linear-gradient(#000 0 0) top/100% calc(100% - 28px) no-repeat,radial-gradient(circle 28.5px at 124px calc(100% - 28px),#000 98%,transparent) no-repeat;
  mask:linear-gradient(#000 0 0) top/100% calc(100% - 28px) no-repeat,radial-gradient(circle 28.5px at 124px calc(100% - 28px),#000 98%,transparent) no-repeat}
.hold{position:absolute;left:50%;bottom:2px}
.mdl{position:absolute;left:0;bottom:0;transform:translateX(-50%)}
.spr{position:absolute;inset:0;background-repeat:no-repeat;background-size:var(--tw) 100%;-webkit-mask-repeat:no-repeat;mask-repeat:no-repeat;-webkit-mask-size:var(--tw) 100%;mask-size:var(--tw) 100%}
.spr.anim{animation:play var(--d) steps(var(--f)) infinite}
@keyframes play{to{background-position-x:calc(-1*var(--tw));-webkit-mask-position-x:calc(-1*var(--tw));mask-position-x:calc(-1*var(--tw))}}
.body{filter:drop-shadow(0 0 1px rgba(0,0,0,.8)) drop-shadow(0 2px 2px rgba(0,0,0,.4))}
.aura{background:linear-gradient(#d46bff,#7a14d8 55%,#3d0a80);filter:blur(6px);opacity:0;animation:rise 2.4s linear infinite;transform-origin:50% 100%}
.spr.aura.anim{animation:play var(--d) steps(var(--f)) infinite,rise 2.4s linear infinite}
.a2{animation-delay:-.8s!important}.a3{animation-delay:-1.6s!important}
.spr.aura.anim.a2{animation-delay:0s,-.8s!important}.spr.aura.anim.a3{animation-delay:0s,-1.6s!important}
)PG0",
R"PG1(@keyframes rise{0%{transform:scale(1.02,1.0);opacity:0}22%{opacity:.8}100%{transform:translateY(-9px) scale(1.07,1.15);opacity:0}}
.shadow .body{filter:drop-shadow(0 0 1px #2a0050) drop-shadow(0 0 5px #8a20e0) brightness(.93) saturate(.92)}
.mdl.face{display:none}
.battle .m.on.nomodel .hole{display:block;left:237px;top:auto;bottom:8px;width:55px;height:55px;z-index:2}
.battle .m:not(.on){order:2;flex:0 0 78px;height:86px}
.battle .m:not(.on) .plate,.battle .m:not(.on) .hp,.battle .m:not(.on) .heart,.battle .m:not(.on)::before{display:none}
.battle .m:not(.on) .ringbg{display:block;position:absolute;left:7px;top:0;width:64px;height:64px;border-radius:50%;
  background:radial-gradient(closest-side,transparent 70%,#000 71% 73%,transparent 74%),radial-gradient(closest-side,transparent 94%,#000 95%),conic-gradient(var(--hc) calc(var(--hp)*360deg),#3c3c3c 0);filter:drop-shadow(0 2px 3px rgba(0,0,0,.6))}
.hi{--hc:#00e729}.mid{--hc:#e7e700}.lo{--hc:#e0442a}
.battle .m.shadow:not(.on) .ringbg{box-shadow:0 0 0 3px #5a2a84,0 0 8px 3px rgba(122,30,200,.75)}
.battle .m:not(.on) .hole{left:16px;top:9px;width:46px;height:46px}
.battle .m:not(.on) .nm{left:0;right:0;top:68px;height:18px;font-size:15px;line-height:18px;text-align:center;background:rgba(16,16,16,.86);border-radius:9px;padding:0 5px}
.battle .m:not(.on) .nm .fit{transform-origin:50% 50%}
.battle .m:not(.on) .lv{display:none}
.battle .m:not(.on) .st{left:17px;top:51px;width:44px;height:13px}
.battle .m:not(.on) .star{left:56px;top:0}
.battle .m.low:not(.on) .ringbg{animation:lowr 1.8s ease-in-out infinite}
@keyframes lowr{50%{filter:drop-shadow(0 0 6px #ff4a28) drop-shadow(0 0 2px #ff4a28)}}
.hit .pop,.hit .hole{animation:hf .55s steps(2) 1}@keyframes hf{50%{filter:brightness(2.8)}}
.sent .pop{animation:snt .6s cubic-bezier(.2,.9,.3,1.3)}@keyframes snt{0%{transform:translateY(60px) scale(.3);opacity:0}}
.side #team{flex-direction:column;align-items:flex-start;justify-content:flex-start;left:8px;top:8px;height:auto;bottom:auto;gap:7px}
.side .m{flex:0 0 58px;width:196px;height:58px}
.side.battle .m.on{flex:0 0 71px;width:300px;margin:72px 0 6px}.side.battle .m.on.shadow{margin-bottom:16px}
.side.battle .m:not(.on){flex:0 0 58px;width:196px;height:58px}
.side.battle .m:not(.on) .plate,.side.battle .m:not(.on) .hp{display:block}.side.battle .m:not(.on) .ringbg{display:none}
.side.battle .m:not(.on) .hole{left:8px;top:9px;width:40px;height:40px}
.side.battle .m:not(.on) .nm{left:60px;right:20px;top:6px;height:22px;font-size:18px;line-height:22px;text-align:left;background:none;padding:0}
.side.battle .m:not(.on) .lv{display:block}.side.battle .m:not(.on) .st{left:58px;top:31px;width:38px}.side.battle .m:not(.on) .star{left:40px;top:1px}
</style></head>
<body><div id="team"></div>
<script>
(function () {
  var q = new URLSearchParams(location.search), dn = q.get('data');
  var src = dn && /^[A-Za-z0-9_-]+$/.test(dn) ? 'data/' + dn + '.js' : 'party.js';
  var last = null, prev = {}, els = {};
  var XD = window.XD = {
    q: q,
    el: function (t, c, x) { var e = document.createElement(t); if (c) e.className = c; if (x !== undefined) e.textContent = x; return e; },
    hpClass: function (m) { var p = m.hp / Math.max(1, m.max_hp); return p > .5 ? 'hi' : p > .2 ? 'mid' : 'lo'; },
    fainted: function (m) { return !m.egg && (m.status === 'fainted' || m.hp <= 0); },
    name: function (m) { return m.egg ? 'EGG' : (m.nickname || m.name || ''); },
    small: function (m) { return m.small || m.face || ''; },
    setName: function (span, m) {
      span.textContent = XD.name(m);
      if (!m.egg && (m.gender === 'male' || m.gender === 'female')) span.append(XD.el('span', 'g ' + m.gender, m.gender === 'male' ? '\u2642' : '\u2640'));
    },
    expFraction: function (m) {
      var a = m.exp_level_start, b = m.exp_next;
      if (m.egg || typeof m.exp !== 'number' || typeof a !== 'number' || typeof b !== 'number' || b <= a) return -1;
      return Math.max(0, Math.min(1, (m.exp - a) / (b - a)));
    },
    STATUS: { poison: 'poison', bad_poison: 'poison', burn: 'burn', paralysis: 'paralysis', sleep: 'sleep', freeze: 'freeze', fainted: 'fainted' },
    fit: function (e) { e.style.transform = ''; var cs = getComputedStyle(e.parentNode), w = e.parentNode.clientWidth - parseFloat(cs.paddingLeft) - parseFloat(cs.paddingRight), s = e.scrollWidth; if (s > w && w > 0) e.style.transform = 'scaleX(' + Math.max(.45, w / s) + ')'; },
    flash: function (e, c, ms) { e.classList.remove(c); void e.offsetWidth; e.classList.add(c); setTimeout(function () { e.classList.remove(c); }, ms || 900); }
  };
  window.XDParty = function (d) {
    var t = JSON.stringify(d); if (t === last) return; last = t;
    var root = XD.root, party = (d && d.party) || [], seen = {};
    document.body.classList.toggle('battle', !!(d && d.in_battle));
    document.body.dataset.n = party.length;
    document.body.dataset.out = party.filter(function (m) { return m.on_field; }).length;
    party.forEach(function (m) {
      var k = m.slot, e = els[k], old = prev[k], sp = m.species_id + '/' + m.shiny + '/' + m.egg + '/' + (m.still || '') + '/' + (m.idle ? m.idle.src : '') + '/' + (m.small || m.face || ''); seen[k] = 1;
      if (!e || e.dataset.sp !== sp) { if (e) e.remove(); e = els[k] = XD.build(m); e.dataset.sp = sp; root.append(e); old = null; }
      var tg = { on: !!m.on_field, faint: XD.fainted(m), shadow: !!m.shadow, shiny: !!m.shiny, low: !XD.fainted(m) && !m.egg && m.hp / Math.max(1, m.max_hp) <= .2 };
      Object.keys(tg).forEach(function (c) { e.classList.toggle(c, tg[c]); });
      ['hi', 'mid', 'lo'].forEach(function (c) { e.classList.toggle(c, XD.hpClass(m) === c); });
      var xf = XD.expFraction(m); e.classList.toggle('hasexp', xf >= 0);
      e.dataset.st = XD.STATUS[m.status] || 'none';
      e.style.setProperty('--hp', Math.max(0, Math.min(1, m.hp / Math.max(1, m.max_hp))));
      e.style.setProperty('--heart', m.heart_max > 0 ? m.heart / m.heart_max : 0);
      e.style.setProperty('--exp', xf < 0 ? 0 : xf);
      XD.update(e, m, party, d);
      if (old) {
        if (m.hp < old.hp) XD.flash(e, 'hit'); else if (m.hp > old.hp) XD.flash(e, 'heal', 1200);
        if (m.level > old.level) XD.flash(e, 'lvup', 2600);
        if (m.on_field && !old.on_field) XD.flash(e, 'sent', 1000);
      }
      prev[k] = JSON.parse(JSON.stringify(m));
    });
    Object.keys(els).forEach(function (k) { if (!seen[k]) { els[k].remove(); delete els[k]; delete prev[k]; } });
    if (XD.layout) XD.layout(party, d, els);
    XD.refit();
  };
  XD.refit = function () { document.querySelectorAll('.fit').forEach(XD.fit); };
  var rt = null; document.addEventListener('transitionend', function () { clearTimeout(rt); rt = setTimeout(XD.refit, 30); });
  XD.start = function () {
    var tries = 0;
    function pieces() { var p = new Image(); p.onload = function () { if (document.body.classList.contains('nopieces')) location.reload(); }; p.onerror = function () { document.body.classList.add('nopieces'); }; p.src = 'ui/small_silver.png?t=' + Date.now(); }
    pieces();
    function poll() { var o = document.getElementById('feed'); if (o) o.remove(); var s = document.createElement('script'); s.id = 'feed'; s.charset = 'utf-8'; s.src = src + (src.indexOf('?') < 0 ? '?t=' : '&t=') + Date.now(); document.head.append(s); }
    var f = document.fonts ? Promise.all(['XD0', 'XD1', 'XD2', 'XD3'].map(function (n) { return document.fonts.load('18px ' + n); })) : Promise.resolve();
    f.catch(function () {}).then(function () { if (q.get('hold')) return; poll(); if (!dn) setInterval(function () { poll(); if (document.body.classList.contains('nopieces') && ++tries % 5 === 0) pieces(); }, 1000); document.fonts && document.fonts.addEventListener && document.fonts.addEventListener('loadingdone', XD.refit); });
  };
})();
</script><script>
XD.modelOf = function (m) {
  var T = window.XDModels || {}, t = T[m.species_id + (m.shiny ? 's' : '')];
  if (t) return t;
  if (m.idle && m.idle.src && m.idle.frames > 0 && m.idle.w > 0 && m.idle.h > 0) return { src: m.idle.src, w: m.idle.w, h: m.idle.h, frames: m.idle.frames, fps: m.idle.fps || 15, r: 11 };
  if (m.still && m.still_w > 0 && m.still_h > 0) return { src: m.still, w: m.still_w, h: m.still_h, frames: 1, fps: 1, r: 11 };
  return null;
};
XD.makeModel = function (m) {
  var E = XD.el, box = E('div', 'mdl'), M = XD.q.get('nomodels') ? null : XD.modelOf(m);
  if (!M) { box.classList.add('face'); if (XD.small(m)) { var f = E('img'); f.alt = ''; f.src = XD.small(m); f.onerror = function () { this.remove(); }; box.append(f); } return box; }
  box._M = M;
  ['aura a1', 'aura a2', 'aura a3', 'body'].forEach(function (c) {
    if (c !== 'body' && !m.shadow) return;
    var s = E('div', 'spr ' + c), img = 'url(' + M.src + ')';
    if (c === 'body') s.style.backgroundImage = img; else { s.style.webkitMaskImage = img; s.style.maskImage = img; }
    s.style.setProperty('--f', M.frames); s.style.setProperty('--d', (M.frames / M.fps) + 's');
    if (M.frames > 1) s.classList.add('anim');
    box.append(s);
  });
  return box;
};
XD.sizeModel = function (box, maxW, maxH, mass) {
)PG1",
R"PG2(  var M = box._M; if (!M) return;
  var rel = Math.max(.82, Math.min(1.12, Math.pow(M.r / 11, .13)));
  var k = Math.sqrt(mass / (M.w * M.h)) * rel; k = Math.min(k, maxW / M.w, maxH / M.h);
  var w = M.w * k, h = M.h * k; box.style.width = w + 'px'; box.style.height = h + 'px';
  box.style.setProperty('--w', w + 'px'); box.style.setProperty('--tw', (w * M.frames) + 'px');
};
</script>
<script>
XD.root = document.getElementById('team');
if (XD.q.get('shape') === 'side') document.body.classList.add('side');
XD.build = function (m) {
  var E = XD.el, e = E('div', 'm'), hole = E('div', 'hole');
  if (XD.small(m)) { var i = E('img'); i.alt = ''; i.src = XD.small(m); i.onerror = function () { this.remove(); }; hole.append(i); }
  var pop = E('div', 'pop'), hold = E('div', 'hold'); e._mdl = XD.makeModel(m); if (!e._mdl._M) e.classList.add('nomodel');
  XD.sizeModel(e._mdl, 175, 165, 18500); hold.append(e._mdl); pop.append(hold);
  var nm = E('div', 'nm n tx'); e._n = E('span', 'fit'); nm.append(e._n);
  e._lv = E('div', 'lv n tx'); e._num = E('div', 'num n tx');
  var hp = E('div', 'hp'), b = E('div', 'hpb'); b.append(E('b'), E('u'), E('i')); hp.append(b);
  var h = E('div', 'heart'); h.append(E('b'), E('i'), E('s'));
  var x = E('div', 'exp'); x.append(E('b'), E('i'));
  e.append(E('div', 'ringbg'), hole, E('div', 'plate'), E('div', 'back'), E('div', 'hud'), E('div', 'lip'), pop, nm, e._lv, hp, e._num, h, E('div', 'hh'), E('div', 'xl'), x, E('div', 'st'), E('div', 'star'));
  return e;
};
XD.update = function (e, m) {
  XD.setName(e._n, m); e._lv.textContent = m.egg ? '' : m.level; e._num.textContent = m.egg ? '' : m.hp + ' / ' + m.max_hp;
};
XD.fitBelt = function () {
  var t = XD.root; t.style.transform = ''; t.style.width = '';
  var kids = Array.prototype.slice.call(t.children), need = 0, gap = 4;
  kids.forEach(function (c) { var cs = getComputedStyle(c); need += (parseFloat(cs.flexBasis) || c.offsetWidth) + (parseFloat(cs.marginRight) || 0); });
  need += gap * Math.max(0, kids.length - 1);
  var avail = document.documentElement.clientWidth - 8;
  if (!document.body.classList.contains('side') && need > avail) { t.style.width = need + 'px'; t.style.right = 'auto'; t.style.transformOrigin = '0 100%'; t.style.transform = 'scale(' + (avail / need) + ')'; }
};
(function () { var p = window.XDParty; window.XDParty = function (d) { p(d); XD.fitBelt(); }; })();
XD.start();
</script></body></html>
)PG2",
nullptr};
