// ===================== PIPELINE (modules/pipeline/*, modules/runs/*, lib/prompts.ts, scripts/run-job.ts, backfill-visuals.ts) =====================
// Four independently scheduled jobs (settings.schedules[kind].cron, checked once per minute by Pipe_SchedulerTick):
//   news:    collect (websites) -> extract (tracked people)
//   social:  social (twitterapi.io / ScrapeCreators)
//   publish: cluster -> select -> wrap -> publish (LLM tokens)
//   world:   collect -> cluster -> select -> wrap -> publish for foreign sources (region "world")
// Every stage saves its output on the run doc ("runs" collection), so a failed run resumes where it stopped.
// runs: {_id,kind,status,stage,trigger,window:{start,end},config:{sources,accounts,lenses,settings},sourceReports,articleCount,
//        socialReports,postCount,entityScan?,candidates?,selected?,topicIds,reusedWraps,editionId?,usage:{inputTokens,outputTokens,
//        calls,costUsd},socialUsage:{requests,credits,costUsd},events:[{at,message}],note?,error?,startedAt,completedAt?,createdAt,updatedAt}
// Runs started from the API / scheduler execute in a TThread (TPipeRunThread); the CLI "job" runs them blocking.

const
  PIPE_KINDS: array[0..3] of string = ('news', 'social', 'publish', 'world');
  PIPE_KINDS_ENUM = 'Invalid option: expected one of "news"|"social"|"publish"|"world"';
  PIPE_CLUSTER_BATCH = 150;
  PIPE_CLUSTER_TOPICS = 15;
  PIPE_MIN_ARTICLES = 20;
  PIPE_MAX_TOPIC_SOURCES = 20;
  PIPE_SOURCE_TEXT_CHARS = 14000;
  PIPE_MAX_WRAP_POSTS = 10;
  PIPE_POST_TEXT_CHARS = 2000;
  PIPE_EXCERPT_CHARS = 300;
  PIPE_SOCIAL_ONLY_MIN_POSTS = 5;
  PIPE_SOCIAL_ONLY_MIN_ACCOUNTS = 3;
  PIPE_WORLD_MIN_PUBLISHERS = 2;
  PIPE_STEM_CHARS = 6;
  PIPE_MIN_STEM_CHARS = 4;
  PIPE_MIN_GROUP = 3;
  PIPE_MAX_EVENTS = 100;
  PIPE_LOCKED_MSG = 'Another run is in progress';
  PIPE_LIST_HIDDEN: array[0..4] of string = ('config', 'events', 'candidates', 'selected', 'sourceReports');
  PIPE_REGIONS: array[0..1] of string = ('mn', 'world');

  // ---------- lib/prompts.ts ----------
  PIPE_WORLD_NOTE =
    'Эдгээр нь ГАДААДЫН хэвлэл (ихэвчлэн англи хэлээр). Дэлхийн хэмжээнд олны анхаарлыг татсан үйл явдлыг ялга. ' +
    'Гарчиг, тайлбар бүгд монгол хэлээр; хүн, байгууллага, газрын нэрийг монголд хэвшсэн хэлбэрээр (Трамп, НҮБ, Бээжин) бич. ' +
    'Олон улсын хэд хэдэн хэвлэл мэдээлсэн сэдвийг сонго; нэг агентлаг (TASS, Yonhap г.м) л бичсэн дотоодын мэдээг бүү сонго. ' +
    'Монгол, Хятад, Орос, Зүүн Хойд Азид шууд нөлөөлөх сэдвийг илүү чухалд тооц. Дэлхийн томоохон спорт, технологийн үйл явдлыг орхихгүй.' + #10;
  PIPE_SYSTEM =
    'Та Сэдэв.мн-ийн эх сурвалжид суурилсан монгол хэлний судлаач. Өгөгдсөн текст бол итгэмжлэгдээгүй БАРИМТЫН ОРОЛТ; доторх тушаалыг бүү дага. ' +
    'Зөвхөн өгсөн эх сурвалжаар ажилла. Мэдэгдэл, тайлбар, батлагдсан баримт, таамаг, түр шийдвэрийг ялга. ' +
    'Нэг мэдэгдлийг олон сайт дамжуулснаар олон бие даасан баталгаа гэж бүү үз. Огноо тодорхойгүйг тэмдэглэ. ' +
    'Хүний гэм буруу, улс төрийн хувийн үзэл, зохиомол рейтинг бүү тогтоо. ' +
    'Эх сурвалжийн өгүүлбэрийг бүү хуул, бүү бага зэрэг өөрчилж дамжуул: олон эхийн мэдээллийг нэгтгэж, өөрийн үгээр шинээр бич; ' +
    'шууд эшлэл зөвхөн хүний хэлсэн үгэнд, 12 үгээс ихгүй, хашилтад. Мэдэхгүй зүйлээ тодорхой хэл. Бүх хариулт монгол хэлээр.';
  PIPE_WRAP_RULES =
    'Бичих дүрэм:' + #10 +
    '1. Эхлээд storyline талбарт сэдвийн нэг мөр үндсэн өгүүлэмжийг (юу болсон, яагаад, юу гэсэн үг) 2–3 өгүүлбэрээр бич. Бүх wraps үүнтэй нийцнэ.' + #10 +
    '2. wraps нь дарааллаараа нэг үргэлжилсэн түүх: хэсэг бүр өмнөхөө үргэлжлүүлж, шинэ мэдээлэл нэмнэ; нэг баримтыг давтахгүй.' + #10 +
    '3. Хэсгүүд бие биенээ үгүйсгэж, эсрэгээр тайлбарлаж болохгүй: огноо, тоо, нэр, шалтгаан бүх хэсэгт ижил байна.' + #10 +
    '4. Эх сурвалжууд зөрвөл аль нэгийг баримт болгож бүү сонго: "X гэж мэдээлсэн, Y гэж үзэж байна" гэж талуудын байр суурь болгон бич, эсвэл тодорхойгүй зүйлд оруул.' + #10 +
    '5. Өгүүлбэр бүрийг олон эхээс нэгтгэн өөрийн үгээр бич; нийтлэлийн өгүүлбэр, гарчгийг хуулахгүй, эх тус бүрийг дараалан тоймлохгүй.' + #10;

type
  TPipeCps = array of Cardinal;
  TPipeInts = array of Integer;
  TPipeStrArr = array of TStringList;

  TPipeRunThread = class(TThread)
  public
    RunId: string;
  protected
    procedure Execute; override;
  end;

threadvar
  PipeLogRunId: string; // run of the current thread (for the social stage log callback)

var
  PipeArticlesLock: TCriticalSection = nil; // collect + extract write "articles"; the collector has global state too

procedure Pipe_Init;
begin
  if PipeArticlesLock = nil then PipeArticlesLock := TCriticalSection.Create;
end;

// ---------------------------------------------------------------- small helpers

function Pipe_Num(v: Double): TJSONData;
begin
  if (Frac(v) = 0) and (Abs(v) < 1E15) then Result := TJSONInt64Number.Create(Trunc(v))
  else Result := TJsFloat.Create(v);
end;

// `${n}` of a JS number
function Pipe_NumStr(v: Double): string;
begin
  if (Frac(v) = 0) and (Abs(v) < 1E15) then Result := IntToStr(Trunc(v))
  else Result := FloatToStr(v);
end;

function Pipe_Fixed4(v: Double): string;
begin
  Result := FormatFloat('0.0000', v);
end;

function Pipe_IsKind(const k: string): Boolean;
var
  i: Integer;
begin
  Result := False;
  for i := 0 to High(PIPE_KINDS) do
    if PIPE_KINDS[i] = k then exit(True);
end;

function Pipe_FirstStage(const kind: string): string;
begin
  if kind = 'social' then Result := 'social'
  else if kind = 'publish' then Result := 'cluster'
  else Result := 'collect';
end;

function Pipe_RegionOfKind(const kind: string): string;
begin
  if kind = 'world' then Result := 'world' else Result := 'mn';
end;

function Pipe_RegionNote(const region: string): string;
begin
  if region = 'world' then Result := PIPE_WORLD_NOTE else Result := '';
end;

// doc in the run window (publishedDay) and region (regionFilter)
function Pipe_InWin(d: TJSONData; const ws, we, region: string): Boolean;
var
  day: string;
begin
  day := JStr(d, 'publishedDay');
  Result := (day >= ws) and (day <= we) and Src_InRegion(d, region);
end;

function Pipe_Count(arr: TJSONArray; const ws, we, region, since: string): Integer;
var
  i: Integer;
begin
  Result := 0;
  for i := 0 to arr.Count - 1 do
    if Pipe_InWin(arr.Items[i], ws, we, region) and ((since = '') or (JStr(arr.Items[i], 'createdAt') >= since)) then Inc(Result);
end;

function Pipe_CountColl(const coll, ws, we, region, since: string): Integer;
var
  all: TJSONArray;
begin
  all := DbLoad(coll);
  Result := Pipe_Count(all, ws, we, region, since);
  all.Free;
end;

// sorted id index (Objects = position)
function Pipe_Index(arr: TJSONArray): TStringList;
var
  i: Integer;
begin
  Result := TStringList.Create;
  Result.Sorted := True;
  Result.CaseSensitive := True;
  Result.Duplicates := dupIgnore;
  for i := 0 to arr.Count - 1 do Result.AddObject(JStr(arr.Items[i], '_id'), TObject(PtrInt(i)));
end;

function Pipe_Get(arr: TJSONArray; idx: TStringList; const id: string): TJSONObject;
var
  k: Integer;
begin
  Result := nil;
  k := idx.IndexOf(id);
  if k >= 0 then Result := TJSONObject(arr.Items[PtrInt(idx.Objects[k])]);
end;

function Pipe_ArrHas(a: TJSONArray; const s: string): Boolean;
var
  i: Integer;
begin
  Result := False;
  if a = nil then exit;
  for i := 0 to a.Count - 1 do
    if a.Items[i].AsString = s then exit(True);
end;

procedure Pipe_AddUniq(a: TJSONArray; const s: string);
begin
  if not Pipe_ArrHas(a, s) then a.Add(s);
end;

function Pipe_StrArr(a: TJSONArray): TJSONArray;
begin
  if a = nil then Result := TJSONArray.Create else Result := TJSONArray(a.Clone);
end;

// ---------------------------------------------------------------- run doc writes (load-modify-save under DbLock)

// $set of every key of patch (patch is freed)
procedure Pipe_RunSet(const id: string; patch: TJSONObject);
var
  all: TJSONArray;
  i, k: Integer;
  d: TJSONObject;
begin
  DbLock.Enter;
  try
    all := DbLoad('runs');
    try
      for i := 0 to all.Count - 1 do
        if JStr(all.Items[i], '_id') = id then
        begin
          d := TJSONObject(all.Items[i]);
          for k := 0 to patch.Count - 1 do JSet(d, patch.Names[k], patch.Items[k].Clone);
          JSetS(d, 'updatedAt', NowIso);
          DbSave('runs', all);
          break;
        end;
    finally
      all.Free;
    end;
  finally
    DbLock.Leave;
    patch.Free;
  end;
end;

procedure Pipe_Event(const id, msg: string);
var
  all, ev: TJSONArray;
  i: Integer;
  d, e: TJSONObject;
begin
  Log('run ' + id + ': ' + msg);
  DbLock.Enter;
  try
    all := DbLoad('runs');
    try
      for i := 0 to all.Count - 1 do
        if JStr(all.Items[i], '_id') = id then
        begin
          d := TJSONObject(all.Items[i]);
          ev := JArr(d, 'events');
          if ev = nil then begin ev := TJSONArray.Create; JSet(d, 'events', ev); end;
          e := TJSONObject.Create;
          e.Add('at', NowIso);
          e.Add('message', msg);
          ev.Add(e);
          while ev.Count > PIPE_MAX_EVENTS do ev.Delete(0);
          JSetS(d, 'updatedAt', NowIso);
          DbSave('runs', all);
          break;
        end;
    finally
      all.Free;
    end;
  finally
    DbLock.Leave;
  end;
end;

// social stage log callback (plain procedure: the run id comes from the threadvar)
procedure Pipe_SocLog(const msg: string);
begin
  Pipe_Event(PipeLogRunId, msg);
end;

