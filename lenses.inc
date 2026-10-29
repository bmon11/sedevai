// ===================== LENSES = wrap points (modules/lenses/*, scripts/merge-lenses.ts) =====================
// lenses collection: {_id, key, label, instruction, format, order, enabled, createdAt, updatedAt}

const
  LENS_FORMATS = 'text,list,keypoints,facts,timeline,perspectives,proscons';
  LENS_CITE_LIMIT = 5;
  // order = reading order on a topic page
  DEFAULT_LENSES_JSON =
    '[{"key":"tldr","label":"Товчхондоо","format":"keypoints","instruction":"Уншигч 20 секундэд ойлгох 3 гол санааг нэг нэг өгүүлбэрээр бич: юу болсон, хэнд хамаатай, дараа нь юу болох."},' +
    '{"key":"summary","label":"Юу болов?","format":"text","instruction":"Үйл явдлыг товч, баримтад тулгуурлан 2–3 догол мөрөөр тайлбарла. Мэдээ бүрийг тусад нь тоймлохын оронд холбоосыг тайлбарла."},' +
    '{"key":"numbers","label":"Тоон баримт","format":"facts","instruction":"Сэдвийг ойлгоход чухал 3–6 тоо баримт (дүн, хувь, тоо ширхэг, байр, огноо) эх сурвалжаас яг байгаагаар нь гарга. Тооцоолж бүү зохио."},' +
    '{"key":"trending","label":"Яагаад анхаарал татаж байна?","format":"text","instruction":"Энэ сэдэв яагаад яг одоо олон нийтийн анхаарлыг татаж байгааг тайлбарла: шинэ үйл явдал, хугацааны шахалт, хэнд хамаатай, хэр өргөн хүрээтэй. ' +
    'Үүнтэй холбоотой суурь шалтгаан, өмнөх нөхцөл байдлыг мөн тайлбарла. Эх сурвалжаар батлагдсан шалтгаан ба талуудын таамаглалыг ялгаж бич."},' +
    '{"key":"timeline","label":"Үйл явдлын дараалал","format":"timeline","instruction":"Гол үйл явдлуудыг болсон огноогоор нь дараалуул (30 хүртэл). Үйл явдал бүрт товч гарчиг өг. Нийтлэл хэвлэгдсэн огноог үйл явдал болсон огноотой бүү андуур."},' +
    '{"key":"perspectives","label":"Байр суурь, үзэл бодол","format":"perspectives","instruction":"Оролцогч талууд (Засгийн газар, сөрөг хүчин, мэргэжилтэн, бизнес, иргэд г.м) ямар байр суурьтай байгааг эх сурвалжид байгаагаар нь бич (4 хүртэл). ' +
    'Тал бүрийн байр сууринд энэ үйл явдлын тэдэнд үзүүлэх эерэг ба сөрөг нөлөө, боломж ба эрсдэлийг багтаа. Өөрийн үзэл бодол бүү нэм, хүний гэм буруу бүү тогтоо."},' +
    '{"key":"unknowns","label":"Тодорхойгүй зүйлс","format":"list","instruction":"Нотолгоо дутуу, хариулт нь одоогоор тодорхойгүй асуултуудыг жагсаа (15 хүртэл)."}]';
  // topics/visuals.ts MOTIFS (copy, keep in sync with the reader)
  LENS_MOTIF_IDS: array[0..24] of string = ('market-down', 'market-up', 'currency', 'prices', 'budget', 'election',
    'parliament', 'court', 'mining', 'dzud', 'wildfire', 'flood', 'air-pollution', 'naadam', 'sport', 'health',
    'education', 'transport', 'energy', 'tech', 'diplomacy', 'conflict', 'culture', 'alert', 'chart');

// LIST_LIMIT of wrap-schema.ts
function Lens_ListLimit(const format: string): Integer;
begin
  if format = 'list' then Result := 6
  else if format = 'keypoints' then Result := 4
  else if format = 'facts' then Result := 6
  else if format = 'timeline' then Result := 10
  else if format = 'perspectives' then Result := 4
  else if format = 'proscons' then Result := 4
  else Result := 0;
end;

