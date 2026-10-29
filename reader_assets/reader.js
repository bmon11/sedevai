// reader.js - Сэдэв.мн reader site, all of it in one file with globals (port of sections/home,
// billboard, edition, reader, chat, audio + pwa + motion setting). No framework: strings of HTML
// glued together and innerHTML'd. Pages call homeInit(), editionPageInit(id), deckInit(id).

// ---------------------------------------------------------------------- helpers

var CATS = {
  politics: { label: 'Улс төр', color: 'primary', hex: '#007BFF', icon: 'solar:buildings-2-bold-duotone' },
  economy: { label: 'Эдийн засаг', color: 'warning', hex: '#0A7E68', icon: 'solar:chart-2-bold-duotone' },
  society: { label: 'Нийгэм', color: 'info', hex: '#D97706', icon: 'solar:users-group-rounded-bold-duotone' },
  world: { label: 'Дэлхий', color: 'secondary', hex: '#6366F1', icon: 'solar:globus-bold-duotone' },
  sports: { label: 'Спорт', color: 'success', hex: '#0284C7', icon: 'solar:cup-star-bold-duotone' },
  culture: { label: 'Соёл', color: 'error', hex: '#E11D48', icon: 'solar:pallete-2-bold-duotone' },
  tech: { label: 'Технологи', color: 'info', hex: '#0891B2', icon: 'solar:cpu-bolt-bold-duotone' },
};
// theme palette main -> darker, for the photo-less tile gradients
var PAL = {
  primary: ['#007BFF', '#0B2A66'], secondary: ['#E50914', '#7A050B'], info: ['#00B8D9', '#003768'],
  success: ['#22C55E', '#065E49'], warning: ['#FFAB00', '#7A4100'], error: ['#FF5630', '#7A0916'],
};
function catKey(k) { return CATS[k] ? k : 'society'; }
function cat(k) { return CATS[catKey(k)]; }

function esc(s) {
  return String(s == null ? '' : s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;').replace(/'/g, '&#39;');
}
function ico(name, w, style) {
  return '<iconify-icon icon="' + name + '" width="' + (w || 20) + '" height="' + (w || 20) + '"' + (style ? ' style="' + style + '"' : '') + '></iconify-icon>';
}

/** GET/POST same-origin JSON; resolves null on 404 (like serverGet), rejects with Error(message) otherwise. */
function api(path, opts) {
  opts = opts || {};
  var init = { credentials: 'same-origin', method: opts.method || 'GET', headers: {} };
  if (opts.body !== undefined) { init.body = JSON.stringify(opts.body); init.headers['Content-Type'] = 'application/json'; }
  return fetch(path, init).then(function (res) {
    if (res.status === 404 && !opts.strict) return null;
    return res.text().then(function (txt) {
      var data = null;
      try { data = txt ? JSON.parse(txt) : null; } catch (e) { data = null; }
      if (!res.ok) {
        var err = new Error((data && data.error) || 'Алдаа гарлаа');
        err.status = res.status;
        throw err;
      }
      return data;
    });
  });
}
function apiSoft(path) { return api(path).catch(function () { return null; }); }

var UB_OFFSET_MS = 8 * 3600 * 1000;
function pad(n) { return String(n).padStart(2, '0'); }
function fDateTimeUB(iso) {
  if (!iso) return '';
  var d = new Date(new Date(iso).getTime() + UB_OFFSET_MS);
  if (isNaN(d)) return '';
  return d.getUTCFullYear() + '.' + pad(d.getUTCMonth() + 1) + '.' + pad(d.getUTCDate()) + ' ' + pad(d.getUTCHours()) + ':' + pad(d.getUTCMinutes());
}
function fWindow(w) { return w ? (w.start + ' – ' + w.end) : ''; }
function fAgo(iso) {
  var minutes = Math.floor((Date.now() - new Date(iso).getTime()) / 60000);
  if (minutes < 1) return 'Дөнгөж сая';
  if (minutes < 60) return minutes + ' минутын өмнө';
  var hours = Math.floor(minutes / 60);
  if (hours < 24) return hours + ' цагийн өмнө';
  return Math.floor(hours / 24) + ' өдрийн өмнө';
}
function fDayParts(date) {
  if (!/^\d{4}-\d{2}-\d{2}$/.test(date || '')) return { day: date || '—', month: '' };
  var a = date.split('-');
  return { day: a[2], month: Number(a[1]) + ' сар' };
}
function emptyHtml(title, desc, filled) {
  return '<div class="empty' + (filled ? ' filled' : '') + '"><img src="/assets/icons/empty/ic-content.svg" alt=""><h6>' + esc(title) + '</h6>' + (desc ? '<p>' + esc(desc) + '</p>' : '') + '</div>';
}
var LOADING = '<div class="lin" style="margin:24px 0"></div>';

// ---------------------------------------------------------------------- motion setting («Хөдөлгөөн»)

var SETTINGS_KEY = 'app-settings';
function readSettings() { try { return JSON.parse(localStorage.getItem(SETTINGS_KEY) || '{}') || {}; } catch (e) { return {}; } }
function motionOn() { return readSettings().motion !== false; }
function setMotion(on) {
  var s = readSettings(); s.motion = !!on;
  try { localStorage.setItem(SETTINGS_KEY, JSON.stringify(s)); } catch (e) { /* storage blocked */ }
  applyMotion();
}
function applyMotion() {
  var on = motionOn();
  document.body.classList.toggle('still', !on);
  var b = document.getElementById('motion-btn');
  if (b) {
    b.innerHTML = ico(on ? 'solar:play-circle-linear' : 'solar:pause-circle-linear', 22);
    b.title = 'Хөдөлгөөн: ' + (on ? 'асаалттай' : 'унтраалттай');
    b.setAttribute('aria-pressed', on ? 'true' : 'false');
  }
}

// ---------------------------------------------------------------------- PWA: service worker + install prompt

var INSTALL_KEY = 'sedev:install-dismissed';
var IN_APP = /FBAN|FBAV|FB_IAB|FBIOS|Messenger|Instagram|Line\/|KAKAOTALK/i;
var installEvent = null;
function installPlatform() {
  var ua = navigator.userAgent;
  var ios = /iPhone|iPad|iPod/.test(ua) || (/Macintosh/.test(ua) && navigator.maxTouchPoints > 1);
  if (IN_APP.test(ua)) return ios ? 'in-app-ios' : 'in-app-android';
  if (ios) return 'ios';
  return 'prompt';
}
function installDismissedRecently() {
  try { var at = Number(localStorage.getItem(INSTALL_KEY)); return at && Date.now() - at < 14 * 86400000; } catch (e) { return false; }
}
function showInstall(platform) {
  if (document.getElementById('inst')) return;
  var body, action = '';
  if (platform === 'ios') {
    body = 'Доор байрлах ' + ico('material-symbols:ios-share', 18, 'vertical-align:text-bottom;color:var(--primary)') + ' <b>Share</b> товчийг дараад <b>«Add to Home Screen»</b> (Нүүр дэлгэцэд нэмэх)-ийг сонгоорой.';
  } else if (platform.indexOf('in-app') === 0) {
    body = 'Messenger, Facebook доторх хөтөч апп суулгаж чаддаггүй. ' + (platform === 'in-app-ios' ? 'Safari' : 'Chrome') + '-оор нээгээд нүүр дэлгэцэндээ нэмээрэй.';
    var l = location;
    var href = platform === 'in-app-ios' ? 'x-safari-https://' + l.host + l.pathname + l.search : 'intent://' + l.host + l.pathname + l.search + '#Intent;scheme=https;package=com.android.chrome;end';
    action = '<a class="btn btn-contained" href="' + esc(href) + '">' + (platform === 'in-app-ios' ? 'Safari-д нээх' : 'Chrome-д нээх') + ' ' + ico('eva:external-link-fill', 18) + '</a>';
  } else {
    body = 'Нүүр дэлгэцнээс нэг товшоод нээнэ. Бүтэн дэлгэцээр, апп шиг хурдан.';
    if (installEvent) action = '<button class="btn btn-contained" id="inst-go">Суулгах</button>';
  }
  var d = document.createElement('div');
  d.className = 'inst'; d.id = 'inst';
  d.innerHTML = '<div class="card"><div style="gap:12px;display:flex;align-items:center"><img src="/pwa/icon-192.png" alt=""><p class="sub1" style="flex:1">Сэдэв.мн-г утсандаа суулгах</p><button class="ibtn sm" aria-label="Хаах" data-x>' + ico('mingcute:close-line', 20) + '</button></div>' +
    '<p class="body2 muted" style="margin:0">' + body + '</p><div style="gap:8px;display:flex;justify-content:flex-end"><button class="btn" data-x>Дараа</button>' + action + '</div></div>';
  document.body.appendChild(d);
  d.querySelectorAll('[data-x]').forEach(function (b) {
    b.onclick = function () { d.remove(); try { localStorage.setItem(INSTALL_KEY, String(Date.now())); } catch (e) { /* again next visit */ } };
  });
  var go = d.querySelector('#inst-go');
  if (go) go.onclick = function () { installEvent.prompt(); installEvent.userChoice.then(function () { installEvent = null; d.remove(); }); };
}
function initInstallPrompt() {
  var phone = window.matchMedia('(pointer: coarse)').matches;
  var installed = window.matchMedia('(display-mode: standalone)').matches || window.navigator.standalone === true;
  if (!phone || installed || installDismissedRecently()) return;
  var p = installPlatform();
  if (p === 'prompt') {
    window.addEventListener('beforeinstallprompt', function (e) {
      e.preventDefault(); installEvent = e;
      setTimeout(function () { showInstall('prompt'); }, 4000);
    });
  } else setTimeout(function () { showInstall(p); }, 4000);
}

function readerBoot(opts) {
  applyMotion();
  var mb = document.getElementById('motion-btn');
  if (mb) mb.onclick = function () { setMotion(!motionOn()); };
  var hdr = document.querySelector('.hdr');
  if (hdr) window.addEventListener('scroll', function () { hdr.classList.toggle('scrolled', window.scrollY > 0); }, { passive: true });
  if ('serviceWorker' in navigator) navigator.serviceWorker.register('/sw.js').catch(function () { /* not installable then */ });
  if (!opts || opts.install !== false) initInstallPrompt();
}

// ---------------------------------------------------------------------- ranking (billboard/ranking.js)

var BILLBOARD_SIZE = 20;
var FEED_PAGE_SIZE = 20;
function rankTopics(topics) {
  return (topics || []).map(function (topic, index) { return { topic: topic, index: index }; })
    .sort(function (a, b) { return ((b.topic.hot || 0) - (a.topic.hot || 0)) || (a.index - b.index); })
    .map(function (x, i) { return Object.assign({}, x.topic, { rank: i + 1 }); });
}
function hotScore(t) { return Math.round((t.hot || 0) * 100); }

// ---------------------------------------------------------------------- rank move badge (rank-move.jsx; CSS spring instead of Remotion)

function rankMove(t) {
  if (t.previousRank === undefined) return null;
  if (t.previousRank === null) return { kind: 'new' };
  var d = t.previousRank - t.rank;
  if (d > 0) return { kind: 'up', delta: d };
  if (d < 0) return { kind: 'down', delta: -d };
  return { kind: 'same' };
}
var RM_STYLE = {
  up: { icon: 'solar:alt-arrow-up-bold', label: function (d) { return d + ' байр дээшилсэн'; } },
  down: { icon: 'solar:alt-arrow-down-bold', label: function (d) { return d + ' байр буурсан'; } },
  new: { icon: 'solar:star-bold', label: function () { return 'Шинээр орсон'; } },
  same: { icon: 'mingcute:minimize-line', label: function () { return 'Байраа хадгалсан'; } },
};
function rankMoveHtml(t, size) {
  var m = rankMove(t);
  if (!m) return '';
  size = size || 22;
  var width = Math.round(size * (m.kind === 'new' ? 3.6 : m.kind === 'same' ? 1.4 : 2.4));
  var label = RM_STYLE[m.kind].label(m.delta);
  var inner = m.kind === 'new' ? 'ШИНЭ' : (m.delta ? '<em style="font-style:normal" data-n="' + m.delta + '">' + m.delta + '</em>' : '');
  return '<span class="rm ' + m.kind + (motionOn() ? ' idle' : '') + '" role="img" aria-label="' + esc(label) + '" title="' + esc(label) + ' (өмнөх багцаас)" style="width:' + width + 'px;height:' + size + 'px">' +
    '<b style="height:' + size + 'px;border-radius:' + (size / 2) + 'px;font-size:' + (size * 0.55) + 'px">' + ico(RM_STYLE[m.kind].icon, Math.round(size * 0.62)) + inner + '</b></span>';
}
/** Plays every badge in `root` once when it scrolls into view: spring in, count up. */
function armRankMoves(root) {
  if (!motionOn() || !('IntersectionObserver' in window)) return;
  var obs = new IntersectionObserver(function (entries) {
    entries.forEach(function (en) {
      if (!en.isIntersecting) return;
      obs.unobserve(en.target);
      var el = en.target;
      el.classList.remove('idle'); el.classList.add('play');
      var n = el.querySelector('[data-n]');
      if (n) {
        var target = Number(n.getAttribute('data-n')), start = performance.now();
        var step = function (now) {
          var f = Math.min(1, Math.max(0, (now - start - 130) / 600));
          n.textContent = String(Math.round(target * f));
          if (f < 1) requestAnimationFrame(step);
        };
        n.textContent = '0';
        requestAnimationFrame(step);
      }
    });
  });
  root.querySelectorAll('.rm.idle').forEach(function (el) { obs.observe(el); });
}