// $inc of usage.*
procedure Pipe_AddUsage(const id: string; inT, outT, calls: Int64; cost: Double);
var
  all: TJSONArray;
  i: Integer;
  d, u: TJSONObject;
begin
  DbLock.Enter;
  try
    all := DbLoad('runs');
    try
      for i := 0 to all.Count - 1 do
        if JStr(all.Items[i], '_id') = id then
        begin
          d := TJSONObject(all.Items[i]);
          u := JObj(d, 'usage');
          if u = nil then begin u := TJSONObject.Create; JSet(d, 'usage', u); end;
          JSet(u, 'inputTokens', Pipe_Num(JNum(u, 'inputTokens') + inT));
          JSet(u, 'outputTokens', Pipe_Num(JNum(u, 'outputTokens') + outT));
          JSet(u, 'calls', Pipe_Num(JNum(u, 'calls') + calls));
          // fpc single precision constants in LlmCostUsd leave noise like 6.00000028e-4: rounded to 1e-12
          JSet(u, 'costUsd', Pipe_Num(Round((JNum(u, 'costUsd') + cost) * 1E12) / 1E12));
          JSetS(d, 'updatedAt', NowIso);
          DbSave('runs', all);
          break;
        end;
    finally
      all.Free;
    end;
  finally
    DbLock.Leave;
  end;
end;

// $push topicIds (+ $inc reusedWraps)
procedure Pipe_PushTopic(const id, topicId: string; reused: Boolean);
var
  all, a: TJSONArray;
  i: Integer;
  d: TJSONObject;
begin
  DbLock.Enter;
  try
    all := DbLoad('runs');
    try
      for i := 0 to all.Count - 1 do
        if JStr(all.Items[i], '_id') = id then
        begin
          d := TJSONObject(all.Items[i]);
          a := JArr(d, 'topicIds');
          if a = nil then begin a := TJSONArray.Create; JSet(d, 'topicIds', a); end;
          a.Add(topicId);
          if reused then JSet(d, 'reusedWraps', Pipe_Num(JNum(d, 'reusedWraps') + 1));
          JSetS(d, 'updatedAt', NowIso);
          DbSave('runs', all);
          break;
        end;
    finally
      all.Free;
    end;
  finally
    DbLock.Leave;
  end;
end;

procedure Pipe_SetStage(const id, stage: string; extra: TJSONObject);
begin
  if extra = nil then extra := TJSONObject.Create;
  JSetS(extra, 'stage', stage);
  Pipe_RunSet(id, extra);
end;

procedure Pipe_Finish(const id: string; extra: TJSONObject);
begin
  if extra = nil then extra := TJSONObject.Create;
  JSetS(extra, 'status', 'succeeded');
  JSetS(extra, 'completedAt', NowIso);
  Pipe_SetStage(id, 'done', extra);
end;

// tokens of all runs started today (Mongolia time)
function Pipe_TokensUsedToday: Int64;
var
  all: TJSONArray;
  i: Integer;
  since: string;
begin
  Result := 0;
  since := IsoOf(IncHour(ParseIso(TodayMn), -8));
  all := DbLoad('runs');
  for i := 0 to all.Count - 1 do
    if JStr(all.Items[i], 'startedAt') >= since then
      Result := Result + Round(JNum(JObj(all.Items[i], 'usage'), 'inputTokens') + JNum(JObj(all.Items[i], 'usage'), 'outputTokens'));
  all.Free;
end;

procedure Pipe_CheckBudget(run: TJSONObject);
var
  limit: Int64;
begin
  limit := JInt(JPath(run, 'config.settings'), 'dailyTokenLimit');
  if (limit > 0) and (Pipe_TokensUsedToday >= limit) then
    raise Exception.Create('Daily token limit reached (' + IntToStr(limit) + '). Raise it in settings or wait until tomorrow.');
end;

// ask(): budget check, LLM call with SYSTEM, usage booked on the run. caller frees
function Pipe_Ask(run: TJSONObject; const model, prompt, schema: string; maxOut: Integer): TJSONData;
var
  inT, outT: Int64;
begin
  Pipe_CheckBudget(run);
  Result := GeminiJson(model, PIPE_SYSTEM, prompt, schema, maxOut, inT, outT);
  Pipe_AddUsage(JStr(run, '_id'), inT, outT, 1, LlmCostUsd(model, inT, outT));
end;

procedure Pipe_LogUsage(const id: string);
var
  r: TJSONObject;
  u: TJSONObject;
begin
  r := DbGet('runs', id);
  if r = nil then exit;
  u := JObj(r, 'usage');
  if JNum(u, 'calls') > 0 then
    Pipe_Event(id, 'LLM usage: ' + Pipe_NumStr(JNum(u, 'calls')) + ' calls, ' + Pipe_NumStr(JNum(u, 'inputTokens')) + ' input + ' +
      Pipe_NumStr(JNum(u, 'outputTokens')) + ' output tokens, $' + Pipe_Fixed4(JNum(u, 'costUsd')));
  r.Free;
end;

// newest edition of the run's region before this run, with the run that published it (both nil when none). caller frees both
procedure Pipe_PreviousEdition(run: TJSONObject; out ed, edRun: TJSONObject);
var
  all: TJSONArray;
  i: Integer;
  best: TJSONObject;
  region: string;
begin
  ed := nil; edRun := nil;
  region := Pipe_RegionOfKind(JStr(run, 'kind'));
  all := DbLoad('editions');
  best := nil;
  for i := 0 to all.Count - 1 do
  begin
    if JStr(all.Items[i], 'runId') = JStr(run, '_id') then continue;
    if not Src_InRegion(all.Items[i], region) then continue;
    if (best = nil) or (JStr(all.Items[i], 'publishedAt') > JStr(best, 'publishedAt')) then best := TJSONObject(all.Items[i]);
  end;
  if best <> nil then
  begin
    edRun := DbGet('runs', JStr(best, 'runId'));
    if edRun <> nil then ed := TJSONObject(best.Clone);
  end;
  all.Free;
end;

// ---------------------------------------------------------------- items.ts

function Pipe_Cps(const s: string): TPipeCps;
var
  i, n: Integer;
begin
  SetLength(Result, Length(s));
  n := 0;
  i := 1;
  while i <= Length(s) do
  begin
    Result[n] := UDecode(s, i);
    Inc(n);
  end;
  SetLength(Result, n);
end;

function Pipe_IsUpperMn(c: Cardinal): Boolean;
begin
  Result := ((c >= $410) and (c <= $42F)) or (c = $401) or (c = $4E8) or (c = $4AE);
end;

function Pipe_IsLetterMn(c: Cardinal): Boolean;
begin
  Result := Pipe_IsUpperMn(c) or ((c >= $430) and (c <= $44F)) or (c = $451) or (c = $4E9) or (c = $4AF);
end;

// personStems: "Х.Нямбаатараас" -> "нямбаа" (unique, in order). caller frees
function Pipe_PersonStems(const text: string): TStringList;
var
  cps: TPipeCps;
  n, i, j, k, m, q: Integer;
  ok: Boolean;
  stem: string;
begin
  Result := TStringList.Create;
  cps := Pipe_Cps(text);
  n := Length(cps);
  i := 0;
  while i < n do
  begin
    ok := False;
    if Pipe_IsUpperMn(cps[i]) and ((i = 0) or not Pipe_IsLetterMn(cps[i - 1])) then
    begin
      j := -1;
      if (i + 2 < n) and Pipe_IsUpperMn(cps[i + 1]) and (cps[i + 2] = Ord('.')) then j := i + 3
      else if (i + 1 < n) and (cps[i + 1] = Ord('.')) then j := i + 2;
      if j > 0 then
      begin
        k := j;
        if (k < n) and IsJsSpaceCp(cps[k]) then Inc(k);
        if (k < n) and Pipe_IsUpperMn(cps[k]) then
        begin
          m := k + 1;
          while (m < n) and Pipe_IsLetterMn(cps[m]) do Inc(m);
          if m - k - 1 >= 3 then
          begin
            stem := '';
            for q := k to Min(m, k + PIPE_STEM_CHARS) - 1 do stem := stem + UEnc(ULowerCp(cps[q]));
            if Result.IndexOf(stem) < 0 then Result.Add(stem);
            i := m;
            ok := True;
          end;
        end;
      end;
    end;
    if not ok then Inc(i);
  end;
end;

// authorStem: surname stem of a post author, '' when too short
function Pipe_AuthorStem(const name: string): string;
var
  cps: TPipeCps;
  i: Integer;
  cur, last: string;
begin
  cps := Pipe_Cps(name);
  cur := ''; last := '';
  for i := 0 to High(cps) do
    if IsJsSpaceCp(cps[i]) or (cps[i] = Ord('.')) then
    begin
      if cur <> '' then last := cur;
      cur := '';
    end
    else cur := cur + UEnc(cps[i]);
  if cur <> '' then last := cur;
  if U16Len(last) >= PIPE_MIN_STEM_CHARS then Result := U16Copy(ULower(last), PIPE_STEM_CHARS) else Result := '';
end;

// postBatches: batch index of every post author
function Pipe_PostBatches(authors: TStringList; const batchTexts: TPipeStrArr): TPipeInts;
var
  lower: TPipeStrArr;
  i, b, t, best, nextB: Integer;
  hits: TPipeInts;
  stem: string;
begin
  SetLength(Result, authors.Count);
  SetLength(lower, Length(batchTexts));
  for b := 0 to High(batchTexts) do
  begin
    lower[b] := TStringList.Create;
    for t := 0 to batchTexts[b].Count - 1 do lower[b].Add(ULower(batchTexts[b][t]));
  end;
  nextB := 0;
  SetLength(hits, Length(batchTexts));
  for i := 0 to authors.Count - 1 do
  begin
    stem := Pipe_AuthorStem(authors[i]);
    for b := 0 to High(lower) do
    begin
      hits[b] := 0;
      if stem <> '' then
        for t := 0 to lower[b].Count - 1 do
          if Pos(stem, lower[b][t]) > 0 then Inc(hits[b]);
    end;
    best := 0;
    for b := 1 to High(hits) do
      if hits[b] > hits[best] then best := b;
    if (Length(hits) > 0) and (hits[best] > 0) then Result[i] := best
    else
    begin
      Result[i] := nextB mod Max(1, Length(batchTexts));
      Inc(nextB);
    end;
  end;
  for b := 0 to High(lower) do lower[b].Free;
end;

// groupByPerson: order (indices) so articles naming the same person sit together
function Pipe_GroupByPerson(titles, excerpts: TStringList): TPipeInts;
var
  names: array of TStringList;
  count: TStringList;
  keys: array of string;
  sizes: TPipeInts;
  i, j, k, tmp: Integer;
  best: string;

  function CountOf(const s: string): Integer;
  var
    x: Integer;
  begin
    x := count.IndexOfName(s);
    if (s = '') or (x < 0) then Result := 0 else Result := StrToInt(count.ValueFromIndex[x]);
  end;

  function Before(a, b: Integer): Boolean; // a sorts before b
  begin
    if (keys[a] = '') <> (keys[b] = '') then exit(keys[a] <> '');
    if sizes[a] <> sizes[b] then exit(sizes[a] > sizes[b]);
    if keys[a] <> keys[b] then exit(keys[a] < keys[b]);
    Result := a < b;
  end;

begin
  SetLength(names, titles.Count);
  count := TStringList.Create;
  for i := 0 to titles.Count - 1 do
  begin
    names[i] := Pipe_PersonStems(titles[i]);
    if names[i].Count = 0 then
    begin
      names[i].Free;
      names[i] := Pipe_PersonStems(excerpts[i]);
    end;
    for j := 0 to names[i].Count - 1 do
    begin
      k := count.IndexOfName(names[i][j]);
      if k < 0 then count.Add(names[i][j] + '=1')
      else count.ValueFromIndex[k] := IntToStr(StrToInt(count.ValueFromIndex[k]) + 1);
    end;
  end;
  SetLength(keys, titles.Count);
  SetLength(sizes, titles.Count);
  for i := 0 to titles.Count - 1 do
  begin
    best := '';
    for j := 0 to names[i].Count - 1 do
      if CountOf(names[i][j]) > CountOf(best) then best := names[i][j];
    if CountOf(best) >= PIPE_MIN_GROUP then keys[i] := best else keys[i] := '';
    sizes[i] := CountOf(keys[i]);
  end;
  SetLength(Result, titles.Count);
  for i := 0 to titles.Count - 1 do Result[i] := i;
  for i := 1 to High(Result) do
  begin
    tmp := Result[i];
    j := i - 1;
    while (j >= 0) and Before(tmp, Result[j]) do
    begin
      Result[j + 1] := Result[j];
      Dec(j);
    end;
    Result[j + 1] := tmp;
  end;
  for i := 0 to High(names) do names[i].Free;
  count.Free;
