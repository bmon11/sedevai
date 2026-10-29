// ===================== collector: modules/collector/* (discover, extract, collect) + scripts/probe.ts + POST /api/admin/sources/probe =====================
// Article result objects: {url,title,text,image,publishedDay} (publishedDay null when unknown).
// Link discovery fills two parallel string lists: links (url) and days (YYYY-MM-DD or '' = null).

const
  COL_MAX_TEXT = 60000;
  COL_NOISE = 'script,style,form,nav,aside,.comments,.related';
  COL_CHROME = 'script,style,form,nav,aside,.comments,.related,header,footer,iframe,noscript,.sidebar,#sidebar,#comments';
  COL_BLOCKS = 'div,p,section,article,ul,ol,table';
  COL_DEADLINE_MS = 600000;

var
  // the "LinkSet" of the discovery in progress (globals, one discovery at a time)
  ColLinks: TStringList = nil;
  ColDays: TStringList = nil;
  ColLimit: Integer = 0;
  ColWinStart: string = '';
  ColWinEnd: string = '';
  ColHost: string = '';
  ColPattern: string = '';
  // robots.txt of the source being collected
  RobUrlSc, RobUrlHost, RobUrlPort: string;
  RobRules: TStringList = nil;   // "agent<TAB>A|D<TAB>pattern"
  RobAgents: TStringList = nil;  // agents that have a group
  RobDelays: TStringList = nil;  // agent=delay seconds

// ---------------------------------------------------------------- http (lib/http.ts fetchText: https only, max 4 same-site redirects, size cap)

function ColSameSite(const a, b: string): Boolean;
var
  x, y: string;
begin
  x := a; y := b;
  if Copy(x, 1, 4) = 'www.' then Delete(x, 1, 4);
  if Copy(y, 1, 4) = 'www.' then Delete(y, 1, 4);
  Result := x = y;
end;

function Col_Fetch(const url: string; maxBytes: Int64 = 3000000; timeoutSec: Integer = 20): string;
var
  p: TProcess;
  o: TStringList;
  resFile, ln, code, eff, firstErr: string;
  st, sp, i: Integer;
begin
  Src_AssertHttps(url);
  if not IsPublicHttpsUrl(url) then raise EApi.CreateS(400, 'Not a public address: ' + UrlHost(url));
  resFile := TmpName('.out');
  p := TProcess.Create(nil);
  o := TStringList.Create;
  try
    p.Executable := 'curl';
    p.Parameters.Add('-sS');
    p.Parameters.Add('--compressed');
    p.Parameters.Add('-L');
    p.Parameters.Add('--max-redirs'); p.Parameters.Add('4');
    p.Parameters.Add('--proto'); p.Parameters.Add('=https');
    p.Parameters.Add('--proto-redir'); p.Parameters.Add('=https');
    p.Parameters.Add('--max-time'); p.Parameters.Add(IntToStr(timeoutSec));
    p.Parameters.Add('--max-filesize'); p.Parameters.Add(IntToStr(maxBytes));
    p.Parameters.Add('-A'); p.Parameters.Add(Env('USER_AGENT', 'SedevResearch/1.0 (+https://sedev.mn)'));
    p.Parameters.Add('-o'); p.Parameters.Add(resFile);
    p.Parameters.Add('-w'); p.Parameters.Add('\n%{http_code} %{url_effective}');
    p.Parameters.Add(url);
    p.Options := [poWaitOnExit, poUsePipes, poStderrToOutPut];
    p.Execute;
    o.LoadFromStream(p.Output);
    firstErr := '';
    ln := '';
    for i := 0 to o.Count - 1 do
    begin
      if Trim(o[i]) = '' then continue;
      if (firstErr = '') and (Copy(o[i], 1, 5) = 'curl:') then firstErr := Trim(o[i]);
      ln := Trim(o[i]);
    end;
    sp := Pos(' ', ln);
    code := Copy(ln, 1, sp - 1);
    eff := Copy(ln, sp + 1, MaxInt);
    st := StrToIntDef(code, 0);
    if p.ExitStatus <> 0 then
    begin
      if p.ExitStatus = 28 then raise EApi.CreateS(400, 'Request timed out');
      if p.ExitStatus = 63 then raise EApi.CreateS(400, 'Response too large');
      if p.ExitStatus = 47 then raise EApi.CreateS(400, 'Too many redirects');
      if firstErr = '' then firstErr := 'curl exit ' + IntToStr(p.ExitStatus);
      raise EApi.CreateS(400, firstErr);
    end;
    if (eff <> '') and not ColSameSite(UrlHost(eff), UrlHost(url)) then
      raise EApi.CreateS(400, 'Redirect to another domain: ' + UrlHost(eff));
    if eff = '' then eff := url;
    if st <> 200 then raise EApi.CreateS(400, 'HTTP ' + IntToStr(st) + ' for ' + eff);
    Result := ReadFileStr(resFile);
    if Length(Result) > maxBytes then raise EApi.CreateS(400, 'Response too large');
  finally
    if FileExists(resFile) then DeleteFile(resFile);
    p.Free;
    o.Free;
  end;
end;

// fetchJson: caller frees. bad json -> plain Exception (500 in routes, like JSON.parse SyntaxError)
function Col_FetchJson(const url: string): TJSONData;
var
  s: string;