// ---------------------------------------------------------------------- billboard (billboard.jsx)

function topicImage(t, iconSize) {
  var c = cat(t.category), g = PAL[c.color];
  return '<div class="timg" style="background:linear-gradient(135deg,' + g[0] + ',' + g[1] + ')">' +
    (t.image ? '<img src="' + esc(t.image) + '" alt="" loading="lazy">' : ico(c.icon, iconSize)) + '</div>';
}
function catChip(k, style) {
  var c = cat(k);
  return '<span class="catchip" style="color:' + c.hex + ';background:' + (style ? '#fff' : c.hex + '1A') + '">' + esc(c.label) + '</span>';
}
function hotPill(t, style) {
  if (!t.hot) return '';
  return '<span class="hotpill" title="Hot оноо: хэвлэлийн нэр хүнд, сошиал идэвх, хурд" style="' + (style || '') + '">' + ico('solar:fire-bold', 14) + hotScore(t) + '</span>';
}
function marker(t, latest, size) {
  if (latest && t.previousRank !== null) return '';
  return rankMoveHtml(t, size);
}
function meta(t, latest, style, cls) {
  if (!latest) return hotPill(t, style);
  if (!t.latestAt) return '';
  return '<span class="ago ' + (cls || '') + '" style="' + (style || '') + '">' + ico('solar:clock-circle-linear', 14) + esc(fAgo(t.latestAt)) + '</span>';
}
function billboardHtml(topics, latest, openId) {
  if (!topics.length) return '';
  var hero = topics[0], runners = topics.slice(1, 3), chart = topics.slice(3), maxHot = hero.hot || 0;
  var entry = function (t, i, cls, inner) {
    return '<div class="entry ' + cls + '" style="' + (t._id === openId ? 'visibility:hidden' : '') + '" data-entry="' + i + '"><button aria-label="' + esc(t.rank + '. ' + t.title) + '" data-open="' + i + '">' + inner + '</button></div>';
  };
  var h = '<section aria-label="' + (latest ? 'Шуурхай мэдээ, хамгийн сүүлийнх нь эхэндээ' : 'Халуун сэдвүүдийн жагсаалт') + '">';
  h += '<div class="bb-head"><h2 class="h4 serif">' + (latest ? 'Шуурхай мэдээ' : 'Шилдэг сэдвүүд') + '</h2><span class="label ' + (latest ? 'info' : 'error') + '">' + (latest ? 'Шинэ нь эхэндээ' : 'Billboard') + '</span></div>';
  // hero
  var hh = '<div class="hero-ph">' + topicImage(hero, 240) + '<div class="shade"></div><div class="hero-rank">' +
    (!latest ? '<span class="rankn onimg">' + hero.rank + '</span>' : '') + marker(hero, latest, 26) + '</div>' +
    '<div class="hero-meta">' + meta(hero, latest, latest ? '' : 'background:#fff', latest ? 'onimg' : '') + '</div>' +
    '<div class="hero-chips"><span class="pill-red">' + (latest ? 'Хамгийн сүүлийн' : 'Хамгийн халуун') + '</span>' + catChip(hero.category, true) + '</div></div>' +
    '<div class="hero-body"><h2 class="hero-title clamp4">' + esc(hero.title) + '</h2>' +
    (hero.reason ? '<div class="ai"><span class="overline">' + ico('solar:magic-stick-3-bold-duotone', 18) + 'AI дүгнэлт</span><p class="clamp4">' + esc(hero.reason) + '</p></div>' : '') +
    '<p class="sub2 more">Задлан унших ' + ico('solar:arrow-right-linear', 18) + '</p></div>';
  h += '<div style="margin-bottom:16px">' + entry(hero, 0, '', hh) + '</div>';
  if (runners.length) {
    h += '<div class="runners n' + runners.length + '">';
    runners.forEach(function (t, k) {
      var c = '<div class="rc"><div class="rc-top"><div class="rc-thumb">' + topicImage(t, 64) + (!latest ? '<span class="rankn onimg">' + t.rank + '</span>' : '') + '</div>' +
        '<div style="flex:1;min-width:0"><div class="chips-line">' + catChip(t.category) + '<span class="sp"></span>' + meta(t, latest) + '</div>' +
        '<h3 class="rc-title clamp2">' + esc(t.title) + '</h3>' + (t.reason ? '<p class="body2 muted clamp1" style="margin:4px 0 0">' + esc(t.reason) + '</p>' : '') + '</div></div>' +
        '<div class="rc-foot">' + marker(t, latest, 20) + '<span class="sub2">Дэлгэрэнгүй ' + ico('eva:arrow-ios-forward-fill', 16) + '</span></div></div>';
      h += entry(t, k + 1, '', c);
    });
    h += '</div>';
  }
  if (chart.length) {
    h += '<h2 class="overline" style="margin-bottom:12px">' + (latest ? 'Өмнөх мэдээ' : 'Бусад өндөр эрэлттэй сэдвүүд') + '</h2><div class="rows">';
    chart.forEach(function (t, k) {
      var share = maxHot > 0 ? (t.hot || 0) / maxHot : 0;
      var r = '<div class="row">' + (!latest ? '<div class="row-rank"><span class="rankn">' + String(t.rank).padStart(2, '0') + '</span>' + rankMoveHtml(t, 18) + '</div>' : '') +
        '<div class="row-thumb">' + topicImage(t, 48) + '</div><div style="flex:1;min-width:0"><div class="chips-line">' + catChip(t.category) + '<span class="sp"></span>' +
        (latest ? marker(t, true, 18) : '') + meta(t, latest, 'padding-left:0;padding-right:0;background:transparent') + '</div>' +
        '<h3 class="sub1 clamp2" style="line-height:1.35">' + esc(t.title) + '</h3>' +
        (!latest ? '<div class="hotbar"><i style="width:' + Math.max(share * 100, 4) + '%"></i></div>' : '') + '</div></div>';
      h += entry(t, k + 3, 'rowe', r);
    });
    h += '</div>';
  }
  return h + '</section>';
}

// ---------------------------------------------------------------------- stories row + reels (topic-stories.jsx, topic-reels.jsx)

var SEEN_KEY = 'sedev:seen-topics';
function seenLoad() { try { return JSON.parse(localStorage.getItem(SEEN_KEY)) || []; } catch (e) { return []; } }
var SEEN = null;
function seenSet() { if (!SEEN) SEEN = new Set(seenLoad()); return SEEN; }
function markSeen(id) {
  var s = seenSet();
  if (s.has(id)) return;
  s.add(id);
  try { localStorage.setItem(SEEN_KEY, JSON.stringify(Array.from(s).slice(-300))); } catch (e) { /* memory only */ }
  document.querySelectorAll('.story[data-id="' + id + '"]').forEach(function (el) { el.classList.add('seen'); });
}
function storiesHtml(topics) {
  var s = seenSet();
  return '<nav class="stories" aria-label="Сэдвүүдийг reels-ээр үзэх">' + topics.map(function (t, i) {
    var c = cat(t.category);
    return '<button class="story' + (s.has(t._id) ? ' seen' : '') + '" data-id="' + esc(t._id) + '" data-reel="' + i + '"><span class="ring"><span class="ph">' +
      (t.image ? '<img src="' + esc(t.image) + '" alt="" loading="lazy">' : ico(c.icon, 32, 'color:' + PAL[c.color][0])) + '</span></span><span class="t">' + esc(t.title) + '</span></button>';
  }).join('') + '</nav>';
}

function openReels(topics, startIndex) {
  var index = startIndex, paused = false, press = null;
  var root = document.createElement('div');
  root.className = 'reels'; root.setAttribute('role', 'dialog'); root.setAttribute('aria-modal', 'true');
  document.body.appendChild(root);
  var prevOverflow = document.body.style.overflow;
  document.body.style.overflow = 'hidden';
  var close = function () { root.remove(); document.body.style.overflow = prevOverflow; window.removeEventListener('keydown', onKey); };
  var next = function () { if (index < topics.length - 1) { index++; render(); } else close(); };
  var prev = function () { index = Math.max(index - 1, 0); render(); };
  var setPaused = function (v) { paused = v; root.classList.toggle('paused', paused); var pb = root.querySelector('[data-pause]'); if (pb) { pb.innerHTML = ico(paused ? 'solar:play-bold' : 'solar:pause-bold', 24); pb.setAttribute('aria-label', paused ? 'Үргэлжлүүлэх' : 'Түр зогсоох'); } };
  var onKey = function (e) {
    if (e.key === 'ArrowRight' || e.key === 'ArrowDown') next();
    else if (e.key === 'ArrowLeft' || e.key === 'ArrowUp') prev();
    else if (e.key === ' ') setPaused(!paused);
    else if (e.key === 'Escape') close();
    else return;
    e.preventDefault();
  };
  window.addEventListener('keydown', onKey);
  function render() {
    var t = topics[index];
    if (!t) return close();
    markSeen(t._id);
    var c = cat(t.category);
    var points = (t.keypoints && t.keypoints.length) ? t.keypoints : [t.reason].filter(Boolean);
    var following = topics[index + 1];
    var bars = topics.map(function (x, i) { return '<i class="' + (i < index ? 'done' : i === index ? 'cur' : '') + '"><b></b></i>'; }).join('');
    root.innerHTML = '<div class="reels-box">' +
      (t.image ? '<img src="' + esc(t.image) + '" alt="" draggable="false">' : '<div class="reels-ico">' + ico(c.icon, 120, 'color:' + PAL[c.color][0] + ';opacity:.3') + '</div>') +
      '<div class="reels-fade"></div><div class="reels-top"><div class="bars">' + bars + '</div><div class="reels-row">' +
      '<span class="lbl-filled" style="background:' + PAL[c.color][0] + '">' + ico(c.icon, 16) + esc(c.label) + '</span>' +
      '<span class="caption" style="flex:1;opacity:.8">' + (index + 1) + ' / ' + topics.length + '</span>' +
      '<button class="ibtn" data-stop data-pause aria-label="Түр зогсоох">' + ico('solar:pause-bold', 24) + '</button>' +
      '<button class="ibtn" data-stop data-close aria-label="Хаах">' + ico('mingcute:close-line', 24) + '</button></div></div>' +
      '<div class="reels-body"><h2>' + esc(t.title) + '</h2><ul>' + points.map(function (p) { return '<li>' + ico('solar:bolt-bold', 18) + '<span>' + esc(typeof p === 'string' ? p : (p && p.text)) + '</span></li>'; }).join('') + '</ul>' +
      '<div class="go"><a class="btn btn-white" data-stop href="/topics/' + encodeURIComponent(t._id) + '">Дэлгэрэнгүй ' + ico('eva:arrow-ios-forward-fill', 18) + '</a></div></div>' +
      (following && following.image ? '<img src="' + esc(following.image) + '" alt="" style="display:none">' : '') + '</div>';
    setPaused(paused);
    var cur = root.querySelector('.bars > i.cur > b');
    if (cur) cur.addEventListener('animationend', next);
    root.querySelectorAll('[data-stop]').forEach(function (b) {
      b.addEventListener('pointerdown', function (e) { e.stopPropagation(); });
      b.addEventListener('pointerup', function (e) { e.stopPropagation(); });
    });
    root.querySelector('[data-pause]').onclick = function () { setPaused(!paused); };
    root.querySelector('[data-close]').onclick = close;
    var box = root.querySelector('.reels-box');
    box.onpointerdown = function (e) { press = { x: e.clientX, y: e.clientY, at: Date.now() }; root.classList.add('paused'); };
    box.onpointercancel = function () { press = null; root.classList.toggle('paused', paused); };
    box.onpointerup = function (e) {
      var s = press; press = null; root.classList.toggle('paused', paused);
      if (!s) return;
      var dy = e.clientY - s.y;
      if (Math.abs(dy) > 60) { if (dy < 0) next(); else prev(); }
      else if (Date.now() - s.at < 250) {
        var r = box.getBoundingClientRect();
        if (e.clientX - r.left < r.width / 3) prev(); else next();
      }
    };
  }
  render();
}

// ---------------------------------------------------------------------- edition view (edition-view.jsx + use-feed.js)

/**
 * Renders an edition into `root`. opts: { latest, feed (first /api/feed page), heading, emptyText }.
 * Latest = Шуурхай: the feed, newest first, next page loads when the sentinel scrolls into view.
 */
