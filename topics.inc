// ===================== TOPICS (modules/topics/*: categories, visuals, feed, edition/topic models, public + admin routes) =====================
// collections:
//   topics:   {_id,runId,position,title,reason,storyline,category,articleIds,postIds,metrics,lenses,wraps,visuals,model,
//              reusedFrom?,hidden,editedBy?,createdAt,updatedAt}
//   editions: {_id,runId,region,window:{start,end},topicIds,articleCount,publishedAt,createdAt,updatedAt}
// no feed cache here (the original cached the feed per edition): rebuilt on every request, slow on purpose.

const
  TOP_CATEGORIES = 'politics,economy,society,world,sports,culture,tech';
  TOP_CATEGORY_HINT =
    'politics=улс төр, төр засаг, хууль; economy=эдийн засаг, төсөв, бизнес, уул уурхай; ' +
    'society=нийгэм, эрүүл мэнд, боловсрол, хот, гэмт хэрэг; world=гадаад харилцаа, дэлхийн мэдээ; ' +
    'sports=спорт, тэмцээн; culture=соёл, урлаг, түүх; tech=технологи, шинжлэх ухаан';
  TOP_MAX_STORY_IMAGES = 12;
  TOP_ADMIN_FIELDS: array[0..8] of string = ('runId', 'position', 'title', 'reason', 'category', 'hidden', 'editedBy', 'createdAt', 'updatedAt');
  // visuals.ts MOTIFS descriptions, same order as LENS_MOTIF_IDS in lenses.inc
  TOP_MOTIF_DESC: array[0..24] of string = (
    'зах зээл, хувьцаа, үнэ, индекс, ашиг буурах (баавгай улаан шугамыг доош түлхэнэ)',
    'зах зээл, хувьцаа, экспорт, ашиг өсөх (бух ногоон шугамыг дээш түлхэнэ)',
    'төгрөгийн ханш, валют, гадаад валютын нөөц (жин дээрх зоос)',
    'үнийн өсөлт, инфляц, өрхийн зардал, шатахуун, хүнсний үнэ (сагс, сум)',
    'төсөв, татвар, зээл, өр, цалин, тэтгэвэр (зоосны овоо)',
    'сонгууль, санал хураалт, нам (саналын хуудас хайрцагт унана)',
    'УИХ, хууль, тогтоол, Засгийн газар, яам, томилгоо',
    'шүүх, прокурор, хэрэг, баривчилгаа, авлига (алх цохино)',
    'уул уурхай, нүүрс, зэс, экспорт, хилийн боомт (ачааны машин хил рүү)',
    'зуд, цасан шуурга, хүйтрэлт, мал хорогдол (цас малыг хучна)',
    'ой хээрийн түймэр, гал (дөл тархана)',
    'үер, аадар бороо, ус (ус нэмэгдэнэ)',
    'агаарын бохирдол, утаа, түлш, гэр хороолол',
    'наадам, морь уралдаан, бөх, сур харваа',
    'спорт, тэмцээн, медаль, аварга шалгаруулалт',
    'эрүүл мэнд, эмнэлэг, өвчин, вакцин, эм',
    'боловсрол, сургууль, их сургууль, багш, шалгалт',
    'зам, тээвэр, түгжрэл, автобус, нисэх, төмөр зам',
    'эрчим хүч, цахилгаан, дулаан, станц, тариф',
    'технологи, интернэт, AI, харилцаа холбоо, цахим үйлчилгээ',
    'гадаад харилцаа, айлчлал, гэрээ, хэлэлцээ, олон улс',
    'дайн, зэвсэгт мөргөлдөөн, хориг арга хэмжээ, хурцадмал байдал',
    'соёл, урлаг, тоглолт, кино, өв',
    'осол, гамшиг, аюул, сэрэмжлүүлэг (бусад тохиолдолд)',
    'тоо баримт, судалгаа, статистик (ерөнхий)');

// categories.ts toCategory
function Top_ToCategory(const v: string): string;
begin
  if (v <> '') and (Pos(',' + v + ',', ',' + TOP_CATEGORIES + ',') > 0) then Result := v else Result := 'society';
end;

// categorySchema as json text
function Top_CategorySchema: string;
begin
  Result := '{"type":"string","enum":["politics","economy","society","world","sports","culture","tech"],"description":' + JEsc(TOP_CATEGORY_HINT) + '}';
end;

// number as json: int when whole, js-like float otherwise
function Top_Num(v: Double): TJSONData;
begin
  if (Frac(v) = 0) and (Abs(v) < 1E15) then Result := TJSONInt64Number.Create(Trunc(v))
  else Result := TJsFloat.Create(v);
end;

// JS Number(x) for a json value (undefined -> NaN)
function Top_JsNumber(d: TJSONData): Double;
var
  s: string;
begin
  Result := NaN;
  if d = nil then exit;
  case d.JSONType of
    jtNumber: Result := d.AsFloat;
    jtNull: Result := 0;
    jtBoolean: if d.AsBoolean then Result := 1 else Result := 0;
    jtString:
      begin
        s := Trim(d.AsString);
        if s = '' then Result := 0
        else if not TryStrToFloat(s, Result) then Result := NaN;
      end;
  else
    Result := NaN;
  end;
end;