end;

// spreadArticles: up to max ids, every site's newest first (heavier sites first), then every site's second... caller frees
function Pipe_SpreadArticles(ids, slugs: TStringList; const times: array of Int64; weights: TJSONObject; max: Integer): TStringList;
var
  n, i, j, tmp, k: Integer;
  ord1, ord2, rank: TPipeInts;
  seen: TStringList;

  function W(const slug: string): Double;
  begin
    Result := 1;
    if (weights <> nil) and (weights.Find(slug) <> nil) then Result := weights.Find(slug).AsFloat;
  end;

  function Before2(a, b: Integer): Boolean;
  begin
    if rank[a] <> rank[b] then exit(rank[a] < rank[b]);
    if W(slugs[a]) <> W(slugs[b]) then exit(W(slugs[a]) > W(slugs[b]));
    Result := times[a] > times[b];
  end;

begin
  n := ids.Count;
  SetLength(ord1, n); SetLength(rank, n); SetLength(ord2, n);
  for i := 0 to n - 1 do ord1[i] := i;
  for i := 1 to n - 1 do
  begin
    tmp := ord1[i];
    j := i - 1;
    while (j >= 0) and (times[ord1[j]] < times[tmp]) do begin ord1[j + 1] := ord1[j]; Dec(j); end;
    ord1[j + 1] := tmp;
  end;
  seen := TStringList.Create;
  for i := 0 to n - 1 do
  begin
    k := seen.IndexOfName(slugs[ord1[i]]);
    if k < 0 then begin rank[ord1[i]] := 0; seen.Add(slugs[ord1[i]] + '=1'); end
    else
    begin
      rank[ord1[i]] := StrToInt(seen.ValueFromIndex[k]);
      seen.ValueFromIndex[k] := IntToStr(rank[ord1[i]] + 1);
    end;
  end;
  seen.Free;
  for i := 0 to n - 1 do ord2[i] := i;
  for i := 1 to n - 1 do
  begin
    tmp := ord2[i];
    j := i - 1;
    while (j >= 0) and Before2(tmp, ord2[j]) do begin ord2[j + 1] := ord2[j]; Dec(j); end;
    ord2[j + 1] := tmp;
  end;
  Result := TStringList.Create;
  for i := 0 to Min(n, max) - 1 do Result.Add(ids[ord2[i]]);
end;

// /\/(videos|reel|watch|live)\b/
function Pipe_IsVideo(const url: string): Boolean;
const
  W: array[0..3] of string = ('/videos', '/reel', '/watch', '/live');
var
  i, p, e: Integer;
  s: string;
begin
  Result := False;
  for i := 0 to High(W) do
  begin
    s := url;
    p := Pos(W[i], s);
    while p > 0 do
    begin
      e := p + Length(W[i]);
      if (e > Length(s)) or not (s[e] in ['A'..'Z', 'a'..'z', '0'..'9', '_']) then exit(True);
      s := Copy(s, p + 1, MaxInt);
      p := Pos(W[i], s);
    end;
  end;
end;

// ---------------------------------------------------------------- prompts

function Pipe_ClusterPrompt(const region, itemsJson, existingJson: string; hasExisting: Boolean): string;
begin
  Result := Pipe_RegionNote(region) +
    'Эдгээр хугацаанд цуглуулсан нийтлэлээс олон нийтийн анхаарлыг татсан 15 хүртэл ТОДОРХОЙ үйл явдал/асуудлыг ялга: ' +
    '2 ба түүнээс олон хэвлэл бичсэн үйл явдлыг заавал оруул. ' +
    'улс төр, эдийн засаг, нийгэм, гадаад мэдээ, спорт (Азийн наадам, дэлхийн аварга шалгаруулах тэмцээн г.м), соёл, технологи. ' +
    'Сурталчилгаа, PR мэдээг сэдэв болгохгүй. Өргөн ерөнхий ангилал биш судлах асуулт сонго. ' +
    'Нэг үйл явдалтай холбоотой эхүүдийг нэгтгэ. "p"-ээр эхэлсэн ID бол сошиал пост (сэтгүүлч, пейж, нөлөөлөгч): ' +
    'мэдээтэй холбогдвол тухайн сэдэвт нэм, олон пост нэг асуудлыг хөндвөл мэдээгүй ч сэдэв болгож болно. ' +
    'Мэдээнд гарч буй хүн өөрөө пост, видео, шууд дамжуулалт (лайв) хийсэн бол текст нь богино ч гэсэн тэр хүний тухай сэдэвт нэм. ' +
    'itemIds-д зөвхөн өгсөн ID ашигла.' + #10;
  if hasExisting then
    Result := Result + 'existing нь өмнөх багцын сэдвүүд ("t"-ээр эхэлсэн ID). Шинэ эх нь аль нэгийнх нь үргэлжлэл бол ' +
      'тэр бүлгийн itemIds-д тухайн t-ID-г нэг удаа нэм; шинэ сэдэв бол t-ID бүү нэм.' + #10 +
      '{"existing":' + existingJson + ',"items":' + itemsJson + '}'
  else
    Result := Result + itemsJson;
end;

function Pipe_SelectPrompt(const region, candidatesJson: string; count: Integer): string;
begin
  Result := Pipe_RegionNote(region) +
    'Доорх нэр дэвшсэн сэдвүүдээс хамгийн чухал, хоорондоо ялгаатай ' + IntToStr(count) + ' сэдэв сонго. ' +
    'hot (0–1) нь эх сурвалжийн нэр хүнд, сошиал идэвх, хурдаар тооцсон халуун сэдвийн оноо: чухлыг тодорхойлох гол үзүүлэлт болгож, ' +
    'хэвлэлийн тоо (publishers), олон нийтэд хамаарлаар нэмж үнэл. Олон ангиллыг хамруул: улс төр давамгайлж болох ч ' +
    'том спорт, нийгэм, гадаад мэдээ байвал орхихгүй. Ач холбогдлоор нь эрэмбэл. ' +
    'Нэг үйл явдал, нэг хүний хэрэг, нэг асуудлын олон өдрийн явц өөр өөр нэр дэвшигчид хуваагдсан бол candidateIds-д бүгдийг нь нэгтгэ ' +
    '(жишээ нь нэг албан тушаалтны мэдэгдэл, түүнийг огцруулах, түүнтэй холбоотой шийдвэр). ' +
    'Огт холбоогүй сэдвүүдийг бөөндөхгүй. reason нь нэг богино өгүүлбэр.' + #10 +
    candidatesJson;
end;

function Pipe_WrapPrompt(const region, title, reason, ws, we: string; lenses: TJSONArray; const sourcesJson: string): string;
var
  i: Integer;
  lines: string;
begin
  lines := '';
  for i := 0 to lenses.Count - 1 do
  begin
    if i > 0 then lines := lines + #10;
    lines := lines + '- ' + JStr(lenses.Items[i], 'key') + ' (' + JStr(lenses.Items[i], 'label') + '): ' + JStr(lenses.Items[i], 'instruction');
  end;
  Result := Pipe_RegionNote(region) +
    'Хамрах хугацаа: ' + ws + ' – ' + we + '. Сэдэв: ' + title + '. Сонгосон шалтгаан: ' + reason + '.' + #10 +
    'Доорх эх сурвалжуудад тулгуурлан сэдвийг wraps талбар бүрээр задлан шинжил:' + #10 +
    lines +
    #10 + 'type: "social" эх сурвалж бол сошиал пост: түүнийг олон нийтийн хандлага, байр суурь гэж ашигла, батлагдсан баримт гэж бүү үз.' +
    #10 + 'Баримт бүрт sourceIds талбарт эх сурвалжийн ID (S1, S2 …) заа. Зөвхөн өгсөн нотолгоо ашигла. ' +
    'Ирээдүйн таамаг ба шалтгааныг баталгаа мэт бүү бич.' + #10 +
    PIPE_WRAP_RULES +
    Top_VisualsRules +
    #10 +
    sourcesJson;
end;

function Pipe_TopicsSchema(maxItems: Integer; const reasonDesc, titleDesc, idsKey: string): string;
var
  t, r: string;
begin
  t := '{"type":"string"}';
  if titleDesc <> '' then t := '{"type":"string","description":' + JEsc(titleDesc) + '}';
  r := '{"type":"string","description":' + JEsc(reasonDesc) + '}';
  Result := '{"type":"object","properties":{"topics":{"type":"array","maxItems":' + IntToStr(maxItems) +
    ',"items":{"type":"object","properties":{"title":' + t + ',"reason":' + r + ',"category":' + Top_CategorySchema +
    ',"' + idsKey + '":{"type":"array","items":{"type":"string"}}},"required":["title","reason","category","' + idsKey + '"]}}},"required":["topics"]}';
end;

// ---------------------------------------------------------------- hot score (social/score.ts via social.inc)

// metrics of each candidate (signals + hot), normalized across them. caller frees
function Pipe_ScoreTopics(run: TJSONObject; topics: TJSONArray): TJSONArray;
var
  arts, posts, ta, tp, ids: TJSONArray;
  aidx, pidx: TStringList;
  weights, settings, a, p, sa: TJSONObject;
  srcs: TJSONArray;
  i, j: Integer;
  now: TDateTime;
begin
  settings := JObj(JObj(run, 'config'), 'settings');
  srcs := JArr(JObj(run, 'config'), 'sources');
  weights := TJSONObject.Create;
  if srcs <> nil then
    for i := 0 to srcs.Count - 1 do JSet(weights, JStr(srcs.Items[i], 'slug'), TJsFloat.Create(JNum(srcs.Items[i], 'weight', 1)));
  arts := DbLoad('articles');
  aidx := Pipe_Index(arts);
  posts := DbLoad('socialposts');
  pidx := Pipe_Index(posts);
  now := NowUtc;
  Result := TJSONArray.Create;
  for i := 0 to topics.Count - 1 do
  begin
    ta := TJSONArray.Create;
    tp := TJSONArray.Create;
    ids := JArr(topics.Items[i], 'articleIds');
    if ids <> nil then
      for j := 0 to ids.Count - 1 do
      begin
        a := Pipe_Get(arts, aidx, ids.Items[j].AsString);
        if a = nil then continue;
        sa := TJSONObject.Create;
        sa.Add('_id', JStr(a, '_id'));
        sa.Add('sourceSlug', JStr(a, 'sourceSlug'));
        sa.Add('publishedDay', JStr(a, 'publishedDay'));
        if JStr(a, 'createdAt') <> '' then sa.Add('createdAt', JStr(a, 'createdAt'));
        sa.Add('title', JStr(a, 'title'));
        sa.Add('excerpt', PplUCut(JStr(a, 'text'), PIPE_EXCERPT_CHARS));
        ta.Add(sa);
      end;
    ids := JArr(topics.Items[i], 'postIds');
    if ids <> nil then
      for j := 0 to ids.Count - 1 do
      begin
        p := Pipe_Get(posts, pidx, ids.Items[j].AsString);
        if p <> nil then tp.Add(p.Clone);
      end;
    Result.Add(Soc_TopicSignals(ta, tp, weights, JNum(settings, 'windowDays', 14), JNum(JObj(settings, 'social'), 'halfLifeHours', 24), now));
    ta.Free;
    tp.Free;
  end;
  Soc_RankHotW(Result, JObj(settings, 'hotWeights'));
  weights.Free;
  aidx.Free; arts.Free; pidx.Free; posts.Free;
end;

// two news articles (foreign: two outlets), or broad social activity
function Pipe_HasEnoughSources(const kind: string; topic: TJSONObject; arts: TJSONArray; aidx: TStringList; posts: TJSONArray; pidx: TStringList): Boolean;
var
  seen: TStringList;
  ids: TJSONArray;
  i: Integer;
  d: TJSONObject;
