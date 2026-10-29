// pages_reader - public reader site (port of sedev-frontend app/(reader)/*, app/(deck)/topics/[id],
// app/layout + manifest, sections home/billboard/edition/reader/chat/audio/people/social).
// Spaghetti on purpose: every page is one big string glued together right here, with its own
// inline <style>/<script>. The shared heavy JS/CSS (billboard, card deck, charts, chat, audio) lives
// in reader_assets/reader.js + reader.css and is served by this file with proper MIME types.
// Data always comes from the same-origin /api/* JSON endpoints in the browser (no SSR of content),
// only the <title> of a topic / person is looked up in the Db store here.

var
  RdAssetHits: Integer = 0; // nobody reads this, it just counts. globals are fun

const
  RD_APP = 'Сэдэв.мн';
  RD_DESC = 'Монголын мэдээг эх сурвалжтай нь задлан шинжилсэн сэдвүүд';
  RD_ICONIFY = 'https://cdn.jsdelivr.net/npm/iconify-icon@2.1.0/dist/iconify-icon.min.js';

// js string literal safe inside <script>
function RdJs(const s: string): string;
begin
  Result := StringReplace(JEsc(s), '<', '\u003c', [rfReplaceAll]);
end;

function RdNavLink(const path, title, icon, cls: string): string;
var
  on: Boolean;
begin
  if path = '/' then on := ReqPath = '/'
  else on := Copy(ReqPath, 1, Length(path)) = path;
  if cls = 'bnav' then
  begin
    Result := '<a href="' + path + '"';
    if on then Result := Result + ' class="on" aria-current="page"';
    Result := Result + '><iconify-icon icon="' + icon + '" width="24" height="24"></iconify-icon>' + title + '</a>';
  end
  else
  begin
    Result := '<a class="btn';
    if on then Result := Result + ' on';
    Result := Result + '" href="' + path + '">' + title + '</a>';
  end;
end;

// the whole html document. chrome = header/footer/bottom nav (the (reader) layout); the deck has none
function RdPage(const title, body, scripts: string; chrome: Boolean): string;
var
  s: string;
begin
  s := '<!doctype html><html lang="mn"><head><meta charset="utf-8">' +
    '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">' +
    '<meta name="theme-color" content="#007BFF">' +
    '<title>' + HtmlEsc(title) + '</title>' +
    '<meta name="description" content="' + HtmlEsc(RD_DESC) + '">' +
    '<link rel="icon" href="/favicon.ico?v=2" sizes="any">' +
    '<link rel="icon" href="/logo/icon-96.png?v=2" type="image/png" sizes="96x96">' +
    '<link rel="apple-touch-icon" href="/logo/apple-touch-icon.png?v=2">' +
    '<link rel="manifest" href="/manifest.webmanifest">' +
    '<meta name="apple-mobile-web-app-capable" content="yes"><meta name="mobile-web-app-capable" content="yes">' +
    '<meta name="apple-mobile-web-app-title" content="Сэдэв"><meta name="apple-mobile-web-app-status-bar-style" content="default">' +
    '<link rel="preload" href="/fonts/inter-cyrillic-wght-normal.woff2" as="font" type="font/woff2" crossorigin>' +
    '<link rel="stylesheet" href="/reader.css?v=1">' +
    '<script src="' + RD_ICONIFY + '"></script>' +
    '</head><body>';
  if chrome then
  begin
    s := s + '<header class="hdr"><div class="container hdr-in">' +
      '<a class="logo" href="/" aria-label="sedev.mn"><img src="/logo/logo-mark.png" alt=""><span>Sedev.<b>mn</b></span></a>' +
      '<nav class="hdr-nav">' +
      RdNavLink('/', 'Нүүр', '', '') + RdNavLink('/people', 'Хүмүүс', '', '') +
      RdNavLink('/social', 'Сошиал', '', '') + RdNavLink('/archive', 'Архив', '', '') +
      '</nav><div class="hdr-tools"><button class="ibtn" id="motion-btn" aria-label="Хөдөлгөөн"></button></div>' +
      '</div></header><main class="main">' + body + '</main>' +
      '<footer class="ftr"><div class="container"><p>Сэдэв.мн — Монголын мэдээг эх сурвалжтай нь задлан шинжилсэн сэдвүүд. ' +
      'Агуулгыг AI иш татсан нийтлэлүүдээс бэлтгэв.</p></div></footer>' +
      '<nav class="bnav" aria-label="Үндсэн цэс">' +
      RdNavLink('/', 'Нүүр', 'solar:home-2-linear', 'bnav') +
      RdNavLink('/people', 'Хүмүүс', 'solar:users-group-rounded-linear', 'bnav') +
      RdNavLink('/social', 'Сошиал', 'solar:chat-round-like-linear', 'bnav') +
      RdNavLink('/archive', 'Архив', 'solar:archive-linear', 'bnav') +
      '</nav>';
  end
  else
    s := s + body;
  s := s + '<script src="/reader.js?v=1"></script>' + scripts;
  if chrome then s := s + '<script>readerBoot();</script>'
  else s := s + '<script>readerBoot({install:false});</script>';
  Result := s + '</body></html>';
