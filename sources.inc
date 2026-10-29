// ===================== sources: modules/sources/* (defaults, world-defaults, model, routes, weights, recalc) + sync-sources / recalc-weights scripts =====================
// collection "sources". Doc shape = source.model.ts:
//   {slug,name,url,weight,region,enabled,discovery:{type,urls,linkPattern,itemsPath,linkTemplate,dateField,maxPages},
//    article:{type,selector,titleSelector,dateSelector,detailUrl,rootPath,titleField,textField,dateField,skipField},_id,createdAt,updatedAt}

var
  SrcIssues: TStringList = nil;

// regionFilter(region): world -> region = 'world'; mn -> region <> 'world' (also docs without the field)
function Src_InRegion(doc: TJSONData; const region: string): Boolean;
begin
  if region = 'world' then Result := JStr(doc, 'region') = 'world'
  else Result := JStr(doc, 'region') <> 'world';
end;

// copies o[name] into dst or puts the default (json text) when missing
procedure SrcCopyOr(dst, src: TJSONObject; const name, defJson: string);
var
  d: TJSONData;
begin
  d := nil;
  if src <> nil then d := src.Find(name);
  if d <> nil then dst.Add(name, d.Clone)
  else dst.Add(name, JParse(defJson));
end;

// new SourceModel(x).toObject(): schema order + defaults, extra top level keys (_id, createdAt...) kept at the end. caller frees
function Src_Canon(src: TJSONObject): TJSONObject;
var
  disc, art, sd, sa: TJSONObject;
  i: Integer;
  k: string;
begin
  Result := TJSONObject.Create;
  SrcCopyOr(Result, src, 'slug', '""');
  SrcCopyOr(Result, src, 'name', '""');
  SrcCopyOr(Result, src, 'url', '""');
  SrcCopyOr(Result, src, 'weight', '1');
  SrcCopyOr(Result, src, 'region', '"mn"');
  SrcCopyOr(Result, src, 'enabled', 'true');
  sd := JObj(src, 'discovery');
  disc := TJSONObject.Create;
  SrcCopyOr(disc, sd, 'type', '"html"');
  SrcCopyOr(disc, sd, 'urls', '[]');
  SrcCopyOr(disc, sd, 'linkPattern', '""');
  SrcCopyOr(disc, sd, 'itemsPath', '""');
  SrcCopyOr(disc, sd, 'linkTemplate', '""');
  SrcCopyOr(disc, sd, 'dateField', '""');
  SrcCopyOr(disc, sd, 'maxPages', '5');
  Result.Add('discovery', disc);
  sa := JObj(src, 'article');
  art := TJSONObject.Create;
  SrcCopyOr(art, sa, 'type', '"html"');
  SrcCopyOr(art, sa, 'selector', '""');
  SrcCopyOr(art, sa, 'titleSelector', '""');
  SrcCopyOr(art, sa, 'dateSelector', '""');
  SrcCopyOr(art, sa, 'detailUrl', '""');
  SrcCopyOr(art, sa, 'rootPath', '""');
  SrcCopyOr(art, sa, 'titleField', '"title"');
  SrcCopyOr(art, sa, 'textField', '"content"');
  SrcCopyOr(art, sa, 'dateField', '"publishedAt"');
  SrcCopyOr(art, sa, 'skipField', '""');
  Result.Add('article', art);
  if src <> nil then
    for i := 0 to src.Count - 1 do
    begin
      k := src.Names[i];
      if Result.IndexOfName(k) < 0 then Result.Add(k, src.Items[i].Clone);
    end;
end;

// ---------------------------------------------------------------- defaults.ts + world-defaults.ts

// DEFAULT_SOURCES with model defaults applied. caller frees
function Src_Defaults: TJSONArray;
var
  arr: TJSONArray;

  procedure S(const json: string);
  var
    d: TJSONData;
  begin
    d := JParse(json);
    if (d = nil) or not (d is TJSONObject) then raise Exception.Create('bad default source json: ' + Copy(json, 1, 80));
    arr.Add(Src_Canon(TJSONObject(d)));
    d.Free;
  end;

  // site(slug, name, url, weight, discovery, article)
  procedure Site(const slug, name, url, w, disc: string; const art: string = '{}');
  begin
    S('{"slug":' + JEsc(slug) + ',"name":' + JEsc(name) + ',"url":' + JEsc(url) + ',"weight":' + w + ',"discovery":' + disc + ',"article":' + art + '}');
  end;

  // listing(linkPattern, url) - pattern is json text already escaped
  function Listing(const pat, url: string): string;
  begin
    Result := '{"type":"html","urls":["' + url + '"],"linkPattern":"' + pat + '"}';
  end;

  function Rss(const url: string): string;
  begin
    Result := '{"type":"rss","urls":["' + url + '"]}';
  end;

  function Dated(const sel: string): string;
  begin
    Result := '{"type":"html","selector":"","dateSelector":' + JEsc(sel) + '}';
  end;

  procedure World(const slug, name, url, w, disc: string);
  begin
    S('{"slug":' + JEsc(slug) + ',"name":' + JEsc(name) + ',"url":' + JEsc(url) + ',"weight":' + w + ',"region":"world","discovery":' + disc + ',"article":{}}');
  end;