function editionView(root, edition, opts) {
  opts = opts || {};
  var st = { pages: opts.feed ? [opts.feed] : [], loading: false, obs: null, openId: null };
  if (!edition) {
    root.innerHTML = '<div class="container">' + emptyHtml('Одоогоор нийтлэгдсэн сэдэв алга', opts.emptyText || 'Анхны багц бэлэн болмогц энд харагдана.') + '</div>';
    return;
  }
  var ranked = function () {
    if (opts.latest) {
      var all = [];
      st.pages.forEach(function (pg) { (pg && pg.topics || []).forEach(function (t) { all.push(t); }); });
      return all.map(function (t, i) { return Object.assign({}, t, { rank: i + 1 }); });
    }
    return rankTopics(edition.topics).slice(0, BILLBOARD_SIZE);
  };
  var hasMore = function () { var last = st.pages[st.pages.length - 1]; return !!(last && last.nextCursor); };
  var render = function () {
    var list = ranked();
    var h = '<div class="container"><div>' + (opts.heading ? '<h1 class="h3" style="margin-bottom:8px">' + esc(opts.heading) + '</h1>' : '') +
      '<div class="status"><span class="dot"></span><span class="txt"><strong>' + list.length + ' сэдэв</strong>' + esc(' · ' + (edition.articleCount || 0) + ' нийтлэл · ' + fWindow(edition.window)) + '</span><span style="flex-shrink:0">' + esc(fDateTimeUB(edition.publishedAt)) + '</span></div></div>';
    if (list.length) h += '<h2 class="overline">Товчоор үзэх</h2>' + storiesHtml(list);
    h += billboardHtml(list, !!opts.latest, st.openId);
    if (opts.latest) h += '<div class="sentinel" data-sentinel>' + (st.loading ? '<div class="lin"></div>' : '') + '</div>';
    h += '</div>';
    root.innerHTML = h;
    armRankMoves(root);
    root.querySelectorAll('[data-open]').forEach(function (b) {
      b.onclick = function () {
        var i = Number(b.getAttribute('data-open'));
        var l = ranked();
        var tile = b.closest('.entry');
        st.openId = l[i]._id;
        if (tile) tile.style.visibility = 'hidden';
        openReader({
          topics: l, index: i, from: tile,
          onClose: function () { st.openId = null; if (tile) tile.style.visibility = ''; },
        });
      };
    });
    root.querySelectorAll('[data-reel]').forEach(function (b) {
      b.onclick = function () { openReels(ranked(), Number(b.getAttribute('data-reel'))); };
    });
    if (opts.latest) armSentinel();
  };
  var loadMore = function () {
    if (st.loading || !hasMore()) return;
    st.loading = true;
    var s = root.querySelector('[data-sentinel]');
    if (s) s.innerHTML = '<div class="lin"></div>';
    var last = st.pages[st.pages.length - 1];
    apiSoft('/api/feed?limit=' + FEED_PAGE_SIZE + '&cursor=' + encodeURIComponent(last.nextCursor)).then(function (page) {
      st.loading = false;
      if (page) st.pages.push(page); else last.nextCursor = null;
      var y = window.scrollY;
      render();
      window.scrollTo(0, y);
    });
  };
  var armSentinel = function () {
    if (st.obs) st.obs.disconnect();
    var node = root.querySelector('[data-sentinel]');
    if (!node || !hasMore() || !('IntersectionObserver' in window)) return;
    st.obs = new IntersectionObserver(function (en) { if (en[0].isIntersecting) loadMore(); }, { rootMargin: '600px' });
    st.obs.observe(node);
  };
  render();
}

// ---------------------------------------------------------------------- home (home-view.jsx)

var HOME_TABS = [
  { value: 'latest', label: 'Шуурхай', icon: 'solar:bolt-bold-duotone' },
  { value: 'hot', label: 'Халуун', icon: 'solar:fire-bold-duotone' },
  { value: 'world', label: 'Гадаад', icon: 'solar:earth-bold-duotone' },
  { value: 'social', label: 'Сошиал', icon: 'solar:chat-round-like-bold-duotone' },
];
function homeInit() {
  var tabsEl = document.getElementById('home-tabs');
  var body = document.getElementById('home-body');
  var tab = 'latest';
  var data = null;
  var drawTabs = function () {
    tabsEl.innerHTML = HOME_TABS.map(function (t) {
      return '<button class="seg" role="tab" aria-selected="' + (t.value === tab) + '" data-tab="' + t.value + '">' + ico(t.icon, 18) + '<span>' + t.label + '</span></button>';
    }).join('');
    tabsEl.querySelectorAll('[data-tab]').forEach(function (b) { b.onclick = function () { tab = b.getAttribute('data-tab'); drawTabs(); drawBody(); }; });
  };
  var drawBody = function () {
    if (!data) { body.innerHTML = '<div class="container">' + LOADING + '</div>'; return; }
    if (tab === 'latest') editionView(body, data.edition, { feed: data.feed || { topics: [], nextCursor: null }, latest: true });
    else if (tab === 'hot') editionView(body, data.edition, {});
    else if (tab === 'world') editionView(body, data.world, { emptyText: 'Гадаадын хэвлэлийн анхны багц бэлэн болмогц энд харагдана.' });
    else { body.innerHTML = '<div class="container"><div id="trend-embed"></div></div>'; if (window.trendingView) window.trendingView(document.getElementById('trend-embed'), data.trending || [], true); }
  };
  drawTabs(); drawBody();
  Promise.all([
    apiSoft('/api/editions/latest'),
    apiSoft('/api/feed?limit=' + FEED_PAGE_SIZE),
    apiSoft('/api/editions/latest?region=world'),
    apiSoft('/api/social/trending?limit=24'),
  ]).then(function (r) {
    data = { edition: r[0], feed: r[1], world: r[2], trending: r[3] || [] };
    drawBody();
  });
}

function editionPageInit(id) {
  var body = document.getElementById('edition-body');
  body.innerHTML = '<div class="container">' + LOADING + '</div>';
  apiSoft('/api/editions/' + encodeURIComponent(id)).then(function (ed) {
    if (!ed) { body.innerHTML = '<div class="container">' + emptyHtml('Хуудас олдсонгүй', 'Энэ багц байхгүй эсвэл устгагдсан.') + '</div>'; return; }
    editionView(body, ed, { heading: 'Архив: ' + fWindow(ed.window) });
  });
}

// ---------------------------------------------------------------------- charts (motifs/charts.jsx, Bloomberg Visual Data style)

var W = 400, H = 140;
var BV = { bg: '#16191F', ink: '#FFFFFF', muted: '#9AA1AC', rule: '#3A3F48', track: '#262A32', orange: '#F5A13A', blue: '#5AAEE3', blueDeep: '#2F7BD0', red: '#E0474F', redDeep: '#A82C33' };
var RAMP = ['#E4E7EC', '#B8BDC5', '#8D939C', '#666C76', '#4A4F58'];
var X0 = 12, X1 = W - 12, Y0 = 26, Y1 = H - 10;
function rampOf(k) { return RAMP[Math.min(Math.max(Math.floor(k * RAMP.length), 0), RAMP.length - 1)]; }
function clamp01(x) { return Math.min(Math.max(x, 0), 1); }
function easeC(x) { return 1 - Math.pow(1 - clamp01(x), 3); }
function part(p, a, b) { return easeC((p - a) / (b - a)); }
function rnd(i, salt) { if (salt === undefined) salt = 1; var x = Math.sin(i * 127.1 + salt * 311.7) * 43758.5453; return x - Math.floor(x); }
function series(count, slope, salt, noise) {
  if (noise === undefined) noise = 0.12;
  var out = [];
  for (var i = 0; i < count; i++) {
    var base = 0.5 + slope * (i / (count - 1) - 0.5) * 0.7;
    out.push(Math.min(Math.max(base + (rnd(i, salt) - 0.5) * noise * 2, 0.05), 0.95));
  }
  return out;
}
function xAt(i, count) { return X0 + ((X1 - X0) * i) / (count - 1); }
function yAt(v) { return Y1 - (Y1 - Y0) * v; }
function f2(n) { return Math.round(n * 100) / 100; }
function pathOf(values) { return values.map(function (v, i) { return (i ? 'L' : 'M') + f2(xAt(i, values.length)) + ' ' + f2(yAt(v)); }).join(' '); }
function otherOf(color) { return color === BV.red ? BV.blue : BV.red; }
function hop(t, count, salt, period) {
  period = period || 1.6;
  var step = Math.floor(t / period);
  var slot = function (s) { return Math.floor(rnd(s, salt) * count); };
  var from = slot(step - 1), to = slot(step);
  var k = easeC((t % period) / (period * 0.3));
  return { pos: from + (to - from) * k, at: k > 0.5 ? to : from };
}
function sweep(t, period) { period = period || 10; return (1 - Math.cos((t * 2 * Math.PI) / period)) / 2; }
function sTag(x, y, text, k) {
  if (k === undefined) k = 1;
  if (!text || k <= 0) return '';
  var w = text.length * 5.4 + 14;
  var left = Math.min(Math.max(x - w / 2, X0), X1 - w);
  return '<g opacity="' + f2(k) + '"><rect x="' + f2(left) + '" y="' + f2(y - 18) + '" width="' + f2(w) + '" height="14" rx="1.5" fill="' + BV.ink + '"/>' +
    '<path d="M' + f2(x - 4) + ' ' + f2(y - 4.5) + ' L' + f2(x + 4) + ' ' + f2(y - 4.5) + ' L' + f2(x) + ' ' + f2(y) + '" fill="' + BV.ink + '"/>' +
    '<text x="' + f2(left + w / 2) + '" y="' + f2(y - 8.2) + '" text-anchor="middle" font-size="8.5" font-weight="700" letter-spacing="0.06em" fill="#1A1D23">' + esc(text.toUpperCase()) + '</text></g>';
}
function sCursor(x, y, color, label, k) {
  return '<g opacity="' + f2(k) + '"><line x1="' + f2(x) + '" x2="' + f2(x) + '" y1="' + Y0 + '" y2="' + Y1 + '" stroke="' + BV.ink + '" stroke-width="0.75" opacity="0.5"/>' +
    '<circle cx="' + f2(x) + '" cy="' + f2(y) + '" r="4" fill="' + color + '" stroke="' + BV.ink + '" stroke-width="1.5"/>' + sTag(x, y - 7, label) + '</g>';
}
function sSel(x, y, w, h, k) {
  if (k === undefined) k = 1;
  return '<rect x="' + f2(x) + '" y="' + f2(y) + '" width="' + f2(w) + '" height="' + f2(h) + '" fill="none" stroke="' + BV.ink + '" stroke-width="1.5" rx="1" opacity="' + f2(k) + '"/>';
}
function sRect(x, y, w, h, fill, extra) { return '<rect x="' + f2(x) + '" y="' + f2(y) + '" width="' + f2(Math.max(w, 0)) + '" height="' + f2(Math.max(h, 0)) + '" fill="' + fill + '"' + (extra || '') + '/>'; }
function sLine(x1, y1, x2, y2, stroke, extra) { return '<line x1="' + f2(x1) + '" y1="' + f2(y1) + '" x2="' + f2(x2) + '" y2="' + f2(y2) + '" stroke="' + stroke + '"' + (extra || '') + '/>'; }