end;

// ---------------------------------------------------------------------- social trending (sections/social), inline js
// used by /social and by the home page Сошиал tab. double quotes only in here (pascal strings!)
function RdTrendingJs: string;
begin
  Result := '<script>' +
    'var PLATFORMS = { facebook: { label: "Facebook", icon: "logos:facebook" }, x: { label: "X", icon: "ri:twitter-x-fill" }, threads: { label: "Threads", icon: "ri:threads-fill" } };' +
    'function getPlatform(k) { return PLATFORMS[k] || { label: k, icon: "solar:share-bold" }; }' +
    'function fCount(n) { if (!n) return "0"; if (n >= 1e6) return (n / 1e6).toFixed(1).replace(/\.0$/, "") + "M"; if (n >= 1e3) return (n / 1e3).toFixed(1).replace(/\.0$/, "") + "K"; return String(Math.round(n)); }' +
    'var METRICS = [["likes", "solar:heart-bold"], ["comments", "solar:chat-round-dots-bold"], ["shares", "solar:share-bold"], ["views", "solar:eye-bold"]];' +
    'function postCard(p) {' +
    '  var pl = getPlatform(p.platform), a = p.author || {};' +
    '  var h = `<div class="post"><div style="gap:8px;display:flex;align-items:center">${ico(pl.icon, 22)}<div style="min-width:0;flex:1">` +' +
    '    `<p class="sub2 nowrap">${esc(a.name || a.handle || "")}</p><p class="caption muted nowrap" style="margin:0">${a.followers ? fCount(a.followers) + " дагагч · " : ""}${p.postedAt ? esc(fDateTimeUB(p.postedAt)) : ""}</p></div>` +' +
    '    (p.hot !== undefined && p.hot !== null ? `<span title="Hot оноо" class="caption" style="gap:2px;display:flex;align-items:center;color:var(--error)">${ico("solar:fire-bold", 16)}${Number(p.hot).toFixed(1)}</span>` : "") + `</div>` +' +
    '    `<p class="ptext">${esc(p.text)}</p>` +' +
    '    (p.image ? `<img class="pimg" src="${esc(p.image)}" alt="" loading="lazy" referrerpolicy="no-referrer" onerror="this.remove()">` : "") +' +
    '    `<div class="metrics">` + METRICS.filter(function (m) { return p.metrics && p.metrics[m[0]]; }).map(function (m) { return `<span>${ico(m[1], 14)}${fCount(p.metrics[m[0]])}</span>`; }).join("") + `</div>` +' +
    '    `<div class="pfoot">` + (p.topic ? `<a class="ptopic" href="/topics/${encodeURIComponent(p.topic._id)}" title="${esc(p.topic.title)}">${ico("solar:document-text-bold-duotone", 14)}<span class="nowrap">${esc(p.topic.title)}</span></a>` : "") +' +
    '    `<a class="orig" href="${esc(p.url)}" target="_blank" rel="noopener noreferrer">${esc(pl.label)}-д харах</a></div></div>`;' +
    '  return h;' +
    '}' +
    'function trendingView(root, initial, embedded) {' +
    '  var platform = "all", pages = [initial || []], loading = false, obs = null, PAGE = 24;' +
    '  var hasMore = function () { var l = pages[pages.length - 1]; return !!l && l.length === PAGE; };' +
    '  var tabs = [["all", "Бүгд", ""]].concat(Object.keys(PLATFORMS).map(function (k) { return [k, PLATFORMS[k].label, PLATFORMS[k].icon]; }));' +
    '  var draw = function () {' +
    '    var posts = [].concat.apply([], pages.filter(Boolean));' +
    '    var h = (embedded ? "" : `<h1 class="h3" style="margin-bottom:8px">Сошиал трэнд</h1>`) +' +
    '      `<p class="muted" style="margin:0 0 24px">Сүүлийн 48 цагт сэтгүүлч, пейж, нөлөөлөгчдийн хамгийн их хандалттай постууд. Оноо нь эх сурвалжийн нэр хүнд, идэвх, шинэ эсэхээр тооцогдоно.</p>` +' +
    '      `<div class="tabs" style="margin-bottom:32px">` + tabs.map(function (t) { return `<button class="tab${t[0] === platform ? " on" : ""}" data-pl="${t[0]}">${t[2] ? ico(t[2], 18) : ""}${t[1]}</button>`; }).join("") + `</div>`;' +
    '    if (pages[0] === null) h += LOADING;' +
    '    else if (!posts.length) h += emptyHtml("Одоогоор пост алга");' +
    '    h += `<div class="grid3 g24">` + posts.map(postCard).join("") + `</div><div class="sentinel" data-sent>${loading ? "<div class=lin></div>" : ""}</div>`;' +
    '    root.innerHTML = h;' +
    '    root.querySelectorAll("[data-pl]").forEach(function (b) { b.onclick = function () { platform = b.getAttribute("data-pl"); pages = [null]; draw(); fetchPage(1, true); }; });' +
    '    if (obs) obs.disconnect();' +
    '    var node = root.querySelector("[data-sent]");' +
    '    if (node && hasMore() && "IntersectionObserver" in window) { obs = new IntersectionObserver(function (e) { if (e[0].isIntersecting) fetchPage(pages.length + 1); }, { rootMargin: "600px" }); obs.observe(node); }' +
    '  };' +
    '  var fetchPage = function (page, replace) {' +
    '    if (loading) return; loading = true;' +
    '    var q = "/api/social/trending?page=" + page + "&limit=" + PAGE + (platform !== "all" ? "&platform=" + platform : "");' +
    '    apiSoft(q).then(function (r) { loading = false; r = Array.isArray(r) ? r : []; if (replace) pages = [r]; else pages.push(r); var y = window.scrollY; draw(); window.scrollTo(0, y); });' +
    '  };' +
    '  if (initial === null) { pages = [null]; draw(); fetchPage(1, true); } else draw();' +
    '}' +
    '</script>';