function Lens_SourceLabel(index: Integer): string;
begin
  Result := 'S' + IntToStr(index + 1);
end;

function Lens_Str(const desc: string): TJSONObject;
begin
  Result := TJSONObject.Create;
  Result.Add('type', 'string');
  if desc <> '' then Result.Add('description', desc);
end;

function Lens_Num(const desc: string): TJSONObject;
begin
  Result := TJSONObject.Create;
  Result.Add('type', 'number');
  if desc <> '' then Result.Add('description', desc);
end;

// obj(): takes ownership of props
function Lens_Obj(props: TJSONObject): TJSONObject;
var
  req: TJSONArray;
  i: Integer;
begin
  Result := TJSONObject.Create;
  Result.Add('type', 'object');
  Result.Add('properties', props);
  req := TJSONArray.Create;
  for i := 0 to props.Count - 1 do req.Add(props.Names[i]);
  Result.Add('required', req);
end;

function Lens_Arr(items: TJSONObject; maxItems: Integer): TJSONObject;
begin
  Result := TJSONObject.Create;
  Result.Add('type', 'array');
  Result.Add('items', items);
  Result.Add('maxItems', maxItems);
end;

function Lens_Enum(const values: array of string; const desc: string): TJSONObject;
var
  a: TJSONArray;
  i: Integer;
begin
  Result := TJSONObject.Create;
  Result.Add('type', 'string');
  a := TJSONArray.Create;
  for i := 0 to High(values) do a.Add(values[i]);
  Result.Add('enum', a);
  if desc <> '' then Result.Add('description', desc);
end;

// cover, kp1..kp6 (when a keypoints lens exists), then every non-keypoints lens key
function Lens_MotifSlots(lenses: TJSONArray): TStringList;
var
  i: Integer;
  hasKp: Boolean;
begin
  Result := TStringList.Create;
  Result.Add('cover');
  hasKp := False;
  for i := 0 to lenses.Count - 1 do
    if JStr(lenses.Items[i], 'format') = 'keypoints' then hasKp := True;
  if hasKp then
    for i := 1 to 6 do Result.Add('kp' + IntToStr(i));
  for i := 0 to lenses.Count - 1 do
    if JStr(lenses.Items[i], 'format') <> 'keypoints' then Result.Add(JStr(lenses.Items[i], 'key'));
end;

// topics/visuals.ts visualsSchema (copy)
function Lens_VisualsSchema(lenses: TJSONArray): TJSONObject;
var
  slots: TStringList;
  slotArr: array of string;
  i: Integer;
  p, m: TJSONObject;
  motifs, balance, gauge: TJSONObject;
begin
  slots := Lens_MotifSlots(lenses);
  SetLength(slotArr, slots.Count);
  for i := 0 to slots.Count - 1 do slotArr[i] := slots[i];
  p := TJSONObject.Create;
  p.Add('lens', Lens_Enum(slotArr, ''));
  p.Add('motif', Lens_Enum(LENS_MOTIF_IDS, ''));
  p.Add('tone', Lens_Enum(['positive', 'negative', 'neutral'], 'positive = сайн мэдээ, negative = муу мэдээ, neutral'));
  p.Add('caption', Lens_Str('Зураг дээрх 6 үгээс уртгүй шошго, баримтад тулгуурласан'));
  motifs := TJSONObject.Create;
  motifs.Add('type', 'array');
  motifs.Add('maxItems', slots.Count);
  motifs.Add('items', Lens_Obj(p));
  p := TJSONObject.Create;
  p.Add('label', Lens_Str('Байр суурийн бүлэг, 3 үгээс уртгүй'));
  p.Add('share', Lens_Num('Хувь, бүгд нийлээд 100'));
  p.Add('tone', Lens_Enum(['positive', 'negative', 'neutral'], ''));
  balance := TJSONObject.Create;
  balance.Add('type', 'array');
  balance.Add('maxItems', 3);
  balance.Add('items', Lens_Obj(p));
  p := TJSONObject.Create;
  p.Add('label', Lens_Str('Үзүүлэлтийн нэр, 6 үгээс уртгүй'));
  p.Add('value', Lens_Num(''));
  p.Add('min', Lens_Num(''));
  p.Add('max', Lens_Num(''));
  p.Add('unit', Lens_Str('%, тэрбум ₮ г.м'));
  p.Add('note', Lens_Str('Энэ тоо юу гэсэн үг вэ, 20 үгээс уртгүй'));
  gauge := TJSONObject.Create;
  gauge.Add('type', 'array');
  gauge.Add('maxItems', 1);
  gauge.Add('items', Lens_Obj(p));
  m := TJSONObject.Create;
  m.Add('motifs', motifs);
  m.Add('balance', balance);
  m.Add('gauge', gauge);
  Result := Lens_Obj(m);
  slots.Free;