begin
  s := Col_Fetch(url);
  Result := JParse(s);
  if Result = nil then raise Exception.Create('Unexpected token in JSON from ' + url + ': "' + Copy(s, 1, 30) + '"... is not valid JSON');
end;

// ---------------------------------------------------------------- discover.ts

// default article rule when a source has no linkPattern
function Col_LooksLikeArticle(const path: string): Boolean;
var
  decoded, last: string;
  segs: TStringList;
  i, n: Integer;
  ok: Boolean;
begin
  Result := False;
  decoded := PctDecode(path, ok);
  if not ok then decoded := path;
  segs := TStringList.Create;
  segs.Delimiter := '/';
  segs.StrictDelimiter := True;
  segs.DelimitedText := decoded;
  for i := segs.Count - 1 downto 0 do
    if segs[i] = '' then segs.Delete(i);
  n := segs.Count;
  if n = 0 then begin segs.Free; exit; end;
  for i := 0 to n - 1 do
    if TxRx('(?i)^(category|categories|cat|angilal|tag|tags|author|authors|page|more|archive|search|login|register|signin|signup|user|profile|about|contact|privacy|terms|ads|advertising|feed|rss|wp-admin|wp-login\.php|wp-content)$', segs[i]) then
    begin
      segs.Free;
      exit;
    end;
  last := TxReplace(segs[n - 1], '\.(html?|php|aspx?)$', '');
  segs.Free;
  Result := TxRx('\d{3,}', last) or TxRx('/20\d\d/\d{1,2}/.', decoded) or
    ((n >= 2) and TxRx('\d', last) and TxRx('(?i)^[a-z0-9]{4,12}$', last)) or
    ((U16Len(last) >= 20) and TxRx('[-_]', last));
end;

// LinkSet.add(href, base, hint, fromFeed)
procedure Col_AddLink(const href, base, hint: string; fromFeed: Boolean);
var
  u, sc, host, port, path, query, search, v, p, key, day: string;
  i, k: Integer;
  trusted, isDigits: Boolean;
const
  ID_PARAMS: array[0..2] of string = ('newsid', 'p', 'id');
begin
  if ColLinks.Count >= ColLimit then exit;
  u := UrlResolve(href, base);
  if u = '' then exit;
  if not UrlParseAbs(u, sc, host, port, path, query) then exit;
  search := '';
  for i := 0 to 2 do
  begin
    v := UrlQueryGet(query, ID_PARAMS[i]);
    isDigits := v <> '';
    for k := 1 to Length(v) do if not (v[k] in ['0'..'9']) then isDigits := False;
    if isDigits then
    begin
      search := '?' + ID_PARAMS[i] + '=' + v;
      break;
    end;
  end;
  p := path + search;
  trusted := fromFeed and (ColPattern = '') and (Length(path) > 1);
  if not ColSameSite(host, ColHost) then exit;
  if not trusted then
  begin
    if ColPattern <> '' then
    begin
      if not RxFullMatch(ColPattern, p) then exit;
    end
    else if not Col_LooksLikeArticle(p) then exit;
  end;
  day := ParseDay(hint);
  if (day <> '') and ((day < ColWinStart) or (day > ColWinEnd)) then exit;
  key := sc + '://' + host;
  if port <> '' then key := key + ':' + port;
  key := key + path + search;
  k := ColLinks.IndexOf(key);
  if k >= 0 then
  begin
    if day <> '' then ColDays[k] := day;
  end
  else
  begin
    ColLinks.Add(key);
    ColDays.Add(day);
  end;
end;

// RSS 2.0, RSS 1.0 (RDF) and Atom
procedure Col_FromRss(const feed: string);
var
  D: THDoc;
  rss, ch, rdf, fd, k, j, l: Integer;
  items, links: TIntArr;
  link, href, rel, hint: string;
  picked: Boolean;
begin
  HParse(D, Col_Fetch(feed, 12000000), True);
  rss := XChild(D, 0, 'rss');
  ch := XChild(D, rss, 'channel');
  items := XChildren(D, ch, 'item');
  for k := 0 to High(items) do
  begin
    links := XChildren(D, items[k], 'link');
    link := '';
    for j := 0 to High(links) do
      if XText(D, links[j]) <> '' then begin link := XText(D, links[j]); break; end;
    if link <> '' then Col_AddLink(link, feed, XText(D, XChild(D, items[k], 'pubDate')), True);
  end;
  rdf := XChild(D, 0, 'RDF');
  items := XChildren(D, rdf, 'item');
  for k := 0 to High(items) do
  begin
    link := XText(D, XChild(D, items[k], 'link'));
    if link <> '' then Col_AddLink(link, feed, XText(D, XChild(D, items[k], 'date')), True);
  end;
  fd := XChild(D, 0, 'feed');
  if fd < 0 then exit;
  items := XChildren(D, fd, 'entry');
  for k := 0 to High(items) do
  begin
    links := XChildren(D, items[k], 'link');
    href := '';
    picked := False;
    for j := 0 to High(links) do
    begin
      l := links[j];
      if Length(D.N[l].AN) = 0 then
      begin
        // plain <link>url</link> parses to a string
        href := XText(D, l);
        picked := True;
        break;
      end;
      rel := HAttr(D, l, 'rel');
      if not HHasAttr(D, l, 'rel') or (rel = '') or (rel = 'alternate') then
      begin
        href := HAttr(D, l, 'href');
        picked := True;
        break;
      end;
    end;
    if not picked or (href = '') then continue;
    if XChild(D, items[k], 'published') >= 0 then hint := XText(D, XChild(D, items[k], 'published'))
    else hint := XText(D, XChild(D, items[k], 'updated'));
    Col_AddLink(href, feed, hint, True);
  end;