end;

// ---------------------------------------------------------------------- people (sections/people), inline js
function RdPeopleCommonJs: string;
begin
  Result := '<script>' +
    'var MENTION_ROLES = { subject: { label: "Гол сэдэв", color: "primary" }, quoted: { label: "Иш татсан", color: "info" }, mentioned: { label: "Дурдсан", color: "" } };' +
    'function toneOf(v) { if (v === null || v === undefined) return { label: "—", color: "" }; if (v > 0.2) return { label: "Эерэг", color: "success" }; if (v < -0.2) return { label: "Сөрөг", color: "error" }; return { label: "Саармаг", color: "" }; }' +
    'function fTone(v) { return v === null || v === undefined ? "—" : (v > 0 ? "+" : "") + Number(v).toFixed(2); }' +
    'function fChange(now, before) { if (!before) return now ? "шинэ" : "0%"; var pct = Math.round(((now - before) / before) * 100); return (pct > 0 ? "+" : "") + pct + "%"; }' +
    'function fPercent(v) { return Math.round((v || 0) * 100) + "%"; }' +
    'function initialOf(p) { var s = String(p.shortName || p.name || "?"); return (s.split(".").pop() || p.name || "?")[0]; }' +
    'function avatar(p, size) { size = size || 48; return `<span class="avatar" style="width:${size}px;height:${size}px;font-size:${size * 0.42}px">${esc(initialOf(p))}</span>`; }' +
    'function sparkline(values, width, height) { values = values || []; width = width || 120; height = height || 32; var max = Math.max.apply(null, [1].concat(values)); var step = values.length > 1 ? width / (values.length - 1) : width;' +
    '  var pts = values.map(function (v, i) { return (i * step).toFixed(1) + "," + (height - 2 - (v / max) * (height - 4)).toFixed(1); }).join(" ");' +
    '  return `<svg viewBox="0 0 ${width} ${height}" style="width:${width}px;height:${height}px;color:var(--primary);flex-shrink:0" aria-hidden="true"><polyline points="${pts}" fill="none" stroke="currentColor" stroke-width="2"/></svg>`; }' +
    'function lbl(text, color, style) { return `<span class="label ${color || ""}" style="${style || ""}">${esc(text)}</span>`; }' +
    '</script>';
end;

procedure RdPeoplePage;
var
  body, js: string;