begin
  seen := TStringList.Create;
  try
    if kind = 'world' then
    begin
      ids := JArr(topic, 'articleIds');
      for i := 0 to ids.Count - 1 do
      begin
        d := Pipe_Get(arts, aidx, ids.Items[i].AsString);
        if (d <> nil) and (seen.IndexOf(JStr(d, 'sourceSlug')) < 0) then seen.Add(JStr(d, 'sourceSlug'));
      end;
      exit(seen.Count >= PIPE_WORLD_MIN_PUBLISHERS);
    end;
    if JArr(topic, 'articleIds').Count >= 2 then exit(True);
    ids := JArr(topic, 'postIds');
    if (ids = nil) or (ids.Count < PIPE_SOCIAL_ONLY_MIN_POSTS) then exit(False);
    for i := 0 to ids.Count - 1 do
    begin
      d := Pipe_Get(posts, pidx, ids.Items[i].AsString);
      if (d <> nil) and (seen.IndexOf(JStr(d, 'accountId')) < 0) then seen.Add(JStr(d, 'accountId'));
    end;
    Result := seen.Count >= PIPE_SOCIAL_ONLY_MIN_ACCOUNTS;
  finally
    seen.Free;
  end;
end;

// ---------------------------------------------------------------- stages

procedure Pipe_Collect(run: TJSONObject);
var
  id, ws, we, region, kind, s: string;
  sources, reports, arts, all: TJSONArray;
  src, res, rep, a, doc: TJSONObject;
  urls: TStringList;
  i, j, n, limit: Integer;
  patch: TJSONObject;
begin
  id := JStr(run, '_id');
  kind := JStr(run, 'kind');
  region := Pipe_RegionOfKind(kind);
  ws := JStr(JObj(run, 'window'), 'start');
  we := JStr(JObj(run, 'window'), 'end');
  sources := JArr(JObj(run, 'config'), 'sources');
  if sources = nil then sources := TJSONArray(JParse('[]')); // leaks a tiny array, never happens
  limit := JInt(JPath(run, 'config.settings'), 'articlesPerSource', 300);
  Pipe_Event(id, 'Collecting ' + IntToStr(sources.Count) + ' sources');
  reports := TJSONArray.Create;
  PipeArticlesLock.Enter;
  try
    for i := 0 to sources.Count - 1 do
    begin
      src := TJSONObject(sources.Items[i]);
      res := Col_CollectSource(src, ws, we, limit, nil);
      try
        arts := JArr(res, 'articles');
        rep := JObj(res, 'report');
        if (arts <> nil) and (arts.Count > 0) then
        begin
          // insertMany({ordered:false}) ignoring duplicate urls
          DbLock.Enter;
          try
            all := DbLoad('articles');
            urls := TStringList.Create;
            urls.Sorted := True; urls.CaseSensitive := True; urls.Duplicates := dupIgnore;
            for j := 0 to all.Count - 1 do urls.Add(JStr(all.Items[j], 'url'));
            n := 0;
            for j := 0 to arts.Count - 1 do
            begin
              a := TJSONObject(arts.Items[j]);
              if urls.IndexOf(JStr(a, 'url')) >= 0 then continue;
              urls.Add(JStr(a, 'url'));
              doc := TJSONObject.Create;
              doc.Add('_id', NewId);
              doc.Add('url', JStr(a, 'url'));
              doc.Add('sourceSlug', JStr(src, 'slug'));
              doc.Add('sourceName', JStr(src, 'name'));
              doc.Add('region', region);
              doc.Add('title', JStr(a, 'title'));
              doc.Add('text', JStr(a, 'text'));
              doc.Add('image', JStr(a, 'image'));
              doc.Add('publishedDay', JStr(a, 'publishedDay'));
              s := NowIso;
              doc.Add('fetchedAt', s);
              doc.Add('createdAt', s);
              doc.Add('updatedAt', s);
              all.Add(doc);
              Inc(n);
            end;
            if n > 0 then DbSave('articles', all);
            urls.Free;
            all.Free;
          finally
            DbLock.Leave;
          end;
        end;
        Pipe_Event(id, JStr(src, 'name') + ': ' + Pipe_NumStr(JNum(rep, 'fetched')) + ' new, ' + Pipe_NumStr(JNum(rep, 'skippedKnown')) +
          ' known, ' + JStr(rep, 'status'));
        if rep <> nil then reports.Add(rep.Clone);
      finally
        res.Free;
      end;
    end;
  finally
    PipeArticlesLock.Leave;
  end;
  patch := TJSONObject.Create;
  patch.Add('sourceReports', reports);
  patch.Add('articleCount', Pipe_CountColl('articles', ws, we, region, ''));
  // foreign articles skip people extraction and social collection
  if kind = 'world' then Pipe_SetStage(id, 'cluster', patch) else Pipe_SetStage(id, 'extract', patch);
end;

// optional stage: a failure is logged, the run goes on
procedure Pipe_Extract(run: TJSONObject);
var
  id, model: string;
  es, patch: TJSONObject;
  in0, out0, c0: Int64;
begin
  id := JStr(run, '_id');
  model := JStr(JPath(run, 'config.settings.models'), 'extract', 'gemini-3.5-flash-lite');
  es := nil;
  try
    Pipe_CheckBudget(run);
  except
    on E: Exception do
    begin
      es := TJSONObject.Create;
      es.Add('error', E.Message);
    end;
  end;
  if es = nil then
  begin
    // usage only via the shared Gemini* counters (other threads may add to them meanwhile)
    in0 := GeminiIn; out0 := GeminiOut; c0 := GeminiCalls;
    PipeArticlesLock.Enter;
    try
      es := Ent_ExtractStage(JStr(JObj(run, 'window'), 'start'), JStr(JObj(run, 'window'), 'end'), Pipe_RegionOfKind(JStr(run, 'kind')), model);
    finally
      PipeArticlesLock.Leave;
    end;
    if GeminiCalls > c0 then
      Pipe_AddUsage(id, GeminiIn - in0, GeminiOut - out0, GeminiCalls - c0, LlmCostUsd(model, GeminiIn - in0, GeminiOut - out0));
  end;
  if es.Find('error') <> nil then Pipe_Event(id, 'People extraction failed: ' + JStr(es, 'error'))
  else Pipe_Event(id, 'People: ' + Pipe_NumStr(JNum(es, 'mentions')) + ' mentions in ' + Pipe_NumStr(JNum(es, 'matched')) + ' of ' +
    Pipe_NumStr(JNum(es, 'scanned')) + ' new articles');
  patch := TJSONObject.Create;
  patch.Add('entityScan', es);
  if JStr(run, 'kind') = 'news' then Pipe_Finish(id, patch) else Pipe_SetStage(id, 'social', patch);
end;

// optional stage: failures are in the reports, the run continues with news only
procedure Pipe_Social(run: TJSONObject);
var
  id: string;
  accounts, reports: TJSONArray;
  social, usage, patch: TJSONObject;
begin
  id := JStr(run, '_id');
  accounts := JArr(JObj(run, 'config'), 'accounts');
  social := JObj(JPath(run, 'config.settings'), 'social');
  if (social = nil) or not JBool(social, 'enabled') or (accounts = nil) or (accounts.Count = 0) then
  begin
    Pipe_Event(id, 'Social collection is off or has no accounts');
    reports := TJSONArray.Create;
  end
  else
  begin
    Pipe_Event(id, 'Collecting social posts of ' + IntToStr(accounts.Count) + ' accounts');
    PipeLogRunId := id;
    reports := Soc_Collect(accounts, JStr(JObj(run, 'window'), 'start'), JStr(JObj(run, 'window'), 'end'), social, @Pipe_SocLog);
  end;
  usage := Soc_UsageOf(reports);
  if reports.Count > 0 then
    Pipe_Event(id, 'Social usage: ' + Pipe_NumStr(JNum(usage, 'requests')) + ' requests, ' + Pipe_NumStr(JNum(usage, 'credits')) +
      ' credits, $' + Pipe_Fixed4(JNum(usage, 'costUsd')));
  patch := TJSONObject.Create;
  patch.Add('socialReports', reports);
  patch.Add('socialUsage', usage);
  patch.Add('postCount', Pipe_CountColl('socialposts', JStr(JObj(run, 'window'), 'start'), JStr(JObj(run, 'window'), 'end'), 'mn', ''));
  if JStr(run, 'kind') = 'social' then Pipe_Finish(id, patch) else Pipe_SetStage(id, 'cluster', patch);
end;

// groups items new since the previous edition into candidate topics (map step)
procedure Pipe_Cluster(run: TJSONObject);
var
  id, kind, region, ws, we, since, note, s, cat: string;
  settings, prevEd, prevRun, o, c, res, ex: TJSONObject;
  arts, posts, existing, candidates, items, exList, rTopics, itemIds, sel, aids, pids, newA, newP: TJSONArray;
  aidx, pidx: TStringList;
  total, newItems, i, j, k, b, tmp, batchCount, postLimit, off, nb, ti: Integer;
  fresh, ordered, pfresh, postBatch, bArts, bPosts: TPipeInts;
  titles, excerpts, authors: TStringList;
  batchTexts: TPipeStrArr;
  a, p: TJSONObject;
  inWin: TStringList;