var CHARTS = {};
CHARTS.Line = function (o) {
  var p = o.p, t = o.t, color = o.color, slope = o.slope != null ? o.slope : 0.8, salt = o.salt != null ? o.salt : 1;
  var values = series(28, slope, salt, o.noise), length = 900, draw = part(p, 0.05, 0.8), id = o.uid + '-area';
  var u = sweep(t) * (values.length - 1), i = Math.floor(u), j = Math.min(i + 1, values.length - 1);
  var v = values[i] + (values[j] - values[i]) * (u - i);
  var s = '<defs><linearGradient id="' + id + '" x1="0" x2="0" y1="0" y2="1"><stop offset="0" stop-color="' + color + '" stop-opacity="0.28"/><stop offset="1" stop-color="' + color + '" stop-opacity="0"/></linearGradient>' +
    '<clipPath id="' + id + '-clip"><rect x="' + X0 + '" y="0" width="' + f2((X1 - X0) * draw) + '" height="' + H + '"/></clipPath></defs>';
  if (o.candles) series(28, slope, salt + 7, 0.4).forEach(function (vol, n) {
    var h = 22 * vol * part(p, n / 40, n / 40 + 0.3) * (1 + 0.15 * Math.sin(t * 1.4 + n));
    s += sRect(xAt(n, 28) - 3.5, Y1 - h, 7, h, rampOf(1 - vol), ' opacity="0.5"');
  });
  if (o.second) s += '<path d="' + pathOf(series(28, -slope * 0.6, salt + 11)) + '" fill="none" stroke="' + BV.muted + '" stroke-width="1" stroke-dasharray="3 3" opacity="' + f2(0.8 * draw) + '"/>';
  s += '<path d="' + pathOf(values) + ' L' + X1 + ' ' + Y1 + ' L' + X0 + ' ' + Y1 + ' Z" fill="url(#' + id + ')" clip-path="url(#' + id + '-clip)"/>';
  s += '<path d="' + pathOf(values) + '" fill="none" stroke="' + color + '" stroke-width="2" stroke-linejoin="round" stroke-dasharray="' + length + '" stroke-dashoffset="' + f2(length * (1 - draw)) + '"/>';
  s += sCursor(X0 + ((X1 - X0) * u) / (values.length - 1), yAt(v), color, o.label, part(p, 0.8, 0.95));
  return s;
};
CHARTS.Bars = function (o) {
  var p = o.p, t = o.t, color = o.color, shape = o.shape || 'up', salt = o.salt != null ? o.salt : 2;
  var count = 36, gap = 2, width = (X1 - X0 - gap * (count - 1)) / count, asc = [];
  for (var i = 0; i < count; i++) asc.push(0.14 + 0.78 * Math.pow(i / (count - 1), 1.35) + (rnd(i, salt) - 0.5) * 0.04);
  var values = shape === 'down' ? asc.slice().reverse() : asc;
  var hatched = Math.floor(rnd(0, salt + 5) * count), hp = hop(t, count, salt), pattern = o.uid + '-hatch';
  var heightOf = function (k) {
    var live = shape === 'eq' ? 0.28 * Math.sin(t * 2.2 + k * 0.7) * rnd(k, salt + 2) : 0.015 * Math.sin(t * 1.3 + k * 0.5);
    var v = shape === 'eq' ? 0.45 + (rnd(k, salt) - 0.5) * 0.4 + live : values[k] + live;
    return (Y1 - Y0) * Math.min(Math.max(v, 0.05), 1) * part(p, k * 0.012, k * 0.012 + 0.5);
  };
  var rankOf = function (k) { return (shape === 'down' ? count - 1 - k : k) / count; };
  var xOf = function (k) { return X0 + k * (width + gap); };
  var lo = Math.floor(hp.pos), hi = Math.min(lo + 1, count - 1);
  var top = Y1 - (heightOf(lo) + (heightOf(hi) - heightOf(lo)) * (hp.pos - lo));
  var s = '<defs><pattern id="' + pattern + '" width="3" height="3" patternUnits="userSpaceOnUse"><rect width="3" height="3" fill="' + RAMP[3] + '"/><path d="M0 3 L3 0" stroke="' + RAMP[1] + '" stroke-width="0.8"/></pattern></defs>';
  for (var k = 0; k < count; k++) {
    var h = heightOf(k);
    var fill = shape === 'eq' ? rampOf(1 - h / (Y1 - Y0)) : rampOf(rankOf(k));
    if (k === hatched) fill = 'url(#' + pattern + ')';
    if (k === hp.at) fill = color;
    s += sRect(xOf(k), Y1 - h, width, h, fill);
  }
  return s + sTag(xOf(hp.pos) + width / 2, top - 3, o.label, part(p, 0.6, 0.9));
};
CHARTS.Diverging = function (o) {
  var p = o.p, t = o.t, salt = o.salt != null ? o.salt : 8;
  var count = 36, gap = 2, width = (X1 - X0 - gap * (count - 1)) / count, mid = (Y0 + Y1) / 2 - 6, scale = 34, base = [];
  for (var i = 0; i < count; i++) base.push(rnd(i, salt) < 0.45 ? 0 : (rnd(i, salt + 1) - 0.6) * 2);
  var hp = hop(t, count, salt, 1.8);
  var xOf = function (k) { return X0 + k * (width + gap); };
  var valueOf = function (k) { return base[k] ? base[k] * (1 + 0.25 * Math.sin(t * 0.9 + k * 1.3)) * part(p, k * 0.015, k * 0.015 + 0.5) : 0; };
  var sel = valueOf(hp.at), selTop = Math.min(mid - Math.max(sel, 0) * scale, mid - 6), selBottom = Math.max(mid - Math.min(sel, 0) * scale, mid + 6);
  var s = '';
  for (var k = 0; k < count; k++) {
    var v = valueOf(k), x = xOf(k);
    if (!base[k]) { s += sLine(x, mid, x + width, mid, BV.muted, ' stroke-width="1"'); continue; }
    var h = Math.max(Math.abs(v) * scale, 1);
    var fill = v > 0 ? BV.redDeep : BV.blueDeep;
    if (k === hp.at) fill = v > 0 ? BV.red : BV.blue;
    s += sRect(x, v > 0 ? mid - h : mid, width, h, fill);
  }
  return s + sSel(xOf(hp.pos) - 1, selTop - 2, width + 2, selBottom - selTop + 4, part(p, 0.6, 0.9));
};
CHARTS.Tiles = function (o) {
  var p = o.p, t = o.t, color = o.color, salt = o.salt != null ? o.salt : 9;
  var cols = 36, gap = 2, width = (X1 - X0 - gap * (cols - 1)) / cols;
  var majority = color === BV.orange ? BV.blue : color, minority = otherOf(majority);
  var top = 12, stripeTop = 47, stripe = 2.6, stripes = Math.floor((Y1 - stripeTop) / stripe);
  var hp = hop(t, cols, salt, 1.5);
  var xOf = function (k) { return X0 + k * (width + gap); };
  var tile = function (k, row) { var r = rnd(k * 7 + row, salt); if (r > 0.97) return '#C5C9CF'; if (r > 0.94) return '#3F444D'; return r < 0.58 ? majority : minority; };
  var rows = [{ y: top, h: 9 }, { y: top + 11, h: 9 }, { y: top + 22, h: 4 }, { y: top + 27.5, h: 4 }];
  var s = '';
  for (var i = 0; i < cols; i++) {
    var k = part(p, i * 0.015, i * 0.015 + 0.4);
    var n = Math.round((2 + Math.pow(rnd(i, salt + 3), 2) * (stripes - 4)) * k + Math.sin(t * 0.7 + i) * 1.2);
    var red = Math.floor(n * rnd(i, salt + 4));
    s += '<g opacity="' + f2(0.2 + 0.8 * k) + '">';
    rows.forEach(function (row, r) { s += sRect(xOf(i), row.y, width, row.h, tile(i, r)); });
    for (var q = 0; q < stripes; q++) {
      var fill = '#2E333B';
      if (q < red) fill = minority; else if (q < n) fill = majority;
      s += sRect(xOf(i), stripeTop + q * stripe, width, stripe - 0.9, fill);
    }
    s += '</g>';
  }
  return s + sSel(xOf(hp.pos) - 1, top - 2, width + 2, Y1 - top + 3, part(p, 0.6, 0.9));
};
CHARTS.HBars = function (o) {
  var p = o.p, t = o.t, color = o.color, salt = o.salt != null ? o.salt : 3;
  var values = [0.86, 0.64, 0.46, 0.3, 0.16].map(function (v, i) { return v + (rnd(i, salt) - 0.5) * 0.08; });
  var top = 12, rowH = (Y1 - top) / values.length, barH = rowH * 0.56, hp = hop(t, values.length, salt, 2), s = '';
  values.forEach(function (v, i) {
    var y = top + i * rowH + (rowH - barH) / 2;
    var w = (X1 - X0) * (v + 0.02 * Math.sin(t * 1.1 + i)) * part(p, i * 0.08, i * 0.08 + 0.6);
    s += sRect(X0, y, X1 - X0, barH, BV.track) + sRect(X0, y, w, barH, i === 0 ? color : RAMP[i - 1]);
  });
  return s + sSel(X0 - 3, top + hp.pos * rowH + (rowH - barH) / 2 - 3, X1 - X0 + 6, barH + 6, part(p, 0.6, 0.9));
};
CHARTS.Hemicycle = function (o) {
  var p = o.p, t = o.t, color = o.color, salt = o.salt != null ? o.salt : 4;
  var majority = color === BV.orange ? BV.blue : color, cx = W / 2, cy = Y1, seats = [];
  [5, 7, 9, 11].forEach(function (n, row) {
    var r = 34 + row * 24, total = n + row * 2;
    for (var i = 0; i < total; i++) {
      var a = Math.PI - (Math.PI * (i + 0.5)) / total;
      seats.push({ x: cx + r * Math.cos(a), y: cy - r * Math.sin(a), share: i / (total - 1) });
    }
  });
  var shown = part(p, 0.05, 0.85), hp = hop(t, seats.length, salt, 1.2);
  var fillOf = function (share) { if (share < 0.56) return majority; if (share < 0.66) return RAMP[2]; return otherOf(majority); };
  var s = seats.map(function (seat) { return sRect(seat.x - 4.5, seat.y - 4.5, 9, 9, fillOf(seat.share), ' rx="1" opacity="' + (seat.share <= shown ? 1 : 0.08) + '"'); }).join('');
  return s + sSel(seats[hp.at].x - 7, seats[hp.at].y - 7, 14, 14, part(p, 0.8, 0.95));
};
CHARTS.Pulse = function (o) {
  var p = o.p, t = o.t, color = o.color, spikes = o.spikes || 3;
  var step = (X1 - X0) / 40, span = (X1 - X0) / spikes, shift = (t * 18) % span, pts = [];
  for (var i = 0; i <= 40; i++) {
    var x = X0 + i * step, local = ((x - X0 + shift) % span) / span, v = 0.4;
    if (local > 0.45 && local < 0.5) v = 0.9; else if (local >= 0.5 && local < 0.55) v = 0.12;
    pts.push((i ? 'L' : 'M') + f2(x) + ' ' + f2(yAt(v)));
  }
  var length = 1200;
  return sLine(X0, yAt(0.4), X1, yAt(0.4), BV.rule, ' stroke-dasharray="2 3"') +
    '<path d="' + pts.join(' ') + '" fill="none" stroke="' + color + '" stroke-width="1.75" stroke-linejoin="round" stroke-dasharray="' + length + '" stroke-dashoffset="' + f2(length * (1 - part(p, 0.05, 0.85))) + '"/>';
};
CHARTS.Network = function (o) {
  var p = o.p, t = o.t, color = o.color, nodes = o.nodes || 7, salt = o.salt != null ? o.salt : 5, pts = [];
  for (var i = 0; i < nodes; i++) pts.push({ x: X0 + 20 + (X1 - X0 - 40) * ((i + rnd(i, salt) * 0.6) / nodes), y: 16 + (Y1 - 26) * rnd(i, salt + 2) });
  var links = pts.slice(1).map(function (pt, k) { return [pts[k], pt]; });
  if (nodes > 3) links.push([pts[0], pts[nodes - 2]], [pts[1], pts[nodes - 1]]);
  var travel = (t / 2.5) % 1, hp = hop(t, nodes, salt, 1.8), s = '';
  links.forEach(function (l, k) {
    var a = l[0], b = l[1], q = part(p, k * 0.06, k * 0.06 + 0.4);
    s += sLine(a.x, a.y, a.x + (b.x - a.x) * q, a.y + (b.y - a.y) * q, BV.rule, ' stroke-width="1"');
    if (q === 1 && k % 2 === 0) s += sRect(a.x + (b.x - a.x) * travel - 2, a.y + (b.y - a.y) * travel - 2, 4, 4, color);
  });
  pts.forEach(function (pt, k) {
    var end = k === 0 || k === nodes - 1, sz = end ? 11 : 7;
    s += '<g opacity="' + f2(part(p, k * 0.05, k * 0.05 + 0.3)) + '">' + sRect(pt.x - sz / 2, pt.y - sz / 2, sz, sz, end || k === hp.at ? color : RAMP[2]) +
      (k === hp.at ? sSel(pt.x - sz / 2 - 3, pt.y - sz / 2 - 3, sz + 6, sz + 6) : '') + '</g>';
  });
  return s;
};
CHARTS.Route = function (o) {
  var p = o.p, t = o.t, color = o.color, stops = o.stops || 6;
  var y = (Y0 + Y1) / 2 - 4, draw = part(p, 0.05, 0.75), travel = X0 + (X1 - X0) * ((t / 6) % 1) * draw;
  var s = sLine(X0, y, X1, y, BV.rule, ' stroke-width="1"') + sLine(X0, y, X0 + (X1 - X0) * draw, y, color, ' stroke-width="2"');
  for (var i = 0; i < stops; i++) {
    var x = X0 + ((X1 - X0) * i) / (stops - 1), passed = draw === 1 && travel >= x;
    s += '<g opacity="' + f2(part(p, i * 0.1, i * 0.1 + 0.3)) + '">' + sLine(x, y - 14, x, y + 14, BV.rule) + sRect(x - 5, y - 5, 10, 10, passed ? color : BV.bg, ' stroke="' + color + '" stroke-width="1.5"') + '</g>';
  }
  if (draw === 1) s += sSel(travel - 7, y - 7, 14, 14);
  return s;
};
CHARTS.Gauge = function (o) {
  var p = o.p, t = o.t, color = o.color, level = o.level != null ? o.level : 0.78, cx = W / 2, cy = Y1, r = 100;
  var arc = function (from, to) {
    var a0 = Math.PI * (1 - from), a1 = Math.PI * (1 - to);
    return 'M' + f2(cx + r * Math.cos(a0)) + ' ' + f2(cy - r * Math.sin(a0)) + ' A' + r + ' ' + r + ' 0 0 1 ' + f2(cx + r * Math.cos(a1)) + ' ' + f2(cy - r * Math.sin(a1));
  };
  var value = clamp01(level * part(p, 0.1, 0.9) + 0.025 * Math.sin(t * 1.7) + 0.01 * Math.sin(t * 5.3)), a = Math.PI * (1 - value);
  var s = '<path d="' + arc(0, 1) + '" fill="none" stroke="' + BV.track + '" stroke-width="12"/><path d="' + arc(0, value) + '" fill="none" stroke="' + color + '" stroke-width="12"/>';
  for (var i = 0; i <= 10; i++) { var ta = Math.PI * (1 - i / 10); s += sLine(cx + (r - 18) * Math.cos(ta), cy - (r - 18) * Math.sin(ta), cx + (r - 11) * Math.cos(ta), cy - (r - 11) * Math.sin(ta), BV.muted); }
  return s + sLine(cx, cy, cx + (r - 24) * Math.cos(a), cy - (r - 24) * Math.sin(a), BV.ink, ' stroke-width="2"') + sRect(cx - 5, cy - 5, 10, 10, BV.ink);
};
CHARTS.Donut = function (o) {
  var p = o.p, t = o.t, color = o.color, cx = W / 2, cy = (12 + Y1) / 2, r = 50, shares = [0.38, 0.26, 0.2, 0.16];
  var circ = 2 * Math.PI * r, shown = part(p, 0.05, 0.85), active = Math.floor(t / 2.2) % shares.length, offset = 0;
  var s = '<g transform="rotate(-90 ' + cx + ' ' + cy + ')">';
  shares.forEach(function (share, i) {
    var dash = circ * share * shown;
    s += '<circle cx="' + cx + '" cy="' + f2(cy) + '" r="' + r + '" fill="none" stroke="' + (i === active ? color : RAMP[i + 1]) + '" stroke-width="' + (i === active ? 20 : 16) + '" stroke-dasharray="' + f2(Math.max(dash - 2.5, 0)) + ' ' + f2(circ) + '" stroke-dashoffset="' + f2(-offset) + '"/>';
    offset += circ * share * shown;
  });
  return s + '</g>';
};
CHARTS.Waffle = function (o) {
  var p = o.p, t = o.t, color = o.color, share = o.share != null ? o.share : 0.38, salt = o.salt != null ? o.salt : 4;
  var cols = 30, rows = 7, gap = 2, top = 12;
  var size = Math.min((X1 - X0 - gap * (cols - 1)) / cols, (Y1 - top - gap * (rows - 1)) / rows);
  var left = X0 + (X1 - X0 - (size * cols + gap * (cols - 1))) / 2;
  var filled = Math.round(cols * rows * (share + (rnd(1, salt) - 0.5) * 0.1)), shown = part(p, 0.05, 0.85) * cols * rows, bright = (t * 7) % (cols + 14), s = '';
  for (var i = 0; i < cols * rows; i++) {
    var col = Math.floor(i / rows), row = i % rows, on = i < filled, lit = Math.max(0, 1 - Math.abs(col - bright) / 2);
    s += sRect(left + col * (size + gap), top + row * (size + gap), size, size, on ? color : BV.track,
      ' opacity="' + f2(i < shown ? (on ? 0.7 + 0.3 * lit : 1) : 0.15) + '"' + (lit > 0.5 ? ' stroke="' + BV.ink + '" stroke-opacity="' + f2((lit - 0.5) * 1.2) + '" stroke-width="0.75"' : ''));
  }
  return s;
};
CHARTS.Scatter = function (o) {
  var p = o.p, t = o.t, color = o.color, slope = o.slope != null ? o.slope : 0.6, salt = o.salt != null ? o.salt : 6, count = 36, pts = [];
  for (var i = 0; i < count; i++) {
    var u = rnd(i, salt), v = 0.5 + slope * (u - 0.5) * 0.7 + (rnd(i, salt + 1) - 0.5) * 0.35;
    pts.push({ x: X0 + (X1 - X0) * u + 2.5 * Math.sin(t * 0.6 + i), y: yAt(Math.min(Math.max(v, 0.06), 0.94)) + 2.5 * Math.cos(t * 0.5 + i * 1.7) });
  }
  var line = part(p, 0.55, 0.95), ya = yAt(0.5 - slope * 0.35), yb = yAt(0.5 + slope * 0.35), hp = hop(t, count, salt, 1.8), sel = pts[hp.at];
  var s = sLine(X0, ya, X0 + (X1 - X0) * line, ya + (yb - ya) * line, color, ' stroke-width="1.5" opacity="0.8"');
  pts.forEach(function (pt, k) { s += sRect(pt.x - 3, pt.y - 3, 6, 6, k === hp.at ? color : RAMP[(k % 3) + 1], ' opacity="' + f2(part(p, (k / count) * 0.5, (k / count) * 0.5 + 0.2)) + '"'); });
  return s + '<g opacity="' + f2(part(p, 0.8, 0.95)) + '">' + sSel(sel.x - 6, sel.y - 6, 12, 12) + sTag(sel.x, sel.y - 8, o.label) + '</g>';
};
CHARTS.Steps = function (o) {
  var p = o.p, t = o.t, color = o.color, slope = o.slope != null ? o.slope : 0.7, salt = o.salt != null ? o.salt : 7;
  var values = series(7, slope, salt, 0.06), stepW = (X1 - X0) / values.length;
  var d = values.map(function (v, i) { return (i ? 'L' : 'M') + f2(X0 + i * stepW) + ' ' + f2(yAt(v)) + ' L' + f2(X0 + (i + 1) * stepW) + ' ' + f2(yAt(v)); }).join(' ');
  var length = 1400, draw = part(p, 0.05, 0.85), x = X0 + (X1 - X0) * (0.02 + 0.96 * sweep(t, 12)), step = Math.min(Math.floor((x - X0) / stepW), values.length - 1), s = '';
  values.forEach(function (v, i) { s += sRect(X0 + i * stepW + 1, yAt(v), stepW - 2, Y1 - yAt(v), i === step ? color : RAMP[4], ' opacity="' + f2((i === step ? 0.22 : 0.35) * draw) + '"'); });
  s += '<path d="' + d + '" fill="none" stroke="' + color + '" stroke-width="2" stroke-dasharray="' + length + '" stroke-dashoffset="' + f2(length * (1 - draw)) + '"/>';
  return s + sCursor(x, yAt(values[step]), color, o.label, part(p, 0.8, 0.95));
};
var ON_BASELINE = { Line: 1, Bars: 1, Scatter: 1, Steps: 1, Pulse: 1 };
function chartSvg(kind, options, p, t, color, label, uid) {
  var o = Object.assign({}, options || {}, { p: p, t: t, color: color, label: label, uid: uid });
  var fn = CHARTS[kind] || CHARTS.Line;
  return (ON_BASELINE[kind] ? sLine(X0, Y1, X1, Y1, BV.rule, ' stroke-width="1"') : '') + fn(o);
}