end;

function Lens_Cite(sourceCount: Integer): TJSONObject;
var
  labels: TJSONArray;
  it: TJSONObject;
  i: Integer;
begin
  labels := TJSONArray.Create;
  for i := 0 to sourceCount - 1 do labels.Add(Lens_SourceLabel(i));
  it := TJSONObject.Create;
  it.Add('type', 'string');
  it.Add('enum', labels);
  Result := TJSONObject.Create;
  Result.Add('type', 'array');
  Result.Add('items', it);
  Result.Add('maxItems', LENS_CITE_LIMIT);
end;

function Lens_FormatSchema(const format: string; sourceCount: Integer): TJSONObject;
var
  p, side: TJSONObject;
begin
  p := TJSONObject.Create;
  if format = 'text' then
  begin
    p.Add('text', Lens_Str('')); p.Add('sourceIds', Lens_Cite(sourceCount));
    exit(Lens_Obj(p));
  end;
  if format = 'list' then
  begin
    p.Add('text', Lens_Str('')); p.Add('sourceIds', Lens_Cite(sourceCount));
    exit(Lens_Arr(Lens_Obj(p), Lens_ListLimit('list')));
  end;
  if format = 'keypoints' then
  begin
    p.Add('text', Lens_Str('25 үгээс уртгүй нэг өгүүлбэр')); p.Add('sourceIds', Lens_Cite(sourceCount));
    exit(Lens_Arr(Lens_Obj(p), Lens_ListLimit('keypoints')));
  end;
  if format = 'facts' then
  begin
    p.Add('value', Lens_Str('Тоо, хэмжээ, жишээ нь "15 медаль", "₮1.2 их наяд", "27-р байр"'));
    p.Add('label', Lens_Str('Тоо юуг илэрхийлэхийг 8 үгээр'));
    p.Add('sourceIds', Lens_Cite(sourceCount));
    exit(Lens_Arr(Lens_Obj(p), Lens_ListLimit('facts')));
  end;
  if format = 'timeline' then
  begin
    p.Add('date', Lens_Str('YYYY-MM-DD'));
    p.Add('title', Lens_Str('6 үгээс уртгүй товч гарчиг'));
    p.Add('event', Lens_Str('40 үгээс уртгүй'));
    p.Add('sourceIds', Lens_Cite(sourceCount));
    exit(Lens_Arr(Lens_Obj(p), Lens_ListLimit('timeline')));
  end;
  if format = 'perspectives' then
  begin
    p.Add('actor', Lens_Str(''));
    p.Add('position', Lens_Str('70 үгээс уртгүй'));
    p.Add('sourceIds', Lens_Cite(sourceCount));
    exit(Lens_Arr(Lens_Obj(p), Lens_ListLimit('perspectives')));
  end;
  // proscons
  side := TJSONObject.Create;
  side.Add('text', Lens_Str('')); side.Add('sourceIds', Lens_Cite(sourceCount));
  p.Add('good', Lens_Arr(Lens_Obj(side), Lens_ListLimit('proscons')));
  side := TJSONObject.Create;
  side.Add('text', Lens_Str('')); side.Add('sourceIds', Lens_Cite(sourceCount));
  p.Add('bad', Lens_Arr(Lens_Obj(side), Lens_ListLimit('proscons')));
  Result := Lens_Obj(p);
end;

// buildWrapSchema: structured output schema (JSON text) of one topic's wrap.
// lenses = snapshots [{key,label,instruction,format}], sourceCount = number of topic sources (S1..Sn)
function Lens_WrapSchema(lenses: TJSONArray; sourceCount: Integer): string;
var
  props, wraps, root: TJSONObject;
  i: Integer;