function Top_Finite(v: Double): Boolean;
begin
  Result := not IsNan(v) and not IsInfinite(v);
end;

// String(value ?? '').trim().slice(0, max)
function Top_Clean(d: TJSONData; max: Integer): string;
begin
  if (d = nil) or (d.JSONType = jtNull) then Result := ''
  else Result := JValStr(d);
  Result := U16Copy(JsTrim(Result), max);
end;

function Top_Tone(d: TJSONData): string;
begin
  Result := 'neutral';
  if (d <> nil) and (d.JSONType = jtString) and ((d.AsString = 'positive') or (d.AsString = 'negative') or (d.AsString = 'neutral')) then
    Result := d.AsString;
end;

// visuals.ts visualsRules()
function Top_VisualsRules: string;
var
  i: Integer;
  cat: string;
begin
  cat := '';
  for i := 0 to High(LENS_MOTIF_IDS) do
  begin
    if i > 0 then cat := cat + #10;
    cat := cat + '  ' + LENS_MOTIF_IDS[i] + ': ' + TOP_MOTIF_DESC[i];
  end;
  Result :=
    'visuals талбар (уншигчийн хуудсын хөдөлгөөнт зураг):' + #10 +
    '- motifs: "cover" (нүүр), гол санаа бүрт (kp1, kp2… дарааллаар, гол санааны тоогоор) болон wraps-ийн хэсэг бүрт нэг зураг. ' +
    'Тухайн хэсэг / гол санааны агуулгад хамгийн тохирохыг сонго (жишээ нь хувьцаа унасан бол market-down); ' +
    'нэг зургийг дараалан бүү давт. tone: тухайн хэсгийн мэдээ сайн (positive), муу (negative) эсвэл саармаг (neutral). ' +
    'caption: зураг дээрх богино шошго, баримтаас (жишээ нь "Хилийн боомт: өдөрт 110 машин").' + #10 +
    cat +
    #10 + '- balance: талуудын байр суурь хэрхэн хуваагдаж байгааг 2–3 бүлгээр (жишээ нь Дэмжсэн / Саармаг / Эсэргүүцсэн), хувь нийлбэр 100. ' +
    'Зөвхөн эх сурвалжид байгаа байр суурьт тулгуурла; талууд тодорхойгүй бол хоосон жагсаалт.' + #10 +
    '- gauge: эх сурвалжид байгаа нэг бодит хувь, биелэлт, хэмжээ (жишээ нь төлөвлөгөөний биелэлт 84%, min 0, max 100). ' +
    'Тоог өөрөө тооцож бүү зохио; байхгүй бол хоосон жагсаалт.' + #10;
end;

// visuals.ts cleanVisuals: known motifs once per slot, balance scaled to 100, gauge in range. caller frees
function Top_CleanVisuals(lenses: TJSONArray; raw: TJSONData): TJSONObject;
var
  slots, seen: TStringList;
  i, k: Integer;
  arr, motifs, balance: TJSONArray;
  m, o, g: TJSONData;
  lens, motif: string;
  okMotif: Boolean;
  labels, tones: array of string;
  shares: array of Double;
  total, v, mn, mx, sh: Double;
  gauge: TJSONObject;