// ---------------------------------------------------------------------- motifs (motifs/index.jsx + photo.jsx)

var TONE_COLORS = { positive: BV.blue, negative: BV.red, neutral: BV.orange };
var MOTIFS = {
  'market-down': { label: 'Зах зээл', kind: 'Line', options: { slope: -0.9, candles: true } },
  'market-up': { label: 'Зах зээл', kind: 'Line', options: { slope: 0.9, candles: true } },
  currency: { label: 'Ханш', kind: 'Line', options: { slope: 0.6, second: true, salt: 4 } },
  prices: { label: 'Үнэ', kind: 'Bars', options: { shape: 'up' } },
  budget: { label: 'Төсөв', kind: 'Donut' },
  election: { label: 'Сонгууль', kind: 'Tiles' },
  parliament: { label: 'Төр засаг', kind: 'Hemicycle' },
  court: { label: 'Хууль, шүүх', kind: 'Route', options: { stops: 5 } },
  mining: { label: 'Уул уурхай', kind: 'Line', options: { slope: 0.5, salt: 6, noise: 0.18 } },
  dzud: { label: 'Зуд', kind: 'Bars', options: { shape: 'down' } },
  wildfire: { label: 'Түймэр', kind: 'Line', options: { slope: 1, salt: 8, noise: 0.05 } },
  flood: { label: 'Үер', kind: 'Line', options: { slope: 0.7, salt: 9, noise: 0.2 } },
  'air-pollution': { label: 'Агаарын чанар', kind: 'Gauge', options: { level: 0.82 } },
  naadam: { label: 'Наадам', kind: 'Bars', options: { shape: 'ranked', salt: 3 } },
  sport: { label: 'Спорт', kind: 'Bars', options: { shape: 'ranked' } },
  health: { label: 'Эрүүл мэнд', kind: 'Pulse' },
  education: { label: 'Боловсрол', kind: 'Bars', options: { shape: 'up', salt: 5 } },
  transport: { label: 'Тээвэр', kind: 'Route' },
  energy: { label: 'Эрчим хүч', kind: 'Line', options: { slope: 0.3, salt: 12, noise: 0.25 } },
  tech: { label: 'Технологи', kind: 'Network' },
  diplomacy: { label: 'Дипломат', kind: 'Network', options: { nodes: 5, salt: 9 } },
  conflict: { label: 'Аюулгүй байдал', kind: 'Pulse', options: { spikes: 5 } },
  culture: { label: 'Соёл', kind: 'Bars', options: { shape: 'eq' } },
  alert: { label: 'Онцгой байдал', kind: 'Pulse', options: { spikes: 2 } },
  chart: { label: 'Статистик', kind: 'Line', options: { slope: 0.4, salt: 2 } },
};
var ALTERNATES = {
  Line: function (slope) { return [{ kind: 'Steps', options: { slope: slope } }, { kind: 'Diverging' }, { kind: 'Bars', options: { shape: slope < 0 ? 'down' : 'up' } }]; },
  Bars: function (slope) { return [{ kind: 'Line', options: { slope: slope } }, { kind: 'Diverging' }, { kind: 'Waffle', options: { share: 0.45 } }]; },
  Tiles: function () { return [{ kind: 'HBars' }, { kind: 'Diverging' }, { kind: 'Waffle', options: { share: 0.52 } }]; },
  HBars: function () { return [{ kind: 'Tiles' }, { kind: 'Donut' }]; },
  Donut: function () { return [{ kind: 'HBars' }, { kind: 'Waffle', options: { share: 0.42 } }]; },
  Hemicycle: function () { return [{ kind: 'Tiles' }, { kind: 'HBars' }]; },
  Route: function () { return [{ kind: 'Steps', options: { slope: 0.5 } }, { kind: 'Network', options: { nodes: 6 } }]; },
  Pulse: function () { return [{ kind: 'Diverging' }, { kind: 'Line', options: { slope: 0.2, noise: 0.3 } }]; },
  Network: function () { return [{ kind: 'Scatter', options: { slope: 0.4 } }, { kind: 'Diverging' }]; },
  Gauge: function (slope, options) { return [{ kind: 'Waffle', options: { share: (options && options.level) || 0.6 } }, { kind: 'Bars', options: { shape: 'up' } }]; },
};
var SHAPE_SLOPES = { up: 0.8, down: -0.8 };
function chartOf(spec, seed) {
  seed = seed || 0;
  var options = spec.options || {};
  var slope = options.slope != null ? options.slope : (SHAPE_SLOPES[options.shape] != null ? SHAPE_SLOPES[options.shape] : 0.5);
  var charts = [{ kind: spec.kind, options: options }].concat(ALTERNATES[spec.kind] ? ALTERNATES[spec.kind](slope, options) : []);
  var c = charts[seed % charts.length];
  return { kind: c.kind, options: Object.assign({}, c.options, { salt: ((c.options && c.options.salt) || 1) + seed }) };
}
var KEYWORDS = [
  ['wildfire', ['түймэр', 'гал гар']], ['dzud', ['зуд', 'цасан шуурга', 'хүйтр']], ['flood', ['үер', 'аадар']],
  ['air-pollution', ['утаа', 'бохирдол']], ['mining', ['уурхай', 'нүүрс', 'зэс', 'оюу толгой', 'боомт']],
  ['election', ['сонгууль', 'санал хураа']], ['court', ['шүүх', 'прокурор', 'баривч', 'авлига', 'цагдаа', 'хэрэг']],
  ['currency', ['ханш', 'валют', 'доллар']], ['prices', ['үнэ', 'инфляц', 'шатахуун']],
  ['budget', ['төсөв', 'татвар', 'зээл', 'цалин', 'тэтгэвэр']], ['market-down', ['унал', 'буурал', 'хямрал']],
  ['market-up', ['хувьцаа', 'бирж', 'өсөлт']], ['naadam', ['наадам', 'морь', 'бөх']],
  ['sport', ['спорт', 'тэмцээн', 'медаль', 'аварга', 'олимп']], ['health', ['эмнэлэг', 'эрүүл мэнд', 'өвчин', 'вакцин']],
  ['education', ['сургууль', 'боловсрол', 'шалгалт', 'оюутан', 'багш']], ['energy', ['цахилгаан', 'эрчим хүч', 'дулаан', 'станц']],
  ['transport', ['зам', 'тээвэр', 'түгжрэл', 'автобус', 'нисэх', 'онгоц']], ['tech', ['технологи', 'интернэт', 'хиймэл оюун', 'цахим']],
  ['conflict', ['дайн', 'халдлага', 'дрон', 'мөргөлдөө', 'хориг']], ['diplomacy', ['айлчлал', 'гэрээ', 'хэлэлцээ', 'элчин']],
  ['parliament', ['уих', 'хууль', 'засгийн газар', 'сайд', 'тогтоол']], ['culture', ['соёл', 'урлаг', 'кино', 'тоглолт']],
  ['alert', ['осол', 'гамшиг', 'нас барсан']],
];
var CATEGORY_MOTIFS = { politics: 'parliament', economy: 'market-up', society: 'chart', world: 'diplomacy', sports: 'sport', culture: 'culture', tech: 'tech' };
function motifOfText(text) {
  var lower = String(text || '').toLowerCase();
  for (var i = 0; i < KEYWORDS.length; i++) if (KEYWORDS[i][1].some(function (w) { return lower.indexOf(w) >= 0; })) return KEYWORDS[i][0];
  return null;
}
function guessMotif(topic) { return motifOfText(topic.title) || CATEGORY_MOTIFS[topic.category] || 'chart'; }