begin
  wraps := TJSONObject.Create;
  for i := 0 to lenses.Count - 1 do
    wraps.Add(JStr(lenses.Items[i], 'key'), Lens_FormatSchema(JStr(lenses.Items[i], 'format'), sourceCount));
  props := TJSONObject.Create;
  props.Add('storyline', Lens_Str('Сэдвийн үндсэн өгүүлэмж, 2–3 өгүүлбэр, өөрийн үгээр'));
  props.Add('title', Lens_Str('Сэдвийн тодорхой, асуулт хэлбэрийн гарчиг'));
  props.Add('wraps', Lens_Obj(wraps));
  props.Add('visuals', Lens_VisualsSchema(lenses));
  root := Lens_Obj(props);
  Result := root.AsJSON;
  root.Free;
end;

// fix(): item with deduped, known, max 5 citations. returns new object
function Lens_FixCited(item: TJSONData; sourceCount: Integer): TJSONObject;
var
  ids, src: TJSONArray;
  i, n: Integer;
  s: string;
  seen: TStringList;
begin
  if (item <> nil) and (item is TJSONObject) then Result := TJSONObject(item.Clone)
  else Result := TJSONObject.Create;
  src := JArr(Result, 'sourceIds');
  ids := TJSONArray.Create;
  seen := TStringList.Create;
  if src <> nil then
    for i := 0 to src.Count - 1 do
    begin
      if src.Items[i].JSONType <> jtString then continue;
      s := src.Items[i].AsString;
      if seen.IndexOf(s) >= 0 then continue;
      seen.Add(s);
      if (Copy(s, 1, 1) = 'S') then
      begin
        n := StrToIntDef(Copy(s, 2, MaxInt), 0);
        if (n >= 1) and (n <= sourceCount) and (Lens_SourceLabel(n - 1) = s) and (ids.Count < LENS_CITE_LIMIT) then ids.Add(s);
      end;
    end;
  seen.Free;
  JSet(Result, 'sourceIds', ids);
end;

// cited(): fixed items with at least one citation, max limit
function Lens_CitedList(items: TJSONData; limit, sourceCount: Integer): TJSONArray;
var
  i: Integer;
  it: TJSONObject;
begin
  Result := TJSONArray.Create;
  if (items = nil) or not (items is TJSONArray) then exit;
  for i := 0 to TJSONArray(items).Count - 1 do
  begin
    it := Lens_FixCited(TJSONArray(items).Items[i], sourceCount);
    if (JArr(it, 'sourceIds').Count > 0) and (Result.Count < limit) then Result.Add(it) else it.Free;
  end;
end;

// cleanWraps: keeps only citations to known sources and the item limits. caller frees
function Lens_CleanWraps(lenses: TJSONArray; wraps: TJSONObject; sourceCount: Integer): TJSONObject;
var
  i, j, lim: Integer;
  key, fmt: string;
  v: TJSONData;
  arr: TJSONArray;
  pc: TJSONObject;
begin
  Result := TJSONObject.Create;
  if wraps = nil then exit;
  for i := 0 to lenses.Count - 1 do
  begin
    key := JStr(lenses.Items[i], 'key');
    fmt := JStr(lenses.Items[i], 'format');
    v := wraps.Find(key);
    if v = nil then continue;
    if fmt = 'text' then JSet(Result, key, Lens_FixCited(v, sourceCount))
    else if (fmt = 'list') or (fmt = 'keypoints') then
    begin
      arr := TJSONArray.Create;
      lim := Lens_ListLimit(fmt);
      if v is TJSONArray then
        for j := 0 to TJSONArray(v).Count - 1 do
          if arr.Count < lim then arr.Add(Lens_FixCited(TJSONArray(v).Items[j], sourceCount));
      JSet(Result, key, arr);
    end
    else if fmt = 'proscons' then
    begin
      pc := TJSONObject.Create;
      pc.Add('good', Lens_CitedList(JPath(v, 'good'), Lens_ListLimit('proscons'), sourceCount));
      pc.Add('bad', Lens_CitedList(JPath(v, 'bad'), Lens_ListLimit('proscons'), sourceCount));
      JSet(Result, key, pc);
    end
    else JSet(Result, key, Lens_CitedList(v, Lens_ListLimit(fmt), sourceCount));
  end;