end;

procedure Col_FromHtml(urls: TJSONArray; autoFeed: Boolean);
var
  D: THDoc;
  i, k, before: Integer;
  page, feed, href, hint: string;
  ids: TIntArr;
begin
  for i := 0 to urls.Count - 1 do
  begin
    page := urls.Items[i].AsString;
    HParse(D, Col_Fetch(page, 12000000), False);
    // Without a linkPattern, the site's own RSS feed (if any) is the most reliable article list.
    feed := '';
    if autoFeed then
    begin
      ids := HSelect(D, 0, 'link[rel="alternate"][type="application/rss+xml"]');
      if Length(ids) > 0 then feed := HAttr(D, ids[0], 'href');
    end;
    if feed <> '' then
    begin
      before := ColLinks.Count;
      try
        Col_FromRss(UrlResolve(feed, page));
      except
        // ignored, like .catch(() => undefined)
      end;
      if ColLinks.Count > before then continue;
    end;
    // A /YYYY/MM/DD/ segment in the link is its publication day (e.g. CCTV).
    ids := HSelect(D, 0, 'a[href]');
    for k := 0 to High(ids) do
    begin
      href := HAttr(D, ids[k], 'href');
      if not RxFirst('/(20\d\d/\d\d/\d\d)/', href, 1, hint) then hint := '';
      Col_AddLink(href, page, hint, False);
    end;
  end;
end;

procedure Col_FromSitemap(urls: TJSONArray; maxPages: Integer);
var
  D: THDoc;
  queue: TStringList;
  fetched, k, si, us: Integer;
  url, modified: string;
  kids: TIntArr;
begin
  queue := TStringList.Create;
  try
    for k := 0 to urls.Count - 1 do queue.Add(urls.Items[k].AsString);
    fetched := 0;
    while (queue.Count > 0) and (fetched < maxPages) and (ColLinks.Count < ColLimit) do
    begin
      url := queue[0];
      queue.Delete(0);
      HParse(D, Col_Fetch(url, 12000000), True);
      si := XChild(D, 0, 'sitemapindex');
      kids := XChildren(D, si, 'sitemap');
      for k := 0 to High(kids) do queue.Add(XText(D, XChild(D, kids[k], 'loc')));
      us := XChild(D, 0, 'urlset');
      kids := XChildren(D, us, 'url');
      for k := 0 to High(kids) do
      begin
        // lastmod is only a discovery filter; the article page decides the publication date.
        modified := ParseDay(XText(D, XChild(D, kids[k], 'lastmod')));
        if (modified <> '') and (modified < ColWinStart) then continue;
        Col_AddLink(XText(D, XChild(D, kids[k], 'loc')), url, XText(D, XChild(D, XChild(D, kids[k], 'news'), 'publication_date')), False);
      end;
      Inc(fetched);
    end;
  finally
    queue.Free;
  end;
end;

procedure Col_FromJson(src: TJSONObject);
var
  disc, pageObj: TJSONObject;
  urls: TJSONArray;
  body, itemsD, item, hintD: TJSONData;
  items: TJSONArray;
  t, page, maxPages, k, n, older: Integer;
  dateField, day, hint: string;
begin
  disc := JObj(src, 'discovery');
  urls := JArr(disc, 'urls');
  if urls = nil then exit;
  maxPages := JInt(disc, 'maxPages', 5);
  dateField := JStr(disc, 'dateField');
  for t := 0 to urls.Count - 1 do
  begin
    page := 1;
    while (page <= maxPages) and (ColLinks.Count < ColLimit) do
    begin
      pageObj := TJSONObject.Create(['page', page]);
      body := Col_FetchJson(FillTemplate(urls.Items[t].AsString, pageObj, False));
      pageObj.Free;
      items := TJSONArray.Create;
      try
        if JStr(disc, 'itemsPath') = '' then itemsD := body else itemsD := JPath(body, JStr(disc, 'itemsPath'));
        if itemsD <> nil then
        begin
          if itemsD is TJSONArray then
          begin
            for k := 0 to TJSONArray(itemsD).Count - 1 do items.Add(TJSONArray(itemsD).Items[k].Clone);
          end
          else items.Add(itemsD.Clone);
        end;
        if items.Count = 0 then break;
        n := 0;
        older := 0;
        for k := 0 to items.Count - 1 do
        begin
          item := items.Items[k];
          hintD := nil;
          if dateField <> '' then hintD := JPath(item, dateField);
          day := ParseDayJ(hintD);
          if day <> '' then
          begin
            Inc(n);
            if day < ColWinStart then Inc(older);
          end;
          if (hintD <> nil) and (hintD.JSONType <> jtNull) then hint := JValStr(hintD) else hint := '';
          Col_AddLink(FillTemplate(JStr(disc, 'linkTemplate'), item, True), JStr(src, 'url'), hint, False);
        end;
        // Lists are newest first: stop once a whole page is older than the window.
        if (n > 0) and (older = n) then break;
      finally
        items.Free;
        body.Free;
      end;
      Inc(page);
    end;
  end;