var MOVES = [
  { from: [1.04, 0, 0], to: [1.18, 0, 0] }, { from: [1.2, 0, 0], to: [1.06, 0, 0] }, { from: [1.16, 3, 0], to: [1.16, -3, 0] },
  { from: [1.16, -3, 0], to: [1.16, 3, 0] }, { from: [1.16, 0, 3], to: [1.16, 0, -3] }, { from: [1.08, 2, 2], to: [1.18, -2, -2] },
];
var GRADES = { natural: 'contrast(1.04) saturate(1.02)', warm: 'sepia(0.14) saturate(1.08) contrast(1.04)', cool: 'saturate(0.82) contrast(1.06) brightness(0.97) hue-rotate(-6deg)', mono: 'grayscale(1) contrast(1.12)' };
var TONE_GRADES = { negative: ['cool', 'mono', 'natural'], positive: ['warm', 'natural', 'warm'], neutral: ['natural', 'warm', 'cool', 'natural', 'mono'] };
function photoLook(seed, tone) {
  var g = TONE_GRADES[tone || 'neutral'] || TONE_GRADES.neutral;
  return { move: seed % MOVES.length, grade: g[seed % g.length], focus: seed % 3 === 1 };
}

// ---------------------------------------------------------------------- story slides (story-slides.js)