end;

// enabled lenses in reading order as run snapshots [{key,label,instruction,format}]. caller frees
function Lens_LoadSnapshots: TJSONArray;
var
  all: TJSONArray;
  i: Integer;
  o, s: TJSONObject;
begin
  all := DbLoad('lenses');
  JSortBy(all, 'order', False);
  Result := TJSONArray.Create;
  for i := 0 to all.Count - 1 do
  begin
    o := TJSONObject(all.Items[i]);
    if not JBool(o, 'enabled', True) then continue;
    s := TJSONObject.Create;
    s.Add('key', JStr(o, 'key'));
    s.Add('label', JStr(o, 'label'));
    s.Add('instruction', JStr(o, 'instruction'));
    s.Add('format', JStr(o, 'format'));
    Result.Add(s);
  end;
  all.Free;
end;

// seed: adds missing default lenses (order = index). returns how many defaults exist
function Lens_SeedDefaults: Integer;
var
  defs: TJSONArray;
  i: Integer;
  ex, o: TJSONObject;
begin
  defs := TJSONArray(JParse(DEFAULT_LENSES_JSON));
  Result := defs.Count;
  for i := 0 to defs.Count - 1 do
  begin
    ex := DbFindOne('lenses', 'key', JStr(defs.Items[i], 'key'));
    if ex = nil then
    begin
      o := TJSONObject(defs.Items[i].Clone);
      o.Add('order', i);
      o.Add('enabled', True);
      DbInsert('lenses', o);
      o.Free;
    end;
    ex.Free;
  end;
  defs.Free;
end;

// ---------- merge-lenses one-off migration (cause -> trending, impact -> perspectives) ----------
function Lens_UniqueIds(points: TJSONArray): TJSONArray;
var
  i, j: Integer;
  ids: TJSONArray;
  seen: TStringList;
begin
  Result := TJSONArray.Create;
  seen := TStringList.Create;
  if points <> nil then
    for i := 0 to points.Count - 1 do
    begin
      ids := JArr(points.Items[i], 'sourceIds');
      if ids <> nil then
        for j := 0 to ids.Count - 1 do
          if seen.IndexOf(ids.Items[j].AsString) < 0 then
          begin
            seen.Add(ids.Items[j].AsString);
            Result.Add(ids.Items[j].AsString);
          end;
    end;
  seen.Free;
end;

function Lens_SideAsPerspective(const actor: string; points: TJSONArray): TJSONObject;
var
  i: Integer;
  t, all: string;
begin
  Result := nil;
  all := '';
  if points <> nil then
    for i := 0 to points.Count - 1 do
    begin
      t := Trim(JStr(points.Items[i], 'text'));
      if t = '' then continue;
      if all <> '' then all := all + ' ';
      all := all + t;
    end;
  if all = '' then exit;
  Result := TJSONObject.Create;
  Result.Add('actor', actor);
  Result.Add('position', all);
  Result.Add('sourceIds', Lens_UniqueIds(points));
end;

procedure Lens_MergeLenses(dryRun: Boolean);
var
  defs, all, lensesArr, newLenses, pts, persp: TJSONArray;
  i, j, n, cnt: Integer;
  o, t, wraps, cause, trending, nt, impact, side: TJSONObject;
  txt: string;
  hasTrending, hasPersp, touched: Boolean;