end;

// discoverLinks(source, window, limit): fills links (urls) and days ('' = null). lists are created by the caller
procedure Col_DiscoverLinks(src: TJSONObject; const ws, we: string; limit: Integer; links, days: TStringList);
var
  disc: TJSONObject;
  typ: string;
  urls: TJSONArray;
  i: Integer;
begin
  ColLinks := links;
  ColDays := days;
  ColLinks.CaseSensitive := True;
  ColLimit := limit;
  ColWinStart := ws;
  ColWinEnd := we;
  ColHost := UrlHost(JStr(src, 'url'));
  if ColHost = '' then raise EApi.CreateS(400, 'Invalid URL: ' + JStr(src, 'url'));
  disc := JObj(src, 'discovery');
  ColPattern := JStr(disc, 'linkPattern');
  typ := JStr(disc, 'type', 'html');
  urls := JArr(disc, 'urls');
  if urls = nil then urls := TJSONArray(JParse('[]'));
  if typ = 'html' then Col_FromHtml(urls, ColPattern = '')
  else if typ = 'rss' then
  begin
    for i := 0 to urls.Count - 1 do Col_FromRss(urls.Items[i].AsString);
  end
  else if typ = 'sitemap' then Col_FromSitemap(urls, JInt(disc, 'maxPages', 5))
  else Col_FromJson(src);
end;

// ---------------------------------------------------------------- extract.ts

function Col_PublicationDay(var D: THDoc; const dateSelector: string): string;
var
  ids: TIntArr;
  k: Integer;
  nm, m: string;
begin
  Result := '';
  ids := HSelect(D, 0, 'meta[content]');
  for k := 0 to High(ids) do
  begin
    nm := HAttr(D, ids[k], 'property');
    if nm = '' then nm := HAttr(D, ids[k], 'name');
    nm := LowerCase(nm);
    if InList(',article:published_time,og:article:published_time,datepublished,pubdate,publishdate,', nm) then
    begin
      Result := ParseDay(HAttr(D, ids[k], 'content'));
      if Result <> '' then exit;
    end;
  end;
  ids := HSelect(D, 0, 'script[type="application/ld+json"]');
  for k := 0 to High(ids) do
    if RxFirst('"datePublished"\s*:\s*"([^"]+)"', HText(D, ids[k]), 1, m) then
    begin
      Result := ParseDay(m);
      if Result <> '' then exit;
    end;
  ids := HSelect(D, 0, 'time[datetime]');
  for k := 0 to High(ids) do
  begin
    Result := ParseDay(HAttr(D, ids[k], 'datetime'));
    if Result <> '' then exit;
  end;
  // Only the configured date element; never scan the whole page for dates.
  if dateSelector <> '' then
  begin
    ids := HSelect(D, 0, dateSelector);
    for k := 0 to High(ids) do
    begin
      Result := ParseDay(HText(D, ids[k]));
      if Result <> '' then exit;
    end;
  end;
end;

// text of the given elements, one line per paragraph, short and duplicate lines dropped
function Col_BlockText(var D: THDoc; const els: TIntArr): string;
var
  lines, parts: TStringList;
  e, k, blockLen: Integer;
  blocks, brs: TIntArr;
  el: Integer;