begin
  body := '<div class="container"><h1 class="h3" style="margin-bottom:8px">Хүмүүс</h1>' +
    '<p class="muted" style="margin:0 0 32px">Олон нийтийн зүтгэлтнүүд мэдээнд хэр их, ямар өнгөөр гарч байна. ' +
    'Сүүлийн 7 хоногийн дурдалтаар эрэмбэлэв; шугам нь 14 хоногийн өдөр тутмын дурдалт.</p>' +
    '<div id="people"><div class="lin" style="margin:24px 0"></div></div></div>';
  js := RdPeopleCommonJs + '<script>' +
    'apiSoft("/api/entities").then(function (people) {' +
    '  people = Array.isArray(people) ? people : [];' +
    '  var el = document.getElementById("people");' +
    '  if (!people.length) { el.innerHTML = emptyHtml("Одоогоор мэдээлэл алга"); return; }' +
    '  el.innerHTML = `<div class="grid3">` + people.map(function (p, i) { var tone = toneOf(p.tone7d);' +
    '    return `<a class="card pcard" href="/people/${encodeURIComponent(p.slug)}"><span class="sub2" style="width:20px;color:var(--text3)">${i + 1}</span>${avatar(p)}` +' +
    '      `<span style="flex:1;min-width:0;display:block"><p class="sub1 nowrap">${esc(p.shortName)}</p><span class="caption muted nowrap" style="display:block">${esc(p.title || "")}</span>` +' +
    '      `<span style="margin-top:4px;gap:8px;display:flex;align-items:center"><span class="body2"><strong>${p.mentions7d || 0}</strong> мэдээ</span><span class="caption muted">${fChange(p.mentions7d || 0, p.prev7d)}</span>` +' +
    '      (p.tone7d !== null && p.tone7d !== undefined ? lbl(tone.label, tone.color) : "") + `</span></span>${sparkline(p.spark, 72)}</a>`; }).join("") + `</div>`;' +
    '});' +
    '</script>';
  SendHtml(RdPage('Хүмүүс · ' + RD_APP, body, js, True));
end;

procedure RdPersonPage(const slug: string);
var
  title, body, js: string;
  e: TJSONObject;