begin
  slots := Lens_MotifSlots(lenses);
  for i := 0 to lenses.Count - 1 do slots.Add(JStr(lenses.Items[i], 'key'));
  seen := TStringList.Create;
  motifs := TJSONArray.Create;
  balance := TJSONArray.Create;
  gauge := nil;
  arr := nil;
  if (raw <> nil) and (raw is TJSONObject) then arr := JArr(raw, 'motifs');
  if arr <> nil then
    for i := 0 to arr.Count - 1 do
    begin
      m := arr.Items[i];
      if not (m is TJSONObject) then continue;
      o := TJSONObject(m).Find('lens');
      if (o = nil) or (o.JSONType <> jtString) then continue;
      lens := o.AsString;
      if slots.IndexOf(lens) < 0 then continue;
      o := TJSONObject(m).Find('motif');
      if (o = nil) or (o.JSONType <> jtString) then continue;
      motif := o.AsString;
      okMotif := False;
      for k := 0 to High(LENS_MOTIF_IDS) do
        if LENS_MOTIF_IDS[k] = motif then okMotif := True;
      if not okMotif then continue;
      if seen.IndexOf(lens) >= 0 then continue;
      seen.Add(lens);
      o := TJSONObject.Create;
      TJSONObject(o).Add('lens', lens);
      TJSONObject(o).Add('motif', motif);
      TJSONObject(o).Add('tone', Top_Tone(TJSONObject(m).Find('tone')));
      TJSONObject(o).Add('caption', Top_Clean(TJSONObject(m).Find('caption'), 60));
      motifs.Add(o);
    end;
  // balance
  SetLength(labels, 0); SetLength(shares, 0); SetLength(tones, 0);
  arr := nil;
  if (raw <> nil) and (raw is TJSONObject) then arr := JArr(raw, 'balance');
  if arr <> nil then
    for i := 0 to arr.Count - 1 do
    begin
      m := arr.Items[i];
      if m is TJSONObject then
      begin
        lens := Top_Clean(TJSONObject(m).Find('label'), 30);
        sh := Top_JsNumber(TJSONObject(m).Find('share'));
        motif := Top_Tone(TJSONObject(m).Find('tone'));
      end
      else
      begin
        lens := ''; sh := NaN; motif := 'neutral';
      end;
      if (lens = '') or not Top_Finite(sh) or (sh <= 0) then continue;
      if Length(labels) >= 3 then break;
      SetLength(labels, Length(labels) + 1); labels[High(labels)] := lens;
      SetLength(shares, Length(shares) + 1); shares[High(shares)] := sh;
      SetLength(tones, Length(tones) + 1); tones[High(tones)] := motif;
    end;
  if Length(labels) >= 2 then
  begin
    total := 0;
    for i := 0 to High(shares) do total := total + shares[i];
    for i := 0 to High(labels) do
    begin
      o := TJSONObject.Create;
      TJSONObject(o).Add('label', labels[i]);
      TJSONObject(o).Add('share', Top_Num(Floor(shares[i] / total * 100 + 0.5)));
      TJSONObject(o).Add('tone', tones[i]);
      balance.Add(o);
    end;
  end;
  // gauge (array of at most one)
  g := nil;
  if (raw <> nil) and (raw is TJSONObject) and (JArr(raw, 'gauge') <> nil) and (JArr(raw, 'gauge').Count > 0) then
    g := JArr(raw, 'gauge').Items[0];
  if (g <> nil) and (g is TJSONObject) then
  begin
    v := Top_JsNumber(TJSONObject(g).Find('value'));
    mn := Top_JsNumber(TJSONObject(g).Find('min'));
    mx := Top_JsNumber(TJSONObject(g).Find('max'));
    if Top_Finite(v) and Top_Finite(mn) and Top_Finite(mx) and (mn < mx) and (v >= mn) and (v <= mx) and
      (Top_Clean(TJSONObject(g).Find('label'), 60) <> '') then
    begin
      gauge := TJSONObject.Create;
      gauge.Add('label', Top_Clean(TJSONObject(g).Find('label'), 60));
      gauge.Add('value', Top_Num(v));
      gauge.Add('min', Top_Num(mn));
      gauge.Add('max', Top_Num(mx));
      gauge.Add('unit', Top_Clean(TJSONObject(g).Find('unit'), 16));
      gauge.Add('note', Top_Clean(TJSONObject(g).Find('note'), 160));
    end;
  end;
  Result := TJSONObject.Create;
  Result.Add('motifs', motifs);
  Result.Add('balance', balance);
  if gauge <> nil then Result.Add('gauge', gauge) else Result.Add('gauge', TJSONNull.Create);
  slots.Free;
  seen.Free;
end;

// wraps without "sourceIds" keys (deep). caller frees
function Top_WithoutCitations(d: TJSONData): TJSONData;
var
  i: Integer;
  o: TJSONObject;
  a: TJSONArray;
begin
  if d = nil then exit(TJSONObject.Create);
  if d is TJSONObject then
  begin
    o := TJSONObject.Create;
    for i := 0 to TJSONObject(d).Count - 1 do
      if TJSONObject(d).Names[i] <> 'sourceIds' then
        o.Add(TJSONObject(d).Names[i], Top_WithoutCitations(TJSONObject(d).Items[i]));
    Result := o;
  end
  else if d is TJSONArray then
  begin
    a := TJSONArray.Create;
    for i := 0 to TJSONArray(d).Count - 1 do a.Add(Top_WithoutCitations(TJSONArray(d).Items[i]));
    Result := a;
  end
  else Result := d.Clone;
end;

// texts of the first keypoints lens of a topic
function Top_Keypoints(t: TJSONData): TJSONArray;
var
  lenses, arr: TJSONArray;
  i: Integer;
  key, s: string;
  v, p: TJSONData;
begin
  Result := TJSONArray.Create;
  lenses := JArr(t, 'lenses');
  if lenses = nil then exit;
  key := '';
  for i := 0 to lenses.Count - 1 do
    if JStr(lenses.Items[i], 'format') = 'keypoints' then begin key := JStr(lenses.Items[i], 'key'); break; end;
  if key = '' then
  begin
    // lens.find(...) undefined -> no keypoints (a lens with key '' would be odd anyway)
    exit;
  end;
  v := JObj(t, 'wraps');
  if v <> nil then v := TJSONObject(v).Find(key);
  if (v = nil) or not (v is TJSONArray) then exit;
  arr := TJSONArray(v);
  for i := 0 to arr.Count - 1 do
  begin
    p := arr.Items[i];
    s := '';
    if (p is TJSONObject) and (TJSONObject(p).Find('text') <> nil) and (TJSONObject(p).Find('text').JSONType <> jtNull) then
      s := JValStr(TJSONObject(p).Find('text'));
    if s <> '' then Result.Add(s);
  end;
end;

// region query param (z.enum(['mn','world']).default('mn')); errors go to Pv*
function Top_RegionQ: string;
begin
  Result := 'mn';
  if Req.QueryFields.IndexOfName('region') < 0 then exit;
  Result := Req.QueryFields.Values['region'];
  if (Result <> 'mn') and (Result <> 'world') then
    PvFail('region', 'Invalid option: expected one of "mn"|"world"');
end;

// index (TStringList sorted, Objects = position) of docs by _id. caller frees
function Top_IdIndex(arr: TJSONArray): TStringList;
var
  i: Integer;