begin
  lines := TStringList.Create;
  parts := TStringList.Create;
  try
    for e := 0 to High(els) do
    begin
      el := els[e];
      HRemoveSel(D, el, COL_NOISE);
      blocks := HSelect(D, el, 'p,h2,h3,li');
      blockLen := 0;
      for k := 0 to High(blocks) do blockLen := blockLen + U16Len(CleanWs(HText(D, blocks[k])));
      if (Length(blocks) > 0) and (blockLen >= U16Len(CleanWs(HText(D, el))) / 2) then
      begin
        for k := 0 to High(blocks) do lines.Add(HText(D, blocks[k]));
      end
      else
      begin
        brs := HSelect(D, el, 'br');
        for k := 0 to High(brs) do HReplaceWithText(D, brs[k], #10);
        brs := HSelect(D, el, COL_BLOCKS);
        for k := 0 to High(brs) do HAppendText(D, brs[k], #10);
        parts.Text := StringReplace(HText(D, el), #13, ' ', [rfReplaceAll]);
        for k := 0 to parts.Count - 1 do lines.Add(parts[k]);
      end;
    end;
    Result := TxtUniqueJoin(lines, 15);
  finally
    lines.Free;
    parts.Free;
  end;
end;

// articleBody of a JSON-LD NewsArticle/Article (also inside @graph)
function Col_JsonLdBody(var D: THDoc): string;
var
  ids: TIntArr;
  k, i: Integer;
  data, item, ab, graph: TJSONData;
  queue: TList;
  D2: THDoc;
  lines: TStringList;
  s, l: string;
begin
  Result := '';
  ids := HSelect(D, 0, 'script[type="application/ld+json"]');
  for k := 0 to High(ids) do
  begin
    data := JParse(HText(D, ids[k]));
    if data = nil then continue;
    queue := TList.Create;
    try
      queue.Add(data);
      while queue.Count > 0 do
      begin
        item := TJSONData(queue[0]);
        queue.Delete(0);
        if item is TJSONArray then
        begin
          for i := 0 to TJSONArray(item).Count - 1 do queue.Add(TJSONArray(item).Items[i]);
        end
        else if item is TJSONObject then
        begin
          ab := TJSONObject(item).Find('articleBody');
          graph := TJSONObject(item).Find('@graph');
          if (ab <> nil) and (ab.JSONType = jtString) and (U16Len(ab.AsString) >= 200) then
          begin
            // articleBody is sometimes HTML: keep paragraph breaks, drop tags
            s := TxReplace(ab.AsString, '(?i)</p>|<br\s*/?>', #10);
            HParse(D2, s, False);
            lines := TStringList.Create;
            lines.Text := StringReplace(HText(D2, 0), #13, ' ', [rfReplaceAll]);
            for i := 0 to lines.Count - 1 do
            begin
              l := CleanWs(lines[i]);
              if l = '' then continue;
              if Result <> '' then Result := Result + #10;
              Result := Result + l;
            end;
            lines.Free;
            exit;
          end;
          if JsTruthy(graph) then queue.Add(graph);
        end;
      end;
    finally
      queue.Free;
      data.Free;
    end;
  end;
end;

function Col_ParaLen(var D: THDoc; c: Integer): Integer;
var
  t: string;
begin
  Result := 0;
  if D.N[c].Kind <> 1 then exit;
  t := D.N[c].Tag;
  if (t = 'p') or (((t = 'div') or (t = 'span')) and (HFirstDesc(D, c, COL_BLOCKS) < 0)) then
    Result := U16Len(CleanWs(HText(D, c)));
end;

// guesses the body: the element whose paragraph children (and own text) hold the most text
function Col_DensestText(var D: THDoc): string;
var
  body, best, bestLen, len, k, j: Integer;
  all, one: TIntArr;
begin
  HRemoveSel(D, 0, COL_CHROME);
  body := HFirstDesc(D, 0, 'body');
  if body < 0 then body := 0;
  all := HSelect(D, body, '*');
  best := -1;
  bestLen := 0;
  for k := 0 to High(all) do
  begin
    len := U16Len(CleanWs(HOwnText(D, all[k])));
    for j := 0 to High(D.N[all[k]].Kids) do len := len + Col_ParaLen(D, D.N[all[k]].Kids[j]);
    if len > bestLen then
    begin
      best := all[k];
      bestLen := len;
    end;
  end;
  if best < 0 then exit('');
  SetLength(one, 1);
  one[0] := best;
  Result := Col_BlockText(D, one);
end;

// extractHtmlArticle(html, url, source). caller frees
function Col_ExtractHtml(const html, url: string; src: TJSONObject): TJSONObject;
var
  D: THDoc;
  art: TJSONObject;
  selector, titleSel, dateSel, title, day, text, image: string;
  ids: TIntArr;
  k: Integer;
begin
  HParse(D, html, False);
  art := JObj(src, 'article');
  selector := JStr(art, 'selector');
  titleSel := JStr(art, 'titleSelector');
  dateSel := JStr(art, 'dateSelector');
  title := '';
  if titleSel <> '' then
  begin
    ids := HSelect(D, 0, titleSel);
    if Length(ids) > 0 then title := HText(D, ids[0]);
  end;
  if title = '' then
  begin
    ids := HSelect(D, 0, 'meta[property="og:title"]');
    if Length(ids) > 0 then title := HAttr(D, ids[0], 'content');
  end;
  if title = '' then
  begin
    ids := HSelect(D, 0, 'title');
    for k := 0 to High(ids) do title := title + HText(D, ids[k]);
  end;
  title := CleanWs(title);
  // Date first: guessing the body removes page chrome (header, footer) where dates often are.
  day := Col_PublicationDay(D, dateSel);
  if selector <> '' then text := Col_BlockText(D, HSelect(D, 0, selector))
  else
  begin
    text := Col_JsonLdBody(D);
    if text = '' then text := Col_DensestText(D);
  end;
  image := '';
  ids := HSelect(D, 0, 'meta[property="og:image"]');
  if Length(ids) > 0 then image := HAttr(D, ids[0], 'content');
  Result := TJSONObject.Create;
  Result.Add('url', url);
  Result.Add('title', U16Copy(title, 300));
  Result.Add('text', U16Copy(text, COL_MAX_TEXT));
  Result.Add('image', image);
  if day <> '' then Result.Add('publishedDay', day) else Result.Add('publishedDay', TJSONNull.Create);
end;

// fetchJsonArticle(url, source): JSON detail API. caller frees
function Col_FetchJsonArticle(const url: string; src: TJSONObject): TJSONObject;
var
  art, vals: TJSONObject;
  path, last, skip, day: string;
  segs: TStringList;
  i: Integer;
  body, doc, img: TJSONData;
begin
  art := JObj(src, 'article');
  path := UrlPath(url);
  segs := TStringList.Create;
  segs.Delimiter := '/';
  segs.StrictDelimiter := True;
  segs.DelimitedText := path;
  last := '';
  for i := segs.Count - 1 downto 0 do
    if segs[i] <> '' then begin last := segs[i]; break; end;
  segs.Free;
  vals := TJSONObject.Create;
  vals.Add('path', path);
  if last <> '' then vals.Add('last', last);
  body := Col_FetchJson(FillTemplate(JStr(art, 'detailUrl'), vals, False));
  vals.Free;
  try
    if JStr(art, 'rootPath') <> '' then doc := JPath(body, JStr(art, 'rootPath')) else doc := body;
    skip := JStr(art, 'skipField');
    if (skip <> '') and JsTruthy(JPath(doc, skip)) then raise Exception.Create('Skipped (' + skip + ')');
    img := JPath(doc, 'image|imageUrl|welcomePic|thumbnail');
    Result := TJSONObject.Create;
    Result.Add('url', url);
    Result.Add('title', U16Copy(JsTrim(JValStr(JPath(doc, JStr(art, 'titleField', 'title')))), 300));
    Result.Add('text', U16Copy(RichText(JPath(doc, JStr(art, 'textField', 'content'))), COL_MAX_TEXT));
    if (img <> nil) and (img.JSONType = jtString) then Result.Add('image', img.AsString) else Result.Add('image', '');
    day := ParseDayJ(JPath(doc, JStr(art, 'dateField', 'publishedAt')));
    if day <> '' then Result.Add('publishedDay', day) else Result.Add('publishedDay', TJSONNull.Create);
  finally
    body.Free;
  end;
end;

// ---------------------------------------------------------------- collect.ts

// fetchArticle(url, source). caller frees
function Col_FetchArticle(const url: string; src: TJSONObject): TJSONObject;
begin
  if JStr(JObj(src, 'article'), 'type') = 'json' then Result := Col_FetchJsonArticle(url, src)
  else Result := Col_ExtractHtml(Col_Fetch(url), url, src);
end;

function Col_IsUsable(a: TJSONData): Boolean;
begin
  Result := (U16Len(JStr(a, 'title')) >= 8) and (U16Len(JStr(a, 'text')) >= 80);
end;

// robots.txt (robots-parser semantics)
function ColRobAgent(const ua: string): string;
begin
  Result := LowerCase(ua);
  if Pos('/', Result) > 0 then Result := Copy(Result, 1, Pos('/', Result) - 1);
  Result := Trim(Result);
end;

procedure Col_LoadRobots(const siteUrl: string);
var
  body, ln, k, v, robotsUrl, pth, qry: string;
  lines, cur: TStringList;
  i, j, p: Integer;
  noneUA: Boolean;
begin
  if RobRules = nil then
  begin
    RobRules := TStringList.Create;
    RobAgents := TStringList.Create;
    RobDelays := TStringList.Create;
  end;
  RobRules.Clear; RobAgents.Clear; RobDelays.Clear;
  robotsUrl := UrlResolve('/robots.txt', siteUrl);
  UrlParseAbs(robotsUrl, RobUrlSc, RobUrlHost, RobUrlPort, pth, qry);
  try
    body := Col_Fetch(robotsUrl);
  except
    body := '';
  end;
  lines := TStringList.Create;
  cur := TStringList.Create;
  lines.Text := body;
  noneUA := True;
  for i := 0 to lines.Count - 1 do
  begin
    ln := lines[i];
    p := Pos('#', ln);
    if p > 0 then ln := Copy(ln, 1, p - 1);
    p := Pos(':', ln);
    if p = 0 then continue;
    k := LowerCase(Trim(Copy(ln, 1, p - 1)));
    v := Trim(Copy(ln, p + 1, MaxInt));
    if k = '' then continue;
    if k = 'user-agent' then
    begin
      if noneUA then cur.Clear;
      if v <> '' then cur.Add(ColRobAgent(v));
    end
    else if (k = 'disallow') or (k = 'allow') then
    begin
      for j := 0 to cur.Count - 1 do
      begin
        if RobAgents.IndexOf(cur[j]) < 0 then RobAgents.Add(cur[j]);
        if v <> '' then
        begin
          if k = 'allow' then RobRules.Add(cur[j] + #9 + 'A' + #9 + UrlEncPart(v, False))
          else RobRules.Add(cur[j] + #9 + 'D' + #9 + UrlEncPart(v, False));
        end;
      end;
    end
    else if k = 'crawl-delay' then
    begin
      for j := 0 to cur.Count - 1 do
      begin
        if RobAgents.IndexOf(cur[j]) < 0 then RobAgents.Add(cur[j]);
        if StrToFloatDef(v, -1) >= 0 then RobDelays.Values[cur[j]] := v;
      end;
    end;
    noneUA := k <> 'user-agent';
  end;
  lines.Free;
  cur.Free;
end;

function ColRobGroup: string;
begin
  Result := ColRobAgent(Env('USER_AGENT', 'SedevResearch/1.0 (+https://sedev.mn)'));
  if RobAgents.IndexOf(Result) < 0 then Result := '*';
end;

function Col_CrawlDelaySec: Double;
begin
  Result := StrToFloatDef(RobDelays.Values[ColRobGroup], 0);
end;

// google robotstxt matching (* and $)
function ColRobMatch(const pattern, path: string): Boolean;
var
  lens: TIntArr;
  num, p, i, nm: Integer;
begin
  SetLength(lens, Length(path) + 1);
  lens[0] := 0;
  num := 1;
  for p := 1 to Length(pattern) do
  begin
    if (pattern[p] = '$') and (p = Length(pattern)) then exit(lens[num - 1] = Length(path));
    if pattern[p] = '*' then
    begin
      num := Length(path) - lens[0] + 1;
      for i := 1 to num - 1 do lens[i] := lens[i - 1] + 1;
    end
    else
    begin
      nm := 0;
      for i := 0 to num - 1 do
        if (lens[i] < Length(path)) and (path[lens[i] + 1] = pattern[p]) then
        begin
          lens[nm] := lens[i] + 1;
          Inc(nm);
        end;
      if nm = 0 then exit(False);
      num := nm;
    end;
  end;
  Result := True;
end;

// robots.isAllowed(url) !== false
function Col_RobotsAllowed(const url: string): Boolean;
var
  sc, host, port, path, query, grp, pat, bestPat: string;
  i, p1, p2: Integer;
  bestAllow, found, allow: Boolean;
begin
  Result := True;
  if not UrlParseAbs(url, sc, host, port, path, query) then exit;
  if (sc <> RobUrlSc) or (host <> RobUrlHost) or (port <> RobUrlPort) then exit;
  if query <> '' then path := path + '?' + query;
  grp := ColRobGroup;
  found := False;
  bestPat := '';
  bestAllow := True;
  for i := 0 to RobRules.Count - 1 do
  begin
    p1 := Pos(#9, RobRules[i]);
    if Copy(RobRules[i], 1, p1 - 1) <> grp then continue;
    allow := RobRules[i][p1 + 1] = 'A';
    p2 := p1 + 2;
    pat := Copy(RobRules[i], p2 + 1, MaxInt);
    if not ColRobMatch(pat, path) then continue;
    if not found or (Length(pat) > Length(bestPat)) or ((Length(pat) = Length(bestPat)) and allow and not bestAllow) then
    begin
      found := True;
      bestPat := pat;
      bestAllow := allow;
    end;
  end;
  if found then Result := bestAllow;
end;

// collectSource(source, window, limit, isKnown). known = urls already stored (nil: looked up in "articles").
// returns {articles:[{url,title,text,image,publishedDay}], report:{slug,name,found,fetched,skippedKnown,outsideWindow,unknownDate,errors:[{url,error}],status}}. caller frees
function Col_CollectSource(src: TJSONObject; const ws, we: string; limit: Integer; known: TStringList): TJSONObject;
var
  report, a: TJSONObject;
  arts, errors, all: TJSONArray;
  links, days, knownSet: TStringList;
  deadline: QWord;
  delayMs, i, skippedKnown, outside, unknownDate: Integer;
  url, day: string;

  procedure AddErr(const u, msg: string);
  var
    e: TJSONObject;
  begin
    if errors.Count >= 20 then exit;
    e := TJSONObject.Create;
    e.Add('url', u);
    e.Add('error', U16Copy(msg, 200));
    errors.Add(e);
  end;

begin
  Result := TJSONObject.Create;
  arts := TJSONArray.Create;
  errors := TJSONArray.Create;
  report := TJSONObject.Create;
  Result.Add('articles', arts);
  Result.Add('report', report);
  links := TStringList.Create;
  days := TStringList.Create;
  knownSet := TStringList.Create;
  knownSet.Sorted := True; knownSet.CaseSensitive := True; knownSet.UseLocale := False; knownSet.Duplicates := dupIgnore;
  skippedKnown := 0; outside := 0; unknownDate := 0;
  try
    deadline := GetTickCount64 + COL_DEADLINE_MS;
    Col_LoadRobots(JStr(src, 'url'));
    // politeness delay only when robots.txt asks for it (original: max(300ms, crawl-delay) with 3 parallel fetches)
    delayMs := Round(Col_CrawlDelaySec * 1000);
    try
      Col_DiscoverLinks(src, ws, we, limit, links, days);
    except
      on E: Exception do
      begin
        AddErr(JStr(src, 'url'), E.Message);
        report.Add('slug', JStr(src, 'slug')); report.Add('name', JStr(src, 'name'));
        report.Add('found', 0); report.Add('fetched', 0); report.Add('skippedKnown', 0);
        report.Add('outsideWindow', 0); report.Add('unknownDate', 0);
        report.Add('errors', errors); errors := nil;
        report.Add('status', 'failed');
        exit;
      end;
    end;
    if known <> nil then
    begin
      for i := 0 to known.Count - 1 do knownSet.Add(known[i]);
    end
    else
    begin
      all := DbLoad('articles');
      for i := 0 to all.Count - 1 do
        if links.IndexOf(JStr(all.Items[i], 'url')) >= 0 then knownSet.Add(JStr(all.Items[i], 'url'));
      all.Free;
    end;
    for i := 0 to links.Count - 1 do
      if knownSet.IndexOf(links[i]) >= 0 then Inc(skippedKnown);
    for i := 0 to links.Count - 1 do
    begin
      url := links[i];
      if knownSet.IndexOf(url) >= 0 then continue;
      if GetTickCount64 > deadline then break;
      if not Col_RobotsAllowed(url) then begin AddErr(url, 'Blocked by robots.txt'); continue; end;
      if delayMs > 0 then Sleep(delayMs);
      try
        a := Col_FetchArticle(url, src);
        if not Col_IsUsable(a) then
        begin
          a.Free;
          AddErr(url, 'Article body not found');
          continue;
        end;
        day := JStr(a, 'publishedDay');
        if day = '' then day := days[i];
        if day = '' then begin a.Free; Inc(unknownDate); continue; end;
        if (day < ws) or (day > we) then begin a.Free; Inc(outside); continue; end;
        JSetS(a, 'publishedDay', day);
        arts.Add(a);
      except
        on E: Exception do AddErr(url, E.Message);
      end;
    end;
    report.Add('slug', JStr(src, 'slug'));
    report.Add('name', JStr(src, 'name'));
    report.Add('found', links.Count);
    report.Add('fetched', arts.Count);
    report.Add('skippedKnown', skippedKnown);
    report.Add('outsideWindow', outside);
    report.Add('unknownDate', unknownDate);
    report.Add('errors', errors.Clone);
    if (errors.Count > 0) or (GetTickCount64 > deadline) then
    begin
      if (arts.Count > 0) or (skippedKnown > 0) then report.Add('status', 'partial') else report.Add('status', 'failed');
    end
    else report.Add('status', 'ok');
  finally
    errors.Free;
    links.Free;
    days.Free;
    knownSet.Free;
  end;
end;

// ---------------------------------------------------------------- scripts/probe.ts

// checks every built-in source (or only `only`) against the live sites without a database
procedure Col_Probe(const only: string);
var
  defs: TJSONArray;
  src, a: TJSONObject;
  i, k, idx: Integer;
  ws, we, recent, url, day, failed, last: string;
  links, days, sample: TStringList;
  pass, usable: Boolean;
  nf: Integer;
begin
  DateWindow(14, ws, we);
  recent := AddDays(we, -7);
  failed := '';
  nf := 0;
  defs := Src_Defaults;
  links := TStringList.Create;
  days := TStringList.Create;
  sample := TStringList.Create;
  try
    for i := 0 to defs.Count - 1 do
    begin
      src := TJSONObject(defs.Items[i]);
      if (only <> '') and (JStr(src, 'slug') <> only) then continue;
      pass := False;
      links.Clear;
      days.Clear;
      try
        Col_DiscoverLinks(src, ws, we, 30, links, days);
        WriteLn;
        WriteLn('== ', JStr(src, 'slug'), ': ', links.Count, ' links');
        sample.Clear;
        last := '';
        for k := 0 to 3 do
        begin
          if links.Count = 0 then break;
          idx := (k * links.Count) div 4;
          if sample.IndexOf(IntToStr(idx)) < 0 then sample.Add(IntToStr(idx));
        end;
        for k := 0 to sample.Count - 1 do
        begin
          idx := StrToInt(sample[k]);
          url := links[idx];
          try
            a := Col_FetchArticle(url, src);
            try
              day := JStr(a, 'publishedDay');
              if day = '' then day := days[idx];
              usable := Col_IsUsable(a);
              if usable and (day <> '') and (day >= recent) then pass := True;
              if day = '' then day := 'no-date';
              if usable then Write('  OK  ') else Write('  BAD ');
              WriteLn(day, ' text=', U16Len(JStr(a, 'text')), ' ', U16Copy(JStr(a, 'title'), 60), '  ', url);
            finally
              a.Free;
            end;
          except
            on E: Exception do WriteLn('  ERR ', url, ': ', E.Message);
          end;
        end;
      except
        on E: Exception do
        begin
          WriteLn;
          WriteLn('== ', JStr(src, 'slug'), ': discovery failed: ', E.Message);
        end;
      end;
      if not pass then
      begin
        if failed <> '' then failed := failed + ', ';
        failed := failed + JStr(src, 'slug');
        Inc(nf);
      end;
    end;
    WriteLn;
    if nf > 0 then WriteLn('FAILED (', nf, '): ', failed) else WriteLn('All sources passed');
  finally
    links.Free; days.Free; sample.Free; defs.Free;
  end;
end;

// ---------------------------------------------------------------- POST /api/admin/sources/probe (sources.routes.ts)

procedure Routes_collector;
var
  src, a, s, res: TJSONObject;
  samples: TJSONArray;
  links, days: TStringList;
  ws, we, url, day: string;
  k: Integer;
begin
  if Route('POST', '/api/admin/sources/probe') then
  begin
    RequirePerm('sources');
    src := Src_ParseBody(ReqBody);
    links := TStringList.Create;
    days := TStringList.Create;
    try
      DateWindow(14, ws, we);
      Col_DiscoverLinks(src, ws, we, 50, links, days);
      samples := TJSONArray.Create;
      for k := 0 to Min(3, links.Count) - 1 do
      begin
        url := links[k];
        s := TJSONObject.Create;
        try
          a := Col_FetchArticle(url, src);
          try
            s.Add('url', url);
            s.Add('title', JStr(a, 'title'));
            day := JStr(a, 'publishedDay');
            if day = '' then day := days[k];
            if day <> '' then s.Add('publishedDay', day) else s.Add('publishedDay', TJSONNull.Create);
            s.Add('textLength', U16Len(JStr(a, 'text')));
            s.Add('excerpt', U16Copy(JStr(a, 'text'), 300));
            s.Add('usable', Col_IsUsable(a));
          finally
            a.Free;
          end;
        except
          on E: Exception do
          begin
            s.Clear;
            s.Add('url', url);
            s.Add('error', E.Message);
            s.Add('usable', False);
          end;
        end;
        samples.Add(s);
      end;
      res := TJSONObject.Create;
      res.Add('linkCount', links.Count);
      res.Add('samples', samples);
      SendJson(200, res);
    finally
      src.Free;
      links.Free;
      days.Free;
    end;
  end;
end;