begin
  title := 'Хүн · ' + RD_APP;
  try
    e := DbFindOne('entities', 'slug', slug);
    if e <> nil then
    begin
      if JStr(e, 'shortName') <> '' then title := JStr(e, 'shortName') + ' · ' + RD_APP;
      e.Free;
    end;
  except
    // store not there yet or broken: the browser sets the title later anyway
  end;
  body := '<div class="container"><div id="person" data-slug="' + HtmlEsc(slug) + '"><div class="lin" style="margin:24px 0"></div></div></div>';
  js := RdPeopleCommonJs + '<script>' +
    'var SLUG = document.getElementById("person").getAttribute("data-slug");' +
    'var TABS = [["news", "Мэдээ"], ["statements", "Мэдэгдэл"], ["coverage", "Хэвлэл мэдээлэл"], ["related", "Холбоо"], ["topics", "Сэдвүүд"]];' +
    'function trendChart(daily) {' +
    '  daily = daily || []; var w = 760, h = 300, l = 36, r = 40, t = 40, b = 28, n = Math.max(daily.length, 1);' +
    '  var maxM = Math.max.apply(null, [1].concat(daily.map(function (d) { return d.mentions || 0; })));' +
    '  var nice = Math.ceil(maxM / 4) * 4 || 4, bw = (w - l - r) / n;' +
    '  var yM = function (v) { return t + (h - t - b) * (1 - v / nice); }, yT = function (v) { return t + (h - t - b) * (1 - (v + 1) / 2); };' +
    '  var s = `<svg viewBox="0 0 ${w} ${h}" style="width:100%;height:320px;display:block;font-family:inherit" role="img" aria-label="Хандлага">`;' +
    '  for (var g = 0; g <= 4; g++) { var gy = t + (h - t - b) * g / 4; s += `<line x1="${l}" x2="${w - r}" y1="${gy}" y2="${gy}" stroke="rgba(145,158,171,0.2)" stroke-dasharray="3"/>` +' +
    '    `<text x="${l - 6}" y="${gy + 4}" font-size="11" text-anchor="end" fill="#919EAB">${Math.round(nice * (1 - g / 4))}</text><text x="${w - r + 6}" y="${gy + 4}" font-size="11" fill="#919EAB">${(1 - g / 2).toFixed(1)}</text>`; }' +
    '  var every = Math.max(1, Math.ceil(n / 10)), line = [];' +
    '  daily.forEach(function (d, i) { var x = l + i * bw; var m = d.mentions || 0;' +
    '    s += `<rect x="${(x + bw * 0.2).toFixed(1)}" y="${yM(m).toFixed(1)}" width="${(bw * 0.6).toFixed(1)}" height="${(h - b - yM(m)).toFixed(1)}" rx="2" fill="#007BFF"><title>${esc(d.day)}: ${m} мэдээ, өнгө ${fTone(d.tone === undefined ? null : d.tone)}</title></rect>`;' +
    '    if (i % every === 0) s += `<text x="${(x + bw / 2).toFixed(1)}" y="${h - 8}" font-size="11" text-anchor="middle" fill="#919EAB">${esc(String(d.day || "").slice(5).replace("-", "."))}</text>`;' +
    '    if (d.tone !== null && d.tone !== undefined) line.push([x + bw / 2, yT(d.tone)]); });' +
    '  if (line.length) { s += `<polyline points="${line.map(function (p) { return p[0].toFixed(1) + "," + p[1].toFixed(1); }).join(" ")}" fill="none" stroke="#FFAB00" stroke-width="2.5"/>` + line.map(function (p) { return `<circle cx="${p[0].toFixed(1)}" cy="${p[1].toFixed(1)}" r="3" fill="#FFAB00"/>`; }).join(""); }' +
    '  s += `<g font-size="12" fill="#637381"><rect x="${w - r - 130}" y="8" width="10" height="10" rx="5" fill="#007BFF"/><text x="${w - r - 115}" y="17">Мэдээ</text><rect x="${w - r - 60}" y="8" width="10" height="10" rx="5" fill="#FFAB00"/><text x="${w - r - 45}" y="17">Өнгө</text></g>`;' +
    '  return s + `</svg>`;' +
    '}' +
    'function tile(label, value, hint, formula, color) { return `<div class="card ttip" data-tip="${esc(formula)}"><span class="caption muted">${esc(label)}</span><div class="v" style="${color ? "color:var(--" + color + ")" : ""}">${esc(value)}</div><span class="caption muted nowrap" style="display:block">${esc(hint)}</span></div>`; }' +
    'function kpisHtml(k) { k = k || {}; var tone = toneOf(k.tone30d), hot = (k.attention || 0) >= 2;' +
    '  return `<div class="kpis">` + tile("7 хоногт", k.mentions7d || 0, fChange(k.mentions7d || 0, k.prev7d) + " өмнөх 7 хоногоос", "Сүүлийн 7 хоногт түүнийг дурдсан мэдээний тоо") +' +
    '    tile("Анхаарлын индекс", k.attention > 0 ? "+" + k.attention : (k.attention || 0), hot ? "Ердийнхөөс огцом их" : "Ердийн түвшин", "z = (сүүлийн 2 хоногийн өдрийн дундаж − өмнөх 28 хоногийн дундаж) / стандарт хазайлт. 2-оос дээш бол ердийнхөөс огцом их.", hot ? "warning" : "") +' +
    '    tile("Хэвлэлийн өнгө", fTone(k.tone30d), tone.label + ", 30 хоног", "Нийтлэл бүр түүнийг ямар өнгөөр харуулсныг AI −1…+1-ээр үнэлж, жигнэсэн дундаж (гол сэдэв, иш татсан ×1, дурдсан ×0.5). Хүний тухай үнэлгээ биш.", tone.color) +' +
    '    tile("Яриа эзлэх хувь", fPercent(k.shareOfVoice7d), "Хянаж буй хүмүүсийн дотор", "7 хоногт түүнийг дурдсан тоо / хянаж буй бүх хүний дурдалтын нийлбэр") +' +
    '    tile("Маргаантай байдал", fPercent((k.controversy30d || 0) * 2), "Эерэг, сөрөг хуваагдал", "min(эерэг, сөрөг) / (эерэг + сөрөг) × 2. 100% бол хэвлэл тэнцүү хуваагдсан.") +' +
    '    tile("Хэвлэл", k.sources30d || 0, (k.mentions30d || 0) + " мэдээ, 30 хоног", "Сүүлийн 30 хоногт түүнийг дурдсан өөр өөр хэвлэлийн тоо") + `</div>`; }' +
    'var NEWS = { role: "all", page: 1 };' +
    'function loadNews(box) {' +
    '  var roles = [["all", "Бүгд"]].concat(Object.keys(MENTION_ROLES).map(function (k) { return [k, MENTION_ROLES[k].label]; }));' +
    '  var head = `<div class="tgl">` + roles.map(function (r) { return `<button class="${r[0] === NEWS.role ? "on" : ""}" data-role="${r[0]}">${r[1]}</button>`; }).join("") + `</div>`;' +
    '  box.innerHTML = head + LOADING;' +
    '  var wire = function () { box.querySelectorAll("[data-role]").forEach(function (b) { b.onclick = function () { NEWS.role = b.getAttribute("data-role"); NEWS.page = 1; loadNews(box); }; });' +
    '    box.querySelectorAll("[data-pg]").forEach(function (b) { b.onclick = function () { NEWS.page = Number(b.getAttribute("data-pg")); loadNews(box); }; }); };' +
    '  wire();' +
    '  apiSoft("/api/entities/" + encodeURIComponent(SLUG) + "/mentions?page=" + NEWS.page + "&limit=20" + (NEWS.role !== "all" ? "&role=" + NEWS.role : "")).then(function (d) {' +
    '    var list = (d && d.mentions) || [], h = head;' +
    '    if (!list.length) h += emptyHtml("Мэдээ алга");' +
    '    h += `<div class="stack">` + list.map(function (m) { var tone = toneOf(m.sentiment), role = MENTION_ROLES[m.role] || { label: m.role || "", color: "" }, a = m.article || {};' +
    '      return `<div><span class="caption muted">${esc(m.day)} · ${esc(m.sourceName)}</span><a class="sub1" style="display:block" href="${esc(a.url || "#")}" target="_blank" rel="noopener">${esc(a.title || "—")}</a>` +' +
    '        `<p class="body2 muted" style="margin:4px 0">${esc(m.context)}</p><div style="display:flex;gap:8px">${lbl(role.label, role.color)}${lbl(tone.label, tone.color)}</div></div>`; }).join("") + `</div>`;' +
    '    if (d && d.pages > 1) { h += `<div class="pager">`; for (var i = 1; i <= d.pages; i++) h += `<button class="${i === NEWS.page ? "on" : ""}" data-pg="${i}">${i}</button>`; h += `</div>`; }' +
    '    box.innerHTML = h; wire();' +
    '  });' +
    '}' +
    'apiSoft("/api/entities/" + encodeURIComponent(SLUG)).then(function (prof) {' +
    '  var root = document.getElementById("person");' +
    '  if (!prof || !prof.entity) { document.title = "404 · Сэдэв.мн"; root.innerHTML = emptyHtml("Хуудас олдсонгүй", "Энэ хүн бүртгэлгүй байна.") + `<p style="text-align:center"><a class="btn btn-contained" href="/people">Хүмүүс</a></p>`; return; }' +
    '  var e = prof.entity, tab = "news", empty = function (t) { return `<div class="empty" style="padding:48px 0"><img src="/assets/icons/empty/ic-content.svg" alt=""><h6>${esc(t)}</h6></div>`; };' +
    '  document.title = (e.shortName || "Хүн") + " · Сэдэв.мн";' +
    '  var content = {' +
    '    statements: function () { var l = prof.statements || []; if (!l.length) return empty("30 хоногт иш татсан үг алга");' +
    '      return `<div class="stack">` + l.map(function (s) { var a = s.article || {}; return `<div><blockquote style="margin:0;font-style:italic;font-size:1.125rem;font-weight:600">“${esc(s.quote)}”</blockquote><p class="body2 muted" style="margin:4px 0">${esc(s.context)}</p><span class="caption muted">${esc(s.day)} · <a href="${esc(a.url || "#")}" target="_blank" rel="noopener">${esc(s.sourceName)}: ${esc(a.title || "")}</a></span></div>`; }).join("") + `</div>`; },' +
    '    coverage: function () { var l = prof.coverage || []; if (!l.length) return empty("30 хоногт мэдээ алга"); var max = l[0].count || 1;' +
    '      var COL = { success: "#22C55E", error: "#FF5630", "": "#637381" };' +
    '      return `<div style="display:flex;flex-direction:column;gap:12px">` + l.map(function (s) { var tone = toneOf(s.tone); return `<div style="gap:16px;display:flex;align-items:center"><span class="body2 nowrap" style="width:150px">${esc(s.name)}</span><span class="bar8"><i style="width:${(s.count / max) * 100}%;background:${COL[tone.color]}"></i></span><span class="body2" style="width:32px;text-align:right">${s.count}</span>${lbl(fTone(s.tone), tone.color, "width:64px")}</div>`; }).join("") +' +
    '        `<span class="caption muted">Хэвлэл бүр 30 хоногт хэдэн удаа дурдсан, ямар өнгөөр. Хэвлэлүүдийн хандлагын зөрүүг эндээс харна.</span></div>`; },' +
    '    related: function () { var l = prof.related || []; if (!l.length) return empty("Хамт дурдагдсан хүн алга");' +
    '      return `<div style="gap:8px;display:flex;flex-wrap:wrap">` + l.map(function (p) { return `<a class="chip" style="padding-left:4px" href="/people/${encodeURIComponent(p.slug)}">${avatar(p, 24)}${esc(p.shortName)} · ${p.count}</a>`; }).join("") + `<span class="caption muted" style="width:100%">30 хоногт нэг мэдээнд хамт гарсан хүмүүс, хамт гарсан мэдээний тоогоор.</span></div>`; },' +
    '    topics: function () { var l = prof.topics || []; if (!l.length) return empty("Сэдэвт ороогүй");' +
    '      return `<div style="display:flex;flex-direction:column;gap:12px">` + l.map(function (t) { return `<div style="gap:8px;display:flex;align-items:center">${lbl(cat(t.category).label)}<a class="sub2" href="/topics/${encodeURIComponent(t._id)}">${esc(t.title)}</a></div>`; }).join("") + `</div>`; },' +
    '  };' +
    '  root.innerHTML = `<a class="btn" href="/people" style="margin-bottom:16px">${ico("eva:arrow-ios-back-fill", 20)}Хүмүүс</a>` +' +
    '    `<div style="gap:16px;margin-bottom:24px;display:flex;align-items:center">${avatar(e, 72)}<div><h1 class="h3">${esc(e.name)}</h1><p class="muted" style="margin:0">${esc(e.title || "")}</p></div></div>` +' +
    '    (e.description ? `<p style="margin:0 0 24px">${esc(e.description)}</p>` : "") +' +
    '    `<div style="display:flex;flex-direction:column;gap:24px">${kpisHtml(prof.kpis)}` +' +
    '    `<div class="card"><div style="padding:24px 24px 0"><p class="sub1" style="font-size:1.125rem">Хандлага</p><p class="body2 muted" style="margin:4px 0 0">Өдөр бүр дурдсан мэдээ ба хэвлэлийн өнгө (−1…+1)</p></div><div style="padding:16px">${trendChart(prof.daily)}</div></div>` +' +
    '    `<div class="card"><div class="tabs" style="padding:0 16px">` + TABS.map(function (t) { return `<button class="tab${t[0] === tab ? " on" : ""}" data-tab="${t[0]}">${t[1]}</button>`; }).join("") + `</div><div id="tabbody" style="padding:24px"></div></div>` +' +
    '    `<span class="caption muted">Мэдээ бүрээс хүнийг AI таньж, ямар үүрэгтэй, ямар өнгөөр гарсныг тэмдэглэсэн. “Хэвлэлийн өнгө” нь хэвлэл түүнийг хэрхэн харуулсныг заана, хүний тухай үнэлгээ биш. Тоо бүрийн тооцоог дээр нь хулганаа аваачиж харна. Алдаа анзаарвал редакцид мэдэгдэнэ үү.</span></div>`;' +
    '  var tb = document.getElementById("tabbody");' +
    '  var show = function () { root.querySelectorAll("[data-tab]").forEach(function (b) { b.classList.toggle("on", b.getAttribute("data-tab") === tab); }); if (tab === "news") loadNews(tb); else tb.innerHTML = content[tab](); };' +
    '  root.querySelectorAll("[data-tab]").forEach(function (b) { b.onclick = function () { tab = b.getAttribute("data-tab"); show(); }; });' +
    '  show();' +
    '});' +
    '</script>';
  SendHtml(RdPage(title, body, js, True));