begin
  arr := TJSONArray.Create;
  S('{"slug":"ikon","name":"iKon","url":"https://ikon.mn/","weight":3,"discovery":{"type":"rss","urls":["https://ikon.mn/rss"],"linkPattern":"/(?:n|opinion)/[a-z0-9]+"},"article":{"type":"html","selector":".icontent, .op_left_content"}}');
  S('{"slug":"itoim","name":"iToim","url":"https://itoim.mn/","weight":2,"discovery":{"type":"json","urls":["https://customer-api.itoim.mn/pb/api/v1/article/latest?page={page}&pageSize=40"],' +
    '"linkPattern":"/a/[a-zA-Z0-9/-]+","itemsPath":"result.docs","linkTemplate":"{url}","dateField":"publishedAt","maxPages":10},' +
    '"article":{"type":"json","detailUrl":"https://customer-api.itoim.mn/pb/api/v1/article/detail?url={path}","rootPath":"result","dateField":"publishedAt","skipField":"isPaywalled"}}');
  // categories: politics, economy, society, health, education, business
  S('{"slug":"gogo","name":"GoGo","url":"https://gogo.mn/","weight":3,"discovery":{"type":"json","urls":[' +
    '"https://backend.gogo.mn/api/v1/news/2?limit=40&page={page}","https://backend.gogo.mn/api/v1/news/3?limit=40&page={page}",' +
    '"https://backend.gogo.mn/api/v1/news/7?limit=40&page={page}","https://backend.gogo.mn/api/v1/news/4?limit=40&page={page}",' +
    '"https://backend.gogo.mn/api/v1/news/9?limit=40&page={page}","https://backend.gogo.mn/api/v1/news/8?limit=40&page={page}"],' +
    '"linkPattern":"/r/[a-z0-9]+","itemsPath":"data","linkTemplate":"/r/{lid}","dateField":"publishDate","maxPages":3},' +
    '"article":{"type":"json","detailUrl":"https://backend.gogo.mn/api/v1/news/detail/{last}","rootPath":"data","dateField":"publishDate"}}');
  S('{"slug":"unuudur","name":"Өнөөдөр","url":"https://www.unuudur.mn/","weight":2.5,"discovery":{"type":"html","urls":["https://www.unuudur.mn/",' +
    '"https://www.unuudur.mn/as/politics","https://www.unuudur.mn/as/economy","https://www.unuudur.mn/as/society","https://www.unuudur.mn/as/world"],' +
    '"linkPattern":"/a/\\d+"},"article":{"type":"html","selector":"article.article-content"}}');
  S('{"slug":"zarig","name":"Зариг","url":"https://zarig.mn/","weight":1,"discovery":{"type":"sitemap","urls":["https://zarig.mn/sitemaps/news.xml"],"linkPattern":"/[a-z0-9]{4}","maxPages":1},' +
    '"article":{"type":"html","selector":".news-area__body","dateSelector":".publish__time"}}');
  S('{"slug":"medee","name":"Мэдээ.МН","url":"https://medee.mn/","weight":1.5,"discovery":{"type":"html","urls":["https://medee.mn/",' +
    '"https://medee.mn/category/politics","https://medee.mn/category/economy","https://medee.mn/category/law"],"linkPattern":"/p/\\d+"},' +
    '"article":{"type":"html","selector":"#single-content .wordwrap","titleSelector":"#single-content h1, article.panel h2, article.panel h1","dateSelector":"li.date.clearfix"}}');
  S('{"slug":"zindaa","name":"Зиндаа","url":"https://news.zindaa.mn/","weight":1.5,"discovery":{"type":"html","urls":["https://news.zindaa.mn/"],"linkPattern":"/[a-z0-9]{4}"},' +
    '"article":{"type":"html","selector":".desc_contents","dateSelector":".news-tools .left"}}');
  S('{"slug":"eagle","name":"Eagle","url":"https://eagle.mn/","weight":2.5,"discovery":{"type":"html","urls":["https://eagle.mn/"],"linkPattern":"/r/[a-zA-Z0-9]+"},' +
    '"article":{"type":"html","selector":".post-content","dateSelector":".small-600 .text-gray"}}');
  S('{"slug":"dnn","name":"Өдрийн сонин","url":"https://dnn.mn/","weight":2,"discovery":{"type":"json","urls":["https://backend.dnn.mn/api/v1/public/article?page={page}&size=40"],' +
    '"linkPattern":"/article/[a-z0-9]+","itemsPath":"list","linkTemplate":"/article/{id}","dateField":"published_at","maxPages":10},' +
    '"article":{"type":"json","detailUrl":"https://backend.dnn.mn/api/v1/public/article/{last}","dateField":"published_at"}}');
  S('{"slug":"ublife","name":"UB.life","url":"https://ub.life/","weight":1.5,"discovery":{"type":"html","urls":["https://ub.life/"],"linkPattern":"/p/[a-z0-9-]+"},' +
    '"article":{"type":"html","selector":"#article-body","dateSelector":"a:has(address) + time"}}');
  // Sites below use generic extraction (no selector) unless an article rule is given.
  Site('news', 'News.mn', 'https://news.mn/', '3', Listing('/r/\\d+/?', 'https://news.mn/'));
  Site('montsame', 'МОНЦАМЭ', 'https://montsame.mn/', '3', Listing('/mn/read/\\d+', 'https://montsame.mn/mn/'), Dated('.stat'));
  Site('mnb', 'МҮОНРТ', 'https://www.mnb.mn/', '2.5', Rss('https://www.mnb.mn/rss.xml'));
  Site('bloombergtv', 'Bloomberg TV Mongolia', 'https://bloombergtv.mn/', '2',
    '{"type":"json","urls":["https://bloombergtv.mn/api/public/news/leftlatestnews"],"linkPattern":"/news/[a-z0-9]+","itemsPath":"","linkTemplate":"/news/{slug}","dateField":"createdAt","maxPages":1}');
  Site('24tsag', '24 цаг', 'https://24tsag.mn/', '1.5', Listing('/a/\\d+', 'https://24tsag.mn/'));
  Site('isee', 'iSee.mn', 'https://isee.mn/', '1.5', Listing('/n/\\d+', 'https://isee.mn/'), Dated('.meta_date, .meta-date'));
  Site('eguur', 'Eguur.mn', 'https://eguur.mn/', '1.5', Listing('/\\d+/?', 'https://eguur.mn/'));
  Site('ubn', 'Улаанбаатар ньюс', 'https://ubn.mn/', '1.5', Listing('/articles/[a-f0-9]{24}', 'https://ubn.mn/'));
  Site('mongoltv', 'Монгол ТВ', 'https://mongoltv.mn/', '1.5',
    '{"type":"html","urls":["https://mongoltv.mn/","https://mongoltv.mn/news"],"linkPattern":"/post/\\d+"}', Dated('.posted-date'));
  Site('sonin', 'Sonin.mn', 'https://sonin.mn/', '1.5', Listing('/newsDetail/\\d+', 'https://sonin.mn/'), Dated('.dateago.red'));
  Site('shuud', 'Shuud.mn', 'https://shuud.mn/', '1.5', Listing('/a/\\d+', 'https://shuud.mn/'));
  Site('time', 'Time.mn', 'https://time.mn/', '1',
    '{"type":"json","urls":["https://admin.time.mn/wp-json/wp/v2/posts?per_page=40&page={page}&_fields=id,date"],"linkPattern":"/n/\\d+","itemsPath":"","linkTemplate":"/n/{id}","dateField":"date","maxPages":3}');
  Site('unen', 'Үнэн', 'https://unen.mn/', '1', Listing('/a/\\d+', 'https://unen.mn/'));
  Site('posted', 'Posted', 'https://posted.mn/', '1', Listing('/posts/[a-z0-9]+', 'https://posted.mn/'));
  Site('dorgio', 'Dorgio.mn', 'https://dorgio.mn/', '1', Listing('/p/\\d+', 'https://dorgio.mn/'));
  Site('updown', 'UpDown.mn', 'https://updown.mn/', '1', Rss('https://updown.mn/feed'));
  Site('ugluu', 'Өглөө.мн', 'https://ugluu.mn/', '1', Rss('https://ugluu.mn/feed'));
  Site('bolod', 'Bolod.mn', 'https://bolod.mn/', '0.5', Rss('https://bolod.mn/engine/rss.php'));
  Site('zogii', 'Zogii.mn', 'https://zogii.mn/', '0.5', Listing('/newsm/\\d+/\\d+/\\d+', 'https://zogii.mn/'), Dated('.sa-section > div'));
  Site('gereg', 'Gereg.mn', 'https://gereg.mn/', '1', Rss('https://gereg.mn/feed'));
  Site('dardas', 'MMdardas', 'https://dardas.mn/', '0.5', Listing('/news/view/\\d+', 'https://dardas.mn/'), Dated('.journalist .time'));
  Site('newslife', 'NewsLife.mn', 'https://newslife.mn/', '1', Rss('https://newslife.mn/feed'));
  Site('erennews', 'ErenNews.mn', 'https://erennews.mn/', '1', Listing('/article/\\d+', 'https://erennews.mn/'), Dated('.binduz-er-meta-date'));
  Site('tsenzuur', 'Tsenzuur.mn', 'https://tsenzuur.mn/', '1', Rss('https://tsenzuur.mn/feed'));
  Site('shuum', 'Шуум', 'https://shuum.mn/', '0.5', Listing('/news-detail/\\d+', 'https://shuum.mn/'), Dated('.date'));
  Site('shuurhai', 'Шуурхай.мн', 'https://shuurhai.mn/', '0.5', Listing('/p/\\d+', 'https://shuurhai.mn/'));
  Site('paparazzi', 'Папарацци.мн', 'https://paparazzi.mn/', '0.5', Listing('/(?=[a-z]*\\d)[a-z0-9]{3,5}', 'https://paparazzi.mn/'), Dated('.publish__time'));
  Site('tac', 'ТАС.МН', 'https://www.tac.mn/', '0.5', Rss('https://www.tac.mn/feed'));
  Site('seen', 'Seen.mn', 'https://seen.mn/', '1', Listing('/post/\\d+', 'https://seen.mn/'));
  Site('topmedee', 'TopMedee.mn', 'https://topmedee.mn/', '0.5', Listing('/i/\\d+', 'https://topmedee.mn/'), Dated('.meta-info span'));
  Site('hunnu', 'Hunnu.mn', 'https://hunnu.mn/', '0.5', Listing('/r/\\d+', 'https://hunnu.mn/'), Dated('.meta-info li'));
  Site('scandal', 'Scandal.mn', 'https://scandal.mn/', '0.5', Rss('https://scandal.mn/engine/rss.php'));
  Site('dayarmongol', 'Dayarmongol.mn', 'https://dayarmongol.mn/', '0.5', Rss('https://dayarmongol.mn/engine/rss.php'));
  Site('tumen', 'Tumen.mn', 'https://tumen.mn/', '0.5', Listing('/p/\\d+', 'https://tumen.mn/'));
  Site('tovch', 'Товч.мн', 'https://tovch.mn/', '1', Listing('/n/[a-z0-9]+', 'https://tovch.mn/'));
  Site('assa', 'ASSA', 'https://assa.mn/', '1', Listing('/a/\\d+', 'https://assa.mn/'));
  Site('choibalsan', 'Choibalsan.mn', 'https://www.choibalsan.mn/', '0.5', Rss('https://www.choibalsan.mn/engine/rss.php'));
  Site('khuvsguliinungu', 'Хөвсгөлийн өнгө', 'https://khuvsguliinungu.mn/', '0.5', Listing('/news/\\d+/single/\\d+', 'https://khuvsguliinungu.mn/'), Dated('.single_item_date'));
  // Added 2026-10-12 (all checked with probe)
  Site('montsame-en', 'Montsame (English)', 'https://montsame.mn/en/', '2', Listing('/en/read/\\d+', 'https://montsame.mn/en/'), Dated('.stat'));
  Site('polit', 'Polit.mn', 'https://polit.mn/', '1', Listing('/a/\\d+', 'https://polit.mn/'));
  Site('mmj', 'Mongolian Mining Journal', 'https://www.mongolianminingjournal.com/', '1', Listing('/a/\\d+', 'https://www.mongolianminingjournal.com/'));
  Site('arslan', 'Arslan.mn', 'https://arslan.mn/', '0.5', Listing('/[a-z/]+/\\d+\\.shtml', 'https://arslan.mn/'), Dated('.post-statistic span'));
  Site('mongolianeconomy', 'Mongolian Economy', 'https://mongolianeconomy.mn/', '1', Listing('/article/\\d+', 'https://mongolianeconomy.mn/'));
  Site('zms', 'Зууны мэдээ', 'https://zms.mn/', '1', Listing('/a/\\d+', 'https://zms.mn/'));
  Site('ulsturch', 'Улс төрч', 'https://ulsturch.mn/', '0.5', Listing('/a/\\d+', 'https://ulsturch.mn/'));
  Site('ubpost', 'The UB Post', 'https://www.ubpost.mn/', '1.5', Listing('/a/\\d+', 'https://www.ubpost.mn/'));
  Site('dailynews', 'DailyNews.mn', 'https://dailynews.mn/', '1', Listing('/\\d+', 'https://dailynews.mn/'));
  Site('mass', 'Mass.mn', 'https://mass.mn/', '0.5', Listing('/n/\\d+', 'https://mass.mn/'), Dated('.shine-after-time'));
  Site('inet', 'iNET.mn', 'https://inet.mn/', '0.5', Rss('https://inet.mn/engine/rss.php'));
  Site('business', 'BUSINESS.MN', 'https://business.mn/', '1', Listing('', 'https://business.mn/'));
  Site('dailypost', 'DailyPost', 'https://dailypost.mn/', '0.5', Listing('', 'https://dailypost.mn/'));
  Site('newscafe', 'NewsCafe', 'https://newscafe.mn/', '1', Listing('/news/\\d+', 'https://newscafe.mn/'));
  Site('zuv', 'Zuv.mn', 'https://zuv.mn/', '0.5', Listing('/(?=[a-z]*\\d)[a-z0-9]{3,5}', 'https://zuv.mn/'), Dated('.publish__time'));
  Site('chuhag', 'Chuhag.mn', 'https://chuhag.mn/', '0.5', Listing('/(?=[a-z]*\\d)[a-z0-9]{2,5}', 'https://chuhag.mn/'), Dated('.publish__time'));
  Site('barimt', 'Баримт', 'https://barimt.mn/', '0.5', Listing('/(?=[a-z]*\\d)[a-z0-9]{2,5}', 'https://barimt.mn/'), Dated('.publish__time'));
  Site('urug', 'Өрөг.МН', 'https://urug.mn/', '0.5', Rss('https://urug.mn/feed/'));
  Site('newswire', 'NEWSWIRE', 'https://newswire.mn/', '1', Listing('/[pn]/\\d+', 'https://newswire.mn/'));
  Site('mono', 'Mono.mn', 'https://mono.mn/', '0.5', Listing('/n/[^/]+', 'https://mono.mn/'));
  Site('khumuun', 'Khumuun.mn', 'https://www.khumuun.mn/', '0.5', Listing('/a/\\d+', 'https://www.khumuun.mn/'));
  Site('dursamj', 'Dursamj.mn', 'https://dursamj.mn/', '0.5', Rss('https://dursamj.mn/engine/rss.php'));
  Site('subeedei', 'Subeedei.mn', 'https://subeedei.mn/', '0.5', Listing('/\\d+/?', 'https://subeedei.mn/'));
  Site('mgltimes', 'MGLTIMES', 'https://www.mgltimes.mn/', '0.5', Rss('https://www.mgltimes.mn/feed/'));
  Site('ergelt', 'Ergelt.mn', 'https://ergelt.mn/', '0.5', Listing('/news/\\d+/single/\\d+', 'https://ergelt.mn/'), Dated('.single_item_date'));
  Site('nutag', 'Nutag.mn', 'https://nutag.mn/', '0.5', Rss('https://nutag.mn/engine/rss.php'));
  // world-defaults.ts: Reuters, CCTV, BBC, NYT, Al Jazeera (reduced 2026-10-20)
  World('reuters', 'Reuters', 'https://www.reuters.com/', '3', '{"type":"sitemap","urls":["https://www.reuters.com/arc/outboundfeeds/news-sitemap/?outputType=xml"]}');
  World('cctv', 'CCTV', 'https://english.cctv.com/', '2', '{"type":"html","urls":["https://english.cctv.com/"],"linkPattern":"/\\d{4}/\\d{2}/\\d{2}/ARTI\\w+\\.shtml"}');
  World('bbc', 'BBC News', 'https://www.bbc.co.uk/', '3', Rss('https://feeds.bbci.co.uk/news/world/rss.xml'));
  World('nytimes', 'The New York Times', 'https://www.nytimes.com/', '3', Rss('https://rss.nytimes.com/services/xml/rss/nyt/World.xml'));
  World('aljazeera', 'Al Jazeera', 'https://www.aljazeera.com/', '2', Rss('https://www.aljazeera.com/xml/rss/all.xml'));
  Result := arr;