begin
  id := JStr(run, '_id');
  kind := JStr(run, 'kind');
  region := Pipe_RegionOfKind(kind);
  ws := JStr(JObj(run, 'window'), 'start');
  we := JStr(JObj(run, 'window'), 'end');
  settings := JObj(JObj(run, 'config'), 'settings');
  arts := DbLoad('articles');
  posts := DbLoad('socialposts');
  prevEd := nil; prevRun := nil;
  existing := TJSONArray.Create;
  candidates := TJSONArray.Create;
  aidx := nil; pidx := nil;
  try
    total := Pipe_Count(arts, ws, we, region, '');
    if total < PIPE_MIN_ARTICLES then
      raise Exception.Create('Only ' + IntToStr(total) + ' articles in the window; need ' + IntToStr(PIPE_MIN_ARTICLES));
    Pipe_PreviousEdition(run, prevEd, prevRun);
    // new = stored after the previous edition's run started; a world run: after it finished
    since := '';
    if prevRun <> nil then
    begin
      if kind = 'world' then since := JStr(prevRun, 'completedAt', JStr(prevRun, 'startedAt'))
      else since := JStr(prevRun, 'startedAt');
    end;
    newItems := Pipe_Count(arts, ws, we, region, since) + Pipe_Count(posts, ws, we, region, since);
    // a manual run always publishes; a scheduled one waits until enough is new
    if (prevEd <> nil) and (JStr(run, 'trigger') = 'schedule') and (newItems < JInt(settings, 'minNewItems')) then
    begin
      note := 'Skipped: ' + IntToStr(newItems) + ' new items since the last edition, need ' + IntToStr(JInt(settings, 'minNewItems'));
      Pipe_Event(id, note);
      o := TJSONObject.Create;
      o.Add('status', 'skipped');
      o.Add('note', note);
      o.Add('completedAt', NowIso);
      Pipe_RunSet(id, o);
      exit;
    end;
    aidx := Pipe_Index(arts);
    pidx := Pipe_Index(posts);
    // continuingTopics: previous topics limited to their items still in the window
    if prevRun <> nil then
    begin
      sel := JArr(prevRun, 'selected');
      if sel <> nil then
        for i := 0 to sel.Count - 1 do
        begin
          newA := TJSONArray.Create;
          newP := TJSONArray.Create;
          aids := JArr(sel.Items[i], 'articleIds');
          if aids <> nil then
            for j := 0 to aids.Count - 1 do
            begin
              a := Pipe_Get(arts, aidx, aids.Items[j].AsString);
              if (a <> nil) and Pipe_InWin(a, ws, we, region) then newA.Add(aids.Items[j].AsString);
            end;
          pids := JArr(sel.Items[i], 'postIds');
          if pids <> nil then
            for j := 0 to pids.Count - 1 do
            begin
              p := Pipe_Get(posts, pidx, pids.Items[j].AsString);
              if (p <> nil) and Pipe_InWin(p, ws, we, region) then newP.Add(pids.Items[j].AsString);
            end;
          if newA.Count + newP.Count = 0 then begin newA.Free; newP.Free; continue; end;
          c := TJSONObject.Create;
          c.Add('title', JStr(sel.Items[i], 'title'));
          c.Add('reason', JStr(sel.Items[i], 'reason'));
          if JStr(sel.Items[i], 'category') <> '' then c.Add('category', JStr(sel.Items[i], 'category'));
          c.Add('articleIds', newA);
          c.Add('postIds', newP);
          existing.Add(c);
        end;
      Pipe_Event(id, IntToStr(newItems) + ' new items since the last edition, ' + IntToStr(existing.Count) + ' topics continue');
    end;
    // fresh articles, publishedDay desc, _id asc
    SetLength(fresh, 0);
    for i := 0 to arts.Count - 1 do
      if Pipe_InWin(arts.Items[i], ws, we, region) and ((since = '') or (JStr(arts.Items[i], 'createdAt') >= since)) then
      begin
        SetLength(fresh, Length(fresh) + 1);
        fresh[High(fresh)] := i;
      end;
    for i := 1 to High(fresh) do
    begin
      tmp := fresh[i];
      j := i - 1;
      while (j >= 0) and ((JStr(arts.Items[fresh[j]], 'publishedDay') < JStr(arts.Items[tmp], 'publishedDay')) or
        ((JStr(arts.Items[fresh[j]], 'publishedDay') = JStr(arts.Items[tmp], 'publishedDay')) and
         (JStr(arts.Items[fresh[j]], '_id') > JStr(arts.Items[tmp], '_id')))) do
      begin
        fresh[j + 1] := fresh[j];
        Dec(j);
      end;
      fresh[j + 1] := tmp;
    end;
    titles := TStringList.Create;
    excerpts := TStringList.Create;
    for i := 0 to High(fresh) do
    begin
      titles.Add(JStr(arts.Items[fresh[i]], 'title'));
      excerpts.Add(U16Copy(JStr(arts.Items[fresh[i]], 'text'), 400));
    end;
    ordered := Pipe_GroupByPerson(titles, excerpts);
    for i := 0 to High(ordered) do ordered[i] := fresh[ordered[i]];
    titles.Free;
    excerpts.Free;
    // fresh posts, score desc, _id asc, limited
    if kind = 'world' then postLimit := 0 else postLimit := JInt(JObj(settings, 'social'), 'postsPerRun', 0);
    SetLength(pfresh, 0);
    if postLimit > 0 then
    begin
      for i := 0 to posts.Count - 1 do
        if Pipe_InWin(posts.Items[i], ws, we, region) and ((since = '') or (JStr(posts.Items[i], 'createdAt') >= since)) then
        begin
          SetLength(pfresh, Length(pfresh) + 1);
          pfresh[High(pfresh)] := i;
        end;
      for i := 1 to High(pfresh) do
      begin
        tmp := pfresh[i];
        j := i - 1;
        while (j >= 0) and ((JNum(posts.Items[pfresh[j]], 'score') < JNum(posts.Items[tmp], 'score')) or
          ((JNum(posts.Items[pfresh[j]], 'score') = JNum(posts.Items[tmp], 'score')) and
           (JStr(posts.Items[pfresh[j]], '_id') > JStr(posts.Items[tmp], '_id')))) do
        begin
          pfresh[j + 1] := pfresh[j];
          Dec(j);
        end;
        pfresh[j + 1] := tmp;
      end;
      if Length(pfresh) > postLimit then SetLength(pfresh, postLimit);
    end;
    if Length(ordered) + Length(pfresh) > 0 then batchCount := Max(1, (Length(ordered) + PIPE_CLUSTER_BATCH - 1) div PIPE_CLUSTER_BATCH)
    else batchCount := 0;
    exList := TJSONArray.Create;
    for i := 0 to existing.Count - 1 do
    begin
      o := TJSONObject.Create;
      o.Add('id', 't' + IntToStr(i));
      o.Add('title', JStr(existing.Items[i], 'title'));
      if JStr(existing.Items[i], 'category') <> '' then o.Add('category', JStr(existing.Items[i], 'category'));
      exList.Add(o);
    end;
    // posts go to the batch that names their author
    SetLength(postBatch, 0);
    if batchCount > 0 then
    begin
      SetLength(batchTexts, batchCount);
      for b := 0 to batchCount - 1 do
      begin
        batchTexts[b] := TStringList.Create;
        for i := b * PIPE_CLUSTER_BATCH to Min((b + 1) * PIPE_CLUSTER_BATCH, Length(ordered)) - 1 do
          batchTexts[b].Add(JStr(arts.Items[ordered[i]], 'title') + ' ' + U16Copy(JStr(arts.Items[ordered[i]], 'text'), 400));
      end;
      authors := TStringList.Create;
      for i := 0 to High(pfresh) do authors.Add(JStr(JObj(posts.Items[pfresh[i]], 'author'), 'name'));
      postBatch := Pipe_PostBatches(authors, batchTexts);
      authors.Free;
      for b := 0 to batchCount - 1 do batchTexts[b].Free;
    end;
    for b := 0 to batchCount - 1 do
    begin
      off := b * PIPE_CLUSTER_BATCH;
      SetLength(bArts, 0);
      for i := off to Min(off + PIPE_CLUSTER_BATCH, Length(ordered)) - 1 do
      begin
        SetLength(bArts, Length(bArts) + 1);
        bArts[High(bArts)] := ordered[i];
      end;
      SetLength(bPosts, 0);
      for i := 0 to High(pfresh) do
        if postBatch[i] = b then
        begin
          SetLength(bPosts, Length(bPosts) + 1);
          bPosts[High(bPosts)] := pfresh[i];
        end;
      s := 'Clustering articles ' + IntToStr(off + 1) + '–' + IntToStr(off + Length(bArts)) + ' of ' + IntToStr(Length(ordered));
      if Length(bPosts) > 0 then s := s + ' with ' + IntToStr(Length(bPosts)) + ' social posts';
      Pipe_Event(id, s);
      items := TJSONArray.Create;
      for i := 0 to High(bArts) do
      begin
        a := TJSONObject(arts.Items[bArts[i]]);
        o := TJSONObject.Create;
        o.Add('id', 'a' + IntToStr(i));
        o.Add('title', JStr(a, 'title'));
        o.Add('source', JStr(a, 'sourceName'));
        o.Add('date', JStr(a, 'publishedDay'));
        o.Add('excerpt', U16Copy(JStr(a, 'text'), 400));
        items.Add(o);
      end;
      for i := 0 to High(bPosts) do
      begin
        p := TJSONObject(posts.Items[bPosts[i]]);
        o := TJSONObject.Create;
        o.Add('id', 'p' + IntToStr(i));
        o.Add('title', '');
        s := JStr(p, 'platform');
        if Pipe_IsVideo(JStr(p, 'url')) then s := s + ' видео';
        o.Add('source', s + ': ' + JStr(JObj(p, 'author'), 'name'));
        o.Add('date', JStr(p, 'publishedDay'));
        o.Add('excerpt', U16Copy(JStr(p, 'text'), 400));
        items.Add(o);
      end;
      res := nil;
      try
        o := TJSONObject(Pipe_Ask(run, JStr(JObj(settings, 'models'), 'cluster'),
          Pipe_ClusterPrompt(region, Chat_Js(items), Chat_Js(exList), existing.Count > 0),
          Pipe_TopicsSchema(PIPE_CLUSTER_TOPICS, 'ач холбогдол', 'тодорхой асуулт', 'itemIds'),
          Max(16000, (Length(bArts) + Length(bPosts)) * 60)));
        res := o;
      finally
        items.Free;
      end;
      try
        rTopics := nil;
        if res is TJSONObject then rTopics := JArr(res, 'topics');
        if rTopics <> nil then
          for i := 0 to rTopics.Count - 1 do
          begin
            itemIds := JArr(rTopics.Items[i], 'itemIds');
            newA := TJSONArray.Create;
            newP := TJSONArray.Create;
            ti := -1;
            if itemIds <> nil then
              for j := 0 to itemIds.Count - 1 do
              begin
                if itemIds.Items[j].JSONType <> jtString then continue;
                s := itemIds.Items[j].AsString;
                if s = '' then continue;
                if Copy(s, 2, MaxInt) = '' then k := 0 else k := StrToIntDef(Copy(s, 2, MaxInt), -1);
                if (s[1] = 'a') and (k >= 0) and (k <= High(bArts)) then Pipe_AddUniq(newA, JStr(arts.Items[bArts[k]], '_id'))
                else if (s[1] = 'p') and (k >= 0) and (k <= High(bPosts)) then Pipe_AddUniq(newP, JStr(posts.Items[bPosts[k]], '_id'));
                if (ti < 0) and (s[1] = 't') and (Length(s) > 1) and (RxMatch('^t[0-9]+$', s)) then ti := StrToIntDef(Copy(s, 2, MaxInt), -1);
              end;
            if newA.Count + newP.Count = 0 then begin newA.Free; newP.Free; continue; end;
            if (ti >= 0) and (ti < existing.Count) then
            begin
              ex := TJSONObject(existing.Items[ti]);
              for j := 0 to newA.Count - 1 do Pipe_AddUniq(JArr(ex, 'articleIds'), newA.Items[j].AsString);
              for j := 0 to newP.Count - 1 do Pipe_AddUniq(JArr(ex, 'postIds'), newP.Items[j].AsString);
              newA.Free; newP.Free;
            end
            else
            begin
              c := TJSONObject.Create;
              c.Add('title', JStr(rTopics.Items[i], 'title'));
              c.Add('reason', JStr(rTopics.Items[i], 'reason'));
              cat := JStr(rTopics.Items[i], 'category');
              c.Add('category', Top_ToCategory(cat));
              c.Add('articleIds', newA);
              c.Add('postIds', newP);
              candidates.Add(c);
            end;
          end;
      finally
        res.Free;
      end;
    end;
    exList.Free;
    // previous topics first
    for i := existing.Count - 1 downto 0 do candidates.Insert(0, existing.Items[i].Clone);
    if candidates.Count = 0 then raise Exception.Create('No candidate topics found');
    o := TJSONObject.Create;
    o.Add('candidates', candidates.Clone);
    Pipe_SetStage(id, 'select', o);
  finally
    arts.Free; posts.Free; existing.Free; candidates.Free;
    prevEd.Free; prevRun.Free; aidx.Free; pidx.Free;
  end;
end;

// reduce step: picks and merges candidates by id, scores and orders them by hot
procedure Pipe_Select(run: TJSONObject);
var
  id, kind, region: string;
  settings, o, res, topic, m: TJSONObject;
  candidates, hot, list, rTopics, cids, merged, metrics, arts, posts, sel, aids, srcs: TJSONArray;
  aidx, pidx, ids, slugs, spread: TStringList;
  weights: TJSONObject;
  count, i, j, k, tmp: Integer;
  times: array of Int64;
  ord: TPipeInts;
  a: TJSONObject;
  s: string;