end;

// ---------------------------------------------------------------------- archive (edition/view/archive-view.jsx)
procedure RdArchivePage;
var
  body, js: string;
begin
  body := '<div class="container md"><h1 class="h3" style="margin-bottom:32px">Архив</h1><div id="arch"><div class="lin"></div></div></div>';
  js := '<script>' +
    'apiSoft("/api/editions").then(function (list) {' +
    '  list = Array.isArray(list) ? list : [];' +
    '  var el = document.getElementById("arch");' +
    '  if (!list.length) { el.innerHTML = emptyHtml("Архив хоосон байна", "", true); return; }' +
    '  el.innerHTML = `<div class="card list">` + list.map(function (e) {' +
    '    return `<a href="/editions/${encodeURIComponent(e._id)}"><p style="margin:0" class="body2">${esc(fDateTimeUB(e.publishedAt))}</p>` +' +
    '      `<p style="margin:0" class="body2 muted">${esc(fWindow(e.window))} · ${e.topicCount || 0} сэдэв · ${e.articleCount || 0} нийтлэл</p></a>`;' +
    '  }).join("") + `</div>`;' +
    '});' +
    '</script>';
  SendHtml(RdPage('Архив · ' + RD_APP, body, js, True));
end;

// ---------------------------------------------------------------------- home + edition + topic deck
procedure RdHomePage;
var
  body: string;