end;

// REMOVED_SOURCES (+ REMOVED_WORLD_SOURCES), comma list
const
  SRC_REMOVED = ',etv,etzone,trends,unread,chuham,todotgol,shudarga,fact,mmnews,vip76,livetv,erdenetlife,erdenetnews,comment,lemonpress,politic,mongolcomment,' +
    'guardian,dw,france24,independent,cbsnews,euronews,politico-eu,cnbc,scmp,cna,yonhap,thediplomat,tass,moscowtimes,asiatimes,techcrunch,theverge,arstechnica,bbc-sport,espn,';

// built-in weight of a slug (prior tier), 1 when not built-in
function Src_PriorTier(defs: TJSONArray; const slug: string): Double;
var
  i: Integer;
begin
  Result := 1;
  for i := 0 to defs.Count - 1 do
    if JStr(defs.Items[i], 'slug') = slug then exit(JNum(defs.Items[i], 'weight', 1));
end;

// ---------------------------------------------------------------- validation (zod sourceBody in sources.routes.ts)

function SrcTypeName(d: TJSONData): string;
begin
  if d = nil then exit('undefined');
  case d.JSONType of
    jtNull: Result := 'null';
    jtString: Result := 'string';
    jtNumber: Result := 'number';
    jtBoolean: Result := 'boolean';
    jtArray: Result := 'array';
    jtObject: Result := 'object';
  else
    Result := 'unknown';
  end;
end;