begin
  Result := TStringList.Create;
  Result.Sorted := True;
  Result.CaseSensitive := True;
  Result.Duplicates := dupIgnore;
  for i := 0 to arr.Count - 1 do
    Result.AddObject(JStr(arr.Items[i], '_id'), TObject(PtrInt(i)));
end;

function Top_Find(arr: TJSONArray; idx: TStringList; const id: string): TJSONObject;
var
  k: Integer;
begin
  Result := nil;
  k := idx.IndexOf(id);
  if k >= 0 then Result := TJSONObject(arr.Items[PtrInt(idx.Objects[k])]);
end;

function Top_HasId(arr: TJSONArray; const id: string): Boolean;
var
  i: Integer;
begin
  Result := False;
  if arr = nil then exit;
  for i := 0 to arr.Count - 1 do
    if arr.Items[i].AsString = id then exit(True);
end;

// articleTime of an article doc in ms
function Top_ArticleMs(a: TJSONData): Int64;
begin
  Result := Soc_ArticleTimeMs(JStr(a, 'publishedDay'), Soc_DocMs(a, 'createdAt'));
end;

// topicCards: reader cards of topics (list of refs). caller frees
function Top_Cards(topics: TList): TJSONArray;
var
  arts: TJSONArray;
  aidx: TStringList;
  i, j: Integer;
  t: TJSONObject;
  ids: TJSONArray;
  a: TJSONObject;
  card: TJSONObject;
  img: string;
  best, ms: Int64;
  hot: TJSONData;
begin
  Result := TJSONArray.Create;
  arts := DbLoad('articles');
  aidx := Top_IdIndex(arts);
  for i := 0 to topics.Count - 1 do
  begin
    t := TJSONObject(topics[i]);
    ids := JArr(t, 'articleIds');
    img := '';
    best := 0;
    if ids <> nil then
      for j := 0 to ids.Count - 1 do
      begin
        a := Top_Find(arts, aidx, ids.Items[j].AsString);
        if a = nil then continue;
        if (img = '') and (JStr(a, 'image') <> '') then img := JStr(a, 'image');
        ms := Top_ArticleMs(a);
        if ms > best then best := ms;
      end;
    card := TJSONObject.Create;
    card.Add('_id', JStr(t, '_id'));
    card.Add('title', JStr(t, 'title'));
    if t.Find('reason') <> nil then card.Add('reason', t.Find('reason').Clone);
    if t.Find('category') <> nil then card.Add('category', t.Find('category').Clone);
    if img <> '' then card.Add('image', img) else card.Add('image', TJSONNull.Create);
    hot := JPath(t, 'metrics.hot');
    if hot <> nil then card.Add('hot', hot.Clone) else card.Add('hot', TJSONNull.Create);
    if best > 0 then card.Add('latestAt', Soc_IsoMs(best)) else card.Add('latestAt', TJSONNull.Create);
    card.Add('keypoints', Top_Keypoints(t));
    Result.Add(card);
  end;
  aidx.Free;
  arts.Free;
end;

// sorts a TList of topic objects by position (stable insertion sort)
procedure Top_SortByPosition(l: TList);
var
  i, j: Integer;
  tmp: Pointer;
begin
  for i := 1 to l.Count - 1 do
  begin
    tmp := l[i];
    j := i - 1;
    while (j >= 0) and (JNum(TJSONObject(l[j]), 'position') > JNum(TJSONObject(tmp), 'position')) do
    begin
      l[j + 1] := l[j];
      Dec(j);
    end;
    l[j + 1] := tmp;
  end;
end;

// editionView: the edition with its visible topics as cards (+ previousRank). caller frees
function Top_EditionView(ed: TJSONObject): TJSONObject;
var
  allTop, allEd, tids, cards, aids: TJSONArray;
  tidx: TStringList;
  vis, before: TList;
  i, j, k, n, best, shared, tmp: Integer;
  t, prev, match, b: TJSONObject;
  region, pubAt, reused: string;
  order: array of Integer;
  hots: array of Double;
  ranks: array of Integer;
  card: TJSONObject;
  hasPrev: Boolean;