begin
  body := '<div class="container"><h1 class="vh">Өнөөдрийн сэдвүүд</h1>' +
    '<div class="segs" id="home-tabs" role="tablist" aria-label="Мэдээний төрөл"></div></div>' +
    '<div id="home-body"></div>';
  SendHtml(RdPage(RD_APP, body, RdTrendingJs + '<script>homeInit();</script>', True));
end;

procedure RdEditionPage(const id: string);
begin
  SendHtml(RdPage(RD_APP, '<div id="edition-body"></div>',
    '<script>editionPageInit(' + RdJs(id) + ');</script>', True));
end;

procedure RdTopicPage(const id: string);
var
  title: string;
  t: TJSONObject;
begin
  title := RD_APP;
  try
    if id <> '' then
    begin
      t := DbGet('topics', id);
      if t <> nil then
      begin
        if (JStr(t, 'title') <> '') and not JBool(t, 'hidden', False) then title := JStr(t, 'title') + ' · ' + RD_APP;
        t.Free;
      end;
    end;
  except
    // no topics store yet
  end;
  SendHtml(RdPage(title,
    '<div id="deck" style="position:fixed;inset:0;background:#000"><div class="lin" style="margin:0"></div></div>',
    '<script>deckInit(' + RdJs(id) + ');</script>', False));