var PROSCONS = { good: 'Эерэг нөлөө', bad: 'Эрсдэл ба сорилт' };
var FORMATS = ['text', 'list', 'keypoints', 'facts', 'timeline', 'perspectives', 'proscons'];
var SLIDE_CHARS = 240;
var SENTENCE_RE = null;
try { SENTENCE_RE = new RegExp('(?<=[.!?…]["»”]?)\\s+(?=[А-ЯЁӨҮA-Z0-9"«])', 'u'); } catch (e) { SENTENCE_RE = /\s+(?=[А-ЯЁӨҮA-Z0-9"«])/; }
function pieces(paragraph) {
  return paragraph.split(SENTENCE_RE).reduce(function (out, s) {
    var last = out.length - 1;
    if (last >= 0 && out[last].length + s.length < SLIDE_CHARS) out[last] += ' ' + s; else out.push(s);
    return out;
  }, []);
}
function paragraphs(text) {
  return String(text || '').split('\\n').join('\n').split(/\n+/)
    .reduce(function (a, p) { return a.concat(pieces(p.trim())); }, [])
    .map(function (p) { return p.trim(); }).filter(Boolean);
}
function textOf(item) { return typeof item === 'string' ? item : (item && item.text); }
function hasContent(format, value) {
  if (!value) return false;
  if (format === 'text') return Boolean(value.text);
  if (format === 'proscons') return Boolean((value.good && value.good.length) || (value.bad && value.bad.length));
  return Array.isArray(value) && value.length > 0 && FORMATS.indexOf(format) >= 0;
}
function chapterSlides(ch) {
  var format = ch.lens.format, label = ch.lens.label, value = ch.value;
  var slide = function (text, kicker) { return { section: label, kicker: kicker, text: text }; };
  switch (format) {
    case 'text': return paragraphs(value && value.text).map(function (p) { return slide(p); });
    case 'list': case 'keypoints': return value.map(textOf).filter(Boolean).map(function (t) { return slide(t); });
    case 'facts': return value.filter(function (i) { return i && (i.value || i.label); }).map(function (i) { return Object.assign(slide(i.label || '', i.value), { data: true }); });
    case 'timeline':
      return value.slice().sort(function (a, b) { return String(a.date || '').localeCompare(String(b.date || '')); }).map(function (i) {
        var dp = fDayParts(i.date);
        return slide([i.title, i.event].filter(Boolean).join('. '), dp.month ? dp.month + ' ' + dp.day : dp.day);
      });
    case 'perspectives': return value.map(function (i) { return slide(i.position || '', i.actor); });
    case 'proscons':
      return ['good', 'bad'].reduce(function (a, side) {
        return a.concat(((value && value[side]) || []).map(textOf).filter(Boolean).map(function (t) { return slide(t, PROSCONS[side]); }));
      }, []);
    default: return [];
  }
}
function gaugeUnit(g, v) { return g.unit === '%' ? v + '%' : (v + ' ' + (g.unit || '')).trim(); }
function topicSlides(topic, keypoints, chapters) {
  var visuals = topic.visuals || {};
  var slides = paragraphs(topic.storyline || topic.reason).map(function (text) { return { section: 'Тойм', text: text, slot: 'cover' }; })
    .concat(keypoints.map(function (text, i) { return { section: 'Товчхондоо', text: text, slot: 'kp' + (i + 1) }; }));
  if (visuals.balance && visuals.balance.length > 1) {
    slides.push({
      section: 'Байр суурийн тэнцвэр', slot: 'cover', data: true,
      kicker: visuals.balance.map(function (s) { return s.share + '%'; }).join(' · '),
      text: visuals.balance.map(function (s) { return s.label + ' ' + s.share + '%'; }).join(', '),
    });
  }
  var fc = chapters.find(function (c) { return c.lens.format === 'facts'; }) || chapters[0];
  var gaugeKey = fc && fc.key;
  chapters.forEach(function (ch) {
    chapterSlides(ch).forEach(function (s) { slides.push(Object.assign({}, s, { slot: ch.key })); });
    var g = visuals.gauge;
    if (g && ch.key === gaugeKey) slides.push({ section: ch.lens.label, kicker: gaugeUnit(g, g.value) + ' / ' + gaugeUnit(g, g.max), text: [g.label, g.note].filter(Boolean).join('. '), slot: ch.key, data: true });
  });
  return slides;
}
function keypointsOf(topic) {
  var lens = (topic.lenses || []).find(function (l) { return l.format === 'keypoints'; });
  var items = lens ? (topic.wraps || {})[lens.key] : topic.keypoints;
  return (Array.isArray(items) ? items : []).map(textOf).filter(Boolean);
}
/** Slides of a topic with their scene (motif, tone, caption, photo) — topic-story.jsx useMemo. */
function buildSlides(full, summary) {
  var topic = Object.assign({}, full || {}, summary);
  var keypoints = keypointsOf(full || summary);
  var chapters = ((full && full.lenses) || []).filter(function (l) { return l.format !== 'keypoints'; })
    .map(function (l) { return { key: l.key, lens: l, value: (full.wraps || {})[l.key] }; })
    .filter(function (c) { return hasContent(c.lens.format, c.value); });
  var plan = (full && full.visuals && full.visuals.motifs) || [];
  var planned = function (slot) { return plan.find(function (i) { return i.lens === slot; }) || null; };
  var cover = planned('cover') || { motif: guessMotif(topic) };
  var images = (full && full.images) || (topic.image ? [topic.image] : []);
  var chartEvery = images.length >= 3 ? 4 : 2, textSlides = 0, photos = 0;
  return topicSlides(topic, keypoints, chapters).map(function (slide, i) {
    var text = (slide.kicker || '') + ' ' + slide.text;
    var named = motifOfText(text);
    var scene = planned(slide.slot) || (named ? { motif: named } : cover);
    var tone = scene.tone || cover.tone;
    var nth = slide.data ? -1 : textSlides++;
    var photo = images.length > 0 && nth >= 0 && nth % chartEvery !== chartEvery - 1 ? images[photos++ % images.length] : null;
    return Object.assign({}, slide, { motif: MOTIFS[scene.motif] ? scene.motif : 'chart', tone: tone, caption: scene.caption, photo: photo, look: photo ? photoLook(i, tone) : null, seed: i });
  });
}

// ---------------------------------------------------------------------- scene clock: one rAF loop for the slide on screen

var SCENE = null; // { el, point, start, uid, failed }
var SCENE_MS = 1800, MOVE_S = 16;
function sceneTick(now) {
  var s = SCENE;
  if (!s || !s.el.isConnected) { SCENE = null; return; }
  var still = !motionOn();
  var elapsed = now - s.start;
  var p = still ? 1 : Math.min(Math.max(elapsed / SCENE_MS, 0), 1);
  var t = still ? 0 : elapsed / 1000;
  var pt = s.point;
  if (s.photoEl && !s.failed) {
    var mv = MOVES[pt.look.move], k = 0.5 - Math.cos(Math.PI * Math.min(Math.max(t / MOVE_S, 0), 1)) / 2;
    var v = mv.from.map(function (a, i) { return a + (mv.to[i] - a) * k; });
    var blur = pt.look.focus ? (1 - Math.min(p * 1.4, 1)) * 8 : 0;
    s.photoEl.style.opacity = s.loaded ? Math.min(p * 2.5, 1) : 0;
    s.photoEl.style.transform = 'scale(' + v[0] + ') translate(' + v[1] + '%, ' + v[2] + '%)';
    s.photoEl.style.filter = GRADES[pt.look.grade] + (blur ? ' blur(' + blur.toFixed(1) + 'px)' : '');
  } else if (s.svgEl) {
    var spec = MOTIFS[pt.motif] || MOTIFS.chart, ch = chartOf(spec, pt.seed || 0);
    s.svgEl.innerHTML = chartSvg(ch.kind, ch.options, p, t, TONE_COLORS[pt.tone] || BV.orange, spec.label, s.uid);
  }
  if (!still || p < 1) requestAnimationFrame(sceneTick);
  else s.idle = true;
}
var SCENE_UID = 0;
function mountScene(wrap, point) {
  var uid = 'sc' + (++SCENE_UID);
  var s = { el: wrap, point: point, start: performance.now(), uid: uid, failed: false, loaded: false };
  var drawChart = function () {
    wrap.innerHTML = '<div class="scene"><svg viewBox="0 0 ' + W + ' ' + H + '" preserveAspectRatio="xMidYMid meet" aria-hidden="true"></svg></div>';
    s.svgEl = wrap.querySelector('svg'); s.photoEl = null;
  };
  if (point.photo && point.look) {
    wrap.innerHTML = '<div class="photo" aria-hidden="true"><img alt="" draggable="false" referrerpolicy="no-referrer"><div class="vig"></div></div>';
    s.photoEl = wrap.querySelector('img');
    s.photoEl.onload = function () { s.loaded = true; if (s.idle) { s.idle = false; requestAnimationFrame(sceneTick); } };
    s.photoEl.onerror = function () { s.failed = true; drawChart(); if (s.idle) { s.idle = false; requestAnimationFrame(sceneTick); } };
    s.photoEl.src = point.photo;
  } else drawChart();
  var wasRunning = SCENE && !SCENE.idle;
  SCENE = s;
  if (!wasRunning) requestAnimationFrame(sceneTick);
}
// motion toggled while a scene is idle: wake it up
document.addEventListener('click', function (e) {
  if (e.target.closest && e.target.closest('#motion-btn') && SCENE && SCENE.idle) { SCENE.idle = false; SCENE.start = performance.now(); requestAnimationFrame(sceneTick); }
});

// ---------------------------------------------------------------------- topic reader (topic-reader.jsx + topic-story.jsx + keypoint-cards.jsx)

var TOPIC_CACHE = {};
function getTopic(id) {
  if (!TOPIC_CACHE[id]) TOPIC_CACHE[id] = api('/api/topics/' + encodeURIComponent(id)).then(function (t) { if (!t) throw new Error('Сэдэв олдсонгүй'); return t; });
  TOPIC_CACHE[id].catch(function () { delete TOPIC_CACHE[id]; });
  return TOPIC_CACHE[id];
}
var MAX_TOPIC_BARS = 30, MAX_BARS = 12, MIN_TEXT_PX = 14;
var READER = null;
function textSize(text) { if (text.length > 280) return 16; if (text.length > 160) return 18; if (text.length > 100) return 20; return 24; }
function stopTap(el) {
  el.addEventListener('pointerdown', function (e) { e.stopPropagation(); });
  el.addEventListener('pointerup', function (e) { e.stopPropagation(); });
}

/**
 * Opens the full-screen reader over `opts.topics` at `opts.index`. opts.initial: full topic already
 * loaded; onNavigate(index), onClose(): the caller's hooks. Swipe up/down or ↑↓ change topic.
 */
function openReader(opts) {
  if (READER) READER.close(true);
  var topics = opts.topics, index = opts.index;
  var root = document.createElement('div');
  root.className = 'rdr' + (motionOn() ? ' opening' : '');
  root.setAttribute('role', 'dialog'); root.setAttribute('aria-modal', 'true');
  root.innerHTML = '<div class="rdr-pages"></div>' + (topics.length > 1 ? '<div class="rdr-bars"></div>' : '');
  if (opts.from && motionOn()) {
    var r = opts.from.getBoundingClientRect();
    root.style.transformOrigin = (r.left + r.width / 2) + 'px ' + (r.top + r.height / 2) + 'px';
  }
  document.body.appendChild(root);
  var prevOverflow = document.body.style.overflow;
  document.body.style.overflow = 'hidden';
  var pages = root.querySelector('.rdr-pages');
  var self = {};
  var drawBars = function () {
    var b = root.querySelector('.rdr-bars');
    if (!b) return;
    if (topics.length > MAX_TOPIC_BARS) b.innerHTML = '<i><b style="width:' + ((index + 1) / topics.length) * 100 + '%"></b></i>';
    else b.innerHTML = topics.map(function (t, i) { return '<i><b style="width:' + (i <= index ? '100%' : '0') + '"></b></i>'; }).join('');
  };
  var show = function (dir) {
    stopAudio();
    var page = document.createElement('div');
    page.className = 'rdr-page' + (dir && motionOn() ? (dir > 0 ? ' in-next' : ' in-prev') : '');
    pages.innerHTML = '';
    pages.appendChild(page);
    drawBars();
    var summary = topics[index];
    topicStory(page, summary, opts.initial && opts.initial._id === summary._id ? opts.initial : null, topics.length > 1 ? (index + 1) + ' / ' + topics.length : null, self);
    [topics[index + 1], topics[index - 1]].forEach(function (t) { if (t) getTopic(t._id).catch(function () {}); });
  };
  self.goTo = function (target) {
    if (!topics[target] || target === index) return;
    var dir = target > index ? 1 : -1;
    index = target;
    if (opts.onNavigate) opts.onNavigate(index, topics[index]);
    show(dir);
  };
  self.next = function () { self.goTo(index + 1); };
  self.prev = function () { self.goTo(index - 1); };
  self.hasNext = function () { return index < topics.length - 1; };
  self.hasPrev = function () { return index > 0; };
  var onKey = function (e) {
    if (e.target.closest && e.target.closest('input, textarea')) return;
    if (document.querySelector('.drw')) return; // chat open: its own keys
    if (e.key === 'Escape') self.close();
    else if (e.key === 'ArrowRight' || e.key === 'ArrowDown') self.goTo(index + 1);
    else if (e.key === 'ArrowLeft' || e.key === 'ArrowUp') self.goTo(index - 1);
  };
  window.addEventListener('keydown', onKey);
  self.close = function (silent) {
    stopAudio();
    window.removeEventListener('keydown', onKey);
    document.body.style.overflow = prevOverflow;
    READER = null;
    var done = function () { root.remove(); if (!silent && opts.onClose) opts.onClose(); };
    if (motionOn() && !silent) { root.classList.remove('opening'); root.classList.add('closing'); setTimeout(done, 240); } else done();
  };
  READER = self;
  show(0);
  return self;
}

function topicStory(page, summary, initial, place, reader) {
  page.innerHTML = '<div class="story-wrap"><div class="skel"></div></div>';
  var wrap = page.firstChild;
  var go = function (full, error) {
    if (!page.isConnected) return;
    var topic = Object.assign({}, full || {}, summary);
    var slides = buildSlides(full, summary);
    if (!slides.length) {
      wrap.innerHTML = '';
      slides = [{ section: 'Тойм', text: error ? 'Сэдвийг ачаалж чадсангүй.' : (topic.title || ''), motif: guessMotif(topic), tone: 'neutral', seed: 0 }];
    }
    keyPointCards(wrap, slides, topic, place, reader);
  };
  if (initial) go(initial);
  else getTopic(summary._id).then(function (t) { go(t); }, function () { go(null, true); });
}

function keyPointCards(wrap, points, topic, place, reader) {
  var index = 0, dir = 1, press = null, count = points.length;
  var c = cat(topic.category);
  wrap.innerHTML = '<section class="kpc" tabindex="0" aria-label="Товчхондоо" aria-roledescription="carousel">' +
    '<div class="scene-host"></div><div class="kpc-shade"></div>' +
    '<div class="kpc-hdr"><div class="r1"><span class="catb" style="background:' + c.hex + '">' + ico(c.icon, 16) + esc((topic.rank ? '#' + topic.rank + ' · ' : '') + c.label) + '</span>' +
    (place ? '<span class="caption" style="opacity:.8">' + esc(place) + '</span>' : '') + '<span style="flex:1"></span>' +
    '<button class="ibtn" data-share aria-label="Хуваалцах">' + ico('solar:share-bold', 20) + '</button>' +
    '<button class="ibtn" data-close aria-label="Хаах">' + ico('mingcute:close-line', 22) + '</button></div>' +
    '<h1>' + esc(topic.title) + '</h1></div>' +
    '<div class="kpc-bars"><div class="bars"></div><div class="kpc-sec"></div></div>' +
    '<button class="kpc-arrow l" aria-label="Өмнөх">' + ico('eva:arrow-ios-back-fill', 20) + '</button>' +
    '<button class="kpc-arrow r" aria-label="Дараах">' + ico('eva:arrow-ios-forward-fill', 20) + '</button>' +
    '<div class="kpc-mid"><div class="kpc-gap"></div><div class="slide-txt" aria-live="polite"></div></div>' +
    '<div class="kpc-foot"><div class="aud-host"></div><div class="chat-host"></div></div>' +
    '<img class="pre" alt="" referrerpolicy="no-referrer" style="display:none"></section>';
  var card = wrap.querySelector('.kpc');
  var host = card.querySelector('.scene-host');
  var txt = card.querySelector('.slide-txt');
  [card.querySelector('.kpc-hdr'), card.querySelector('.kpc-foot'), card.querySelector('.kpc-arrow.l'), card.querySelector('.kpc-arrow.r')].forEach(stopTap);
  card.querySelector('[data-close]').onclick = function () { reader.close(); };
  var shareBtn = card.querySelector('[data-share]');
  shareBtn.onclick = function () {
    var url = location.origin + '/topics/' + encodeURIComponent(topic._id);
    if (navigator.share) { navigator.share({ title: topic.title, url: url }).catch(function () {}); return; }
    if (navigator.clipboard) navigator.clipboard.writeText(url).then(function () { shareBtn.innerHTML = ico('solar:check-circle-bold', 20); shareBtn.setAttribute('aria-label', 'Холбоос хуулсан'); });
  };
  card.querySelector('.kpc-arrow.l').onclick = function () { goTo(index - 1); };
  card.querySelector('.kpc-arrow.r').onclick = function () { goTo(index + 1); };
  topicAudioButton(card.querySelector('.aud-host'), topic._id);
  chatButton(card.querySelector('.chat-host'), topic);
  var edge = function (step) { if (step < 0) { if (reader.hasPrev()) reader.prev(); } else if (reader.hasNext()) reader.next(); };
  var goTo = function (target) {
    if (target < 0 || target >= count) { edge(target < 0 ? -1 : 1); return; }
    if (target === index) return;
    dir = target > index ? 1 : -1;
    index = target;
    draw();
  };
  var fit = function () {
    var st = txt.querySelector('.st');
    if (!st) return;
    var size = textSize(points[index].text);
    st.style.fontSize = size + 'px';
    while (txt.scrollHeight > txt.clientHeight && size > MIN_TEXT_PX) { size -= 1; st.style.fontSize = size + 'px'; }
  };
  var draw = function () {
    var pt = points[index];
    // bars
    var bars = card.querySelector('.bars');
    if (count > MAX_BARS) bars.innerHTML = '<i><b style="width:' + ((index + 1) / count) * 100 + '%"></b></i>';
    else bars.innerHTML = points.map(function (x, i) { return '<i><b style="width:' + (i <= index ? '100%' : '0') + '"></b></i>'; }).join('');
    card.querySelector('.kpc-sec').textContent = (pt.section || 'Товчхондоо') + ' · ' + (index + 1) + ' / ' + count;
    card.querySelector('.kpc-arrow.l').style.visibility = index > 0 ? '' : 'hidden';
    card.querySelector('.kpc-arrow.r').style.visibility = index < count - 1 ? '' : 'hidden';
    // scene (crossfade: new layer on top, old removed after the fade)
    var layer = document.createElement('div');
    layer.className = 'scene-wrap';
    host.appendChild(layer);
    while (host.children.length > 2) host.removeChild(host.firstChild);
    var old = host.children.length > 1 ? host.firstChild : null;
    if (old) setTimeout(function () { if (old.parentNode) old.remove(); }, motionOn() ? 420 : 0);
    mountScene(layer, pt);
    // text
    var tone = TONE_COLORS[pt.tone] || c.hex;
    txt.className = 'slide-txt' + (dir < 0 ? ' back' : '');
    void txt.offsetWidth; // restart the slide-in animation
    txt.innerHTML = (pt.caption ? '<span class="caption-chip"><i style="background:' + tone + '"></i>' + esc(pt.caption) + '</span>' : '') +
      (pt.kicker ? '<p class="kick" style="color:' + c.hex + '">' + esc(pt.kicker) + '</p>' : '') +
      '<p class="st">' + esc(pt.text) + '</p>';
    fit();
    if (document.fonts && document.fonts.ready) document.fonts.ready.then(fit);
    var nx = points[index + 1];
    var pre = card.querySelector('img.pre');
    if (nx && nx.photo) pre.src = nx.photo;
  };
  card.addEventListener('pointerdown', function (e) { press = { x: e.clientX, y: e.clientY, at: Date.now() }; });
  card.addEventListener('pointercancel', function () { press = null; });
  card.addEventListener('pointerup', function (e) {
    var s = press; press = null;
    if (!s) return;
    var dx = e.clientX - s.x, dy = e.clientY - s.y;
    if (Math.abs(dy) > 50 && Math.abs(dy) > Math.abs(dx)) edge(dy < 0 ? 1 : -1);
    else if (Math.abs(dx) > 50) goTo(index + (dx < 0 ? 1 : -1));
    else if (Date.now() - s.at < 250 && Math.abs(dy) < 10) {
      var r = card.getBoundingClientRect();
      goTo(index + (e.clientX - r.left < r.width / 3 ? -1 : 1));
    }
  });
  card.addEventListener('keydown', function (e) {
    if (e.key !== 'ArrowLeft' && e.key !== 'ArrowRight') return;
    e.stopPropagation();
    goTo(index + (e.key === 'ArrowRight' ? 1 : -1));
  });
  if ('ResizeObserver' in window) new ResizeObserver(fit).observe(card);
  draw();
}

// ---------------------------------------------------------------------- audio FAB (topic-audio-player.jsx + use-topic-audio.js)

var AUDIO = null;
function stopAudio() { if (AUDIO) { AUDIO.destroy(); AUDIO = null; } }
function topicAudioButton(host, topicId) {
  stopAudio();
  var st = { status: 'idle', parts: [], index: 0, progress: 0 };
  var audio = new window.Audio();
  audio.preload = 'auto';
  var partUrl = function (i) { return '/api/topics/' + encodeURIComponent(topicId) + '/audio/' + i + '.mp3'; };
  var warm = function (from) { for (var i = from; i < Math.min(from + 2, st.parts.length); i++) fetch(partUrl(i)).catch(function () {}); };
  var playPart = function (i) {
    st.index = i; st.progress = 0; st.status = 'loading'; render();
    audio.src = partUrl(i);
    audio.play().catch(function () { st.status = 'error'; render(); });
  };
  var reset = function () { st.index = 0; st.status = 'idle'; st.progress = 0; render(); };
  audio.addEventListener('playing', function () { st.status = 'playing'; render(); warm(st.index + 1); });
  audio.addEventListener('pause', function () { if (!audio.ended && st.status === 'playing') { st.status = 'paused'; render(); } });
  audio.addEventListener('timeupdate', function () { if (audio.duration) { st.progress = audio.currentTime / audio.duration; renderRing(); } });
  audio.addEventListener('ended', function () { if (st.index + 1 < st.parts.length) playPart(st.index + 1); else reset(); });
  audio.addEventListener('error', function () { if (audio.getAttribute('src')) { st.status = 'error'; render(); } });
  var play = function () {
    if (st.status === 'paused') { audio.play(); return; }
    playPart(0);
    api('/api/topics/' + encodeURIComponent(topicId) + '/audio').then(function (d) { if (d && d.parts) { st.parts = d.parts; render(); warm(1); } }).catch(function () {});
  };
  var skip = function (step) {
    var list = st.parts, title = function (i) { return list[i] && list[i].title; }, i = st.index;
    if (step > 0) { while (i < list.length && title(i) === title(st.index)) i++; }
    else if (i > 0 && title(i - 1) === title(i)) { while (i > 0 && title(i - 1) === title(i)) i--; }
    else if (i > 0) { i--; while (i > 0 && title(i - 1) === title(i)) i--; }
    if (i !== st.index && i >= 0 && i < list.length) playPart(i);
  };
  var overall = function () { return st.parts.length ? (st.index + st.progress) / st.parts.length : 0; };
  var renderRing = function () {
    var ring = host.querySelector('.aud-ring circle.v');
    if (ring) ring.setAttribute('stroke-dashoffset', String(188.5 * (1 - overall())));
  };
  var LABELS = { idle: 'Сэдвийг сонсох', error: 'Дахин оролдох', loading: 'Бэлдэж байна', playing: 'Түр зогсоох', paused: 'Үргэлжлүүлэх' };
  var ICONS = { idle: 'solar:headphones-round-sound-bold', error: 'solar:refresh-bold', playing: 'solar:pause-bold', paused: 'solar:play-bold' };
  var render = function () {
    var active = st.status !== 'idle' && st.status !== 'error';
    var part = st.parts[st.index];
    var nextDisabled = !st.parts.length || st.parts.slice(st.index).every(function (p) { return p.title === (part && part.title); });
    host.innerHTML = '<div class="aud">' +
      (active ? '<div class="aud-panel" role="region" aria-label="Сэдвийг сонсож байна"><div style="flex:1;min-width:0"><span class="caption muted" style="display:block">' + (st.status === 'loading' ? 'Бэлдэж байна…' : 'Сонсож байна') + '</span><p class="sub2 nowrap">' + esc((part && part.title) || 'Сэдэв') + '</p></div>' +
        '<button class="ibtn sm" data-a="prev" aria-label="Өмнөх хэсэг"' + (st.index === 0 ? ' disabled' : '') + '>' + ico('solar:skip-previous-bold', 18) + '</button>' +
        '<button class="ibtn sm" data-a="next" aria-label="Дараах хэсэг"' + (nextDisabled ? ' disabled' : '') + '>' + ico('solar:skip-next-bold', 18) + '</button>' +
        '<button class="ibtn sm" data-a="stop" aria-label="Сонсохыг зогсоох">' + ico('mingcute:close-line', 18) + '</button></div>' : '') +
      '<button class="fab paper" data-a="main" aria-label="' + LABELS[st.status] + '"' + (st.status === 'loading' ? ' disabled' : '') + '>' + (st.status === 'loading' ? '<span class="spin"></span>' : ico(ICONS[st.status], 26)) + '</button>' +
      (active ? '<svg class="aud-ring" viewBox="0 0 64 64" aria-hidden="true"><circle class="v" cx="32" cy="32" r="30" fill="none" stroke="#007BFF" stroke-width="2" stroke-dasharray="188.5" stroke-dashoffset="188.5"/></svg>' : '') + '</div>';
    renderRing();
    host.querySelectorAll('[data-a]').forEach(function (b) {
      b.onclick = function () {
        var a = b.getAttribute('data-a');
        if (a === 'main') { if (st.status === 'playing') audio.pause(); else play(); }
        else if (a === 'prev') skip(-1);
        else if (a === 'next') skip(1);
        else if (a === 'stop') { audio.pause(); reset(); }
      };
    });
  };
  AUDIO = { destroy: function () { audio.pause(); audio.removeAttribute('src'); } };
  render();
}

// ---------------------------------------------------------------------- topic chat (chat-drawer.jsx + use-chat.js + chat-cards.jsx)

var TOPIC_PROMPTS = ['Товчхондоо юу болсон бэ?', 'Яагаад энэ чухал вэ?', 'Цаашид юу болох вэ?', 'Хэн юу гэж хэлсэн бэ?'];
var CHAT_TOPIC = null;
function chatLoad(id) { try { return JSON.parse(sessionStorage.getItem('sedev-chat:' + id) || '[]'); } catch (e) { return []; } }
function chatSave(id, m) { try { sessionStorage.setItem('sedev-chat:' + id, JSON.stringify(m.slice(-30))); } catch (e) { /* page view only */ } }
function chatButton(host, topic) {
  CHAT_TOPIC = topic;
  host.innerHTML = '<button class="fab primary" title="Энэ сэдвийн талаар асуух (⌘J)" aria-label="Энэ сэдвийн талаар асуух">' + ico('solar:chat-round-dots-bold', 28) + '</button>';
  host.firstChild.onclick = function () { openChat(topic); };
}
window.addEventListener('keydown', function (e) {
  if ((e.metaKey || e.ctrlKey) && e.key && e.key.toLowerCase() === 'j' && CHAT_TOPIC && READER) {
    e.preventDefault();
    var open = document.querySelector('.drw');
    if (open) closeChat(); else openChat(CHAT_TOPIC);
  }
});
function closeChat() { document.querySelectorAll('.drw, .drw-back').forEach(function (el) { el.remove(); }); }
function answerHtml(text) {
  return String(text).split(/(\[[TA]\d+\])/g).map(function (piece) { return /^\[[TA]\d+\]$/.test(piece) ? '<span class="cite">' + esc(piece.slice(1, -1)) + '</span>' : esc(piece); }).join('');
}
function chatCardsHtml(m) {
  var arts = m.articles || [], text = m.text || '';
  var cited = arts.map(function (a, i) { return { a: a, id: 'A' + (i + 1) }; }).filter(function (x) { return text.indexOf('[' + x.id + ']') >= 0; });
  if (!cited.length) return '';
  return '<div style="margin-top:8px;gap:8px;display:flex;flex-direction:column">' + cited.map(function (x) {
    return '<a class="acard" href="' + esc(x.a.url) + '" target="_blank" rel="noopener"><div style="margin-bottom:4px;gap:6px;display:flex;align-items:center"><span class="label">' + x.id + '</span><span class="caption muted nowrap">' + esc(x.a.sourceName) + ' · ' + esc(x.a.publishedDay) + '</span>' + ico('solar:arrow-right-up-linear', 14, 'margin-left:auto;color:var(--text3)') + '</div><span class="body2 clamp2">' + esc(x.a.title) + '</span></a>';
  }).join('') + '</div>';
}
function openChat(topic) {
  closeChat();
  var messages = chatLoad(topic._id), busy = false;
  var back = document.createElement('div'); back.className = 'drw-back';
  var d = document.createElement('div'); d.className = 'drw'; d.setAttribute('role', 'dialog');
  d.innerHTML = '<div class="drw-hd">' + ico('solar:chat-round-dots-bold-duotone', 28, 'color:var(--primary)') + '<div style="flex:1;min-width:0"><p class="sub1">Сэдэв туслах</p><span class="caption muted clamp1">' + esc(topic.title) + '</span></div>' +
    '<button class="ibtn" data-reset aria-label="Шинэ яриа" title="Шинэ яриа">' + ico('solar:restart-bold', 22) + '</button><button class="ibtn" data-x aria-label="Хаах">' + ico('mingcute:close-line', 22) + '</button></div>' +
    '<div class="drw-bd"></div><form class="drw-ft"><input maxlength="500" aria-label="Асуулт" placeholder="Энэ сэдвийн талаар асуух…" autocomplete="off"><button class="ibtn" type="submit" aria-label="Илгээх" style="color:var(--primary)">' + ico('solar:plain-2-bold', 22) + '</button></form>';
  document.body.appendChild(back); document.body.appendChild(d);
  var bd = d.querySelector('.drw-bd'), input = d.querySelector('input'), send = d.querySelector('[type=submit]');
  back.onclick = closeChat;
  d.querySelector('[data-x]').onclick = closeChat;
  d.querySelector('[data-reset]').onclick = function () { messages = []; chatSave(topic._id, messages); render(); };
  d.addEventListener('keydown', function (e) { if (e.key === 'Escape') { e.stopPropagation(); closeChat(); } });
  var render = function () {
    var h = '';
    if (!messages.length) h += '<p class="body2 muted" style="margin:0">Зөвхөн энэ сэдвийн талаар асуугаарай. Хариулт нь сэдвийн задлал, нийтлэлүүдэд тулгуурлана.</p><div style="gap:8px;display:flex;flex-wrap:wrap">' + TOPIC_PROMPTS.map(function (t) { return '<button class="chip" data-ask="' + esc(t) + '">' + esc(t) + '</button>'; }).join('') + '</div>';
    messages.forEach(function (m) {
      if (m.role === 'user') h += '<div class="bub mine">' + esc(m.text) + '</div>';
      else h += '<div style="display:flex;flex-direction:column">' + (m.error ? '<div class="bub"><span style="color:var(--error)">' + esc(m.error) + '</span></div>' : (m.text ? '<div class="bub">' + answerHtml(m.text) + '</div>' : '')) + chatCardsHtml(m) + '</div>';
    });
    if (busy) h += '<div class="bub"><span style="gap:4px;display:flex;align-items:center;color:var(--text2)">' + ico('svg-spinners:3-dots-fade', 20) + 'Хайж байна…</span></div>';
    bd.innerHTML = h + '<div data-end></div>';
    d.querySelector('[data-reset]').style.display = messages.length ? '' : 'none';
    send.disabled = !input.value.trim() || busy;
    bd.querySelectorAll('[data-ask]').forEach(function (b) { b.onclick = function () { ask(b.getAttribute('data-ask')); }; });
    bd.querySelector('[data-end]').scrollIntoView({ block: 'end', behavior: 'smooth' });
  };
  var ask = function (text) {
    var history = messages.filter(function (m) { return m.text; }).slice(-4).map(function (m) { return { role: m.role, text: m.text.slice(0, 1500) }; });
    messages.push({ role: 'user', text: text }); busy = true; render();
    api('/api/chat', { method: 'POST', body: { message: text, history: history, topicId: topic._id }, strict: true }).then(function (data) {
      messages.push(Object.assign({ role: 'assistant' }, data || {}, { text: (data && data.answer) || '' }));
    }, function (err) {
      messages.push({ role: 'assistant', text: '', error: err.message });
    }).then(function () { busy = false; chatSave(topic._id, messages); render(); });
  };
  d.querySelector('form').onsubmit = function (e) {
    e.preventDefault();
    var text = input.value.trim();
    if (!text || busy) return;
    input.value = '';
    ask(text);
  };
  input.oninput = function () { send.disabled = !input.value.trim() || busy; };
  render();
  input.focus();
}

// ---------------------------------------------------------------------- /topics/:id page (topic-reader-view.jsx + app/(deck)/topics/[id]/page.jsx)

function deckInit(id) {
  var mount = document.getElementById('deck');
  api('/api/topics/' + encodeURIComponent(id)).then(function (topic) {
    if (!topic) throw Object.assign(new Error('nf'), { status: 404 });
    var editionP = topic.editionId ? apiSoft('/api/editions/' + encodeURIComponent(topic.editionId)) : Promise.resolve(null);
    return editionP.then(function (edition) {
      var topics = rankTopics((edition && edition.topics) || []);
      var start = topics.findIndex(function (t) { return t._id === topic._id; });
      if (start < 0) {
        topics = [{ _id: topic._id, title: topic.title, reason: topic.reason, category: topic.category, image: topic.image, hot: topic.hot, keypoints: [] }];
        start = 0;
      }
      TOPIC_CACHE[topic._id] = Promise.resolve(topic);
      mount.innerHTML = '';
      openReader({
        topics: topics, index: start, initial: topic,
        onNavigate: function (i, t) {
          history.replaceState(history.state, '', '/topics/' + encodeURIComponent(t._id));
          document.title = t.title + ' · Сэдэв.мн';
        },
        onClose: function () {
          var entry = performance.getEntriesByType ? performance.getEntriesByType('navigation')[0] : null;
          var landed = new URL((entry && entry.name) || '/', location.href).pathname;
          if (landed === '/topics/' + encodeURIComponent(topic._id) || landed === '/topics/' + topic._id || history.length < 2) location.replace('/');
          else history.back();
        },
      });
    });
  }).catch(function (e) {
    mount.innerHTML = '<div class="container" style="padding-top:64px">' + emptyHtml(e && e.status === 404 ? 'Сэдэв олдсонгүй' : 'Алдаа гарлаа', e && e.status === 404 ? 'Энэ сэдэв устсан эсвэл нуугдсан байна.' : (e && e.message) || '') + '<p style="text-align:center"><a class="btn btn-contained" href="/">Нүүр хуудас</a></p></div>';
  });
}