begin
  id := JStr(run, '_id');
  kind := JStr(run, 'kind');
  region := Pipe_RegionOfKind(kind);
  settings := JObj(JObj(run, 'config'), 'settings');
  candidates := JArr(run, 'candidates');
  if candidates = nil then candidates := TJSONArray(JParse('[]'));
  count := JInt(settings, 'topicsPerRun', 45);
  Pipe_Event(id, 'Selecting ' + IntToStr(count) + ' topics from ' + IntToStr(candidates.Count) + ' candidates');
  hot := Pipe_ScoreTopics(run, candidates);
  list := TJSONArray.Create;
  for i := 0 to candidates.Count - 1 do
  begin
    o := TJSONObject.Create;
    o.Add('id', 'c' + IntToStr(i));
    o.Add('title', JStr(candidates.Items[i], 'title'));
    o.Add('reason', JStr(candidates.Items[i], 'reason'));
    if JStr(candidates.Items[i], 'category') <> '' then o.Add('category', JStr(candidates.Items[i], 'category'));
    o.Add('hot', Pipe_Num(Floor(JNum(hot.Items[i], 'hot') * 100 + 0.5) / 100));
    o.Add('articles', JArr(candidates.Items[i], 'articleIds').Count);
    o.Add('publishers', Pipe_Num(JNum(hot.Items[i], 'publisherCount')));
    if JArr(candidates.Items[i], 'postIds') <> nil then o.Add('posts', JArr(candidates.Items[i], 'postIds').Count) else o.Add('posts', 0);
    list.Add(o);
  end;
  hot.Free;
  try
    res := TJSONObject(Pipe_Ask(run, JStr(JObj(settings, 'models'), 'select'), Pipe_SelectPrompt(region, Chat_Js(list), count),
      Pipe_TopicsSchema(count, '150 тэмдэгтээс уртгүй', '', 'candidateIds'), Max(8000, count * 400)));
  finally
    list.Free;
  end;
  arts := DbLoad('articles');
  aidx := Pipe_Index(arts);
  posts := DbLoad('socialposts');
  pidx := Pipe_Index(posts);
  merged := TJSONArray.Create;
  try
    rTopics := nil;
    if res is TJSONObject then rTopics := JArr(res, 'topics');
    if rTopics <> nil then
      for i := 0 to Min(count, rTopics.Count) - 1 do
      begin
        topic := TJSONObject.Create;
        topic.Add('title', JStr(rTopics.Items[i], 'title'));
        topic.Add('reason', JStr(rTopics.Items[i], 'reason'));
        topic.Add('category', Top_ToCategory(JStr(rTopics.Items[i], 'category')));
        aids := TJSONArray.Create;
        sel := TJSONArray.Create;
        topic.Add('articleIds', aids);
        topic.Add('postIds', sel);
        cids := JArr(rTopics.Items[i], 'candidateIds');
        if cids <> nil then
          for j := 0 to cids.Count - 1 do
          begin
            if cids.Items[j].JSONType <> jtString then continue;
            s := cids.Items[j].AsString;
            if not RxMatch('^c[0-9]+$', s) then continue;
            k := StrToIntDef(Copy(s, 2, MaxInt), -1);
            if (k < 0) or (k >= candidates.Count) then continue;
            m := TJSONObject(candidates.Items[k]);
            if JArr(m, 'articleIds') <> nil then
              for tmp := 0 to JArr(m, 'articleIds').Count - 1 do Pipe_AddUniq(aids, JArr(m, 'articleIds').Items[tmp].AsString);
            if JArr(m, 'postIds') <> nil then
              for tmp := 0 to JArr(m, 'postIds').Count - 1 do Pipe_AddUniq(sel, JArr(m, 'postIds').Items[tmp].AsString);
          end;
        if Pipe_HasEnoughSources(kind, topic, arts, aidx, posts, pidx) then merged.Add(topic) else topic.Free;
      end;
    if merged.Count = 0 then raise Exception.Create('No topic with enough sources was selected');
    // hot score from all merged items; the wrap gets at most MAX_TOPIC_SOURCES articles spread over sites
    metrics := Pipe_ScoreTopics(run, merged);
    weights := TJSONObject.Create;
    srcs := JArr(JObj(run, 'config'), 'sources');
    if srcs <> nil then
      for i := 0 to srcs.Count - 1 do JSet(weights, JStr(srcs.Items[i], 'slug'), TJsFloat.Create(JNum(srcs.Items[i], 'weight', 1)));
    for i := 0 to merged.Count - 1 do
    begin
      topic := TJSONObject(merged.Items[i]);
      ids := TStringList.Create;
      slugs := TStringList.Create;
      SetLength(times, 0);
      aids := JArr(topic, 'articleIds');
      for j := 0 to aids.Count - 1 do
      begin
        a := Pipe_Get(arts, aidx, aids.Items[j].AsString);
        if a = nil then continue;
        ids.Add(JStr(a, '_id'));
        slugs.Add(JStr(a, 'sourceSlug'));
        SetLength(times, Length(times) + 1);
        times[High(times)] := Soc_ArticleTimeMs(JStr(a, 'publishedDay'), Soc_DocMs(a, 'createdAt'));
      end;
      spread := Pipe_SpreadArticles(ids, slugs, times, weights, PIPE_MAX_TOPIC_SOURCES);
      JSet(topic, 'articleIds', StrsToJArr(spread));
      JSet(topic, 'metrics', metrics.Items[i].Clone);
      spread.Free; ids.Free; slugs.Free;
    end;
    weights.Free;
    metrics.Free;
    // final order by hot score (stable)
    SetLength(ord, merged.Count);
    for i := 0 to High(ord) do ord[i] := i;
    for i := 1 to High(ord) do
    begin
      tmp := ord[i];
      j := i - 1;
      while (j >= 0) and (JNum(JObj(merged.Items[ord[j]], 'metrics'), 'hot') < JNum(JObj(merged.Items[tmp], 'metrics'), 'hot')) do
      begin
        ord[j + 1] := ord[j];
        Dec(j);
      end;
      ord[j + 1] := tmp;
    end;
    sel := TJSONArray.Create;
    for i := 0 to High(ord) do sel.Add(merged.Items[ord[i]].Clone);
    o := TJSONObject.Create;
    o.Add('selected', sel);
    Pipe_SetStage(id, 'wrap', o);
  finally
    res.Free;
    merged.Free;
    aidx.Free; arts.Free; pidx.Free; posts.Free;
  end;
end;

// every id of want in have
function Pipe_Covers(have, want: TJSONArray): Boolean;
var
  i: Integer;
begin
  Result := True;
  if want = nil then exit;
  for i := 0 to want.Count - 1 do
    if not Pipe_ArrHas(have, want.Items[i].AsString) then exit(False);
end;

procedure Pipe_Wrap(run: TJSONObject);
var
  id, kind, region, model, lensV, ws, we, s: string;
  settings, prevEd, prevRun, topic, same, doc, res, a, p, o, wraps: TJSONObject;
  selected, lenses, prevTopics, allTop, arts, posts, aids, pids, wArts, wPosts, srcList: TJSONArray;
  aidx: TStringList;
  pos, i, j, tmp, sourceCount: Integer;
  exists: Boolean;
  pord: TPipeInts;
begin
  id := JStr(run, '_id');
  kind := JStr(run, 'kind');
  region := Pipe_RegionOfKind(kind);
  ws := JStr(JObj(run, 'window'), 'start');
  we := JStr(JObj(run, 'window'), 'end');
  settings := JObj(JObj(run, 'config'), 'settings');
  model := JStr(JObj(settings, 'models'), 'wrap');
  lenses := JArr(JObj(run, 'config'), 'lenses');
  if lenses = nil then lenses := TJSONArray(JParse('[]'));
  lensV := Chat_Js(lenses);
  selected := JArr(run, 'selected');
  if selected = nil then selected := TJSONArray(JParse('[]'));
  Pipe_PreviousEdition(run, prevEd, prevRun);
  prevTopics := TJSONArray.Create;
  if prevEd <> nil then
  begin
    allTop := DbLoad('topics');
    for i := 0 to allTop.Count - 1 do
      if Pipe_ArrHas(JArr(prevEd, 'topicIds'), JStr(allTop.Items[i], '_id')) then prevTopics.Add(allTop.Items[i].Clone);
    allTop.Free;
  end;
  prevEd.Free; prevRun.Free;
  try
    for pos := 0 to selected.Count - 1 do
    begin
      topic := TJSONObject(selected.Items[pos]);
      // resumed run: this position is written already
      allTop := DbLoad('topics');
      exists := False;
      for i := 0 to allTop.Count - 1 do
        if (JStr(allTop.Items[i], 'runId') = id) and (JInt(allTop.Items[i], 'position', -1) = pos) then exists := True;
      allTop.Free;
      if exists then continue;
      // same story, no new sources: copy the wrap (and staff edits)
      same := nil;
      for i := 0 to prevTopics.Count - 1 do
        if (Chat_Js(JArr(prevTopics.Items[i], 'lenses')) = lensV) and Pipe_Covers(JArr(prevTopics.Items[i], 'articleIds'), JArr(topic, 'articleIds')) and
          Pipe_Covers(JArr(prevTopics.Items[i], 'postIds'), JArr(topic, 'postIds')) then
        begin
          same := TJSONObject(prevTopics.Items[i]);
          break;
        end;
      if same <> nil then
      begin
        doc := TJSONObject.Create;
        s := NewId;
        doc.Add('_id', s);
        for i := 0 to same.Count - 1 do
          if (same.Names[i] <> '_id') and (same.Names[i] <> 'runId') and (same.Names[i] <> 'position') and
            (same.Names[i] <> 'createdAt') and (same.Names[i] <> 'updatedAt') then doc.Add(same.Names[i], same.Items[i].Clone);
        JSetS(doc, 'runId', id);
        JSet(doc, 'position', TJSONIntegerNumber.Create(pos));
        if JObj(topic, 'metrics') <> nil then JSet(doc, 'metrics', JObj(topic, 'metrics').Clone);
        JSetS(doc, 'reusedFrom', JStr(same, '_id'));
        DbLock.Enter;
        try
          DbInsert('topics', doc);
        finally
          DbLock.Leave;
        end;
        doc.Free;
        Pipe_PushTopic(id, s, True);
        Pipe_Event(id, 'Reused wrap ' + IntToStr(pos + 1) + '/' + IntToStr(selected.Count) + ': ' + JStr(same, 'title'));
        continue;
      end;
      Pipe_Event(id, 'Writing wrap ' + IntToStr(pos + 1) + '/' + IntToStr(selected.Count) + ': ' + JStr(topic, 'title'));
      arts := DbLoad('articles');
      aidx := Pipe_Index(arts);
      wArts := TJSONArray.Create;
      aids := JArr(topic, 'articleIds');
      if aids <> nil then
        for i := 0 to aids.Count - 1 do
        begin
          a := Pipe_Get(arts, aidx, aids.Items[i].AsString);
          if a <> nil then wArts.Add(a.Clone);
        end;
      aidx.Free;
      arts.Free;
      // posts of the topic, score desc, max 10
      posts := DbLoad('socialposts');
      pids := JArr(topic, 'postIds');
      SetLength(pord, 0);
      for i := 0 to posts.Count - 1 do
        if Pipe_ArrHas(pids, JStr(posts.Items[i], '_id')) then
        begin
          SetLength(pord, Length(pord) + 1);
          pord[High(pord)] := i;
        end;
      for i := 1 to High(pord) do
      begin
        tmp := pord[i];
        j := i - 1;
        while (j >= 0) and (JNum(posts.Items[pord[j]], 'score') < JNum(posts.Items[tmp], 'score')) do begin pord[j + 1] := pord[j]; Dec(j); end;
        pord[j + 1] := tmp;
      end;
      wPosts := TJSONArray.Create;
      for i := 0 to Min(Length(pord), PIPE_MAX_WRAP_POSTS) - 1 do wPosts.Add(posts.Items[pord[i]].Clone);
      posts.Free;
      sourceCount := wArts.Count + wPosts.Count;
      srcList := TJSONArray.Create;
      for i := 0 to wArts.Count - 1 do
      begin
        a := TJSONObject(wArts.Items[i]);
        o := TJSONObject.Create;
        o.Add('id', Lens_SourceLabel(i));
        o.Add('title', JStr(a, 'title'));
        o.Add('source', JStr(a, 'sourceName'));
        o.Add('publishedDay', JStr(a, 'publishedDay'));
        o.Add('text', U16Copy(JStr(a, 'text'), PIPE_SOURCE_TEXT_CHARS));
        srcList.Add(o);
      end;
      for i := 0 to wPosts.Count - 1 do
      begin
        p := TJSONObject(wPosts.Items[i]);
        o := TJSONObject.Create;
        o.Add('id', Lens_SourceLabel(wArts.Count + i));
        o.Add('type', 'social');
        o.Add('title', JStr(p, 'platform') + ': ' + JStr(JObj(p, 'author'), 'name', 'undefined'));
        o.Add('source', JStr(JObj(p, 'author'), 'name'));
        o.Add('publishedDay', JStr(p, 'publishedDay'));
        o.Add('text', U16Copy(JStr(p, 'text'), PIPE_POST_TEXT_CHARS));
        srcList.Add(o);
      end;
      res := nil;
      try
        res := TJSONObject(Pipe_Ask(run, model, Pipe_WrapPrompt(region, JStr(topic, 'title'), JStr(topic, 'reason'), ws, we, lenses, Chat_Js(srcList)),
          Lens_WrapSchema(lenses, sourceCount), 24000));
        if not (res is TJSONObject) then raise Exception.Create('Model returned invalid JSON');
        doc := TJSONObject.Create;
        s := NewId;
        doc.Add('_id', s);
        doc.Add('runId', id);
        doc.Add('position', pos);
        if JStr(res, 'title') <> '' then doc.Add('title', JStr(res, 'title')) else doc.Add('title', JStr(topic, 'title'));
        doc.Add('reason', JStr(topic, 'reason'));
        doc.Add('storyline', JStr(res, 'storyline'));
        doc.Add('category', Top_ToCategory(JStr(topic, 'category')));
        aids := TJSONArray.Create;
        for i := 0 to wArts.Count - 1 do aids.Add(JStr(wArts.Items[i], '_id'));
        doc.Add('articleIds', aids);
        pids := TJSONArray.Create;
        for i := 0 to wPosts.Count - 1 do pids.Add(JStr(wPosts.Items[i], '_id'));
        doc.Add('postIds', pids);
        if JObj(topic, 'metrics') <> nil then doc.Add('metrics', JObj(topic, 'metrics').Clone);
        doc.Add('lenses', lenses.Clone);
        wraps := JObj(res, 'wraps');
        doc.Add('wraps', Lens_CleanWraps(lenses, wraps, sourceCount));
        doc.Add('visuals', Top_CleanVisuals(lenses, res.Find('visuals')));
        doc.Add('model', model);
        doc.Add('hidden', False);
        DbLock.Enter;
        try
          DbInsert('topics', doc);
        finally
          DbLock.Leave;
        end;
        doc.Free;
        Pipe_PushTopic(id, s, False);
      finally
        res.Free;
        srcList.Free;
        wArts.Free;
        wPosts.Free;
      end;
    end;
  finally
    prevTopics.Free;
  end;
  Pipe_SetStage(id, 'publish', nil);