end;

procedure RdSocialPage;
begin
  SendHtml(RdPage('Сошиал трэнд · ' + RD_APP, '<div class="container"><div id="trend"></div></div>',
    RdTrendingJs + '<script>trendingView(document.getElementById("trend"), null, false);</script>', True));
end;

// manifest.webmanifest (app/manifest.js)
procedure RdManifest;
begin
  SendRaw(200, 'application/manifest+json; charset=utf-8',
    '{"id":"/","name":' + JEsc(RD_APP) + ',"short_name":"Сэдэв","description":' + JEsc(RD_DESC) + ',' +
    '"lang":"mn","start_url":"/?source=pwa","scope":"/","display":"standalone","orientation":"portrait",' +
    '"background_color":"#F6F8FC","theme_color":"#007BFF","icons":[' +
    '{"src":"/pwa/icon-192.png","sizes":"192x192","type":"image/png","purpose":"any"},' +
    '{"src":"/pwa/icon-512.png","sizes":"512x512","type":"image/png","purpose":"any"},' +
    '{"src":"/pwa/icon-maskable-512.png","sizes":"512x512","type":"image/png","purpose":"maskable"}]}');
end;

// css/js/sw/svg of the reader from reader_assets/ with real content types (public/ would go out as octet-stream)
procedure RdAsset(const fn, ctype: string; cacheSec: Integer);
var
  s: string;
begin
  s := ReadFileStr('reader_assets/' + fn);
  if s = '' then exit; // not handled -> 404
  Inc(RdAssetHits);
  SendRaw(200, ctype, s);
  if cacheSec > 0 then Resp.SetCustomHeader('Cache-Control', 'public, max-age=' + IntToStr(cacheSec))
  else Resp.SetCustomHeader('Cache-Control', 'no-cache');
  if fn = 'sw.js' then Resp.SetCustomHeader('Service-Worker-Allowed', '/');
end;

procedure Routes_pages_reader;
begin
  if Handled then exit;
  if ReqMethod <> 'GET' then exit;
  if Route('GET', '/reader.css') then RdAsset('reader.css', 'text/css; charset=utf-8', 300)
  else if Route('GET', '/reader.js') then RdAsset('reader.js', 'application/javascript; charset=utf-8', 300)
  else if Route('GET', '/sw.js') then RdAsset('sw.js', 'application/javascript; charset=utf-8', 0)
  else if Route('GET', '/assets/icons/empty/ic-content.svg') then RdAsset('ic-content.svg', 'image/svg+xml', 86400)
  else if Route('GET', '/manifest.webmanifest') then RdManifest
  else if (ReqPath = '/') or (ReqPath = '') then RdHomePage
  else if Route('GET', '/archive') then RdArchivePage
  else if Route('GET', '/editions/:id') then RdEditionPage(P(1))
  else if Route('GET', '/people') then RdPeoplePage
  else if Route('GET', '/people/:slug') then RdPersonPage(P(1))
  else if Route('GET', '/social') then RdSocialPage
  else if Route('GET', '/topics/:id') then RdTopicPage(P(1));
end;
