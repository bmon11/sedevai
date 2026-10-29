// ===================== ARTICLES public list (modules/articles/articles.routes.ts) =====================
// articles: {_id, url, sourceSlug, sourceName, region, title, text, image, publishedDay, fetchedAt, entityScanAt, createdAt}

const
  ART_EXCERPT_CHARS = 220;

procedure Routes_articles;
var
  all, rows, outArr: TJSONArray;
  keys, slugs: TStringList;
  i, j, page, limit, total, idx: Integer;
  q, qLow, source: string;
  a, o: TJSONObject;
  counts: array of Integer;
  names: TStringList;
  res: TJSONObject;
begin
  // every collected article, newest first, page by page
  if Route('GET', '/api/articles') then
  begin
    PvReset;
    page := PvQInt('page', 1, 1, 0);
    limit := PvQInt('limit', 24, 1, 60);
    q := Trim(Req.QueryFields.Values['q']);
    if PplULen(q) > 100 then PvFail('q', 'Too big: expected string to have <=100 characters');
    source := Req.QueryFields.Values['source'];
    if PplULen(source) > 60 then PvFail('source', 'Too big: expected string to have <=60 characters');
    PvDone;
    qLow := PplLower(q);
    all := DbLoad('articles');
    // filter, then sort by publishedDay desc, createdAt desc (sorted string keys, reversed)
    keys := TStringList.Create;
    for i := 0 to all.Count - 1 do
    begin
      a := TJSONObject(all.Items[i]);
      if (source <> '') and (JStr(a, 'sourceSlug') <> source) then continue;
      if (q <> '') and (Pos(qLow, PplLower(JStr(a, 'title'))) = 0) then continue;
      keys.AddObject(JStr(a, 'publishedDay') + '|' + JStr(a, 'createdAt') + '|' + Format('%.8d', [i]), TObject(PtrInt(i)));
    end;
    keys.Sort;
    total := keys.Count;
    outArr := TJSONArray.Create;
    for j := (page - 1) * limit to Min(total, page * limit) - 1 do
    begin
      idx := PtrInt(keys.Objects[total - 1 - j]);
      a := TJSONObject(all.Items[idx]);
      o := TJSONObject.Create;
      o.Add('_id', JStr(a, '_id'));
      o.Add('url', JStr(a, 'url'));
      o.Add('title', JStr(a, 'title'));
      o.Add('image', JStr(a, 'image'));
      o.Add('sourceSlug', JStr(a, 'sourceSlug'));
      o.Add('sourceName', JStr(a, 'sourceName'));
      o.Add('publishedDay', JStr(a, 'publishedDay'));
      o.Add('excerpt', PplUCut(JStr(a, 'text'), ART_EXCERPT_CHARS));
      outArr.Add(o);
    end;
    keys.Free;
    all.Free;
    res := TJSONObject.Create;
    res.Add('total', total);
    res.Add('page', page);
    res.Add('pages', Max(1, Ceil(total / limit)));
    res.Add('articles', outArr);
    SendJson(200, res);
    exit;
  end;

  // publishers that have articles, for the source filter: [{slug, name, count}] by name
  if Route('GET', '/api/articles/sources') then
  begin
    all := DbLoad('articles');
    slugs := TStringList.Create;
    names := TStringList.Create;
    SetLength(counts, 0);
    for i := 0 to all.Count - 1 do
    begin
      idx := slugs.IndexOf(JStr(all.Items[i], 'sourceSlug'));
      if idx < 0 then
      begin
        slugs.Add(JStr(all.Items[i], 'sourceSlug'));
        names.Add(JStr(all.Items[i], 'sourceName'));
        SetLength(counts, Length(counts) + 1);
        counts[High(counts)] := 1;
      end
      else Inc(counts[idx]);
    end;
    all.Free;
    rows := TJSONArray.Create;
    for i := 0 to slugs.Count - 1 do
    begin
      o := TJSONObject.Create;
      o.Add('slug', slugs[i]);
      o.Add('name', names[i]);
      o.Add('count', counts[i]);
      rows.Add(o);
    end;
    slugs.Free;
    names.Free;
    JSortBy(rows, 'name', False);
    SendJson(200, rows);
    exit;
  end;
end;