begin
  allTop := DbLoad('topics');
  tidx := Top_IdIndex(allTop);
  vis := TList.Create;
  before := TList.Create;
  region := JStr(ed, 'region', 'mn');
  // visible topics of the edition (mongo $in: natural order), sorted by position
  tids := JArr(ed, 'topicIds');
  for i := 0 to allTop.Count - 1 do
  begin
    t := TJSONObject(allTop.Items[i]);
    if JBool(t, 'hidden') then continue;
    if Top_HasId(tids, JStr(t, '_id')) then vis.Add(t);
  end;
  Top_SortByPosition(vis);
  // previous edition of the same region
  allEd := DbLoad('editions');
  prev := nil;
  pubAt := JStr(ed, 'publishedAt');
  for i := 0 to allEd.Count - 1 do
  begin
    b := TJSONObject(allEd.Items[i]);
    if not Src_InRegion(b, region) then continue;
    if not (JStr(b, 'publishedAt') < pubAt) then continue;
    if (prev = nil) or (JStr(b, 'publishedAt') > JStr(prev, 'publishedAt')) then prev := b;
  end;
  hasPrev := prev <> nil;
  if hasPrev then
  begin
    for i := 0 to allTop.Count - 1 do
    begin
      t := TJSONObject(allTop.Items[i]);
      if JBool(t, 'hidden') then continue;
      if Top_HasId(JArr(prev, 'topicIds'), JStr(t, '_id')) then before.Add(t);
    end;
    Top_SortByPosition(before);
    // chartRanks: hot desc, index asc
    n := before.Count;
    SetLength(order, n); SetLength(hots, n); SetLength(ranks, n);
    for i := 0 to n - 1 do
    begin
      order[i] := i;
      if (JPath(TJSONObject(before[i]), 'metrics.hot') <> nil) and (JPath(TJSONObject(before[i]), 'metrics.hot').JSONType = jtNumber) then
        hots[i] := JPath(TJSONObject(before[i]), 'metrics.hot').AsFloat
      else hots[i] := 0;
    end;
    for i := 1 to n - 1 do
    begin
      tmp := order[i];
      j := i - 1;
      while (j >= 0) and ((hots[order[j]] < hots[tmp]) or ((hots[order[j]] = hots[tmp]) and (order[j] > tmp))) do
      begin
        order[j + 1] := order[j];
        Dec(j);
      end;
      order[j + 1] := tmp;
    end;
    for i := 0 to n - 1 do ranks[order[i]] := i + 1;
  end;
  cards := Top_Cards(vis);
  for i := 0 to cards.Count - 1 do
  begin
    card := TJSONObject(cards.Items[i]);
    if not hasPrev then continue;
    t := TJSONObject(vis[i]);
    match := nil;
    k := -1;
    reused := JStr(t, 'reusedFrom');
    if reused <> '' then
      for j := 0 to before.Count - 1 do
        if JStr(TJSONObject(before[j]), '_id') = reused then begin match := TJSONObject(before[j]); k := j; break; end;
    if match = nil then
    begin
      best := 0;
      aids := JArr(t, 'articleIds');
      for j := 0 to before.Count - 1 do
      begin
        shared := 0;
        b := TJSONObject(before[j]);
        if (JArr(b, 'articleIds') <> nil) and (aids <> nil) then
          for n := 0 to JArr(b, 'articleIds').Count - 1 do
            if Top_HasId(aids, JArr(b, 'articleIds').Items[n].AsString) then Inc(shared);
        if shared > best then begin best := shared; match := b; k := j; end;
      end;
    end;
    if match <> nil then card.Add('previousRank', ranks[k]) else card.Add('previousRank', TJSONNull.Create);
  end;
  Result := TJSONObject.Create;
  Result.Add('_id', JStr(ed, '_id'));
  Result.Add('region', region);
  if JObj(ed, 'window') <> nil then Result.Add('window', JObj(ed, 'window').Clone);
  Result.Add('publishedAt', JStr(ed, 'publishedAt'));
  Result.Add('articleCount', Top_Num(JNum(ed, 'articleCount')));
  Result.Add('topics', cards);
  vis.Free;
  before.Free;
  tidx.Free;
  allTop.Free;
  allEd.Free;
end;

// ---------- feed.ts (Шуурхай): every story of a region once, newest news first ----------
type
  TTopFeedEntry = record
    topicId: string;
    ms: Int64; // 0 = no latestAt
    isNew: Boolean;
  end;
  TTopFeed = array of TTopFeedEntry;

// compare(a, b) of feed.ts: <0 when a comes first
function Top_FeedCmp(const a, b: TTopFeedEntry): Integer;
begin
  if b.ms > a.ms then exit(1);
  if b.ms < a.ms then exit(-1);
  if a.topicId < b.topicId then Result := 1
  else if a.topicId > b.topicId then Result := -1
  else Result := 0;
end;

function Top_FeedEntries(const region: string): TTopFeed;
var
  allEd, allTop, arts, ids: TJSONArray;
  tidx, aidx: TStringList;
  eds: array of TRepEdition;
  i, j, k, ne, cnt: Integer;
  e, t, a: TJSONObject;
  newestIso: string;
  newest: TDateTime;
  hasAny: Boolean;
  tmp: TTopFeedEntry;
  best, ms: Int64;