begin
  defs := TJSONArray(JParse(DEFAULT_LENSES_JSON));
  if not dryRun then
  begin
    all := DbLoad('lenses');
    for i := all.Count - 1 downto 0 do
      if (JStr(all.Items[i], 'key') = 'cause') or (JStr(all.Items[i], 'key') = 'impact') then all.Delete(i);
    for i := 0 to all.Count - 1 do
      for j := 0 to defs.Count - 1 do
        if (JStr(all.Items[i], 'key') = JStr(defs.Items[j], 'key')) and
          ((JStr(defs.Items[j], 'key') = 'trending') or (JStr(defs.Items[j], 'key') = 'perspectives')) then
        begin
          JSetS(TJSONObject(all.Items[i]), 'label', JStr(defs.Items[j], 'label'));
          JSetS(TJSONObject(all.Items[i]), 'instruction', JStr(defs.Items[j], 'instruction'));
        end;
    JSortBy(all, 'order', False);
    for i := 0 to all.Count - 1 do JSetN(TJSONObject(all.Items[i]), 'order', i);
    DbSave('lenses', all);
    all.Free;
  end;
  defs.Free;

  all := DbLoad('topics');
  cnt := 0;
  for i := 0 to all.Count - 1 do
  begin
    t := TJSONObject(all.Items[i]);
    lensesArr := JArr(t, 'lenses');
    if lensesArr = nil then continue;
    touched := False;
    for j := 0 to lensesArr.Count - 1 do
      if (JStr(lensesArr.Items[j], 'key') = 'cause') or (JStr(lensesArr.Items[j], 'key') = 'impact') then touched := True;
    if not touched then continue;
    Inc(cnt);
    newLenses := TJSONArray.Create;
    hasTrending := False; hasPersp := False;
    for j := 0 to lensesArr.Count - 1 do
      if (JStr(lensesArr.Items[j], 'key') <> 'cause') and (JStr(lensesArr.Items[j], 'key') <> 'impact') then
      begin
        newLenses.Add(lensesArr.Items[j].Clone);
        if JStr(lensesArr.Items[j], 'key') = 'trending' then hasTrending := True;
        if JStr(lensesArr.Items[j], 'key') = 'perspectives' then hasPersp := True;
      end;
    wraps := JObj(t, 'wraps');
    if wraps <> nil then
    begin
      cause := JObj(wraps, 'cause');
      trending := JObj(wraps, 'trending');
      if hasTrending and (cause <> nil) and (JStr(cause, 'text') <> '') then
      begin
        txt := JStr(trending, 'text');
        if txt <> '' then txt := txt + #10#10;
        txt := txt + JStr(cause, 'text');
        pts := TJSONArray.Create;
        if trending <> nil then pts.Add(trending.Clone) else pts.Add(TJSONObject.Create);
        pts.Add(cause.Clone);
        nt := TJSONObject.Create;
        nt.Add('text', txt);
        nt.Add('sourceIds', Lens_UniqueIds(pts));
        pts.Free;
        JSet(wraps, 'trending', nt);
      end;
      impact := JObj(wraps, 'impact');
      if hasPersp and (impact <> nil) then
      begin
        persp := TJSONArray.Create;
        if JArr(wraps, 'perspectives') <> nil then
          for n := 0 to JArr(wraps, 'perspectives').Count - 1 do persp.Add(JArr(wraps, 'perspectives').Items[n].Clone);
        side := Lens_SideAsPerspective('Эерэг нөлөө', JArr(impact, 'good'));
        if side <> nil then persp.Add(side);
        side := Lens_SideAsPerspective('Сөрөг нөлөө', JArr(impact, 'bad'));
        if side <> nil then persp.Add(side);
        JSet(wraps, 'perspectives', persp);
      end;
      if wraps.IndexOfName('cause') >= 0 then wraps.Delete('cause');
      if wraps.IndexOfName('impact') >= 0 then wraps.Delete('impact');
    end;
    JSet(t, 'lenses', newLenses);
  end;
  if (not dryRun) and (cnt > 0) then DbSave('topics', all);
  all.Free;
  if dryRun then WriteLn('(dry run) Merged wrap points; ' + IntToStr(cnt) + ' topics updated')
  else WriteLn('Merged wrap points; ' + IntToStr(cnt) + ' topics updated');
end;

// ---------- validation of the lens body ----------
function Lens_ParseBody: TJSONObject;
var
  b, emptyBody: TJSONObject;