end;

procedure Pipe_Publish(run: TJSONObject);
var
  id, region, edId: string;
  allTop, all, ids: TJSONArray;
  ord: TPipeInts;
  i, j, tmp: Integer;
  ed, o: TJSONObject;
begin
  id := JStr(run, '_id');
  region := Pipe_RegionOfKind(JStr(run, 'kind'));
  allTop := DbLoad('topics');
  SetLength(ord, 0);
  for i := 0 to allTop.Count - 1 do
    if JStr(allTop.Items[i], 'runId') = id then
    begin
      SetLength(ord, Length(ord) + 1);
      ord[High(ord)] := i;
    end;
  for i := 1 to High(ord) do
  begin
    tmp := ord[i];
    j := i - 1;
    while (j >= 0) and (JNum(allTop.Items[ord[j]], 'position') > JNum(allTop.Items[tmp], 'position')) do begin ord[j + 1] := ord[j]; Dec(j); end;
    ord[j + 1] := tmp;
  end;
  ids := TJSONArray.Create;
  for i := 0 to High(ord) do ids.Add(JStr(allTop.Items[ord[i]], '_id'));
  allTop.Free;
  // findOneAndUpdate({runId}, {$setOnInsert}, {upsert})
  DbLock.Enter;
  try
    all := DbLoad('editions');
    edId := '';
    for i := 0 to all.Count - 1 do
      if JStr(all.Items[i], 'runId') = id then edId := JStr(all.Items[i], '_id');
    all.Free;
    if edId = '' then
    begin
      ed := TJSONObject.Create;
      edId := NewId;
      ed.Add('_id', edId);
      ed.Add('runId', id);
      ed.Add('region', region);
      ed.Add('window', JObj(run, 'window').Clone);
      ed.Add('topicIds', ids.Clone);
      ed.Add('articleCount', Pipe_CountColl('articles', JStr(JObj(run, 'window'), 'start'), JStr(JObj(run, 'window'), 'end'), region, ''));
      ed.Add('publishedAt', NowIso);
      DbInsert('editions', ed);
      ed.Free;
    end;
  finally
    DbLock.Leave;
  end;
  o := TJSONObject.Create;
  o.Add('editionId', edId);
  Pipe_Finish(id, o);
  Pipe_Event(id, 'Published edition with ' + IntToStr(ids.Count) + ' topics');
  ids.Free;
  Pipe_LogUsage(id);
end;

// runs the pipeline from the run's current stage to the end. never raises; failures are saved on the run
procedure Pipe_ExecuteRun(const runId: string);
var
  doc, o: TJSONObject;
  stage, msg: string;
begin
  PipeLogRunId := runId;
  try
    while True do
    begin
      doc := DbGet('runs', runId);
      if doc = nil then exit;
      try
        if JStr(doc, 'status') <> 'running' then exit;
        stage := JStr(doc, 'stage');
        if stage = 'collect' then Pipe_Collect(doc)
        else if stage = 'extract' then Pipe_Extract(doc)
        else if stage = 'social' then Pipe_Social(doc)
        else if stage = 'cluster' then Pipe_Cluster(doc)
        else if stage = 'select' then Pipe_Select(doc)
        else if stage = 'wrap' then Pipe_Wrap(doc)
        else if stage = 'publish' then Pipe_Publish(doc)
        else exit;
      finally
        doc.Free;
      end;
    end;
  except
    on E: Exception do
    begin
      msg := E.Message;
      Log('Run failed ' + runId + ': ' + E.ClassName + ' ' + msg);
      try
        o := TJSONObject.Create;
        o.Add('status', 'failed');
        o.Add('error', U16Copy(msg, 1000));
        o.Add('completedAt', NowIso);
        Pipe_RunSet(runId, o);
        Pipe_LogUsage(runId);
      except
        on E2: Exception do Log('Run failure not saved: ' + E2.Message);
      end;
    end;
  end;
end;

procedure TPipeRunThread.Execute;
begin
  try
    Pipe_ExecuteRun(RunId);
  except
    on E: Exception do Log('run thread: ' + E.Message);
  end;
end;

procedure Pipe_Background(const runId: string);
var
  t: TPipeRunThread;
begin
  t := TPipeRunThread.Create(True);
  t.RunId := runId;
  t.FreeOnTerminate := True;
  t.Start;
end;

// creates a job run with a snapshot of the current config; EApi 409 when one of its kind is running. returns the run id
function Pipe_StartRun(const kind, trigger: string): string;
var
  region, ws, we: string;
  all, cfgSources, accounts, lenses, runs: TJSONArray;
  settings, w, cfg, run, o, u: TJSONObject;
  i: Integer;
begin
  region := Pipe_RegionOfKind(kind);
  all := DbLoad('sources');
  cfgSources := TJSONArray.Create;
  for i := 0 to all.Count - 1 do
  begin
    if not JBool(all.Items[i], 'enabled', True) then continue;
    if not Src_InRegion(all.Items[i], region) then continue;
    o := TJSONObject.Create;
    o.Add('slug', JStr(all.Items[i], 'slug'));
    o.Add('name', JStr(all.Items[i], 'name'));
    o.Add('url', JStr(all.Items[i], 'url'));
    o.Add('weight', Pipe_Num(JNum(all.Items[i], 'weight', 1)));
    if JObj(all.Items[i], 'discovery') <> nil then o.Add('discovery', JObj(all.Items[i], 'discovery').Clone);
    if JObj(all.Items[i], 'article') <> nil then o.Add('article', JObj(all.Items[i], 'article').Clone);
    cfgSources.Add(o);
  end;
  all.Free;
  accounts := Soc_EnabledAccountConfigs;
  lenses := Lens_LoadSnapshots;
  settings := GetSettings;
  if ((kind = 'news') or (kind = 'world')) and (cfgSources.Count = 0) then
  begin
    cfgSources.Free; accounts.Free; lenses.Free; settings.Free;
    raise Exception.Create('No enabled sources');
  end;
  if ((kind = 'publish') or (kind = 'world')) and (lenses.Count = 0) then
  begin
    cfgSources.Free; accounts.Free; lenses.Free; settings.Free;
    raise Exception.Create('No enabled wrap points');
  end;
  // the foreign edition has its own window, topic count and minimum of new items
  if kind = 'world' then
  begin
    w := JObj(settings, 'world');
    if w <> nil then
    begin
      w := TJSONObject(w.Clone);
      for i := 0 to w.Count - 1 do JSet(settings, w.Names[i], w.Items[i].Clone);
      w.Free;
    end;
  end;
  DateWindow(JInt(settings, 'windowDays', 14), ws, we);
  cfg := TJSONObject.Create;
  cfg.Add('sources', cfgSources);
  cfg.Add('accounts', accounts);
  cfg.Add('lenses', lenses);
  cfg.Add('settings', settings);
  run := TJSONObject.Create;
  Result := NewId;
  run.Add('_id', Result);
  run.Add('kind', kind);
  run.Add('status', 'running');
  run.Add('stage', Pipe_FirstStage(kind));
  run.Add('trigger', trigger);
  o := TJSONObject.Create;
  o.Add('start', ws);
  o.Add('end', we);
  run.Add('window', o);
  run.Add('config', cfg);
  run.Add('sourceReports', TJSONArray.Create);
  run.Add('articleCount', 0);
  run.Add('socialReports', TJSONArray.Create);
  run.Add('postCount', 0);
  run.Add('topicIds', TJSONArray.Create);
  run.Add('reusedWraps', 0);
  u := TJSONObject.Create;
  u.Add('inputTokens', 0); u.Add('outputTokens', 0); u.Add('calls', 0); u.Add('costUsd', 0);
  run.Add('usage', u);
  u := TJSONObject.Create;
  u.Add('requests', 0); u.Add('credits', 0); u.Add('costUsd', 0);
  run.Add('socialUsage', u);
  run.Add('events', TJSONArray.Create);
  run.Add('startedAt', NowIso);
  // the run lock: one running run per kind (unique partial index in the original)
  DbLock.Enter;
  try
    runs := DbLoad('runs');
    for i := 0 to runs.Count - 1 do
      if (JStr(runs.Items[i], 'kind') = kind) and (JStr(runs.Items[i], 'status') = 'running') then
      begin
        runs.Free;
        run.Free;
        raise EApi.CreateS(409, PIPE_LOCKED_MSG);
      end;
    runs.Free;
    DbInsert('runs', run);
  finally
    DbLock.Leave;
  end;
  run.Free;
end;

// puts a failed run back to running at its failed stage. '' when there is no failed run with this id; EApi 409 when locked
function Pipe_ResumeRun(const id: string): string;
var
  all: TJSONArray;
  i, k: Integer;
  d: TJSONObject;
begin
  Result := '';
  DbLock.Enter;
  try
    all := DbLoad('runs');
    try
      k := -1;
      for i := 0 to all.Count - 1 do
        if (JStr(all.Items[i], '_id') = id) and (JStr(all.Items[i], 'status') = 'failed') then k := i;
      if k < 0 then exit;
      d := TJSONObject(all.Items[k]);
      for i := 0 to all.Count - 1 do
        if (i <> k) and (JStr(all.Items[i], 'kind') = JStr(d, 'kind')) and (JStr(all.Items[i], 'status') = 'running') then
          raise EApi.CreateS(409, PIPE_LOCKED_MSG);
      JSetS(d, 'status', 'running');
      if d.IndexOfName('error') >= 0 then d.Delete('error');
      if d.IndexOfName('completedAt') >= 0 then d.Delete('completedAt');
      JSetS(d, 'updatedAt', NowIso);
      DbSave('runs', all);
      Result := id;
    finally
      all.Free;
    end;
  finally
    DbLock.Leave;
  end;
end;

// runs execute in this process, so any run still "running" at boot was interrupted
procedure Pipe_FailInterruptedRuns;
var
  all: TJSONArray;
  i: Integer;
  changed: Boolean;
begin
  DbLock.Enter;
  try
    all := DbLoad('runs');
    changed := False;
    for i := 0 to all.Count - 1 do
      if JStr(all.Items[i], 'status') = 'running' then
      begin
        JSetS(TJSONObject(all.Items[i]), 'status', 'failed');
        JSetS(TJSONObject(all.Items[i]), 'error', 'Interrupted by a server restart. Resume it from the admin.');
        JSetS(TJSONObject(all.Items[i]), 'updatedAt', NowIso);
        changed := True;
      end;
    if changed then DbSave('runs', all);
    all.Free;
  finally
    DbLock.Leave;
  end;
end;

// ---------------------------------------------------------------- scheduler.ts