procedure SrcIssue(const msg, path: string);
begin
  if path <> '' then SrcIssues.Add('✖ ' + msg + #10 + '  → at ' + path)
  else SrcIssues.Add('✖ ' + msg);
end;

function SrcNumStr(v: Double): string;
begin
  Result := FloatToStr(v);
end;

// assertPublicHttpsUrl (lib/http.ts): raises 400 like HttpError
procedure Src_AssertHttps(const v: string);
var
  sc, host, port, path, query, rest: string;
  p: Integer;
begin
  if not TxRx('^[a-zA-Z][a-zA-Z0-9+.\-]*:', Trim(v)) or not UrlParseAbs(Trim(v), sc, host, port, path, query) then
    raise EApi.CreateS(400, 'Invalid URL: ' + v);
  rest := Copy(Trim(v), Pos(':', Trim(v)) + 1, MaxInt);
  while (rest <> '') and (rest[1] in ['/', '\']) do Delete(rest, 1, 1);
  p := 1;
  while (p <= Length(rest)) and not (rest[p] in ['/', '?', '#', '\']) do Inc(p);
  if (sc <> 'https') or (Pos('@', Copy(rest, 1, p - 1)) > 0) or ((port <> '') and (port <> '443')) then
    raise EApi.CreateS(400, 'Only public https URLs without credentials or custom ports are allowed');
  if (host = 'localhost') or TxRx('\.(local|internal|localhost)$', host) then
    raise EApi.CreateS(400, 'Internal hosts are not allowed');
end;

function SrcGet(o: TJSONObject; const key: string): TJSONData;
begin
  Result := nil;
  if o <> nil then Result := o.Find(key);
end;

// string field. dst gets the value (or default). required = no default
procedure SrcStrField(src, dst: TJSONObject; const key, path: string; required: Boolean; const def: string);
var
  d: TJSONData;
begin
  d := SrcGet(src, key);
  if d = nil then
  begin
    if required then SrcIssue('Invalid input: expected string, received undefined', path)
    else dst.Add(key, def);
    exit;
  end;
  if d.JSONType <> jtString then
  begin
    SrcIssue('Invalid input: expected string, received ' + SrcTypeName(d), path);
    exit;
  end;
  dst.Add(key, d.AsString);
end;

procedure SrcEnumField(src, dst: TJSONObject; const key, path, options, def: string);
var
  d: TJSONData;
  opts: TStringList;
  i: Integer;
  msg: string;
begin
  d := SrcGet(src, key);
  if (d = nil) and (def <> '') then begin dst.Add(key, def); exit; end;
  if (d <> nil) and (d.JSONType = jtString) and InList(',' + options + ',', d.AsString) then
  begin
    dst.Add(key, d.AsString);
    exit;
  end;
  opts := TStringList.Create;
  opts.Delimiter := ',';
  opts.StrictDelimiter := True;
  opts.DelimitedText := options;
  msg := '';
  for i := 0 to opts.Count - 1 do
  begin
    if i > 0 then msg := msg + '|';
    msg := msg + '"' + opts[i] + '"';
  end;
  opts.Free;
  SrcIssue('Invalid option: expected one of ' + msg, path);
end;

procedure SrcNumField(src, dst: TJSONObject; const key, path: string; hasDef: Boolean; def: Double; isInt: Boolean; mn, mx: Double);
var
  d: TJSONData;
  v: Double;
  ok: Boolean;
begin
  d := SrcGet(src, key);
  if d = nil then
  begin
    if hasDef then JSetN(dst, key, def)
    else SrcIssue('Invalid input: expected number, received undefined', path);
    exit;
  end;
  if d.JSONType <> jtNumber then
  begin
    SrcIssue('Invalid input: expected number, received ' + SrcTypeName(d), path);
    exit;
  end;
  v := d.AsFloat;
  ok := True;
  if isInt and (Frac(v) <> 0) then
  begin
    SrcIssue('Invalid input: expected int, received number', path);
    exit;
  end;
  if v < mn then begin SrcIssue('Too small: expected number to be >=' + SrcNumStr(mn), path); ok := False; end;
  if v > mx then begin SrcIssue('Too big: expected number to be <=' + SrcNumStr(mx), path); ok := False; end;
  if ok then JSetN(dst, key, v);
end;

procedure SrcBoolField(src, dst: TJSONObject; const key, path: string; def: Boolean);
var
  d: TJSONData;
begin
  d := SrcGet(src, key);
  if d = nil then begin dst.Add(key, def); exit; end;
  if d.JSONType <> jtBoolean then
  begin
    SrcIssue('Invalid input: expected boolean, received ' + SrcTypeName(d), path);
    exit;
  end;
  dst.Add(key, d.AsBoolean);
end;

// sourceBody.parse(body): returns the clean source (schema order, defaults). raises 400 like the zod handler
function Src_ParseBody(body: TJSONData): TJSONObject;
var
  b, sd, sa, disc, art: TJSONObject;
  d: TJSONData;
  urls: TJSONArray;
  i: Integer;
  s: string;
begin
  if SrcIssues = nil then SrcIssues := TStringList.Create;
  SrcIssues.Clear;
  if (body = nil) or not (body is TJSONObject) then
    raise EApi.CreateS(400, '✖ Invalid input: expected object, received ' + SrcTypeName(body));
  b := TJSONObject(body);
  Result := TJSONObject.Create;
  try
    // slug
    d := SrcGet(b, 'slug');
    if (d <> nil) and (d.JSONType = jtString) then
    begin
      if not TxRx('^[a-z0-9-]+$', d.AsString) then SrcIssue('Invalid string: must match pattern /^[a-z0-9-]+$/', 'slug')
      else Result.Add('slug', d.AsString);
    end
    else SrcIssue('Invalid input: expected string, received ' + SrcTypeName(d), 'slug');
    // name
    d := SrcGet(b, 'name');
    if (d <> nil) and (d.JSONType = jtString) then
    begin
      if U16Len(d.AsString) < 1 then SrcIssue('Too small: expected string to have >=1 characters', 'name')
      else if U16Len(d.AsString) > 100 then SrcIssue('Too big: expected string to have <=100 characters', 'name')
      else Result.Add('name', d.AsString);
    end
    else SrcIssue('Invalid input: expected string, received ' + SrcTypeName(d), 'name');
    // url (the refine throws, like the original)
    d := SrcGet(b, 'url');
    if (d <> nil) and (d.JSONType = jtString) then
    begin
      Src_AssertHttps(d.AsString);
      Result.Add('url', d.AsString);
    end
    else SrcIssue('Invalid input: expected string, received ' + SrcTypeName(d), 'url');
    SrcNumField(b, Result, 'weight', 'weight', True, 1, False, 0.5, 3);
    SrcEnumField(b, Result, 'region', 'region', 'mn,world', 'mn');
    SrcBoolField(b, Result, 'enabled', 'enabled', True);
    // discovery
    d := SrcGet(b, 'discovery');
    if (d = nil) or not (d is TJSONObject) then SrcIssue('Invalid input: expected object, received ' + SrcTypeName(d), 'discovery')
    else
    begin
      sd := TJSONObject(d);
      disc := TJSONObject.Create;
      Result.Add('discovery', disc);
      SrcEnumField(sd, disc, 'type', 'discovery.type', 'html,rss,sitemap,json', '');
      d := SrcGet(sd, 'urls');
      if (d = nil) or not (d is TJSONArray) then SrcIssue('Invalid input: expected array, received ' + SrcTypeName(d), 'discovery.urls')
      else
      begin
        urls := TJSONArray.Create;
        disc.Add('urls', urls);
        for i := 0 to TJSONArray(d).Count - 1 do
        begin
          if TJSONArray(d).Items[i].JSONType <> jtString then
          begin
            SrcIssue('Invalid input: expected string, received ' + SrcTypeName(TJSONArray(d).Items[i]), 'discovery.urls[' + IntToStr(i) + ']');
            continue;
          end;
          Src_AssertHttps(TJSONArray(d).Items[i].AsString);
          urls.Add(TJSONArray(d).Items[i].AsString);
        end;
        if TJSONArray(d).Count < 1 then SrcIssue('Too small: expected array to have >=1 items', 'discovery.urls');
        if TJSONArray(d).Count > 20 then SrcIssue('Too big: expected array to have <=20 items', 'discovery.urls');
      end;
      d := SrcGet(sd, 'linkPattern');
      if d = nil then disc.Add('linkPattern', '')
      else if d.JSONType <> jtString then SrcIssue('Invalid input: expected string, received ' + SrcTypeName(d), 'discovery.linkPattern')
      else if not RxValid(d.AsString) then SrcIssue('Invalid regular expression', 'discovery.linkPattern')
      else disc.Add('linkPattern', d.AsString);
      SrcStrField(sd, disc, 'itemsPath', 'discovery.itemsPath', False, '');
      SrcStrField(sd, disc, 'linkTemplate', 'discovery.linkTemplate', False, '');
      SrcStrField(sd, disc, 'dateField', 'discovery.dateField', False, '');
      SrcNumField(sd, disc, 'maxPages', 'discovery.maxPages', True, 5, True, 1, 50);
    end;
    // article
    d := SrcGet(b, 'article');
    if (d = nil) or not (d is TJSONObject) then SrcIssue('Invalid input: expected object, received ' + SrcTypeName(d), 'article')
    else
    begin
      sa := TJSONObject(d);
      art := TJSONObject.Create;
      Result.Add('article', art);
      SrcEnumField(sa, art, 'type', 'article.type', 'html,json', 'html');
      SrcStrField(sa, art, 'selector', 'article.selector', False, '');
      SrcStrField(sa, art, 'titleSelector', 'article.titleSelector', False, '');
      SrcStrField(sa, art, 'dateSelector', 'article.dateSelector', False, '');
      d := SrcGet(sa, 'detailUrl');
      if d = nil then art.Add('detailUrl', '')
      else if d.JSONType <> jtString then SrcIssue('Invalid input', 'article.detailUrl')
      else
      begin
        s := d.AsString;
        if s <> '' then Src_AssertHttps(s);
        art.Add('detailUrl', s);
      end;
      SrcStrField(sa, art, 'rootPath', 'article.rootPath', False, '');
      SrcStrField(sa, art, 'titleField', 'article.titleField', False, 'title');
      SrcStrField(sa, art, 'textField', 'article.textField', False, 'content');
      SrcStrField(sa, art, 'dateField', 'article.dateField', False, 'publishedAt');
      SrcStrField(sa, art, 'skipField', 'article.skipField', False, '');
    end;
  except
    Result.Free;
    raise;
  end;
  if SrcIssues.Count > 0 then
  begin
    Result.Free;
    s := SrcIssues.Text;
    while (s <> '') and (s[Length(s)] in [#10, #13]) do Delete(s, Length(s), 1);
    raise EApi.CreateS(400, StringReplace(s, #13, '', [rfReplaceAll]));
  end;
end;

// ---------------------------------------------------------------- weights.ts

function SrcRound(v, step: Double): Double;
begin
  Result := Floor(v / step + 0.5) * step;
  Result := StrToFloat(FormatFloat('0.00', Result));
end;

function SrcClampW(v: Double): Double;
begin
  Result := Min(3, Max(0.5, v));
end;

// sourceWeights({sources, articles, stories}): pure function.
// sources: [{slug,name?,weight,prior}], articles: [{id,sourceSlug,time(ms)}], stories: [[articleId,...],...]
// returns rows [{slug,name,old,prior,weight,articles,used,stories,firsts,reach,originality,reliability}]. caller frees
function Src_SourceWeights(sources, articles, stories: TJSONArray): TJSONArray;
const
  HOUR: Double = 3600000.0;
  FIRST_HOURS = 2;
  ORIGINALITY_PRIOR = 5;
  RELIABILITY_PRIOR = 20;
  SHRINK_ARTICLES = 30;
  MIN_QUALITY = 0.25;
var
  slugIdx, artIdx, used, storyIds: TStringList;
  sArt, sUsed, sStories, sMulti, sFirsts: array of Integer;
  aSrc: array of Integer;
  aTime: array of Double;
  reach, orig, rel, qual: array of Double;
  eSrc: array of Integer;
  eTime: array of Double;
  i, j, k, x, si, ai, nE, totUsed, totArt, totFirsts, totMulti, nActive: Integer;
  usedRate, firstRate, meanReach, typical, up, down, start, t, q, xx, tier, lambda, mx, mn: Double;
  story: TJSONArray;
  activeQ: array of Double;
  row: TJSONObject;
  found: Boolean;
  tmp: Double;
begin
  Result := TJSONArray.Create;
  slugIdx := TStringList.Create; slugIdx.Sorted := True; slugIdx.CaseSensitive := True; slugIdx.UseLocale := False; slugIdx.Duplicates := dupIgnore;
  artIdx := TStringList.Create; artIdx.Sorted := True; artIdx.CaseSensitive := True; artIdx.UseLocale := False; artIdx.Duplicates := dupIgnore;
  used := TStringList.Create; used.Sorted := True; used.CaseSensitive := True; used.UseLocale := False;
  storyIds := TStringList.Create; storyIds.Sorted := True; storyIds.CaseSensitive := True; storyIds.UseLocale := False; storyIds.Duplicates := dupIgnore;
  try
    SetLength(sArt, sources.Count); SetLength(sUsed, sources.Count); SetLength(sStories, sources.Count);
    SetLength(sMulti, sources.Count); SetLength(sFirsts, sources.Count);
    for i := 0 to sources.Count - 1 do
    begin
      sArt[i] := 0; sUsed[i] := 0; sStories[i] := 0; sMulti[i] := 0; sFirsts[i] := 0;
      if slugIdx.IndexOf(JStr(sources.Items[i], 'slug')) < 0 then
        slugIdx.AddObject(JStr(sources.Items[i], 'slug'), TObject(PtrInt(i)));
    end;
    SetLength(aSrc, articles.Count);
    SetLength(aTime, articles.Count);
    for i := 0 to articles.Count - 1 do
    begin
      k := slugIdx.IndexOf(JStr(articles.Items[i], 'sourceSlug'));
      if k >= 0 then aSrc[i] := PtrInt(slugIdx.Objects[k]) else aSrc[i] := -1;
      aTime[i] := JNum(articles.Items[i], 'time');
      if artIdx.IndexOf(JStr(articles.Items[i], 'id')) < 0 then artIdx.AddObject(JStr(articles.Items[i], 'id'), TObject(PtrInt(i)));
      if aSrc[i] >= 0 then Inc(sArt[aSrc[i]]);
    end;

    for x := 0 to stories.Count - 1 do
    begin
      if not (stories.Items[x] is TJSONArray) then continue;
      story := TJSONArray(stories.Items[x]);
      storyIds.Clear;
      SetLength(eSrc, 0);
      SetLength(eTime, 0);
      nE := 0;
      for j := 0 to story.Count - 1 do
      begin
        // new Set(story)
        if storyIds.IndexOf(story.Items[j].AsString) >= 0 then continue;
        storyIds.Add(story.Items[j].AsString);
        k := artIdx.IndexOf(story.Items[j].AsString);
        if k < 0 then continue;
        ai := PtrInt(artIdx.Objects[k]);
        si := aSrc[ai];
        if si < 0 then continue;
        if used.IndexOf(story.Items[j].AsString) < 0 then
        begin
          Inc(sUsed[si]);
          used.Add(story.Items[j].AsString);
        end;
        t := aTime[ai];
        found := False;
        for k := 0 to nE - 1 do
          if eSrc[k] = si then
          begin
            eTime[k] := Min(t, eTime[k]);
            found := True;
          end;
        if not found then
        begin
          SetLength(eSrc, nE + 1); SetLength(eTime, nE + 1);
          eSrc[nE] := si; eTime[nE] := t;
          Inc(nE);
        end;
      end;
      start := Infinity;
      for k := 0 to nE - 1 do start := Min(start, eTime[k]);
      for k := 0 to nE - 1 do
      begin
        Inc(sStories[eSrc[k]]);
        if nE < 2 then continue; // a single-source story says nothing about originality
        Inc(sMulti[eSrc[k]]);
        if eTime[k] - start <= FIRST_HOURS * HOUR then Inc(sFirsts[eSrc[k]]);
      end;
    end;

    totUsed := 0; totArt := 0; totFirsts := 0; totMulti := 0; nActive := 0;
    meanReach := 0;
    for i := 0 to sources.Count - 1 do
    begin
      totUsed := totUsed + sUsed[i];
      totArt := totArt + sArt[i];
      totFirsts := totFirsts + sFirsts[i];
      totMulti := totMulti + sMulti[i];
      if sArt[i] > 0 then
      begin
        Inc(nActive);
        meanReach := meanReach + Ln(1 + sStories[i]);
      end;
    end;
    usedRate := totUsed / Max(1, totArt);
    if usedRate = 0 then usedRate := 1;
    firstRate := totFirsts / Max(1, totMulti);
    if firstRate = 0 then firstRate := 1;
    meanReach := meanReach / Max(1, nActive);
    if meanReach = 0 then meanReach := 1;

    SetLength(reach, sources.Count); SetLength(orig, sources.Count); SetLength(rel, sources.Count); SetLength(qual, sources.Count);
    SetLength(activeQ, 0);
    for i := 0 to sources.Count - 1 do
    begin
      reach[i] := Ln(1 + sStories[i]) / meanReach;
      orig[i] := (sFirsts[i] + ORIGINALITY_PRIOR * firstRate) / (sMulti[i] + ORIGINALITY_PRIOR) / firstRate;
      rel[i] := (sUsed[i] + RELIABILITY_PRIOR * usedRate) / (sArt[i] + RELIABILITY_PRIOR) / usedRate;
      qual[i] := Max(MIN_QUALITY, reach[i] * Sqrt(orig[i] * rel[i]));
      if sArt[i] > 0 then
      begin
        SetLength(activeQ, Length(activeQ) + 1);
        activeQ[High(activeQ)] := qual[i];
      end;
    end;
    // median (sort copy)
    for i := 1 to High(activeQ) do
    begin
      tmp := activeQ[i];
      j := i - 1;
      while (j >= 0) and (activeQ[j] > tmp) do begin activeQ[j + 1] := activeQ[j]; Dec(j); end;
      activeQ[j + 1] := tmp;
    end;
    if Length(activeQ) = 0 then typical := 1
    else if Length(activeQ) mod 2 = 1 then typical := activeQ[Length(activeQ) div 2]
    else typical := (activeQ[Length(activeQ) div 2 - 1] + activeQ[Length(activeQ) div 2]) / 2;
    mx := typical; mn := typical;
    for i := 0 to High(activeQ) do begin mx := Max(mx, activeQ[i]); mn := Min(mn, activeQ[i]); end;
    up := Ln(mx / typical);
    down := Ln(typical / mn);

    for i := 0 to sources.Count - 1 do
    begin
      q := qual[i];
      xx := Ln(q / typical);
      if (xx > 0) and (up > 0) then tier := Power(3, xx / up)
      else if (xx < 0) and (down > 0) then tier := Power(0.5, -xx / down)
      else tier := 1;
      lambda := sArt[i] / (sArt[i] + SHRINK_ARTICLES);
      row := TJSONObject.Create;
      row.Add('slug', JStr(sources.Items[i], 'slug'));
      row.Add('name', JStr(sources.Items[i], 'name', JStr(sources.Items[i], 'slug')));
      JSetN(row, 'old', JNum(sources.Items[i], 'weight', 1));
      JSetN(row, 'prior', JNum(sources.Items[i], 'prior', 1));
      JSetN(row, 'weight', SrcRound(SrcClampW(Power(JNum(sources.Items[i], 'prior', 1), 1 - lambda) * Power(tier, lambda)), 0.1));
      row.Add('articles', sArt[i]);
      row.Add('used', sUsed[i]);
      row.Add('stories', sStories[i]);
      row.Add('firsts', sFirsts[i]);
      JSetN(row, 'reach', SrcRound(reach[i], 0.01));
      JSetN(row, 'originality', SrcRound(orig[i], 0.01));
      JSetN(row, 'reliability', SrcRound(rel[i], 0.01));
      Result.Add(row);
    end;
  finally
    slugIdx.Free; artIdx.Free; used.Free; storyIds.Free;
  end;
end;

// ---------------------------------------------------------------- recalc.ts

const
  SRC_DAY_MS: Double = 86400000.0;
  SRC_HOUR_MS: Double = 3600000.0;

function SrcIsoMs(const iso: string): Double;
var
  dt: TDateTime;
begin
  dt := ParseIso(iso);
  if dt = 0 then exit(0);
  Result := (dt - UnixDateDelta) * SRC_DAY_MS;
end;

// social/score.ts articleTime(publishedDay, firstSeen) in ms: clamp(firstSeen, day start, day end) in Mongolia time
function Src_ArticleTimeMs(const publishedDay, firstSeenIso: string): Double;
var
  start, fs: Double;
begin
  start := SrcIsoMs(publishedDay + 'T00:00:00.000Z') - 8 * SRC_HOUR_MS;
  if firstSeenIso = '' then exit(start + 12 * SRC_HOUR_MS);
  fs := SrcIsoMs(firstSeenIso);
  Result := Min(Max(fs, start), start + 24 * SRC_HOUR_MS - 1);
end;

// recalcSourceWeights({days, dryRun}): rows largest weight first; saves weights unless dryRun. caller frees
function Src_RecalcSourceWeights(days: Integer; dryRun: Boolean): TJSONArray;
const
  HOUR: Double = 3600000.0;
  LINK_HOURS = 48;
var
  defs, all, srcIn, artIn, eds, tops, stIn, edTopics, ids, rows: TJSONArray;
  ws, we, day, cut: string;
  i, j, k, m, e, best, n, sidx, cnt: Integer;
  o, t: TJSONObject;
  topicIdx, wanted, byTopic, items: TStringList;
  edOrder: array of Integer;
  edAt: array of Double;
  stItems, stTopics: array of TStringList;
  stLast: array of Double;
  claimed: array of Boolean;
  open: array of Integer;
  at, cutMs, ratio, tmpD: Double;
  tmpI: Integer;
  posArr: array of Double;
  tOrder: array of Integer;
  reusedFrom, tid: string;
  a: TJSONData;
begin
  DateWindow(days, ws, we);
  defs := Src_Defaults;
  srcIn := TJSONArray.Create;
  artIn := TJSONArray.Create;
  stIn := TJSONArray.Create;
  topicIdx := TStringList.Create; topicIdx.Sorted := True; topicIdx.CaseSensitive := True; topicIdx.UseLocale := False; topicIdx.Duplicates := dupIgnore;
  wanted := TStringList.Create; wanted.Sorted := True; wanted.CaseSensitive := True; wanted.UseLocale := False; wanted.Duplicates := dupIgnore;
  byTopic := TStringList.Create; byTopic.Sorted := True; byTopic.CaseSensitive := True; byTopic.UseLocale := False;
  SetLength(stItems, 0); SetLength(stTopics, 0); SetLength(stLast, 0);
  try
    // sources (mn)
    all := DbLoad('sources');
    for i := 0 to all.Count - 1 do
      if Src_InRegion(all.Items[i], 'mn') then
      begin
        o := TJSONObject.Create;
        o.Add('slug', JStr(all.Items[i], 'slug'));
        o.Add('name', JStr(all.Items[i], 'name'));
        JSetN(o, 'weight', JNum(all.Items[i], 'weight', 1));
        JSetN(o, 'prior', Src_PriorTier(defs, JStr(all.Items[i], 'slug')));
        srcIn.Add(o);
      end;
    all.Free;
    // articles in the window (mn)
    all := DbLoad('articles');
    for i := 0 to all.Count - 1 do
    begin
      day := JStr(all.Items[i], 'publishedDay');
      if (day < ws) or (day > we) or not Src_InRegion(all.Items[i], 'mn') then continue;
      o := TJSONObject.Create;
      o.Add('id', JStr(all.Items[i], '_id'));
      o.Add('sourceSlug', JStr(all.Items[i], 'sourceSlug'));
      JSetN(o, 'time', Src_ArticleTimeMs(day, JStr(all.Items[i], 'createdAt')));
      artIn.Add(o);
    end;
    all.Free;
    // editions since window start (Mongolia midnight)
    cutMs := SrcIsoMs(ws + 'T00:00:00.000Z') - 8 * HOUR;
    all := DbLoad('editions');
    eds := TJSONArray.Create;
    for i := 0 to all.Count - 1 do
      if Src_InRegion(all.Items[i], 'mn') and (JStr(all.Items[i], 'publishedAt') <> '') and (SrcIsoMs(JStr(all.Items[i], 'publishedAt')) >= cutMs) then
      begin
        eds.Add(all.Items[i].Clone);
        ids := JArr(all.Items[i], 'topicIds');
        if ids <> nil then
          for j := 0 to ids.Count - 1 do wanted.Add(ids.Items[j].AsString);
      end;
    all.Free;
    // topics of those editions, not hidden
    tops := DbLoad('topics');
    for i := tops.Count - 1 downto 0 do
      if (wanted.IndexOf(JStr(tops.Items[i], '_id')) < 0) or JBool(tops.Items[i], 'hidden') then tops.Delete(i);
    for i := 0 to tops.Count - 1 do topicIdx.AddObject(JStr(tops.Items[i], '_id'), TObject(PtrInt(i)));

    // ---- linkStories (reports/report.ts), only what weights need: story -> topic ids
    SetLength(edOrder, eds.Count);
    SetLength(edAt, eds.Count);
    for i := 0 to eds.Count - 1 do
    begin
      edOrder[i] := i;
      edAt[i] := SrcIsoMs(JStr(eds.Items[i], 'publishedAt'));
    end;
    for i := 1 to eds.Count - 1 do
    begin
      tmpI := edOrder[i];
      j := i - 1;
      while (j >= 0) and (edAt[edOrder[j]] > edAt[tmpI]) do begin edOrder[j + 1] := edOrder[j]; Dec(j); end;
      edOrder[j + 1] := tmpI;
    end;
    items := TStringList.Create;
    for e := 0 to eds.Count - 1 do
    begin
      at := edAt[edOrder[e]];
      SetLength(open, 0);
      for k := 0 to High(stLast) do
        if at - stLast[k] <= LINK_HOURS * HOUR then
        begin
          SetLength(open, Length(open) + 1);
          open[High(open)] := k;
        end;
      SetLength(claimed, Length(stLast));
      for k := 0 to High(claimed) do claimed[k] := False;
      // edition topics (existing ones) sorted by position
      ids := JArr(eds.Items[edOrder[e]], 'topicIds');
      SetLength(tOrder, 0);
      SetLength(posArr, 0);
      if ids <> nil then
        for j := 0 to ids.Count - 1 do
        begin
          k := topicIdx.IndexOf(ids.Items[j].AsString);
          if k < 0 then continue;
          SetLength(tOrder, Length(tOrder) + 1);
          tOrder[High(tOrder)] := PtrInt(topicIdx.Objects[k]);
          SetLength(posArr, Length(posArr) + 1);
          posArr[High(posArr)] := JNum(tops.Items[tOrder[High(tOrder)]], 'position');
        end;
      for i := 1 to High(tOrder) do
      begin
        tmpI := tOrder[i];
        tmpD := posArr[i];
        j := i - 1;
        while (j >= 0) and (posArr[j] > tmpD) do begin tOrder[j + 1] := tOrder[j]; posArr[j + 1] := posArr[j]; Dec(j); end;
        tOrder[j + 1] := tmpI;
        posArr[j + 1] := tmpD;
      end;
      for m := 0 to High(tOrder) do
      begin
        t := TJSONObject(tops.Items[tOrder[m]]);
        tid := JStr(t, '_id');
        items.Clear;
        a := JArr(t, 'articleIds');
        if a <> nil then for j := 0 to TJSONArray(a).Count - 1 do items.Add(TJSONArray(a).Items[j].AsString);
        a := JArr(t, 'postIds');
        if a <> nil then for j := 0 to TJSONArray(a).Count - 1 do items.Add(TJSONArray(a).Items[j].AsString);
        reusedFrom := JStr(t, 'reusedFrom');
        sidx := -1;
        if reusedFrom <> '' then
        begin
          k := byTopic.IndexOf(reusedFrom);
          if k >= 0 then sidx := PtrInt(byTopic.Objects[k]);
        end;
        if (sidx < 0) or claimed[sidx] then
        begin
          sidx := -1;
          best := 0;
          for k := 0 to High(open) do
          begin
            if claimed[open[k]] then continue;
            n := 0;
            for j := 0 to items.Count - 1 do
              if stItems[open[k]].IndexOf(items[j]) >= 0 then Inc(n);
            ratio := n / Max(1, Min(stItems[open[k]].Count, items.Count));
            if ((n >= 2) or (ratio >= 0.5)) and (n > best) then
            begin
              best := n;
              sidx := open[k];
            end;
          end;
        end;
        if sidx < 0 then
        begin
          cnt := Length(stLast);
          SetLength(stLast, cnt + 1); SetLength(stItems, cnt + 1); SetLength(stTopics, cnt + 1); SetLength(claimed, cnt + 1);
          stItems[cnt] := TStringList.Create;
          stItems[cnt].Sorted := True; stItems[cnt].CaseSensitive := True; stItems[cnt].UseLocale := False; stItems[cnt].Duplicates := dupIgnore;
          stTopics[cnt] := TStringList.Create;
          stLast[cnt] := at;
          claimed[cnt] := False;
          sidx := cnt;
        end;
        claimed[sidx] := True;
        k := byTopic.IndexOf(tid);
        if k >= 0 then byTopic.Objects[k] := TObject(PtrInt(sidx)) else byTopic.AddObject(tid, TObject(PtrInt(sidx)));
        stLast[sidx] := at;
        stItems[sidx].Clear;
        for j := 0 to items.Count - 1 do stItems[sidx].Add(items[j]);
        stTopics[sidx].Add(tid);
      end;
    end;
    items.Free;
    // stories -> article ids
    for k := 0 to High(stTopics) do
    begin
      edTopics := TJSONArray.Create;
      for j := 0 to stTopics[k].Count - 1 do
      begin
        i := topicIdx.IndexOf(stTopics[k][j]);
        if i < 0 then continue;
        a := JArr(tops.Items[PtrInt(topicIdx.Objects[i])], 'articleIds');
        if a <> nil then for m := 0 to TJSONArray(a).Count - 1 do edTopics.Add(TJSONArray(a).Items[m].AsString);
      end;
      stIn.Add(edTopics);
    end;
    tops.Free;
    eds.Free;

    rows := Src_SourceWeights(srcIn, artIn, stIn);
    // sort: weight desc, then articles desc (stable insertion sort)
    for i := 1 to rows.Count - 1 do
    begin
      j := i;
      while (j > 0) and ((JNum(rows.Items[j - 1], 'weight') < JNum(rows.Items[j], 'weight')) or
        ((JNum(rows.Items[j - 1], 'weight') = JNum(rows.Items[j], 'weight')) and (JNum(rows.Items[j - 1], 'articles') < JNum(rows.Items[j], 'articles')))) do
      begin
        rows.Exchange(j - 1, j);
        Dec(j);
      end;
    end;
    if not dryRun then
    begin
      all := DbLoad('sources');
      for i := 0 to all.Count - 1 do
        for j := 0 to rows.Count - 1 do
          if JStr(rows.Items[j], 'slug') = JStr(all.Items[i], 'slug') then
          begin
            JSetN(TJSONObject(all.Items[i]), 'weight', JNum(rows.Items[j], 'weight'));
            JSetS(TJSONObject(all.Items[i]), 'updatedAt', NowIso);
          end;
      DbSave('sources', all);
      all.Free;
    end;
    Result := rows;
  finally
    for k := 0 to High(stItems) do stItems[k].Free;
    for k := 0 to High(stTopics) do stTopics[k].Free;
    defs.Free; srcIn.Free; artIn.Free; stIn.Free;
    topicIdx.Free; wanted.Free; byTopic.Free;
  end;
end;

// ---------------------------------------------------------------- scripts

// seed.ts part: creates built-in sources that do not exist yet. returns DEFAULT_SOURCES.length
function Src_SeedSources: Integer;
var
  defs, all: TJSONArray;
  i, j: Integer;
  found, changed: Boolean;
  o: TJSONObject;
begin
  defs := Src_Defaults;
  all := DbLoad('sources');
  changed := False;
  for i := 0 to defs.Count - 1 do
  begin
    found := False;
    for j := 0 to all.Count - 1 do
      if JStr(all.Items[j], 'slug') = JStr(defs.Items[i], 'slug') then found := True;
    if found then continue;
    o := TJSONObject(defs.Items[i].Clone);
    JSetS(o, '_id', NewId);
    JSetS(o, 'createdAt', NowIso);
    JSetS(o, 'updatedAt', NowIso);
    all.Add(o);
    changed := True;
  end;
  if changed then DbSave('sources', all);
  Result := defs.Count;
  all.Free;
  defs.Free;
end;

// sync-sources.ts: updates name/url/weight/region/discovery/article of built-ins, creates missing, disables removed
procedure Src_SyncSources;
var
  defs, all: TJSONArray;
  i, j, created, updated, disabled: Integer;
  found: Boolean;
  o, d: TJSONObject;
  before: string;

  procedure SetF(const k: string);
  begin
    JSet(o, k, d.Find(k).Clone);
  end;

begin
  defs := Src_Defaults;
  all := DbLoad('sources');
  created := 0; updated := 0; disabled := 0;
  for i := 0 to defs.Count - 1 do
  begin
    d := TJSONObject(defs.Items[i]);
    found := False;
    for j := 0 to all.Count - 1 do
      if JStr(all.Items[j], 'slug') = JStr(d, 'slug') then
      begin
        found := True;
        o := TJSONObject(all.Items[j]);
        before := o.AsJSON;
        SetF('name'); SetF('url'); SetF('weight'); SetF('region'); SetF('discovery'); SetF('article');
        // compare ignoring key order changes we just did: re-canon both
        if before <> o.AsJSON then
        begin
          d := Src_Canon(TJSONObject(JParse(before)));
          if d.AsJSON <> Src_Canon(o).AsJSON then
          begin
            Inc(updated);
            JSetS(o, 'updatedAt', NowIso);
          end;
          d := TJSONObject(defs.Items[i]);
        end;
        break;
      end;
    if not found then
    begin
      o := TJSONObject(d.Clone);
      JSetS(o, '_id', NewId);
      JSetS(o, 'createdAt', NowIso);
      JSetS(o, 'updatedAt', NowIso);
      all.Add(o);
      Inc(created);
    end;
  end;
  for j := 0 to all.Count - 1 do
    if InList(SRC_REMOVED, JStr(all.Items[j], 'slug')) and JBool(all.Items[j], 'enabled', True) then
    begin
      JSetB(TJSONObject(all.Items[j]), 'enabled', False);
      JSetS(TJSONObject(all.Items[j]), 'updatedAt', NowIso);
      Inc(disabled);
    end;
  DbSave('sources', all);
  WriteLn('Sources: ', created, ' created, ', updated, ' updated, ', disabled, ' removed ones disabled (', defs.Count, ' built-in)');
  all.Free;
  defs.Free;
end;

function SrcPad(const s: string; w: Integer): string;
begin
  Result := s;
  while U16Len(Result) < w do Result := Result + ' ';
end;

// recalc-weights.ts: reads --days N and --dry-run from the command line, prints old -> new
procedure Src_RecalcWeights;
var
  i, days: Integer;
  dryRun: Boolean;
  rows: TJSONArray;
  r: TJSONData;
begin
  days := 30;
  dryRun := False;
  for i := 1 to ParamCount do
  begin
    if (ParamStr(i) = '--days') then days := StrToIntDef(ParamStr(i + 1), -1);
    if ParamStr(i) = '--dry-run' then dryRun := True;
  end;
  if (days < 1) or (days > 365) then
  begin
    WriteLn('--days must be a whole number from 1 to 365');
    Halt(1);
  end;
  rows := Src_RecalcSourceWeights(days, dryRun);
  if dryRun then WriteLn('Source weights from the last ', days, ' days (dry run, nothing saved):')
  else WriteLn('Source weights from the last ', days, ' days:');
  WriteLn(SrcPad('source', 18), SrcPad('old', 6), SrcPad('prior', 6), SrcPad('new', 6), SrcPad('articles', 9), SrcPad('used', 6),
    SrcPad('stories', 8), SrcPad('firsts', 7), SrcPad('reach', 7), SrcPad('orig', 7), 'reliab');
  for i := 0 to rows.Count - 1 do
  begin
    r := rows.Items[i];
    WriteLn(SrcPad(JStr(r, 'slug'), 18), SrcPad(Fmt2(JNum(r, 'old')), 6), SrcPad(Fmt2(JNum(r, 'prior')), 6), SrcPad(Fmt2(JNum(r, 'weight')), 6),
      SrcPad(IntToStr(JInt(r, 'articles')), 9), SrcPad(IntToStr(JInt(r, 'used')), 6), SrcPad(IntToStr(JInt(r, 'stories')), 8),
      SrcPad(IntToStr(JInt(r, 'firsts')), 7), SrcPad(Fmt2(JNum(r, 'reach')), 7), SrcPad(Fmt2(JNum(r, 'originality')), 7), Fmt2(JNum(r, 'reliability')));
  end;
  rows.Free;
end;

// ---------------------------------------------------------------- routes (/api/admin/sources, permission "sources")

procedure Routes_sources;
var
  all, rows: TJSONArray;
  o, cur, res: TJSONObject;
  i, days: Integer;
  id: string;
  d: TJSONData;
  dryRun: Boolean;
begin
  if Handled then exit;
  if (ReqPath = '/api/admin/sources') or (Copy(ReqPath, 1, 19) = '/api/admin/sources/') then RequirePerm('sources')
  else exit;

  if Route('GET', '/api/admin/sources') then
  begin
    all := DbLoad('sources');
    JSortBy(all, 'name', False);
    SendJson(200, all);
    exit;
  end;

  if Route('POST', '/api/admin/sources') then
  begin
    o := Src_ParseBody(ReqBody);
    try
      cur := DbFindOne('sources', 'slug', JStr(o, 'slug'));
      if cur <> nil then
      begin
        cur.Free;
        raise EApi.CreateS(409, 'Already exists');
      end;
      DbInsert('sources', o);
      SendJson(201, o, False);
    finally
      o.Free;
    end;
    exit;
  end;

  if Route('PUT', '/api/admin/sources/:id') then
  begin
    id := P(3);
    o := Src_ParseBody(ReqBody);
    try
      if not IsId(id) then raise EApi.CreateS(404, 'Not found');
      cur := DbGet('sources', id);
      if cur = nil then raise EApi.CreateS(404, 'Not found');
      cur.Free;
      all := DbLoad('sources');
      try
        for i := 0 to all.Count - 1 do
          if (JStr(all.Items[i], 'slug') = JStr(o, 'slug')) and (JStr(all.Items[i], '_id') <> id) then
            raise EApi.CreateS(409, 'Already exists');
      finally
        all.Free;
      end;
      if not DbUpdate('sources', id, o) then raise EApi.CreateS(404, 'Not found');
      // arrays/objects are replaced as a whole by findByIdAndUpdate; DbUpdate merges objects, so fix urls explicitly
      cur := DbGet('sources', id);
      JSet(TJSONObject(cur.Find('discovery')), 'urls', JArr(JObj(o, 'discovery'), 'urls').Clone);
      DbReplace('sources', cur);
      res := Src_Canon(cur);
      cur.Free;
      SendJson(200, res);
    finally
      o.Free;
    end;
    exit;
  end;

  if Route('DELETE', '/api/admin/sources/:id') then
  begin
    id := P(3);
    if not IsId(id) or not DbDelete('sources', id) then raise EApi.CreateS(404, 'Not found');
    SendNoContent;
    exit;
  end;

  if Route('POST', '/api/admin/sources/recalculate-weights') then
  begin
    d := ReqBody;
    if (d <> nil) and not (d is TJSONObject) then raise EApi.CreateS(400, '✖ Invalid input: expected object, received ' + SrcTypeName(d));
    if SrcIssues = nil then SrcIssues := TStringList.Create;
    SrcIssues.Clear;
    o := TJSONObject.Create;
    try
      SrcNumField(TJSONObject(d), o, 'days', 'days', True, 30, True, 1, 365);
      SrcBoolField(TJSONObject(d), o, 'dryRun', 'dryRun', False);
      if SrcIssues.Count > 0 then raise EApi.CreateS(400, Trim(StringReplace(SrcIssues.Text, #13, '', [rfReplaceAll])));
      days := JInt(o, 'days');
      dryRun := JBool(o, 'dryRun');
    finally
      o.Free;
    end;
    rows := Src_RecalcSourceWeights(days, dryRun);
    res := TJSONObject.Create;
    res.Add('days', days);
    res.Add('dryRun', dryRun);
    res.Add('rows', rows);
    SendJson(200, res);
    exit;
  end;
end;