begin
  emptyBody := nil;
  if (ReqBody <> nil) and (ReqBody is TJSONObject) then b := TJSONObject(ReqBody)
  else begin emptyBody := TJSONObject.Create; b := emptyBody; end;
  Result := TJSONObject.Create;
  try
    PvReset;
    Result.Add('key', PvStr(b, 'key', '', False, '', False, False, -1, -1, '^[a-z][a-z0-9_]*$', 'lowercase letters, digits, _'));
    Result.Add('label', PvStr(b, 'label', '', False, '', False, False, 1, 100, '', ''));
    Result.Add('instruction', PvStr(b, 'instruction', '', False, '', False, False, 10, 2000, '', ''));
    Result.Add('format', PvEnum(b, 'format', '', LENS_FORMATS));
    Result.Add('enabled', PvBool(b, 'enabled', '', True, True));
    PvDone;
  except
    Result.Free;
    emptyBody.Free;
    raise;
  end;
  emptyBody.Free;
end;

procedure Lens_SendAll(status: Integer);
var
  all: TJSONArray;
begin
  all := DbLoad('lenses');
  JSortBy(all, 'order', False);
  SendJson(status, all);
end;

procedure Routes_lenses;
var
  body, doc, other: TJSONObject;
  all, ids: TJSONArray;
  i, j, mx: Integer;
  id: string;
  found: Boolean;
begin
  if Route('GET', '/api/admin/lenses') then
  begin
    RequirePerm('lenses');
    Lens_SendAll(200);
    exit;
  end;

  if Route('POST', '/api/admin/lenses') then
  begin
    RequirePerm('lenses');
    all := DbLoad('lenses');
    mx := -1;
    for i := 0 to all.Count - 1 do
      if JInt(all.Items[i], 'order') > mx then mx := JInt(all.Items[i], 'order');
    all.Free;
    body := Lens_ParseBody;
    try
      other := DbFindOne('lenses', 'key', JStr(body, 'key'));
      if other <> nil then begin other.Free; raise EApi.CreateS(409, 'Already exists'); end;
      body.Add('order', mx + 1);
      DbInsert('lenses', body);
      SendJson(201, body, False);
    finally
      body.Free;
    end;
    exit;
  end;

  // reading order: body is the full list of lens ids in the new order
  if Route('PUT', '/api/admin/lenses/order') then
  begin
    RequirePerm('lenses');
    PvReset;
    ids := nil;
    if (ReqBody <> nil) and (ReqBody is TJSONObject) then ids := JArr(ReqBody, 'ids');
    if ids = nil then PvFail('ids', 'Invalid input: expected array, received ' + PvType(JPath(ReqBody, 'ids')))
    else
      for i := 0 to ids.Count - 1 do
        if ids.Items[i].JSONType <> jtString then
          PvFail('ids.' + IntToStr(i), 'Invalid input: expected string, received ' + PvType(ids.Items[i]));
    PvDone;
    all := DbLoad('lenses');
    found := False;
    for i := 0 to ids.Count - 1 do
      for j := 0 to all.Count - 1 do
        if JStr(all.Items[j], '_id') = ids.Items[i].AsString then
        begin
          JSetN(TJSONObject(all.Items[j]), 'order', i);
          JSetS(TJSONObject(all.Items[j]), 'updatedAt', NowIso);
          found := True;
        end;
    if found then DbSave('lenses', all);
    all.Free;
    Lens_SendAll(200);
    exit;
  end;

  if Route('PUT', '/api/admin/lenses/:id') then
  begin
    RequirePerm('lenses');
    id := P(3);
    body := Lens_ParseBody;
    try
      doc := DbGet('lenses', id);
      if doc = nil then raise EApi.CreateS(404, 'Not found');
      try
        other := DbFindOne('lenses', 'key', JStr(body, 'key'));
        if (other <> nil) and (JStr(other, '_id') <> id) then begin other.Free; raise EApi.CreateS(409, 'Already exists'); end;
        other.Free;
        JMerge(doc, body);
        DbReplace('lenses', doc);
        SendJson(200, doc, False);
      finally
        doc.Free;
      end;
    finally
      body.Free;
    end;
    exit;
  end;

  if Route('DELETE', '/api/admin/lenses/:id') then
  begin
    RequirePerm('lenses');
    if not DbDelete('lenses', P(3)) then raise EApi.CreateS(404, 'Not found');
    SendNoContent;
    exit;
  end;
end;