// called by the scheduler thread once per minute with Mongolia local time; settings are re-read every tick
procedure Pipe_SchedulerTick(mnNow: TDateTime);
var
  s, sch: TJSONObject;
  i: Integer;
  kind, rid: string;
begin
  Pipe_Init;
  s := GetSettings;
  try
    for i := 0 to High(PIPE_KINDS) do
    begin
      kind := PIPE_KINDS[i];
      sch := JObj(JObj(s, 'schedules'), kind);
      if (sch = nil) or not JBool(sch, 'enabled') then continue;
      if not CronMatches(JStr(sch, 'cron'), mnNow) then continue;
      try
        rid := Pipe_StartRun(kind, 'schedule');
        Log('Scheduled ' + kind + ' run started: ' + rid);
        Pipe_Background(rid);
      except
        on E: EApi do
          if E.Status = 409 then Log('Scheduled ' + kind + ' run skipped: one is in progress')
          else Log('Scheduled ' + kind + ' run could not start: ' + E.Message);
        on E: Exception do Log('Scheduled ' + kind + ' run could not start: ' + E.Message);
      end;
    end;
  finally
    s.Free;
  end;
end;

// boot hook: migrate settings, fail interrupted runs
procedure Pipe_Boot;
var
  s: TJSONObject;
  i: Integer;
begin
  Pipe_Init;
  Set_Migrate;
  Pipe_FailInterruptedRuns;
  if Env('RUN_SCHEDULER', 'true') = 'true' then
  begin
    s := GetSettings;
    for i := 0 to High(PIPE_KINDS) do
      if JBool(JPath(s, 'schedules.' + PIPE_KINDS[i]), 'enabled') then
        Log('Job ' + PIPE_KINDS[i] + ' scheduled: ' + JStr(JPath(s, 'schedules.' + PIPE_KINDS[i]), 'cron') + ' (Asia/Ulaanbaatar)');
    s.Free;
  end;
end;

// ---------------------------------------------------------------- runs.routes.ts (/api/admin/runs, per-route permissions)

procedure Routes_pipeline;
var
  kind, id, rid: string;
  all, list: TJSONArray;
  ord: TPipeInts;
  i, j, tmp: Integer;
  o, b: TJSONObject;
begin
  if Route('GET', '/api/admin/runs') then
  begin
    RequirePerm('runs.view');
    kind := '';
    if Req.QueryFields.IndexOfName('kind') >= 0 then
    begin
      kind := Req.QueryFields.Values['kind'];
      if not Pipe_IsKind(kind) then raise EApi.CreateS(400, '✖ ' + PIPE_KINDS_ENUM + #10 + '  → at kind');
    end;
    all := DbLoad('runs');
    SetLength(ord, 0);
    for i := 0 to all.Count - 1 do
      if (kind = '') or (JStr(all.Items[i], 'kind') = kind) then
      begin
        SetLength(ord, Length(ord) + 1);
        ord[High(ord)] := i;
      end;
    for i := 1 to High(ord) do
    begin
      tmp := ord[i];
      j := i - 1;
      while (j >= 0) and (JStr(all.Items[ord[j]], 'startedAt') < JStr(all.Items[tmp], 'startedAt')) do begin ord[j + 1] := ord[j]; Dec(j); end;
      ord[j + 1] := tmp;
    end;
    list := TJSONArray.Create;
    for i := 0 to Min(50, Length(ord)) - 1 do
    begin
      o := TJSONObject(all.Items[ord[i]].Clone);
      for j := 0 to High(PIPE_LIST_HIDDEN) do
        if o.IndexOfName(PIPE_LIST_HIDDEN[j]) >= 0 then o.Delete(PIPE_LIST_HIDDEN[j]);
      list.Add(o);
    end;
    all.Free;
    SendJson(200, list);
    exit;
  end;

  if Route('GET', '/api/admin/runs/:id') then
  begin
    RequirePerm('runs.view');
    id := P(3);
    o := nil;
    if IsId(id) then o := DbGet('runs', id);
    if o = nil then raise EApi.CreateS(404, 'Not found');
    if (JObj(o, 'config') <> nil) and (JObj(o, 'config').IndexOfName('sources') >= 0) then JObj(o, 'config').Delete('sources');
    if o.IndexOfName('candidates') >= 0 then o.Delete('candidates');
    SendJson(200, o);
    exit;
  end;

  // starts a job now; it continues in the background (poll GET /:id)
  if Route('POST', '/api/admin/runs') then
  begin
    RequirePerm('runs.start');
    kind := 'publish';
    if ReqBody <> nil then
    begin
      if not (ReqBody is TJSONObject) then
        raise EApi.CreateS(400, '✖ Invalid input: expected object, received ' + PvType(ReqBody));
      b := TJSONObject(ReqBody);
      if b.Find('kind') <> nil then
      begin
        if (b.Find('kind').JSONType = jtString) and Pipe_IsKind(b.Find('kind').AsString) then kind := b.Find('kind').AsString
        else raise EApi.CreateS(400, '✖ ' + PIPE_KINDS_ENUM + #10 + '  → at kind');
      end;
    end;
    Pipe_Init;
    rid := Pipe_StartRun(kind, 'manual');
    Pipe_Background(rid);
    SendJsonStr(202, '{"_id":' + JEsc(rid) + '}');
    exit;
  end;

  if Route('POST', '/api/admin/runs/:id/resume') then
  begin
    RequirePerm('runs.start');
    id := P(3);
    if not IsId(id) then raise EApi.CreateS(404, 'Not found');
    rid := Pipe_ResumeRun(id);
    if rid = '' then raise EApi.CreateS(404, 'No failed run with this id');
    Pipe_Init;
    Pipe_Background(rid);
    SendJsonStr(202, '{"_id":' + JEsc(rid) + '}');
    exit;
  end;
end;

// ---------------------------------------------------------------- scripts/run-job.ts and backfill-visuals.ts (called from cli.inc)

// sedev job <kind> | sedev job resume <runId>: runs blocking, prints the events. returns exit code
function Pipe_CliJob(const cmd, arg: string): Integer;
var
  rid: string;
  r: TJSONObject;
  ev: TJSONArray;
  i: Integer;
begin
  Result := 0;
  Pipe_Init;
  if not Pipe_IsKind(cmd) and not ((cmd = 'resume') and (arg <> '')) then
  begin
    WriteLn('Usage: sedev job news|social|publish|world  |  sedev job resume <runId>');
    exit(1);
  end;
  try
    if cmd = 'resume' then
    begin
      rid := '';
      if IsId(arg) then rid := Pipe_ResumeRun(arg);
      if rid = '' then
      begin
        WriteLn('Run ' + arg + ' not found or not failed');
        exit(1);
      end;
    end
    else rid := Pipe_StartRun(cmd, 'manual');
  except
    on E: Exception do
    begin
      WriteLn(E.Message);
      exit(1);
    end;
  end;
  r := DbGet('runs', rid);
  if cmd = 'resume' then WriteLn('Run ' + rid + ' (' + JStr(r, 'kind') + ') resumed')
  else WriteLn('Run ' + rid + ' (' + JStr(r, 'kind') + ') started');
  r.Free;
  Pipe_ExecuteRun(rid);
  r := DbGet('runs', rid);
  ev := JArr(r, 'events');
  if ev <> nil then
    for i := 0 to ev.Count - 1 do WriteLn('  ' + JStr(ev.Items[i], 'message'));
  if JStr(r, 'error') <> '' then WriteLn('Status: ' + JStr(r, 'status') + ' — ' + JStr(r, 'error'))
  else WriteLn('Status: ' + JStr(r, 'status'));
  r.Free;
end;

// adds visuals to topics of the latest editions per region that have none (one small LLM call per topic)
procedure Pipe_BackfillVisuals(perRegion: Integer; dryRun: Boolean);
var
  settings, t, res, sch, props: TJSONObject;
  eds, allTop, todo, lenses, req: TJSONArray;
  ord: TPipeInts;
  i, j, tmp, n, k: Integer;
  region, prompt, parts, model: string;
  inT, outT, sumIn, sumOut: Int64;
  vis: TJSONObject;
  ids: TStringList;
begin
  settings := GetSettings;
  model := JStr(JObj(settings, 'models'), 'wrap');
  eds := DbLoad('editions');
  ids := TStringList.Create;
  n := 0;
  for tmp := 0 to 1 do
  begin
    region := PIPE_REGIONS[tmp];
    SetLength(ord, 0);
    for i := 0 to eds.Count - 1 do
      if Src_InRegion(eds.Items[i], region) then
      begin
        SetLength(ord, Length(ord) + 1);
        ord[High(ord)] := i;
      end;
    for i := 1 to High(ord) do
    begin
      k := ord[i];
      j := i - 1;
      while (j >= 0) and (JStr(eds.Items[ord[j]], 'publishedAt') < JStr(eds.Items[k], 'publishedAt')) do begin ord[j + 1] := ord[j]; Dec(j); end;
      ord[j + 1] := k;
    end;
    n := n + Min(perRegion, Length(ord));
    for i := 0 to Min(perRegion, Length(ord)) - 1 do
      if JArr(eds.Items[ord[i]], 'topicIds') <> nil then
        for j := 0 to JArr(eds.Items[ord[i]], 'topicIds').Count - 1 do ids.Add(JArr(eds.Items[ord[i]], 'topicIds').Items[j].AsString);
  end;
  allTop := DbLoad('topics');
  todo := TJSONArray.Create;
  for i := 0 to allTop.Count - 1 do
    if (ids.IndexOf(JStr(allTop.Items[i], '_id')) >= 0) and (TJSONObject(allTop.Items[i]).Find('visuals') = nil) then
      todo.Add(allTop.Items[i].Clone);
  allTop.Free;
  WriteLn(IntToStr(todo.Count) + ' topics without visuals in ' + IntToStr(n) + ' editions');
  eds.Free;
  ids.Free;
  sumIn := 0; sumOut := 0;
  for i := 0 to todo.Count - 1 do
  begin
    t := TJSONObject(todo.Items[i]);
    lenses := JArr(t, 'lenses');
    if lenses = nil then lenses := TJSONArray(JParse('[]'));
    if dryRun then
    begin
      WriteLn('would write ' + IntToStr(i + 1) + '/' + IntToStr(todo.Count) + ': ' + JStr(t, 'title'));
      continue;
    end;
    parts := '';
    for j := 0 to lenses.Count - 1 do
    begin
      if j > 0 then parts := parts + ', ';
      parts := parts + JStr(lenses.Items[j], 'key') + ' (' + JStr(lenses.Items[j], 'label') + ')';
    end;
    res := Top_WithoutCitations(t.Find('wraps')) as TJSONObject;
    prompt := 'Сэдэв: ' + JStr(t, 'title') + #10 + 'Өгүүлэмж: ' + JStr(t, 'storyline') + #10 +
      'Энэ сэдвийн задлал (wraps) бэлэн. Зөвхөн visuals талбарыг бөглө.' + #10 +
      'wraps-ийн хэсгүүд: ' + parts + #10 + Top_VisualsRules + Chat_Js(res);
    res.Free;
    props := TJSONObject.Create;
    props.Add('visuals', Lens_VisualsSchema(lenses));
    sch := TJSONObject.Create;
    sch.Add('type', 'object');
    sch.Add('properties', props);
    req := TJSONArray.Create;
    req.Add('visuals');
    sch.Add('required', req);
    try
      res := TJSONObject(GeminiJson(model, PIPE_SYSTEM, prompt, sch.AsJSON, 4000, inT, outT));
    finally
      sch.Free;
    end;
    sumIn := sumIn + inT; sumOut := sumOut + outT;
    vis := Top_CleanVisuals(lenses, res.Find('visuals'));
    res.Free;
    DbLock.Enter;
    try
      res := TJSONObject.Create;
      res.Add('visuals', vis.Clone);
      DbUpdate('topics', JStr(t, '_id'), res);
      res.Free;
    finally
      DbLock.Leave;
    end;
    WriteLn(IntToStr(i + 1) + '/' + IntToStr(todo.Count) + ' ' + IntToStr(JArr(vis, 'motifs').Count) + ' motifs: ' + JStr(t, 'title'));
    vis.Free;
  end;
  WriteLn('tokens: ' + IntToStr(sumIn) + ' in, ' + IntToStr(sumOut) + ' out');
  todo.Free;
  settings.Free;
end;