begin
  SetLength(Result, 0);
  allEd := DbLoad('editions');
  allTop := DbLoad('topics');
  tidx := Top_IdIndex(allTop);
  SetLength(eds, 0);
  hasAny := False;
  newest := 0;
  for i := 0 to allEd.Count - 1 do
  begin
    e := TJSONObject(allEd.Items[i]);
    if not Src_InRegion(e, region) then continue;
    SetLength(eds, Length(eds) + 1);
    ne := High(eds);
    eds[ne].id := JStr(e, '_id');
    eds[ne].publishedAt := ParseIso(JStr(e, 'publishedAt'));
    if (not hasAny) or (eds[ne].publishedAt > newest) then newest := eds[ne].publishedAt;
    hasAny := True;
    SetLength(eds[ne].topics, 0);
    ids := JArr(e, 'topicIds');
    if ids <> nil then
      for j := 0 to ids.Count - 1 do
      begin
        t := Top_Find(allTop, tidx, ids.Items[j].AsString);
        if t = nil then continue;
        SetLength(eds[ne].topics, Length(eds[ne].topics) + 1);
        with eds[ne].topics[High(eds[ne].topics)] do
        begin
          id := JStr(t, '_id');
          title := JStr(t, 'title');
          category := JStr(t, 'category');
          position := JNum(t, 'position');
          reusedFrom := JStr(t, 'reusedFrom');
          hasHot := (JPath(t, 'metrics.hot') <> nil) and (JPath(t, 'metrics.hot').JSONType = jtNumber);
          if hasHot then hot := JPath(t, 'metrics.hot').AsFloat else hot := 0;
          itemIds := TStringList.Create;
          if JArr(t, 'articleIds') <> nil then for k := 0 to JArr(t, 'articleIds').Count - 1 do itemIds.Add(JArr(t, 'articleIds').Items[k].AsString);
          if JArr(t, 'postIds') <> nil then for k := 0 to JArr(t, 'postIds').Count - 1 do itemIds.Add(JArr(t, 'postIds').Items[k].AsString);
        end;
      end;
  end;
  allEd.Free;
  if not hasAny then
  begin
    tidx.Free; allTop.Free;
    exit;
  end;
  newestIso := IsoOf(newest);
  Rep_LinkStories(eds);
  arts := DbLoad('articles');
  aidx := Top_IdIndex(arts);
  cnt := 0;
  SetLength(Result, Length(RepStories));
  for i := 0 to High(RepStories) do
  begin
    t := Top_Find(allTop, tidx, RepStories[i].topicId);
    if (t = nil) or JBool(t, 'hidden') then continue;
    best := 0;
    ids := JArr(t, 'articleIds');
    if ids <> nil then
      for j := 0 to ids.Count - 1 do
      begin
        a := Top_Find(arts, aidx, ids.Items[j].AsString);
        if a = nil then continue;
        ms := Top_ArticleMs(a);
        if ms > best then best := ms;
      end;
    Result[cnt].topicId := RepStories[i].topicId;
    Result[cnt].ms := best;
    Result[cnt].isNew := RepStories[i].firstSeen = newestIso;
    Inc(cnt);
  end;
  SetLength(Result, cnt);
  // stable insertion sort by compare
  for i := 1 to cnt - 1 do
  begin
    tmp := Result[i];
    j := i - 1;
    while (j >= 0) and (Top_FeedCmp(Result[j], tmp) > 0) do
    begin
      Result[j + 1] := Result[j];
      Dec(j);
    end;
    Result[j + 1] := tmp;
  end;
  for i := 0 to High(eds) do
    for j := 0 to High(eds[i].topics) do eds[i].topics[j].itemIds.Free;
  aidx.Free; arts.Free; tidx.Free; allTop.Free;
end;

function Top_Cursor(const e: TTopFeedEntry): string;
begin
  Result := IntToStr(e.ms) + '_' + e.topicId;
end;

// ---------- routes ----------
procedure Routes_topics;
var
  region, cursor, s, msStr, id: string;
  limit, i, j, start, cp: Integer;
  qv: Double;
  feed: TTopFeed;
  after: TTopFeedEntry;
  allTop, allEd, arts, list, cards, ids, imgs: TJSONArray;
  tidx: TStringList;
  pageL: TList;
  isNewL: TStringList;
  o, t, ed, e, a: TJSONObject;
  more: Boolean;
  sortIdx: array of Integer;
  tmp: Integer;
  body: TJSONObject;
  title, reason, category: string;
  hidden: Boolean;
  found: Boolean;
begin
  // ---------------- public ----------------
  // GET /api/feed?region&cursor&limit
  if Route('GET', '/api/feed') then
  begin
    PvReset;
    region := Top_RegionQ;
    cursor := Q('cursor');
    limit := 20;
    if Req.QueryFields.IndexOfName('limit') >= 0 then
    begin
      s := Trim(Req.QueryFields.Values['limit']);
      if s = '' then qv := 0
      else if not TryStrToFloat(s, qv) then qv := NaN;
      if IsNan(qv) then PvFail('limit', 'Invalid input: expected number, received NaN')
      else if Frac(qv) <> 0 then PvFail('limit', 'Invalid input: expected int, received number')
      else if qv < 1 then PvFail('limit', 'Too small: expected number to be >=1')
      else if qv > 50 then PvFail('limit', 'Too big: expected number to be <=50')
      else limit := Trunc(qv);
    end;
    PvDone;
    feed := Top_FeedEntries(region);
    start := 0;
    if cursor <> '' then
    begin
      cp := Pos('_', cursor);
      if cp > 0 then begin msStr := Copy(cursor, 1, cp - 1); after.topicId := Copy(cursor, cp + 1, MaxInt); end
      else begin msStr := cursor; after.topicId := ''; end;
      if Pos('_', after.topicId) > 0 then after.topicId := Copy(after.topicId, 1, Pos('_', after.topicId) - 1);
      after.ms := StrToInt64Def(msStr, 0);
      start := -1;
      for i := 0 to High(feed) do
        if Top_FeedCmp(after, feed[i]) < 0 then begin start := i; break; end;
      if start < 0 then start := Length(feed);
    end;
    pageL := TList.Create;
    isNewL := TStringList.Create;
    allTop := DbLoad('topics');
    tidx := Top_IdIndex(allTop);
    for i := start to Min(start + limit, Length(feed)) - 1 do
    begin
      t := Top_Find(allTop, tidx, feed[i].topicId);
      if (t = nil) or JBool(t, 'hidden') then continue;
      pageL.Add(t);
      if feed[i].isNew then isNewL.Add(feed[i].topicId);
    end;
    more := start + limit < Length(feed);
    cards := Top_Cards(pageL);
    for i := 0 to cards.Count - 1 do
      if isNewL.IndexOf(JStr(cards.Items[i], '_id')) >= 0 then TJSONObject(cards.Items[i]).Add('previousRank', TJSONNull.Create)
      else TJSONObject(cards.Items[i]).Add('previousRank', 0);
    o := TJSONObject.Create;
    o.Add('topics', cards);
    j := Min(start + limit, Length(feed)) - 1; // last entry of the page
    if more and (j >= start) then o.Add('nextCursor', Top_Cursor(feed[j])) else o.Add('nextCursor', TJSONNull.Create);
    SendJson(200, o);
    pageL.Free; isNewL.Free; tidx.Free; allTop.Free;
    exit;
  end;

  // GET /api/editions?region
  if Route('GET', '/api/editions') then
  begin
    PvReset;
    region := Top_RegionQ;
    PvDone;
    allEd := DbLoad('editions');
    SetLength(sortIdx, 0);
    for i := 0 to allEd.Count - 1 do
      if Src_InRegion(allEd.Items[i], region) then
      begin
        SetLength(sortIdx, Length(sortIdx) + 1);
        sortIdx[High(sortIdx)] := i;
      end;
    for i := 1 to High(sortIdx) do
    begin
      tmp := sortIdx[i];
      j := i - 1;
      while (j >= 0) and (JStr(allEd.Items[sortIdx[j]], 'publishedAt') < JStr(allEd.Items[tmp], 'publishedAt')) do
      begin
        sortIdx[j + 1] := sortIdx[j];
        Dec(j);
      end;
      sortIdx[j + 1] := tmp;
    end;
    list := TJSONArray.Create;
    for i := 0 to Min(100, Length(sortIdx)) - 1 do
    begin
      e := TJSONObject(allEd.Items[sortIdx[i]]);
      o := TJSONObject.Create;
      o.Add('_id', JStr(e, '_id'));
      o.Add('region', JStr(e, 'region', 'mn'));
      if JObj(e, 'window') <> nil then o.Add('window', JObj(e, 'window').Clone);
      o.Add('publishedAt', JStr(e, 'publishedAt'));
      o.Add('articleCount', Top_Num(JNum(e, 'articleCount')));
      if JArr(e, 'topicIds') <> nil then o.Add('topicCount', JArr(e, 'topicIds').Count) else o.Add('topicCount', 0);
      list.Add(o);
    end;
    allEd.Free;
    SendJson(200, list);
    exit;
  end;

  // GET /api/editions/latest?region
  if Route('GET', '/api/editions/latest') then
  begin
    PvReset;
    region := Top_RegionQ;
    PvDone;
    allEd := DbLoad('editions');
    ed := nil;
    for i := 0 to allEd.Count - 1 do
    begin
      e := TJSONObject(allEd.Items[i]);
      if not Src_InRegion(e, region) then continue;
      if (ed = nil) or (JStr(e, 'publishedAt') > JStr(ed, 'publishedAt')) then ed := e;
    end;
    if ed = nil then begin allEd.Free; raise EApi.CreateS(404, 'No edition published yet'); end;
    ed := TJSONObject(ed.Clone);
    allEd.Free;
    try
      SendJson(200, Top_EditionView(ed));
    finally
      ed.Free;
    end;
    exit;
  end;

  // GET /api/editions/:id
  if Route('GET', '/api/editions/:id') then
  begin
    id := P(2);
    ed := nil;
    if IsId(id) then ed := DbGet('editions', id);
    if ed = nil then raise EApi.CreateS(404, 'Not found');
    try
      SendJson(200, Top_EditionView(ed));
    finally
      ed.Free;
    end;
    exit;
  end;

  // GET /api/topics/:id
  if Route('GET', '/api/topics/:id') then
  begin
    id := P(2);
    t := nil;
    if IsId(id) then t := DbGet('topics', id);
    if (t <> nil) and JBool(t, 'hidden') then FreeAndNil(t);
    if t = nil then raise EApi.CreateS(404, 'Not found');
    try
      // every distinct article photo (mongo $in: natural order)
      ids := JArr(t, 'articleIds');
      imgs := TJSONArray.Create;
      arts := DbLoad('articles');
      isNewL := TStringList.Create;
      for i := 0 to arts.Count - 1 do
      begin
        a := TJSONObject(arts.Items[i]);
        s := JStr(a, 'image');
        if s = '' then continue;
        if not Top_HasId(ids, JStr(a, '_id')) then continue;
        if isNewL.IndexOf(s) >= 0 then continue;
        isNewL.Add(s);
        if imgs.Count < TOP_MAX_STORY_IMAGES then imgs.Add(s);
      end;
      isNewL.Free;
      arts.Free;
      // the edition this topic was published in
      allEd := DbLoad('editions');
      ed := nil;
      for i := 0 to allEd.Count - 1 do
        if Top_HasId(JArr(allEd.Items[i], 'topicIds'), id) then begin ed := TJSONObject(allEd.Items[i]); break; end;
      o := TJSONObject.Create;
      o.Add('_id', JStr(t, '_id'));
      o.Add('runId', JStr(t, 'runId'));
      o.Add('title', JStr(t, 'title'));
      if t.Find('reason') <> nil then o.Add('reason', t.Find('reason').Clone);
      o.Add('storyline', JStr(t, 'storyline'));
      if t.Find('category') <> nil then o.Add('category', t.Find('category').Clone);
      if ed <> nil then o.Add('editionId', JStr(ed, '_id')) else o.Add('editionId', TJSONNull.Create);
      if ed <> nil then o.Add('region', JStr(ed, 'region', 'mn')) else o.Add('region', 'mn');
      o.Add('createdAt', JStr(t, 'createdAt'));
      if t.Find('lenses') <> nil then o.Add('lenses', t.Find('lenses').Clone);
      o.Add('wraps', Top_WithoutCitations(t.Find('wraps')));
      if (t.Find('visuals') <> nil) then o.Add('visuals', t.Find('visuals').Clone) else o.Add('visuals', TJSONNull.Create);
      if JPath(t, 'metrics.hot') <> nil then o.Add('hot', JPath(t, 'metrics.hot').Clone) else o.Add('hot', TJSONNull.Create);
      if imgs.Count > 0 then o.Add('image', imgs.Items[0].AsString) else o.Add('image', TJSONNull.Create);
      o.Add('images', imgs);
      allEd.Free;
      SendJson(200, o);
    finally
      t.Free;
    end;
    exit;
  end;

  // ---------------- admin (/api/admin/topics, per-route permissions) ----------------
  if Route('GET', '/api/admin/topics') then
  begin
    RequirePerm('topics.view');
    allTop := DbLoad('topics');
    // sort createdAt desc, position asc
    SetLength(sortIdx, allTop.Count);
    for i := 0 to allTop.Count - 1 do sortIdx[i] := i;
    for i := 1 to High(sortIdx) do
    begin
      tmp := sortIdx[i];
      j := i - 1;
      while (j >= 0) and ((JStr(allTop.Items[sortIdx[j]], 'createdAt') < JStr(allTop.Items[tmp], 'createdAt')) or
        ((JStr(allTop.Items[sortIdx[j]], 'createdAt') = JStr(allTop.Items[tmp], 'createdAt')) and
         (JNum(allTop.Items[sortIdx[j]], 'position') > JNum(allTop.Items[tmp], 'position')))) do
      begin
        sortIdx[j + 1] := sortIdx[j];
        Dec(j);
      end;
      sortIdx[j + 1] := tmp;
    end;
    list := TJSONArray.Create;
    for i := 0 to Min(200, Length(sortIdx)) - 1 do
    begin
      t := TJSONObject(allTop.Items[sortIdx[i]]);
      o := TJSONObject.Create;
      o.Add('_id', JStr(t, '_id'));
      for j := 0 to High(TOP_ADMIN_FIELDS) do
        if t.Find(TOP_ADMIN_FIELDS[j]) <> nil then o.Add(TOP_ADMIN_FIELDS[j], t.Find(TOP_ADMIN_FIELDS[j]).Clone);
      list.Add(o);
    end;
    allTop.Free;
    SendJson(200, list);
    exit;
  end;

  if Route('PUT', '/api/admin/topics/:id') or Route('PUT', '/api/admin/topics/:id/visibility') then
  begin
    hidden := PCount = 5;
    if hidden then RequirePerm('topics.hide') else RequirePerm('topics.edit');
    PvReset;
    body := PvObj(ReqBody, '', '');
    title := ''; reason := ''; category := '';
    found := False;
    if body <> nil then
    begin
      if hidden then found := PvBool(body, 'hidden', '')
      else
      begin
        title := PvStr(body, 'title', '', False, '', True, False, 1, 300, '', '');
        reason := PvStr(body, 'reason', '', False, '', True, False, -1, 1000, '', '');
        category := PvEnum(body, 'category', '', TOP_CATEGORIES);
      end;
    end;
    PvDone;
    id := P(3);
    if not IsId(id) then raise EApi.CreateS(404, 'Not found');
    DbLock.Enter;
    try
      allTop := DbLoad('topics');
      t := nil;
      for i := 0 to allTop.Count - 1 do
        if JStr(allTop.Items[i], '_id') = id then begin t := TJSONObject(allTop.Items[i]); break; end;
      if t = nil then begin allTop.Free; raise EApi.CreateS(404, 'Not found'); end;
      if hidden then JSetB(t, 'hidden', found)
      else
      begin
        JSetS(t, 'title', title);
        JSetS(t, 'reason', reason);
        JSetS(t, 'category', category);
      end;
      JSetS(t, 'editedBy', JStr(CurUser, 'username'));
      JSetS(t, 'updatedAt', NowIso);
      DbSave('topics', allTop);
      o := TJSONObject(t.Clone);
      allTop.Free;
    finally
      DbLock.Leave;
    end;
    if o.IndexOfName('wraps') >= 0 then o.Delete('wraps');
    if o.IndexOfName('lenses') >= 0 then o.Delete('lenses');
    SendJson(200, o);
    exit;
  end;
end;
