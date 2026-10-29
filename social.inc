// ===================== SOCIAL (modules/social/*: accounts, posts, providers, collect, score, routes) =====================
// everything in one file. dates inside this file are mostly Int64 unix milliseconds (like JS getTime()).
// json shapes (same as the mongo docs of the original):
//   socialaccounts: {_id,platform,handle,url,name,kind,weight,followers,baseline,enabled,lastCollectedAt,lastPostAt,lastPostId,pageId,createdAt,updatedAt}
//   socialposts:    {_id,platform,externalId,accountId,author:{name,handle,followers},url,text,image,postedAt,publishedDay,
//                    metrics:{likes,comments,shares,views},engagement,score,fetchedAt,createdAt,updatedAt}

{$I social_defaults.inc}

type
  TSocDblArr = array of Double;
  TSocIntArr = array of Integer;
  TSocI64Arr = array of Int64;
  TSocCpArr = array of Cardinal;
  TSocLogProc = procedure(const msg: string);
  // test seam: when set, replaces curl for provider requests
  TSocHttpHook = function(const url, headers: string; out status: Integer): string;

var
  Soc_HttpHook: TSocHttpHook = nil;

const
  SOC_HOUR_MS = 3600000;
  SOC_MS_PER_DAY: Double = 86400000; // typed: an untyped 86400000.0 is a Single in fpc
  SOC_DEFAULT_CREDIT_USD = 0.00188;
  SOC_BASELINE_POSTS = 30;
  SOC_PROBE_DAYS = 14;
  SOC_MAX_POSTS_PER_ACCOUNT = 3;
  SOC_POST_RANK_FACTOR = 0.5;
  SOC_REPRINT_FACTOR = 0.3;
  SOC_DUPLICATE_SIMILARITY = 0.5;
  SOC_MAX_DISCUSSION_BOOST = 2;
  SOC_MAX_LIFT = 16;
  SOC_TRIGGER_BOOST = 1.5;
  SOC_TRIGGER_HOURS = 24;
  SOC_TRIGGER_MIN_PUBLISHERS = 2;
  SOC_VELOCITY_HOURS = 24;
  SOC_VELOCITY_PRIOR = 3;
  SOC_SC_API = 'https://api.scrapecreators.com/v1';
  SOC_TW_API = 'https://api.twitterapi.io/twitter/tweet/advanced_search';
  SOC_FB_POSTS_PER_PAGE = 3;
  SOC_TW_HANDLES_PER_QUERY = 15;
  SOC_TW_TWEETS_PER_PAGE = 20;
  SOC_TW_CREDITS_PER_TWEET = 15;
  SOC_TW_USD_PER_CREDIT = 1 / 100000;
  SOC_PLATFORMS_ENUM = 'Invalid option: expected one of "facebook"|"x"|"threads"';
  // utf-8 bytes: unicode literals concatenated at runtime turn into '?' (no widestring manager)
  SOC_CROSS = #$E2#$9C#$96;
  SOC_ARROW = #$E2#$86#$92;
  SOC_DASH = #$E2#$80#$94;
  SOC_KINDS_ENUM = 'Invalid option: expected one of "journalist"|"page"|"influencer"|"media"|"official"';

// ---------- time ----------
function Soc_Ms(dt: TDateTime): Int64;
begin
  Result := Round((dt - EncodeDate(1970, 1, 1)) * SOC_MS_PER_DAY);
end;

function Soc_FromMs(ms: Int64): TDateTime;
begin
  Result := EncodeDate(1970, 1, 1) + ms / SOC_MS_PER_DAY;
end;

function Soc_IsoMs(ms: Int64): string;
begin
  Result := IsoOf(Soc_FromMs(ms));
end;

function Soc_NowMs: Int64;
begin
  Result := Soc_Ms(NowUtc);
end;

function Soc_MonthNo(const m: string): Integer;
var
  p: Integer;
begin
  p := Pos(LowerCase(m), 'janfebmaraprmayjunjulaugsepoctnovdec');
  if (p = 0) or ((p - 1) mod 3 <> 0) then Result := 0 else Result := (p - 1) div 3 + 1;
end;

// parses ISO (Z / +hh:mm / none = UTC), twitter "Sun Oct 05 08:00:00 +0000 2026", RFC "Sun, 05 Oct 2026 08:00:00 GMT"
function Soc_ParseDate(const s0: string; out ms: Int64): Boolean;
var
  r: TRegExpr;
  y, mo, d, h, mi, se, mss, off: Integer;
  s, t: string;
  dt: TDateTime;
begin
  Result := False;
  ms := 0;
  s := Trim(s0);
  if s = '' then exit;
  r := TRegExpr.Create;
  try
    r.Expression := '^(\d{4})-(\d{2})-(\d{2})([T ](\d{2}):(\d{2})(:(\d{2})(\.(\d+))?)?)?(Z|z|[+\-]\d\d:?\d\d)?$';
    if r.Exec(s) then
    begin
      y := StrToInt(r.Match[1]); mo := StrToInt(r.Match[2]); d := StrToInt(r.Match[3]);
      h := StrToIntDef(r.Match[5], 0); mi := StrToIntDef(r.Match[6], 0); se := StrToIntDef(r.Match[8], 0);
      mss := 0;
      if r.Match[10] <> '' then mss := StrToIntDef(Copy(r.Match[10] + '000', 1, 3), 0);
      off := 0;
      t := r.Match[11];
      if (t <> '') and (UpperCase(t) <> 'Z') then
      begin
        t := StringReplace(t, ':', '', []);
        off := StrToInt(Copy(t, 2, 2)) * 60 + StrToInt(Copy(t, 4, 2));
        if t[1] = '-' then off := -off;
      end;
      if not TryEncodeDateTime(y, mo, d, h, mi, se, mss, dt) then exit;
      ms := Soc_Ms(dt) - Int64(off) * 60000;
      exit(True);
    end;
    r.Expression := '^[A-Za-z][A-Za-z][A-Za-z],? ([A-Za-z][A-Za-z][A-Za-z]) (\d\d?) (\d\d):(\d\d):(\d\d) ([+\-]\d\d\d\d) (\d\d\d\d)$';
    if r.Exec(s) then
    begin
      mo := Soc_MonthNo(r.Match[1]); d := StrToInt(r.Match[2]);
      h := StrToInt(r.Match[3]); mi := StrToInt(r.Match[4]); se := StrToInt(r.Match[5]);
      t := r.Match[6]; y := StrToInt(r.Match[7]);
      off := StrToInt(Copy(t, 2, 2)) * 60 + StrToInt(Copy(t, 4, 2));
      if t[1] = '-' then off := -off;
      if (mo = 0) or not TryEncodeDateTime(y, mo, d, h, mi, se, 0, dt) then exit;
      ms := Soc_Ms(dt) - Int64(off) * 60000;
      exit(True);
    end;
    r.Expression := '^([A-Za-z][A-Za-z][A-Za-z],? )?(\d\d?) ([A-Za-z][A-Za-z][A-Za-z]) (\d\d\d\d) (\d\d):(\d\d):(\d\d) ?(GMT|UTC|Z|[+\-]\d\d\d\d)?$';
    if r.Exec(s) then
    begin
      d := StrToInt(r.Match[2]); mo := Soc_MonthNo(r.Match[3]); y := StrToInt(r.Match[4]);
      h := StrToInt(r.Match[5]); mi := StrToInt(r.Match[6]); se := StrToInt(r.Match[7]);
      t := r.Match[8]; off := 0;
      if (t <> '') and (t[1] in ['+', '-']) then
      begin
        off := StrToInt(Copy(t, 2, 2)) * 60 + StrToInt(Copy(t, 4, 2));
        if t[1] = '-' then off := -off;
      end;
      if (mo = 0) or not TryEncodeDateTime(y, mo, d, h, mi, se, 0, dt) then exit;
      ms := Soc_Ms(dt) - Int64(off) * 60000;
      exit(True);
    end;
  finally
    r.Free;
  end;
end;

// providers/types.ts toDate: unix seconds, unix ms or a date string
function Soc_ToDate(v: TJSONData; out ms: Int64): Boolean;
var
  n: Double;
begin
  Result := False;
  ms := 0;
  if (v = nil) or (v.JSONType in [jtNull, jtObject, jtArray, jtBoolean]) then exit;
  if v.JSONType = jtNumber then n := v.AsFloat
  else
  begin
    if v.AsString = '' then exit;
    if not TryStrToFloat(Trim(v.AsString), n) then exit(Soc_ParseDate(v.AsString, ms));
  end;
  if n < 1e12 then ms := Round(n * 1000) else ms := Round(n);
  Result := True;
end;

// date field of a stored doc (ISO string) as ms, 0 when missing
function Soc_DocMs(o: TJSONData; const field: string): Int64;
begin
  if not Soc_ParseDate(JStr(o, field), Result) then Result := 0;
end;

// YYYY-MM-DD 00:00 Mongolia time (+08:00) as ms
function Soc_DayStartMs(const day: string): Int64;
var
  dt: TDateTime;
begin
  dt := ParseIso(Copy(day, 1, 10));
  Result := Soc_Ms(dt) - 8 * SOC_HOUR_MS;
end;

// ---------- utf-8 (lowercase for cyrillic/latin, word chars) ----------
function Soc_Cps(const s: string): TSocCpArr;
var
  i, n, k: Integer;
  c: Cardinal;
  b: Byte;
begin
  SetLength(Result, Length(s));
  n := 0;
  i := 1;
  while i <= Length(s) do
  begin
    b := Ord(s[i]);
    if b < $80 then begin c := b; k := 0; end
    else if (b and $E0) = $C0 then begin c := b and $1F; k := 1; end
    else if (b and $F0) = $E0 then begin c := b and $0F; k := 2; end
    else if (b and $F8) = $F0 then begin c := b and $07; k := 3; end
    else begin c := b; k := 0; end;
    Inc(i);
    while (k > 0) and (i <= Length(s)) and ((Ord(s[i]) and $C0) = $80) do
    begin
      c := (c shl 6) or (Ord(s[i]) and $3F);
      Inc(i); Dec(k);
    end;
    Result[n] := c;
    Inc(n);
  end;
  SetLength(Result, n);
end;

function Soc_CpStr(c: Cardinal): string;
begin
  if c < $80 then Result := Chr(c)
  else if c < $800 then Result := Chr($C0 or (c shr 6)) + Chr($80 or (c and $3F))
  else if c < $10000 then Result := Chr($E0 or (c shr 12)) + Chr($80 or ((c shr 6) and $3F)) + Chr($80 or (c and $3F))
  else Result := Chr($F0 or (c shr 18)) + Chr($80 or ((c shr 12) and $3F)) + Chr($80 or ((c shr 6) and $3F)) + Chr($80 or (c and $3F));
end;

function Soc_LowerCp(c: Cardinal): Cardinal;
begin
  Result := c;
  if (c >= Ord('A')) and (c <= Ord('Z')) then Result := c + 32
  else if (c >= $C0) and (c <= $DE) and (c <> $D7) then Result := c + 32
  else if (c >= $410) and (c <= $42F) then Result := c + 32
  else if (c >= $400) and (c <= $40F) then Result := c + 80
  else if (((c >= $460) and (c <= $481)) or ((c >= $48A) and (c <= $4BF)) or ((c >= $4D0) and (c <= $4FF))) and (c mod 2 = 0) then Result := c + 1
  else if (c >= $4C1) and (c <= $4CE) and (c mod 2 = 1) then Result := c + 1
  else if (c >= $100) and (c <= $17F) and (c mod 2 = 0) then Result := c + 1;
end;

// \p{L}\p{N} approximation: ascii alnum, everything >= U+00C0 except punctuation blocks and ×÷
function Soc_IsWordCp(c: Cardinal): Boolean;
begin
  if c < $80 then Result := ((c >= Ord('0')) and (c <= Ord('9'))) or ((c >= Ord('a')) and (c <= Ord('z'))) or ((c >= Ord('A')) and (c <= Ord('Z')))
  else if c < $C0 then Result := (c = $AA) or (c = $B5) or (c = $BA) or (c = $B2) or (c = $B3) or (c = $B9)
  else if (c = $D7) or (c = $F7) then Result := False
  else if (c >= $2000) and (c <= $2BFF) then Result := False
  else if (c >= $3000) and (c <= $303F) then Result := False
  else if (c >= $FE30) and (c <= $FE6F) then Result := False
  else if (c >= $FF00) and (c <= $FF0F) then Result := False
  else if (c >= $1F000) then Result := False // emoji
  else Result := True;
end;

function Soc_Lower(const s: string): string;
var
  a: TSocCpArr;
  i: Integer;
begin
  a := Soc_Cps(s);
  Result := '';
  for i := 0 to High(a) do Result := Result + Soc_CpStr(Soc_LowerCp(a[i]));
end;

// JS string .slice(0, n) (utf-16 units, astral chars count 2)
function Soc_JsSlice(const s: string; n: Integer): string;
var
  i, units: Integer;
  b: Byte;
begin
  units := 0;
  i := 1;
  while i <= Length(s) do
  begin
    b := Ord(s[i]);
    if (b and $F8) = $F0 then
    begin
      if units + 2 > n then break;
      Inc(units, 2);
    end
    else if (b and $C0) <> $80 then
    begin
      if units + 1 > n then break;
      Inc(units);
    end;
    Inc(i);
    while (i <= Length(s)) and ((Ord(s[i]) and $C0) = $80) do Inc(i);
  end;
  Result := Copy(s, 1, i - 1);
end;

// ---------- lib/similarity.ts (own copy) ----------
function Soc_IndexOfExact(sl: TStrings; const s: string): Integer;
var
  i: Integer;
begin
  Result := -1;
  for i := 0 to sl.Count - 1 do
    if sl[i] = s then exit(i);
end;

// set of lowercase word bigrams; caller frees
function Soc_Shingles(const text: string): TStringList;
var
  a: TSocCpArr;
  words: TStringList;
  i: Integer;
  w, sh: string;
begin
  a := Soc_Cps(text);
  words := TStringList.Create;
  w := '';
  for i := 0 to High(a) do
    if Soc_IsWordCp(a[i]) then w := w + Soc_CpStr(Soc_LowerCp(a[i]))
    else if w <> '' then begin words.Add(w); w := ''; end;
  if w <> '' then words.Add(w);
  Result := TStringList.Create;
  if words.Count < 2 then
  begin
    for i := 0 to words.Count - 1 do
      if Soc_IndexOfExact(Result, words[i]) < 0 then Result.Add(words[i]);
  end
  else
    for i := 0 to words.Count - 2 do
    begin
      sh := words[i] + ' ' + words[i + 1];
      if Soc_IndexOfExact(Result, sh) < 0 then Result.Add(sh);
    end;
  words.Free;
end;

function Soc_Jaccard(a, b: TStrings): Double;
var
  i, shared: Integer;
begin
  if (a.Count = 0) or (b.Count = 0) then exit(0);
  shared := 0;
  for i := 0 to a.Count - 1 do
    if Soc_IndexOfExact(b, a[i]) >= 0 then Inc(shared);
  Result := shared / (a.Count + b.Count - shared);
end;

function Soc_UfFind(var root: TSocIntArr; i: Integer): Integer;
begin
  if root[i] = i then exit(i);
  root[i] := Soc_UfFind(root, root[i]);
  Result := root[i];
end;

// for each text the index of its group's first member
function Soc_NearDupGroups(texts: TStrings; threshold: Double = 0.5): TSocIntArr;
var
  sets: array of TStringList;
  root: TSocIntArr;
  i, j, a, b: Integer;
begin
  SetLength(sets, texts.Count);
  SetLength(root, texts.Count);
  for i := 0 to texts.Count - 1 do begin sets[i] := Soc_Shingles(texts[i]); root[i] := i; end;
  for i := 0 to texts.Count - 1 do
    for j := i + 1 to texts.Count - 1 do
    begin
      a := Soc_UfFind(root, i);
      b := Soc_UfFind(root, j);
      if (a <> b) and (Soc_Jaccard(sets[i], sets[j]) >= threshold) then root[Max(a, b)] := Min(a, b);
    end;
  SetLength(Result, texts.Count);
  for i := 0 to texts.Count - 1 do Result[i] := Soc_UfFind(root, i);
  for i := 0 to High(sets) do sets[i].Free;
end;

// ---------- score.ts ----------
function Soc_Engagement(likes, comments, shares, views: Double): Double;
begin
  Result := likes + 2 * comments + 3 * shares + 0.01 * views;
end;

function Soc_EngagementOf(metrics: TJSONData): Double;
begin
  Result := Soc_Engagement(JNum(metrics, 'likes'), JNum(metrics, 'comments'), JNum(metrics, 'shares'), JNum(metrics, 'views'));
end;

function Soc_PostScore(engagement, followers, weight, baseline: Double): Double;
var
  authority, lift: Double;
begin
  authority := weight * Log10(10 + followers);
  lift := Min(SOC_MAX_LIFT, Max(0.25, engagement / Max(baseline, 1)));
  Result := Log10(1 + engagement) * authority * Sqrt(lift);
end;

function Soc_Decay(ageHours, halfLifeHours: Double): Double;
begin
  Result := Power(0.5, Max(0, ageHours) / halfLifeHours);
end;

function Soc_Median(const values: array of Double): Double;
var
  a: TSocDblArr;
  i, j, n: Integer;
  t: Double;
begin
  n := Length(values);
  if n = 0 then exit(0);
  SetLength(a, n);
  for i := 0 to n - 1 do a[i] := values[i];
  for i := 1 to n - 1 do
  begin
    t := a[i]; j := i - 1;
    while (j >= 0) and (a[j] > t) do begin a[j + 1] := a[j]; Dec(j); end;
    a[j + 1] := t;
  end;
  if n mod 2 = 1 then Result := a[n div 2] else Result := (a[n div 2 - 1] + a[n div 2]) / 2;
end;

// best known publish time of an article (ms). firstSeenMs <= 0 = unknown -> midday
function Soc_ArticleTimeMs(const publishedDay: string; firstSeenMs: Int64): Int64;
var
  start: Int64;
begin
  start := Soc_DayStartMs(publishedDay);
  if firstSeenMs <= 0 then exit(start + 12 * SOC_HOUR_MS);
  Result := Min(Max(firstSeenMs, start), start + 24 * SOC_HOUR_MS - 1);
end;

// same, ISO in / TDateTime out (createdAtIso may be '')
function Soc_ArticleTime(const publishedDay, createdAtIso: string): TDateTime;
var
  ms: Int64;
begin
  if not Soc_ParseDate(createdAtIso, ms) then ms := 0;
  Result := Soc_FromMs(Soc_ArticleTimeMs(publishedDay, ms));
end;

function Soc_DiscussionBoost(comments, shares, hours: Double): Double;
var
  perHour: Double;
begin
  perHour := (comments + shares) / Max(1, hours);
  Result := Min(SOC_MAX_DISCUSSION_BOOST, 1 + 0.5 * Log10(1 + perHour));
end;

// slugs[i] published at timesMs[i]
function Soc_TriggerBoost(postedAtMs: Int64; const slugs: array of string; const timesMs: array of Int64): Double;
var
  seenA, seenB: TStringList;
  i: Integer;
  t, w: Int64;
begin
  seenA := TStringList.Create; seenB := TStringList.Create;
  t := postedAtMs;
  w := Int64(SOC_TRIGGER_HOURS) * SOC_HOUR_MS;
  for i := 0 to High(slugs) do
  begin
    if (timesMs[i] >= t) and (timesMs[i] < t + w) and (Soc_IndexOfExact(seenA, slugs[i]) < 0) then seenA.Add(slugs[i]);
    if (timesMs[i] >= t - w) and (timesMs[i] < t) and (Soc_IndexOfExact(seenB, slugs[i]) < 0) then seenB.Add(slugs[i]);
  end;
  if (seenA.Count >= SOC_TRIGGER_MIN_PUBLISHERS) and (seenA.Count > seenB.Count) then Result := SOC_TRIGGER_BOOST else Result := 1;
  seenA.Free; seenB.Free;
end;

procedure Soc_SortDesc(var a: TSocDblArr);
var
  i, j: Integer;
  t: Double;
begin
  for i := 1 to High(a) do
  begin
    t := a[i]; j := i - 1;
    while (j >= 0) and (a[j] < t) do begin a[j + 1] := a[j]; Dec(j); end;
    a[j + 1] := t;
  end;
end;

// topic signals. articles: [{sourceSlug,publishedDay,createdAt?,title?,excerpt?}], posts: [{accountId,score,postedAt,metrics?:{comments,shares}}]
// sourceWeights: {slug: weight} (missing = 1, nil ok). returns
// {newsScore,socialScore,velocity,publisherCount,reprintCount,postCount,accountCount,triggerCount}; caller frees
function Soc_TopicSignals(articles, posts: TJSONArray; sourceWeights: TJSONObject; windowDays, halfLifeHours: Double; now: TDateTime): TJSONObject;
var
  nowMs, ms: Int64;
  na, np, i, j, k, o: Integer;
  times: TSocI64Arr;
  slugs: array of string;
  texts, pubs, accs: TStringList;
  grp, original, pubOfArt: TSocIntArr;
  isRep: array of Boolean;
  pubNewest: TSocI64Arr;
  pubRepOnly: array of Boolean;
  vals, best: TSocDblArr;
  newsScore, socialScore, velocity, sw, sum, v, trig, expected: Double;
  reprintCount, triggerCount, recent, unitCount: Integer;
  pTimes, unitTimes: TSocI64Arr;
  pAcc: TSocIntArr;
  pScore: TSocDblArr;
  pt: TSocI64Arr;
  a, p: TJSONData;
  t: Int64;
begin
  nowMs := Soc_Ms(now);
  if articles <> nil then na := articles.Count else na := 0;
  if posts <> nil then np := posts.Count else np := 0;
  SetLength(times, na); SetLength(slugs, na);
  texts := TStringList.Create;
  for i := 0 to na - 1 do
  begin
    a := articles.Items[i];
    slugs[i] := JStr(a, 'sourceSlug');
    if not Soc_ParseDate(JStr(a, 'createdAt'), ms) then ms := 0;
    times[i] := Soc_ArticleTimeMs(JStr(a, 'publishedDay'), ms);
    texts.Add(JStr(a, 'title') + ' ' + JStr(a, 'excerpt'));
  end;
  grp := Soc_NearDupGroups(texts, SOC_DUPLICATE_SIMILARITY);
  texts.Free;
  // original of each group = earliest article (index by group root, -1 = none yet)
  SetLength(original, na);
  for i := 0 to na - 1 do original[i] := -1;
  for i := 0 to na - 1 do
  begin
    o := original[grp[i]];
    if (o = -1) or (times[i] < times[o]) then original[grp[i]] := i;
  end;
  SetLength(isRep, na);
  reprintCount := 0;
  for i := 0 to na - 1 do
  begin
    isRep[i] := slugs[original[grp[i]]] <> slugs[i];
    if isRep[i] then Inc(reprintCount);
  end;
  // publishers
  pubs := TStringList.Create;
  SetLength(pubNewest, na); SetLength(pubRepOnly, na); SetLength(pubOfArt, na);
  for i := 0 to na - 1 do
  begin
    j := Soc_IndexOfExact(pubs, slugs[i]);
    if j < 0 then
    begin
      j := pubs.Add(slugs[i]);
      pubNewest[j] := times[i];
      pubRepOnly[j] := isRep[i];
    end
    else
    begin
      if not (pubNewest[j] > times[i]) then pubNewest[j] := times[i];
      pubRepOnly[j] := pubRepOnly[j] and isRep[i];
    end;
    pubOfArt[i] := j;
  end;
  SetLength(vals, pubs.Count);
  for j := 0 to pubs.Count - 1 do
  begin
    sw := 1;
    if (sourceWeights <> nil) and (sourceWeights.Find(pubs[j]) <> nil) then sw := JNum(sourceWeights, pubs[j], 1);
    v := sw * Soc_Decay((nowMs - pubNewest[j]) / SOC_HOUR_MS, halfLifeHours);
    if pubRepOnly[j] then v := v * SOC_REPRINT_FACTOR;
    vals[j] := v;
  end;
  Soc_SortDesc(vals);
  newsScore := 0;
  for j := 0 to High(vals) do newsScore := newsScore + vals[j] * (1 / Sqrt(j + 1));
  // posts per account (insertion order kept)
  accs := TStringList.Create;
  SetLength(pAcc, np); SetLength(pScore, np); SetLength(pTimes, np);
  for i := 0 to np - 1 do
  begin
    p := posts.Items[i];
    j := Soc_IndexOfExact(accs, JStr(p, 'accountId'));
    if j < 0 then j := accs.Add(JStr(p, 'accountId'));
    pAcc[i] := j;
    pScore[i] := JNum(p, 'score');
    if not Soc_ParseDate(JStr(p, 'postedAt'), pTimes[i]) then pTimes[i] := 0;
  end;
  socialScore := 0;
  triggerCount := 0;
  SetLength(unitTimes, 0);
  for k := 0 to accs.Count - 1 do
  begin
    SetLength(vals, 0);
    SetLength(pt, 0);
    for i := 0 to np - 1 do
      if pAcc[i] = k then
      begin
        p := posts.Items[i];
        trig := Soc_TriggerBoost(pTimes[i], slugs, times);
        if trig > 1 then Inc(triggerCount);
        v := pScore[i] * Soc_Decay((nowMs - pTimes[i]) / SOC_HOUR_MS, halfLifeHours) *
          Soc_DiscussionBoost(JNum(JObj(p, 'metrics'), 'comments'), JNum(JObj(p, 'metrics'), 'shares'), (nowMs - pTimes[i]) / SOC_HOUR_MS) * trig;
        SetLength(vals, Length(vals) + 1);
        vals[High(vals)] := v;
        SetLength(pt, Length(pt) + 1);
        pt[High(pt)] := pTimes[i];
      end;
    Soc_SortDesc(vals);
    SetLength(best, Min(Length(vals), SOC_MAX_POSTS_PER_ACCOUNT));
    for j := 0 to High(best) do best[j] := vals[j];
    sum := 0;
    for j := 0 to High(best) do sum := sum + best[j] * Power(SOC_POST_RANK_FACTOR, j);
    socialScore := socialScore + Sqrt(sum);
    // newest 3 post times (insertion sort desc)
    for i := 1 to High(pt) do
    begin
      t := pt[i]; j := i - 1;
      while (j >= 0) and (pt[j] < t) do begin pt[j + 1] := pt[j]; Dec(j); end;
      pt[j + 1] := t;
    end;
    for j := 0 to Min(Length(pt), SOC_MAX_POSTS_PER_ACCOUNT) - 1 do
    begin
      SetLength(unitTimes, Length(unitTimes) + 1);
      unitTimes[High(unitTimes)] := pt[j];
    end;
  end;
  // units: one per reprint group + post times
  unitCount := Length(unitTimes);
  recent := 0;
  for i := 0 to na - 1 do
    if original[i] >= 0 then
    begin
      Inc(unitCount);
      if (nowMs - times[original[i]]) / SOC_HOUR_MS <= SOC_VELOCITY_HOURS then Inc(recent);
    end;
  for i := 0 to High(unitTimes) do
    if (nowMs - unitTimes[i]) / SOC_HOUR_MS <= SOC_VELOCITY_HOURS then Inc(recent);
  expected := (unitCount * SOC_VELOCITY_HOURS) / (windowDays * 24);
  if unitCount > 0 then velocity := (recent + SOC_VELOCITY_PRIOR) / (expected + SOC_VELOCITY_PRIOR) else velocity := 0;
  Result := TJSONObject.Create;
  Result.Add('newsScore', TJSONFloatNumber.Create(newsScore));
  Result.Add('socialScore', TJSONFloatNumber.Create(socialScore));
  Result.Add('velocity', TJSONFloatNumber.Create(velocity));
  Result.Add('publisherCount', pubs.Count);
  Result.Add('reprintCount', reprintCount);
  Result.Add('postCount', np);
  Result.Add('accountCount', accs.Count);
  Result.Add('triggerCount', triggerCount);
  pubs.Free; accs.Free;
end;

function Soc_LogNormalize(const values: array of Double): TSocDblArr;
var
  pos: TSocDblArr;
  i: Integer;
  ref, top, mx: Double;
begin
  SetLength(Result, Length(values));
  SetLength(pos, 0);
  mx := 0;
  for i := 0 to High(values) do
    if values[i] > 0 then
    begin
      SetLength(pos, Length(pos) + 1);
      pos[High(pos)] := values[i];
      if values[i] > mx then mx := values[i];
    end;
  if Length(pos) = 0 then
  begin
    for i := 0 to High(Result) do Result[i] := 0;
    exit;
  end;
  ref := Soc_Median(pos);
  top := LnXP1(mx / ref);
  for i := 0 to High(values) do
    if values[i] > 0 then Result[i] := LnXP1(values[i] / ref) / top else Result[i] := 0;
end;

// adds "hot" (0-1) to every signals object, in place
procedure Soc_RankHot(signals: TJSONArray; wNews, wSocial, wVelocity: Double);
var
  a, b, c, na, nb, nc: TSocDblArr;
  i: Integer;
  total: Double;
begin
  SetLength(a, signals.Count); SetLength(b, signals.Count); SetLength(c, signals.Count);
  for i := 0 to signals.Count - 1 do
  begin
    a[i] := JNum(signals.Items[i], 'newsScore');
    b[i] := JNum(signals.Items[i], 'socialScore');
    c[i] := JNum(signals.Items[i], 'velocity');
  end;
  na := Soc_LogNormalize(a); nb := Soc_LogNormalize(b); nc := Soc_LogNormalize(c);
  total := wNews + wSocial + wVelocity;
  if total = 0 then total := 1;
  for i := 0 to signals.Count - 1 do
    JSet(TJSONObject(signals.Items[i]), 'hot', TJSONFloatNumber.Create((wNews * na[i] + wSocial * nb[i] + wVelocity * nc[i]) / total));
end;

// weights object {news,social,velocity} (settings.hotWeights); nil = defaults 0.5/0.35/0.15
procedure Soc_RankHotW(signals: TJSONArray; weights: TJSONObject);
begin
  if weights = nil then Soc_RankHot(signals, 0.5, 0.35, 0.15)
  else Soc_RankHot(signals, JNum(weights, 'news', 0.5), JNum(weights, 'social', 0.35), JNum(weights, 'velocity', 0.15));
end;

// ---------- account.model.ts ----------
// splits url into scheme/host/path/query; False when not a url at all
function Soc_SplitUrl(const url: string; out host, path, query: string): Boolean;
var
  r: TRegExpr;
  auth: string;
  p: Integer;
begin
  host := ''; path := ''; query := '';
  r := TRegExpr.Create('^([a-zA-Z][a-zA-Z0-9+.\-]*):(//([^/?#]*))?([^?#]*)(\?([^#]*))?(#.*)?$');
  try
    Result := r.Exec(Trim(url));
    if not Result then exit;
    auth := r.Match[3];
    path := r.Match[4];
    query := r.Match[6];
    p := Pos('@', auth); if p > 0 then auth := Copy(auth, p + 1, MaxInt);
    p := Pos(':', auth); if p > 0 then auth := Copy(auth, 1, p - 1);
    host := LowerCase(auth);
    if ((LowerCase(r.Match[1]) = 'http') or (LowerCase(r.Match[1]) = 'https')) and (host = '') then Result := False;
    if Pos(' ', host) > 0 then Result := False;
  finally
    r.Free;
  end;
end;

function Soc_HandleFromUrl(const url: string): string;
var
  host, path, query, seg: string;
  parts: TStringList;
  i: Integer;
begin
  Result := '';
  if not Soc_SplitUrl(url, host, path, query) then exit;
  parts := TStringList.Create;
  parts.Delimiter := '/'; parts.StrictDelimiter := True; parts.DelimitedText := path;
  seg := '';
  for i := 0 to parts.Count - 1 do
    if parts[i] <> '' then begin seg := parts[i]; break; end;
  parts.Free;
  if seg = 'profile.php' then
  begin
    parts := TStringList.Create;
    parts.Delimiter := '&'; parts.StrictDelimiter := True; parts.DelimitedText := query;
    for i := 0 to parts.Count - 1 do
      if Copy(parts[i], 1, 3) = 'id=' then begin Result := UrlDec(Copy(parts[i], 4, MaxInt)); break; end;
    parts.Free;
    exit;
  end;
  if Copy(seg, 1, 1) = '@' then Delete(seg, 1, 1);
  Result := LowerCase(seg);
end;

function Soc_IsPlatform(const p: string): Boolean;
begin
  Result := (p = 'facebook') or (p = 'x') or (p = 'threads');
end;

function Soc_HostOk(const platform, host: string): Boolean;
begin
  if platform = 'facebook' then Result := (host = 'facebook.com') or (host = 'www.facebook.com') or (host = 'm.facebook.com')
  else if platform = 'x' then Result := (host = 'x.com') or (host = 'www.x.com') or (host = 'twitter.com') or (host = 'www.twitter.com')
  else if platform = 'threads' then Result := (host = 'threads.com') or (host = 'www.threads.com') or (host = 'threads.net') or (host = 'www.threads.net')
  else Result := False;
end;

// ---------- providers/index.ts ----------
function Soc_ProviderIds: string;
begin
  Result := 'twitterapiio,scrapecreators';
end;

function Soc_ProviderLabel(const id: string): string;
begin
  if id = 'twitterapiio' then Result := 'twitterapi.io'
  else if id = 'scrapecreators' then Result := 'ScrapeCreators'
  else Result := '';
end;

function Soc_ProviderEnvKey(const id: string): string;
begin
  if id = 'twitterapiio' then Result := 'TWITTERAPI_IO_KEY'
  else if id = 'scrapecreators' then Result := 'SCRAPECREATORS_KEY'
  else Result := '';
end;

function Soc_Supports(const providerId, platform: string): Boolean;
begin
  Result := ((providerId = 'twitterapiio') and (platform = 'x')) or
    ((providerId = 'scrapecreators') and ((platform = 'facebook') or (platform = 'threads')));
end;

// {facebook:[{id,label}],x:[...],threads:[...]}; caller frees
function Soc_ProviderOptions: TJSONObject;
begin
  Result := TJSONObject(JParse('{"facebook":[{"id":"scrapecreators","label":"ScrapeCreators"}],' +
    '"x":[{"id":"twitterapiio","label":"twitterapi.io"}],"threads":[{"id":"scrapecreators","label":"ScrapeCreators"}]}'));
end;

// ---------- providers/request.ts ----------
function Soc_BackoffMs(attempt: Integer; const retryAfter: string): Integer;
var
  sec: Double;
begin
  if (retryAfter <> '') and TryStrToFloat(retryAfter, sec) and (sec >= 0) then exit(Round(sec * 1000));
  Result := Round(Power(2, attempt) * 1000 + Random * 500);
end;

// application/x-www-form-urlencoded like URLSearchParams
function Soc_FormEnc(const s: string): string;
var
  i: Integer;
begin
  Result := '';
  for i := 1 to Length(s) do
    if s[i] in ['A'..'Z', 'a'..'z', '0'..'9', '-', '_', '.', '*'] then Result := Result + s[i]
    else if s[i] = ' ' then Result := Result + '+'
    else Result := Result + '%' + IntToHex(Ord(s[i]), 2);
end;

function Soc_UrlHost(const url: string): string;
var
  path, query: string;
begin
  Soc_SplitUrl(url, Result, path, query);
end;

// GET json with retries on 429/5xx (max 5, exponential backoff); raises Exception like ProviderError
function Soc_GetJson(const url, headers: string): TJSONData;
var
  attempt, st: Integer;
  body: string;
begin
  Result := nil;
  attempt := 0;
  while True do
  begin
    if Assigned(Soc_HttpHook) then body := Soc_HttpHook(url, headers, st)
    else body := CurlRequest(url, 'GET', '', headers, st, 60);
    if (st >= 200) and (st < 300) then
    begin
      Result := JParse(body);
      if Result = nil then raise Exception.Create('Invalid JSON from ' + Soc_UrlHost(url));
      if not (Result is TJSONObject) then begin Result.Free; Result := TJSONObject.Create; end;
      exit;
    end;
    if st = 0 then raise Exception.Create('fetch failed');
    if not ((st = 429) or (st >= 500)) or (attempt >= 5) then
      raise Exception.Create(Soc_UrlHost(url) + ' ' + IntToStr(st) + ': ' + Copy(body, 1, 200));
    Sleep(Soc_BackoffMs(attempt, ''));
    Inc(attempt);
  end;
end;

// ---------- providers/types.ts field readers ----------
function Soc_ValStr(v: TJSONData): string;
begin
  if v = nil then exit('');
  case v.JSONType of
    jtString: Result := v.AsString;
    jtNumber:
      if (v is TJSONIntegerNumber) or (v is TJSONInt64Number) or (v is TJSONQWordNumber) then Result := v.AsString
      else if Frac(v.AsFloat) = 0 then Result := FormatFloat('0', v.AsFloat)
      else Result := FloatToStr(v.AsFloat);
    jtBoolean: if v.AsBoolean then Result := 'true' else Result := 'false';
    jtObject: Result := '[object Object]';
  else
    Result := '';
  end;
end;

// first(item, paths): first value not undefined/null/''
function Soc_First(item: TJSONData; const paths: array of string): TJSONData;
var
  i: Integer;
  v: TJSONData;
begin
  Result := nil;
  for i := 0 to High(paths) do
  begin
    v := JPath(item, paths[i]);
    if (v = nil) or (v.JSONType = jtNull) then continue;
    if (v.JSONType = jtString) and (v.AsString = '') then continue;
    exit(v);
  end;
end;

function Soc_Str(item: TJSONData; const paths: array of string): string;
begin
  Result := Soc_ValStr(Soc_First(item, paths));
end;

function Soc_NumV(v: TJSONData): Double;
begin
  Result := 0;
  if v = nil then exit;
  case v.JSONType of
    jtNumber: Result := v.AsFloat;
    jtString: if Trim(v.AsString) = '' then Result := 0 else Result := StrToFloatDef(Trim(v.AsString), 0);
    jtBoolean: if v.AsBoolean then Result := 1;
  end;
end;

// num(): sum of the numbers at the paths
function Soc_Num(item: TJSONData; const paths: array of string): Double;
var
  i: Integer;
begin
  Result := 0;
  for i := 0 to High(paths) do Result := Result + Soc_NumV(JPath(item, paths[i]));
end;

// optionalNum(): >0 number or -1 for null
function Soc_OptNum(item: TJSONData; const paths: array of string): Double;
var
  v: TJSONData;
begin
  v := Soc_First(item, paths);
  Result := -1;
  if v = nil then exit;
  if (v.JSONType = jtNumber) or ((v.JSONType = jtString) and (StrToFloatDef(Trim(v.AsString), -1) > 0)) or (v.JSONType = jtBoolean) then
    if Soc_NumV(v) > 0 then Result := Soc_NumV(v);
end;

// builds a NormalizedPost json or nil when not usable
function Soc_MakePost(const externalId, url, text, image: string; dateOk: Boolean; postedMs: Int64;
  likes, comments, shares, views: Double; const authorName, authorHandle: string; followers: Double): TJSONObject;
var
  m: TJSONObject;
begin
  Result := nil;
  if (externalId = '') or (url = '') or not dateOk then exit;
  Result := TJSONObject.Create;
  Result.Add('externalId', externalId);
  Result.Add('url', url);
  Result.Add('text', text);
  Result.Add('image', image);
  Result.Add('postedAt', Soc_IsoMs(postedMs));
  m := TJSONObject.Create;
  JSetN(m, 'likes', likes); JSetN(m, 'comments', comments); JSetN(m, 'shares', shares); JSetN(m, 'views', views);
  Result.Add('metrics', m);
  Result.Add('authorName', authorName);
  Result.Add('authorHandle', authorHandle);
  if followers > 0 then JSetN(Result, 'followers', followers) else Result.Add('followers', TJSONNull.Create);
end;

function Soc_PostMs(post: TJSONData): Int64;
begin
  Result := Soc_DocMs(post, 'postedAt');
end;

// ---------- providers/scrapecreators.ts ----------
function Soc_NormalizeFacebookPost(item: TJSONData): TJSONObject;
var
  ms: Int64;
  ok: Boolean;
begin
  ok := Soc_ToDate(JPath(item, 'publishTime'), ms);
  Result := Soc_MakePost(Soc_Str(item, ['id', 'post_id']), Soc_Str(item, ['url', 'permalink']), Soc_Str(item, ['text']),
    Soc_Str(item, ['image', 'videoDetails.thumbnailUrl']), ok, ms,
    Soc_Num(item, ['reactionCount']), Soc_Num(item, ['commentCount']), Soc_Num(item, ['shareCount']), Soc_Num(item, ['videoViewCount']),
    Soc_Str(item, ['author.name']), '', -1);
end;

function Soc_NormalizeThreadsPost(item: TJSONData): TJSONObject;
var
  ms: Int64;
  ok: Boolean;
  handle, code, url: string;
begin
  handle := LowerCase(Soc_Str(item, ['user.username']));
  code := Soc_Str(item, ['code']);
  url := Soc_Str(item, ['url']);
  if (url = '') and (code <> '') and (handle <> '') then url := 'https://www.threads.com/@' + handle + '/post/' + code;
  ok := Soc_ToDate(JPath(item, 'taken_at'), ms);
  Result := Soc_MakePost(Soc_Str(item, ['pk', 'id', 'code']), url, Soc_Str(item, ['caption.text']),
    Soc_Str(item, ['image_versions2.candidates.0.url']), ok, ms,
    Soc_Num(item, ['like_count']), Soc_Num(item, ['text_post_app_info.direct_reply_count']),
    Soc_Num(item, ['text_post_app_info.repost_count', 'text_post_app_info.quote_count']), 0,
    Soc_Str(item, ['user.full_name']), handle, Soc_OptNum(item, ['user.follower_count']));
end;

function Soc_NewFetchResult: TJSONObject;
begin
  Result := TJSONObject(JParse('{"posts":[],"pageIds":{},"errors":[],"usage":{"requests":0,"credits":0,"costUsd":0}}'));
end;

// set a field keeping its position (JSet moves it to the end)
procedure Soc_Put(o: TJSONObject; const name: string; v: TJSONData);
var
  i: Integer;
begin
  i := o.IndexOfName(name);
  if i >= 0 then o.Items[i] := v else o.Add(name, v);
end;

procedure Soc_PutN(o: TJSONObject; const name: string; v: Double);
begin
  if Frac(v) = 0 then Soc_Put(o, name, TJSONInt64Number.Create(Trunc(v))) else Soc_Put(o, name, TJSONFloatNumber.Create(v));
end;

procedure Soc_PutS(o: TJSONObject; const name, v: string);
begin
  Soc_Put(o, name, TJSONString.Create(v));
end;

procedure Soc_AddUsage(res: TJSONObject; requests, credits, costUsd: Double);
var
  u: TJSONObject;
begin
  u := JObj(res, 'usage');
  Soc_PutN(u, 'requests', JNum(u, 'requests') + requests);
  Soc_PutN(u, 'credits', JNum(u, 'credits') + credits);
  Soc_Put(u, 'costUsd', TJSONFloatNumber.Create(JNum(u, 'costUsd') + costUsd));
end;

// one ScrapeCreators request; usage added to res; caller frees page
function Soc_ScRequest(const path, params: string; res: TJSONObject; creditUsd: Double): TJSONData;
var
  credits: Double;
begin
  Result := Soc_GetJson(SOC_SC_API + path + '?' + params, 'x-api-key: ' + Env('SCRAPECREATORS_KEY'));
  if (JPath(Result, 'credits_charged') <> nil) then credits := JNum(Result, 'credits_charged') else credits := 1;
  Soc_AddUsage(res, 1, credits, credits * creditUsd);
end;

// target: {account:{id,platform,handle,url,name}, since: ISO, pageId?, lastPostId?}
procedure Soc_ScFacebook(target: TJSONObject; maxPosts: Integer; creditUsd: Double; res: TJSONObject);
var
  acc: TJSONObject;
  maxPages, n, i: Integer;
  cursor, params, pageId, pid, lastId: string;
  page: TJSONData;
  items: TJSONArray;
  post: TJSONObject;
  sinceMs, lastMs: Int64;
  posts: TJSONArray;
  reachedSeen, hasLast: Boolean;
begin
  acc := JObj(target, 'account');
  if not Soc_ParseDate(JStr(target, 'since'), sinceMs) then sinceMs := 0;
  maxPages := Ceil(maxPosts / SOC_FB_POSTS_PER_PAGE);
  lastId := JStr(target, 'lastPostId');
  cursor := '';
  for n := 0 to maxPages - 1 do
  begin
    if JStr(target, 'pageId') <> '' then params := 'pageId=' + Soc_FormEnc(JStr(target, 'pageId'))
    else params := 'url=' + Soc_FormEnc(JStr(acc, 'url'));
    if cursor <> '' then params := params + '&cursor=' + Soc_FormEnc(cursor);
    page := Soc_ScRequest('/facebook/profile/posts', params, res, creditUsd);
    try
      items := JArr(page, 'posts');
      pageId := '';
      if items <> nil then
        for i := 0 to items.Count - 1 do
        begin
          pid := Soc_Str(items.Items[i], ['author.id']);
          if pid <> '' then begin pageId := pid; break; end;
        end;
      if (pageId <> '') and (JStr(target, 'pageId') = '') then JSetS(JObj(res, 'pageIds'), JStr(acc, 'id'), pageId);
      posts := TJSONArray.Create;
      if items <> nil then
        for i := 0 to items.Count - 1 do
        begin
          post := Soc_NormalizeFacebookPost(items.Items[i]);
          if post <> nil then posts.Add(post);
        end;
      reachedSeen := False;
      for i := 0 to posts.Count - 1 do
      begin
        post := TJSONObject(posts.Items[i]);
        if Soc_PostMs(post) >= sinceMs then
        begin
          post := TJSONObject(post.Clone);
          JSetS(post, 'accountId', JStr(acc, 'id'));
          JSetS(post, 'authorHandle', JStr(acc, 'handle'));
          JArr(res, 'posts').Add(post);
        end;
        if (lastId <> '') and (JStr(posts.Items[i], 'externalId') = lastId) then reachedSeen := True;
      end;
      hasLast := posts.Count > 0;
      lastMs := 0;
      if hasLast then lastMs := Soc_PostMs(posts.Items[posts.Count - 1]);
      posts.Free;
      if (JStr(page, 'cursor') = '') or not hasLast or (lastMs < sinceMs) or reachedSeen then exit;
      cursor := JStr(page, 'cursor');
    finally
      page.Free;
    end;
  end;
end;

procedure Soc_ScThreads(target: TJSONObject; creditUsd: Double; res: TJSONObject);
var
  page: TJSONData;
  items: TJSONArray;
  i: Integer;
  post: TJSONObject;
  sinceMs: Int64;
begin
  if not Soc_ParseDate(JStr(target, 'since'), sinceMs) then sinceMs := 0;
  page := Soc_ScRequest('/threads/user/posts', 'handle=' + Soc_FormEnc(JStr(JObj(target, 'account'), 'handle')), res, creditUsd);
  try
    items := JArr(page, 'posts');
    if items <> nil then
      for i := 0 to items.Count - 1 do
      begin
        post := Soc_NormalizeThreadsPost(items.Items[i]);
        if post = nil then continue;
        if Soc_PostMs(post) >= sinceMs then
        begin
          JSetS(post, 'accountId', JStr(JObj(target, 'account'), 'id'));
          JArr(res, 'posts').Add(post);
        end
        else post.Free;
      end;
  finally
    page.Free;
  end;
end;

// scrapecreators.fetch. returns FetchResult {posts,pageIds,errors,usage}; caller frees
function Soc_ScrapeCreatorsFetch(const platform: string; targets: TJSONArray; maxPostsPerAccount: Integer; creditUsd: Double): TJSONObject;
var
  i: Integer;
  t: TJSONObject;
begin
  Result := Soc_NewFetchResult;
  for i := 0 to targets.Count - 1 do
  begin
    t := TJSONObject(targets.Items[i]);
    try
      if platform = 'facebook' then Soc_ScFacebook(t, maxPostsPerAccount, creditUsd, Result)
      else Soc_ScThreads(t, creditUsd, Result);
    except
      on E: Exception do JArr(Result, 'errors').Add(JStr(JObj(t, 'account'), 'handle') + ': ' + E.Message);
    end;
  end;
end;

// ---------- providers/twitterapiio.ts ----------
function Soc_NormalizeTweet(item: TJSONData): TJSONObject;
var
  ms: Int64;
  ok: Boolean;
  rt: TJSONData;
begin
  Result := nil;
  rt := JPath(item, 'retweeted_tweet');
  if (rt <> nil) and not ((rt.JSONType = jtBoolean) and not rt.AsBoolean) and not ((rt.JSONType = jtString) and (rt.AsString = '')) and
    not ((rt.JSONType = jtNumber) and (rt.AsFloat = 0)) then exit;
  if Copy(Soc_Str(item, ['text']), 1, 4) = 'RT @' then exit;
  ok := Soc_ToDate(JPath(item, 'createdAt'), ms);
  Result := Soc_MakePost(Soc_Str(item, ['id']), Soc_Str(item, ['url', 'twitterUrl']), Soc_Str(item, ['text']),
    Soc_Str(item, ['extendedEntities.media.0.media_url_https', 'entities.media.0.media_url_https']), ok, ms,
    Soc_Num(item, ['likeCount']), Soc_Num(item, ['replyCount']), Soc_Num(item, ['retweetCount', 'quoteCount']), Soc_Num(item, ['viewCount']),
    Soc_Str(item, ['author.name']), LowerCase(Soc_Str(item, ['author.userName'])), Soc_OptNum(item, ['author.followers']));
end;

// "(from:a OR from:b) since_time:<unix>" from the earliest since
function Soc_SearchQuery(targets: TJSONArray): string;
var
  i: Integer;
  ms, mn: Int64;
  from: string;
begin
  mn := High(Int64);
  from := '';
  for i := 0 to targets.Count - 1 do
  begin
    if not Soc_ParseDate(JStr(targets.Items[i], 'since'), ms) then ms := 0;
    if ms < mn then mn := ms;
    if from <> '' then from := from + ' OR ';
    from := from + 'from:' + JStr(JObj(targets.Items[i], 'account'), 'handle');
  end;
  if targets.Count = 0 then mn := 0;
  if mn >= 0 then mn := mn div 1000 else mn := -((-mn + 999) div 1000);
  Result := '(' + from + ') since_time:' + IntToStr(mn);
end;

procedure Soc_TwSearch(const key: string; batch: TJSONArray; maxPages: Integer; res: TJSONObject);
var
  query, cursor: string;
  pg, i: Integer;
  data: TJSONData;
  tweets: TJSONArray;
  credits: Double;
  post: TJSONObject;
  cnt: Integer;
  hasNext: Boolean;
begin
  query := Soc_SearchQuery(batch);
  cursor := '';
  for pg := 0 to maxPages - 1 do
  begin
    data := Soc_GetJson(SOC_TW_API + '?query=' + Soc_FormEnc(query) + '&queryType=Latest&cursor=' + Soc_FormEnc(cursor), 'X-API-Key: ' + key);
    try
      tweets := JArr(data, 'tweets');
      if tweets <> nil then cnt := tweets.Count else cnt := 0;
      credits := Max(cnt, 1) * SOC_TW_CREDITS_PER_TWEET;
      Soc_AddUsage(res, 1, credits, credits * SOC_TW_USD_PER_CREDIT);
      for i := 0 to cnt - 1 do
      begin
        post := Soc_NormalizeTweet(tweets.Items[i]);
        if post <> nil then JArr(res, 'posts').Add(post);
      end;
      hasNext := JBool(data, 'has_next_page');
      if not hasNext or (JStr(data, 'next_cursor') = '') or (cnt = 0) then exit;
      cursor := JStr(data, 'next_cursor');
    finally
      data.Free;
    end;
  end;
end;

function Soc_TwitterApiIoFetch(targets: TJSONArray; maxPostsPerAccount: Integer): TJSONObject;
var
  sorted, batch: TJSONArray;
  i, j: Integer;
  key, hs: string;
  tmp: TJSONData;
begin
  key := Env('TWITTERAPI_IO_KEY');
  Result := Soc_NewFetchResult;
  sorted := TJSONArray(targets.Clone);
  // stable insertion sort by since
  for i := 1 to sorted.Count - 1 do
  begin
    j := i;
    while (j > 0) and (Soc_DocMs(sorted.Items[j - 1], 'since') > Soc_DocMs(sorted.Items[j], 'since')) do
    begin
      sorted.Exchange(j - 1, j);
      Dec(j);
    end;
  end;
  i := 0;
  while i < sorted.Count do
  begin
    batch := TJSONArray.Create;
    for j := i to Min(i + SOC_TW_HANDLES_PER_QUERY, sorted.Count) - 1 do batch.Add(sorted.Items[j].Clone);
    try
      Soc_TwSearch(key, batch, Ceil((maxPostsPerAccount * batch.Count) / SOC_TW_TWEETS_PER_PAGE), Result);
    except
      on E: Exception do
      begin
        hs := '';
        for j := 0 to batch.Count - 1 do
        begin
          if hs <> '' then hs := hs + ', ';
          hs := hs + JStr(JObj(batch.Items[j], 'account'), 'handle');
        end;
        JArr(Result, 'errors').Add(hs + ': ' + E.Message);
      end;
    end;
    batch.Free;
    Inc(i, SOC_TW_HANDLES_PER_QUERY);
  end;
  tmp := sorted; tmp.Free;
end;

// ---------- collect.ts ----------
// fetchPlatformPosts: social = settings.social. returns FetchResult + "provider"; raises on unknown provider / missing key
function Soc_FetchPlatformPosts(const platform: string; targets: TJSONArray; social: TJSONObject; postsPerAccountOverride: Integer = 0): TJSONObject;
var
  prov: string;
  ppa: Integer;
  credit: Double;
begin
  prov := JStr(JObj(social, 'providers'), platform);
  if Soc_ProviderLabel(prov) = '' then raise Exception.Create('Unknown provider "' + prov + '" for ' + platform);
  if Env(Soc_ProviderEnvKey(prov)) = '' then raise Exception.Create(Soc_ProviderEnvKey(prov) + ' is not set');
  ppa := JInt(social, 'postsPerAccount', 20);
  if postsPerAccountOverride > 0 then ppa := postsPerAccountOverride;
  credit := JNum(social, 'creditUsd', SOC_DEFAULT_CREDIT_USD);
  if prov = 'twitterapiio' then Result := Soc_TwitterApiIoFetch(targets, ppa)
  else Result := Soc_ScrapeCreatorsFetch(platform, targets, ppa, credit);
  Result.Add('provider', prov);
end;

// socialSince: ms. lastPostMs/lastCollectedMs 0 = unknown
function Soc_SinceMs(const windowStart: string; lastPostMs, lastCollectedMs: Int64; overlapHours: Double): Int64;
var
  start, seen, since: Int64;
begin
  start := Soc_DayStartMs(windowStart);
  seen := Max(lastPostMs, lastCollectedMs);
  if seen <= 0 then exit(start);
  since := seen - Round(overlapHours * SOC_HOUR_MS);
  if since > start then Result := since else Result := start;
end;

// rescore all posts of an account inside the loaded posts array (mongo version did it in the db)
procedure Soc_RescoreAccount(const accountId: string; posts: TJSONArray);
var
  acc, patch: TJSONObject;
  idx: TSocIntArr;
  i, j, t: Integer;
  engs: TSocDblArr;
  baseline: Double;
begin
  acc := DbGet('socialaccounts', accountId);
  if acc = nil then exit;
  SetLength(idx, 0);
  for i := 0 to posts.Count - 1 do
    if JStr(posts.Items[i], 'accountId') = accountId then
    begin
      SetLength(idx, Length(idx) + 1);
      idx[High(idx)] := i;
    end;
  // newest first
  for i := 1 to High(idx) do
  begin
    t := idx[i]; j := i - 1;
    while (j >= 0) and (Soc_PostMs(posts.Items[idx[j]]) < Soc_PostMs(posts.Items[t])) do begin idx[j + 1] := idx[j]; Dec(j); end;
    idx[j + 1] := t;
  end;
  SetLength(engs, Min(Length(idx), SOC_BASELINE_POSTS));
  for i := 0 to High(engs) do engs[i] := JNum(posts.Items[idx[i]], 'engagement');
  baseline := Soc_Median(engs);
  patch := TJSONObject.Create;
  JSetN(patch, 'baseline', baseline);
  DbUpdate('socialaccounts', accountId, patch);
  patch.Free;
  for i := 0 to Min(Length(idx), 500) - 1 do
    Soc_Put(TJSONObject(posts.Items[idx[i]]), 'score', TJSONFloatNumber.Create(
      Soc_PostScore(JNum(posts.Items[idx[i]], 'engagement'), JNum(acc, 'followers'), JNum(acc, 'weight', 1), baseline)));
  acc.Free;
end;

// savePosts: returns {posts,newPosts,unmatched}
function Soc_SavePosts(const platform: string; fetched, group: TJSONArray; const winStart, winEnd: string): TJSONObject;
var
  all: TJSONArray;
  i, j, k, cPosts, cNew, cUn: Integer;
  post, acc, doc, author, patch, cur: TJSONObject;
  day, accId, extId: string;
  ms: Int64;
  newestIds: TStringList;
  newestMs: TSocI64Arr;
  newestExt: array of string;
  fol: Double;
begin
  cPosts := 0; cNew := 0; cUn := 0;
  all := DbLoad('socialposts');
  newestIds := TStringList.Create;
  SetLength(newestMs, 0); SetLength(newestExt, 0);
  for i := 0 to fetched.Count - 1 do
  begin
    post := TJSONObject(fetched.Items[i]);
    ms := Soc_PostMs(post);
    day := ToDay(Soc_FromMs(ms));
    if (day < winStart) or (day > winEnd) then continue;
    // matchAccount
    acc := nil;
    for j := 0 to group.Count - 1 do
      if (JStr(post, 'accountId') <> '') and (JStr(post, 'accountId') = JStr(group.Items[j], 'id')) then begin acc := TJSONObject(group.Items[j]); break; end;
    if acc = nil then
      for j := 0 to group.Count - 1 do
        if (JStr(post, 'authorHandle') <> '') and (JStr(post, 'authorHandle') = JStr(group.Items[j], 'handle')) then begin acc := TJSONObject(group.Items[j]); break; end;
    if (acc = nil) and (group.Count = 1) then acc := TJSONObject(group.Items[0]);
    if acc = nil then begin Inc(cUn); continue; end;
    accId := JStr(acc, 'id');
    extId := JStr(post, 'externalId');
    // upsert by platform + externalId
    doc := nil;
    for j := 0 to all.Count - 1 do
      if (JStr(all.Items[j], 'platform') = platform) and (JStr(all.Items[j], 'externalId') = extId) then begin doc := TJSONObject(all.Items[j]); break; end;
    if doc = nil then
    begin
      doc := TJSONObject.Create;
      doc.Add('_id', NewId);
      doc.Add('platform', platform);
      doc.Add('externalId', extId);
      doc.Add('accountId', ''); doc.Add('author', TJSONObject.Create); doc.Add('url', ''); doc.Add('text', ''); doc.Add('image', '');
      doc.Add('postedAt', ''); doc.Add('publishedDay', ''); doc.Add('metrics', TJSONObject.Create); doc.Add('engagement', 0);
      doc.Add('score', 0); doc.Add('fetchedAt', ''); doc.Add('createdAt', NowIso); doc.Add('updatedAt', '');
      all.Add(doc);
      Inc(cNew);
    end;
    Soc_PutS(doc, 'accountId', accId);
    author := TJSONObject.Create;
    if JStr(post, 'authorName') <> '' then author.Add('name', JStr(post, 'authorName')) else author.Add('name', JStr(acc, 'name'));
    if JStr(post, 'authorHandle') <> '' then author.Add('handle', JStr(post, 'authorHandle')) else author.Add('handle', JStr(acc, 'handle'));
    fol := JNum(post, 'followers', 0);
    Soc_PutN(author, 'followers', fol);
    Soc_Put(doc, 'author', author);
    Soc_PutS(doc, 'url', JStr(post, 'url'));
    Soc_PutS(doc, 'text', JStr(post, 'text'));
    Soc_PutS(doc, 'image', JStr(post, 'image'));
    Soc_PutS(doc, 'postedAt', Soc_IsoMs(ms));
    Soc_PutS(doc, 'publishedDay', day);
    Soc_Put(doc, 'metrics', JObj(post, 'metrics').Clone);
    Soc_PutN(doc, 'engagement', Soc_EngagementOf(JObj(post, 'metrics')));
    Soc_PutS(doc, 'fetchedAt', NowIso);
    Soc_PutS(doc, 'updatedAt', NowIso);
    if fol > 0 then
    begin
      patch := TJSONObject.Create;
      Soc_PutN(patch, 'followers', fol);
      DbUpdate('socialaccounts', accId, patch);
      patch.Free;
    end;
    k := newestIds.IndexOf(accId);
    if k < 0 then
    begin
      k := newestIds.Add(accId);
      SetLength(newestMs, k + 1); SetLength(newestExt, k + 1);
      newestMs[k] := ms; newestExt[k] := extId;
    end
    else if ms > newestMs[k] then
    begin
      newestMs[k] := ms; newestExt[k] := extId;
    end;
    Inc(cPosts);
  end;
  for k := 0 to newestIds.Count - 1 do
  begin
    cur := DbGet('socialaccounts', newestIds[k]);
    if (cur <> nil) and ((JStr(cur, 'lastPostAt') = '') or (Soc_DocMs(cur, 'lastPostAt') < newestMs[k])) then
    begin
      patch := TJSONObject.Create;
      Soc_PutS(patch, 'lastPostAt', Soc_IsoMs(newestMs[k]));
      Soc_PutS(patch, 'lastPostId', newestExt[k]);
      DbUpdate('socialaccounts', newestIds[k], patch);
      patch.Free;
    end;
    cur.Free;
    Soc_RescoreAccount(newestIds[k], all);
  end;
  if (cPosts > 0) or (cNew > 0) then DbSave('socialposts', all);
  all.Free;
  newestIds.Free;
  Result := TJSONObject.Create;
  Result.Add('posts', cPosts);
  Result.Add('newPosts', cNew);
  Result.Add('unmatched', cUn);
end;

procedure Soc_LogTo(logProc: TSocLogProc; const msg: string);
begin
  if Assigned(logProc) then logProc(msg) else Log(msg);
end;

// collectSocial. accounts: [{id,platform,handle,url,name}] (AccountConfig, "_id" accepted too), window = YYYY-MM-DD..YYYY-MM-DD,
// social = settings.social. returns SocialReport[]:
// [{platform,provider,accounts,posts,newPosts,unmatched,failedAccounts,requests,credits,costUsd,status,error?}]; caller frees
function Soc_Collect(accounts: TJSONArray; const winStart, winEnd: string; social: TJSONObject; logProc: TSocLogProc = nil): TJSONArray;
const
  PL: array[0..2] of string = ('facebook', 'x', 'threads');
var
  pIdx, i: Integer;
  platform, msg, startedAt, err0: string;
  group, targets: TJSONArray;
  report, t, a, st, fr, counts, u, patch: TJSONObject;
  overlap: Double;
  sinceMs: Int64;
  nErr, nPosts: Integer;
begin
  Result := TJSONArray.Create;
  overlap := JNum(social, 'overlapHours', 1);
  for pIdx := 0 to 2 do
  begin
    platform := PL[pIdx];
    group := TJSONArray.Create;
    for i := 0 to accounts.Count - 1 do
      if JStr(accounts.Items[i], 'platform') = platform then
      begin
        a := TJSONObject(accounts.Items[i].Clone);
        if JStr(a, 'id') = '' then Soc_PutS(a, 'id', JStr(a, '_id'));
        group.Add(a);
      end;
    if group.Count = 0 then begin group.Free; continue; end;
    report := TJSONObject.Create;
    report.Add('platform', platform);
    report.Add('provider', JStr(JObj(social, 'providers'), platform));
    report.Add('accounts', group.Count);
    report.Add('posts', 0);
    report.Add('newPosts', 0);
    report.Add('unmatched', 0);
    report.Add('failedAccounts', 0);
    report.Add('requests', 0);
    report.Add('credits', 0);
    report.Add('costUsd', 0);
    report.Add('status', 'ok');
    fr := nil;
    try
      try
        targets := TJSONArray.Create;
        for i := 0 to group.Count - 1 do
        begin
          a := TJSONObject(group.Items[i]);
          st := DbGet('socialaccounts', JStr(a, 'id'));
          t := TJSONObject.Create;
          t.Add('account', a.Clone);
          sinceMs := Soc_SinceMs(winStart, Soc_DocMs(st, 'lastPostAt'), Soc_DocMs(st, 'lastCollectedAt'), overlap);
          t.Add('since', Soc_IsoMs(sinceMs));
          if JStr(st, 'pageId') <> '' then t.Add('pageId', JStr(st, 'pageId'));
          if JStr(st, 'lastPostId') <> '' then t.Add('lastPostId', JStr(st, 'lastPostId'));
          targets.Add(t);
          st.Free;
        end;
        startedAt := NowIso;
        try
          fr := Soc_FetchPlatformPosts(platform, targets, social);
        finally
          targets.Free;
        end;
        u := JObj(fr, 'usage');
        Soc_PutN(report, 'requests', JNum(u, 'requests'));
        Soc_PutN(report, 'credits', JNum(u, 'credits'));
        Soc_Put(report, 'costUsd', TJSONFloatNumber.Create(JNum(u, 'costUsd')));
        counts := Soc_SavePosts(platform, JArr(fr, 'posts'), group, winStart, winEnd);
        Soc_PutN(report, 'posts', JNum(counts, 'posts'));
        Soc_PutN(report, 'newPosts', JNum(counts, 'newPosts'));
        Soc_PutN(report, 'unmatched', JNum(counts, 'unmatched'));
        counts.Free;
        for i := 0 to JObj(fr, 'pageIds').Count - 1 do
        begin
          patch := TJSONObject.Create;
          Soc_PutS(patch, 'pageId', JObj(fr, 'pageIds').Items[i].AsString);
          DbUpdate('socialaccounts', JObj(fr, 'pageIds').Names[i], patch);
          patch.Free;
        end;
        for i := 0 to group.Count - 1 do
        begin
          patch := TJSONObject.Create;
          Soc_PutS(patch, 'lastCollectedAt', startedAt);
          DbUpdate('socialaccounts', JStr(group.Items[i], 'id'), patch);
          patch.Free;
        end;
        nErr := JArr(fr, 'errors').Count;
        Soc_PutN(report, 'failedAccounts', nErr);
        if nErr > 0 then
        begin
          nPosts := JInt(report, 'posts');
          if (nErr >= group.Count) and (nPosts = 0) then Soc_PutS(report, 'status', 'failed') else Soc_PutS(report, 'status', 'partial');
          err0 := JArr(fr, 'errors').Items[0].AsString;
          Soc_PutS(report, 'error', Soc_JsSlice(IntToStr(nErr) + ' failed, e.g. ' + err0, 300));
        end;
      except
        on E: Exception do
        begin
          msg := E.Message;
          if Copy(msg, Length(msg) - 9, 10) = 'is not set' then Soc_PutS(report, 'status', 'skipped') else Soc_PutS(report, 'status', 'failed');
          Soc_PutS(report, 'error', Soc_JsSlice(msg, 300));
        end;
      end;
    finally
      fr.Free;
    end;
    msg := 'Social ' + platform + ' via ' + JStr(report, 'provider') + ': ' + JStr(report, 'newPosts') + ' new / ' + JStr(report, 'posts') +
      ' posts from ' + IntToStr(group.Count) + ' accounts, ' + JStr(report, 'requests') + ' requests, ' + Soc_ValStr(report.Find('credits')) +
      ' credits, $' + FormatFloat('0.0000', JNum(report, 'costUsd'));
    if JStr(report, 'error') <> '' then msg := msg + ' ' + SOC_DASH + ' ' + JStr(report, 'status') + ': ' + JStr(report, 'error');
    Soc_LogTo(logProc, msg);
    Result.Add(report);
    group.Free;
  end;
end;

// sums requests/credits/costUsd of reports -> {requests,credits,costUsd}; caller frees
function Soc_UsageOf(reports: TJSONArray): TJSONObject;
var
  i: Integer;
  r, c, u: Double;
begin
  r := 0; c := 0; u := 0;
  for i := 0 to reports.Count - 1 do
  begin
    r := r + JNum(reports.Items[i], 'requests');
    c := c + JNum(reports.Items[i], 'credits');
    u := u + JNum(reports.Items[i], 'costUsd');
  end;
  Result := TJSONObject.Create;
  JSetN(Result, 'requests', r);
  JSetN(Result, 'credits', c);
  Result.Add('costUsd', TJSONFloatNumber.Create(u));
end;

// enabled accounts as AccountConfig [{id,platform,handle,url,name}]; caller frees
function Soc_EnabledAccountConfigs: TJSONArray;
var
  all: TJSONArray;
  i: Integer;
  o, a: TJSONObject;
begin
  all := DbLoad('socialaccounts');
  Result := TJSONArray.Create;
  for i := 0 to all.Count - 1 do
  begin
    a := TJSONObject(all.Items[i]);
    if not JBool(a, 'enabled', True) then continue;
    o := TJSONObject.Create;
    o.Add('id', JStr(a, '_id'));
    o.Add('platform', JStr(a, 'platform'));
    o.Add('handle', JStr(a, 'handle'));
    o.Add('url', JStr(a, 'url'));
    o.Add('name', JStr(a, 'name'));
    Result.Add(o);
  end;
  all.Free;
end;

// ---------- defaults.ts ----------
// DEFAULT_ACCOUNTS as json [{platform,url,name,kind,weight,followers?,handle}]; caller frees
function Soc_DefaultAccounts: TJSONArray;
var
  i: Integer;
  parts: TStringList;
  o: TJSONObject;
begin
  Result := TJSONArray.Create;
  parts := TStringList.Create;
  parts.Delimiter := '|'; parts.StrictDelimiter := True;
  for i := 0 to High(SOC_DEFAULT_ACCOUNTS) do
  begin
    parts.DelimitedText := SOC_DEFAULT_ACCOUNTS[i];
    o := TJSONObject.Create;
    o.Add('platform', parts[0]);
    o.Add('url', parts[1]);
    o.Add('name', parts[2]);
    o.Add('kind', parts[3]);
    JSetN(o, 'weight', StrToFloat(parts[4]));
    if parts[5] <> '0' then JSetN(o, 'followers', StrToFloat(parts[5]));
    o.Add('handle', Soc_HandleFromUrl(parts[1]));
    Result.Add(o);
  end;
  parts.Free;
end;

// seed.ts part: inserts default accounts missing by (platform, handle); returns how many were inserted
function Soc_SeedAccounts: Integer;
var
  defs, existing: TJSONArray;
  i, j: Integer;
  d, doc: TJSONObject;
  found: Boolean;
begin
  Result := 0;
  defs := Soc_DefaultAccounts;
  for i := 0 to defs.Count - 1 do
  begin
    d := TJSONObject(defs.Items[i]);
    existing := DbFind('socialaccounts', 'platform', JStr(d, 'platform'));
    found := False;
    for j := 0 to existing.Count - 1 do
      if JStr(existing.Items[j], 'handle') = JStr(d, 'handle') then found := True;
    existing.Free;
    if found then continue;
    doc := TJSONObject.Create;
    doc.Add('platform', JStr(d, 'platform'));
    doc.Add('handle', JStr(d, 'handle'));
    doc.Add('url', JStr(d, 'url'));
    doc.Add('name', JStr(d, 'name'));
    doc.Add('kind', JStr(d, 'kind'));
    JSetN(doc, 'weight', JNum(d, 'weight', 1));
    JSetN(doc, 'followers', JNum(d, 'followers', 0));
    doc.Add('baseline', 0);
    doc.Add('enabled', True);
    DbInsert('socialaccounts', doc);
    doc.Free;
    Inc(Result);
  end;
  defs.Free;
end;

// ---------- scripts/collect-social.ts ----------
// returns process exit code (1 when a platform failed)
function Soc_CollectSocialScript: Integer;
var
  settings, social: TJSONObject;
  accounts, reports: TJSONArray;
  ws, we: string;
  i, j: Integer;
  ln: string;
begin
  settings := GetSettings;
  social := JObj(settings, 'social');
  accounts := Soc_EnabledAccountConfigs;
  we := TodayMn;
  ws := AddDays(we, -(JInt(settings, 'windowDays', 14) - 1));
  WriteLn('Collecting ', accounts.Count, ' accounts, ', ws, ' ' + SOC_ARROW + ' ', we);
  reports := Soc_Collect(accounts, ws, we, social, nil);
  Result := 0;
  for i := 0 to reports.Count - 1 do
  begin
    ln := '';
    for j := 0 to TJSONObject(reports.Items[i]).Count - 1 do
      ln := ln + TJSONObject(reports.Items[i]).Names[j] + '=' + Soc_ValStr(TJSONObject(reports.Items[i]).Items[j]) + '  ';
    WriteLn(ln);
    if JStr(reports.Items[i], 'status') = 'failed' then Result := 1;
  end;
  reports.Free; accounts.Free; settings.Free;
end;

// ---------- routes (social.routes.ts) ----------
procedure Soc_Issue(var issues: string; const msg, path: string);
begin
  if issues <> '' then issues := issues + #10;
  issues := issues + SOC_CROSS + ' ' + msg;
  if path <> '' then issues := issues + #10 + '  ' + SOC_ARROW + ' at ' + path;
end;

function Soc_TypeName(v: TJSONData): string;
begin
  if v = nil then exit('undefined');
  case v.JSONType of
    jtNull: Result := 'null';
    jtString: Result := 'string';
    jtNumber: Result := 'number';
    jtBoolean: Result := 'boolean';
    jtArray: Result := 'array';
  else
    Result := 'object';
  end;
end;

// accountBody zod schema: returns parsed doc {platform,url,name,kind,weight,followers,enabled,handle} or raises 400
function Soc_ParseAccountBody(b: TJSONData): TJSONObject;
var
  issues, platform, url, name, kind, host, path, query, handle: string;
  v: TJSONData;
  weight, followers: Double;
  enabled, urlOk: Boolean;
begin
  issues := '';
  if (b = nil) or not (b is TJSONObject) then
    raise EApi.CreateS(400, SOC_CROSS + ' Invalid input: expected object, received ' + Soc_TypeName(b));
  platform := ''; url := ''; name := ''; kind := 'page'; weight := 1; followers := 0; enabled := True; urlOk := False;
  v := TJSONObject(b).Find('platform');
  if (v <> nil) and (v.JSONType = jtString) and Soc_IsPlatform(v.AsString) then platform := v.AsString
  else Soc_Issue(issues, SOC_PLATFORMS_ENUM, 'platform');
  v := TJSONObject(b).Find('url');
  if (v = nil) or (v.JSONType <> jtString) then Soc_Issue(issues, 'Invalid input: expected string, received ' + Soc_TypeName(v), 'url')
  else if not Soc_SplitUrl(v.AsString, host, path, query) then Soc_Issue(issues, 'Invalid URL', 'url')
  else begin url := v.AsString; urlOk := True; end;
  v := TJSONObject(b).Find('name');
  if (v = nil) or (v.JSONType <> jtString) then Soc_Issue(issues, 'Invalid input: expected string, received ' + Soc_TypeName(v), 'name')
  else if Length(Soc_Cps(v.AsString)) < 1 then Soc_Issue(issues, 'Too small: expected string to have >=1 characters', 'name')
  else if Length(Soc_Cps(v.AsString)) > 100 then Soc_Issue(issues, 'Too big: expected string to have <=100 characters', 'name')
  else name := v.AsString;
  v := TJSONObject(b).Find('kind');
  if v <> nil then
  begin
    if (v.JSONType = jtString) and ((v.AsString = 'journalist') or (v.AsString = 'page') or (v.AsString = 'influencer') or (v.AsString = 'media') or (v.AsString = 'official')) then
      kind := v.AsString
    else Soc_Issue(issues, SOC_KINDS_ENUM, 'kind');
  end;
  v := TJSONObject(b).Find('weight');
  if v <> nil then
  begin
    if v.JSONType <> jtNumber then Soc_Issue(issues, 'Invalid input: expected number, received ' + Soc_TypeName(v), 'weight')
    else if v.AsFloat < 0.5 then Soc_Issue(issues, 'Too small: expected number to be >=0.5', 'weight')
    else if v.AsFloat > 3 then Soc_Issue(issues, 'Too big: expected number to be <=3', 'weight')
    else weight := v.AsFloat;
  end;
  v := TJSONObject(b).Find('followers');
  if v <> nil then
  begin
    if v.JSONType <> jtNumber then Soc_Issue(issues, 'Invalid input: expected number, received ' + Soc_TypeName(v), 'followers')
    else if Frac(v.AsFloat) <> 0 then Soc_Issue(issues, 'Invalid input: expected int, received number', 'followers')
    else if v.AsFloat < 0 then Soc_Issue(issues, 'Too small: expected number to be >=0', 'followers')
    else followers := v.AsFloat;
  end;
  v := TJSONObject(b).Find('enabled');
  if v <> nil then
  begin
    if v.JSONType <> jtBoolean then Soc_Issue(issues, 'Invalid input: expected boolean, received ' + Soc_TypeName(v), 'enabled')
    else enabled := v.AsBoolean;
  end;
  if issues <> '' then raise EApi.CreateS(400, issues);
  if not urlOk or not Soc_HostOk(platform, host) then raise EApi.CreateS(400, SOC_CROSS + ' URL does not belong to the chosen platform' + #10 + '  ' + SOC_ARROW + ' at url');
  handle := Soc_HandleFromUrl(url);
  if Copy(url, Length(url), 1) = '/' then Delete(url, Length(url), 1);
  if handle = '' then raise EApi.CreateS(400, SOC_CROSS + ' Profile URL has no account name' + #10 + '  ' + SOC_ARROW + ' at url');
  Result := TJSONObject.Create;
  Result.Add('platform', platform);
  Result.Add('url', url);
  Result.Add('name', name);
  Result.Add('kind', kind);
  JSetN(Result, 'weight', weight);
  JSetN(Result, 'followers', followers);
  Result.Add('enabled', enabled);
  Result.Add('handle', handle);
end;

// z.coerce.number().int().min().max().default() for a query param
function Soc_QInt(const name: string; def, lo, hi: Int64; var issues: string): Int64;
var
  s: string;
  f: Double;
begin
  Result := def;
  s := Q(name);
  if s = '' then exit;
  if not TryStrToFloat(Trim(s), f) then begin Soc_Issue(issues, 'Invalid input: expected number, received NaN', name); exit; end;
  if Frac(f) <> 0 then begin Soc_Issue(issues, 'Invalid input: expected int, received number', name); exit; end;
  if f < lo then begin Soc_Issue(issues, 'Too small: expected number to be >=' + IntToStr(lo), name); exit; end;
  if f > hi then begin Soc_Issue(issues, 'Too big: expected number to be <=' + IntToStr(hi), name); exit; end;
  Result := Round(f);
end;

function Soc_DupAccount(const platform, handle, exceptId: string): Boolean;
var
  all: TJSONArray;
  i: Integer;
begin
  Result := False;
  all := DbLoad('socialaccounts');
  for i := 0 to all.Count - 1 do
    if (JStr(all.Items[i], 'platform') = platform) and (JStr(all.Items[i], 'handle') = handle) and (JStr(all.Items[i], '_id') <> exceptId) then
      Result := True;
  all.Free;
end;

procedure Routes_social;
var
  issues, platform, accountId, id: string;
  hours, limit, page, i, j, k, t, total: Int64;
  nowMs, ms: Int64;
  settings, social, doc, parsed, acc, edition, o, tp, fr, smp: TJSONObject;
  all, posts, outArr, topics, tids, pids, targets, samples, frPosts: TJSONArray;
  idx: TSocIntArr;
  hot: TSocDblArr;
  halfLife, bestPub: Double;
  pos: TSocIntArr;
  topicPost, topicRef: TStringList;
  found: Boolean;
  ok: Boolean;
  bestPubStr: string;
begin
  // ---------------- public: GET /api/social/trending ----------------
  if Route('GET', '/api/social/trending') then
  begin
    issues := '';
    platform := Q('platform');
    if (platform <> '') and not Soc_IsPlatform(platform) then Soc_Issue(issues, SOC_PLATFORMS_ENUM, 'platform');
    hours := Soc_QInt('hours', 48, 1, 24 * 14, issues);
    limit := Soc_QInt('limit', 30, 1, 100, issues);
    page := Soc_QInt('page', 0, 0, High(Int32), issues);
    if issues <> '' then raise EApi.CreateS(400, issues);
    nowMs := Soc_NowMs;
    settings := GetSettings;
    halfLife := JNum(JObj(settings, 'social'), 'halfLifeHours', 24);
    settings.Free;
    all := DbLoad('socialposts');
    // filter
    SetLength(idx, 0);
    for i := 0 to all.Count - 1 do
    begin
      if Soc_PostMs(all.Items[i]) < nowMs - hours * SOC_HOUR_MS then continue;
      if (platform <> '') and (JStr(all.Items[i], 'platform') <> platform) then continue;
      SetLength(idx, Length(idx) + 1);
      idx[High(idx)] := i;
    end;
    // sort by score desc, keep 500
    for i := 1 to High(idx) do
    begin
      t := idx[i]; j := i - 1;
      while (j >= 0) and (JNum(all.Items[idx[j]], 'score') < JNum(all.Items[t], 'score')) do begin idx[j + 1] := idx[j]; Dec(j); end;
      idx[j + 1] := t;
    end;
    if Length(idx) > 500 then SetLength(idx, 500);
    SetLength(hot, Length(idx));
    for i := 0 to High(idx) do
      hot[i] := JNum(all.Items[idx[i]], 'score') * Soc_Decay((nowMs - Soc_PostMs(all.Items[idx[i]])) / SOC_HOUR_MS, halfLife);
    // sort by hot desc (stable)
    for i := 1 to High(idx) do
    begin
      t := idx[i]; halfLife := hot[i]; j := i - 1;
      while (j >= 0) and (hot[j] < halfLife) do begin idx[j + 1] := idx[j]; hot[j + 1] := hot[j]; Dec(j); end;
      idx[j + 1] := t; hot[j + 1] := halfLife;
    end;
    // page slice
    SetLength(pos, 0);
    for i := page * limit to Min((page + 1) * limit, Length(idx)) - 1 do
    begin
      SetLength(pos, Length(pos) + 1);
      pos[High(pos)] := i;
    end;
    // latest mn edition
    topicPost := TStringList.Create; // postId -> index in topicRef
    topicRef := TStringList.Create;  // json {"_id","title"}
    edition := nil;
    tids := DbLoad('editions');
    bestPubStr := '';
    for i := 0 to tids.Count - 1 do
      if JStr(tids.Items[i], 'region') <> 'world' then
        if (edition = nil) or (CompareStr(JStr(tids.Items[i], 'publishedAt'), bestPubStr) > 0) then
        begin
          edition := TJSONObject(tids.Items[i]);
          bestPubStr := JStr(edition, 'publishedAt');
        end;
    if edition <> nil then
    begin
      topics := DbLoad('topics');
      for i := 0 to topics.Count - 1 do
      begin
        tp := TJSONObject(topics.Items[i]);
        if JBool(tp, 'hidden') then continue;
        found := False;
        if JArr(edition, 'topicIds') <> nil then
          for j := 0 to JArr(edition, 'topicIds').Count - 1 do
            if JArr(edition, 'topicIds').Items[j].AsString = JStr(tp, '_id') then begin found := True; break; end;
        if not found then continue;
        pids := JArr(tp, 'postIds');
        if pids = nil then continue;
        found := False;
        for j := 0 to pids.Count - 1 do
          for k := 0 to High(pos) do
            if pids.Items[j].AsString = JStr(all.Items[idx[pos[k]]], '_id') then found := True;
        if not found then continue;
        for j := 0 to pids.Count - 1 do
        begin
          k := Soc_IndexOfExact(topicPost, pids.Items[j].AsString);
          if k < 0 then
          begin
            topicPost.Add(pids.Items[j].AsString);
            topicRef.Add('{"_id":' + JEsc(JStr(tp, '_id')) + ',"title":' + JEsc(JStr(tp, 'title')) + '}');
          end
          else topicRef[k] := '{"_id":' + JEsc(JStr(tp, '_id')) + ',"title":' + JEsc(JStr(tp, 'title')) + '}';
        end;
      end;
      topics.Free;
    end;
    tids.Free;
    outArr := TJSONArray.Create;
    for i := 0 to High(pos) do
    begin
      doc := TJSONObject(all.Items[idx[pos[i]]]);
      o := TJSONObject.Create;
      o.Add('_id', JStr(doc, '_id'));
      o.Add('platform', JStr(doc, 'platform'));
      if JObj(doc, 'author') <> nil then o.Add('author', JObj(doc, 'author').Clone) else o.Add('author', TJSONObject.Create);
      o.Add('url', JStr(doc, 'url'));
      o.Add('text', Soc_JsSlice(JStr(doc, 'text'), 600));
      o.Add('image', JStr(doc, 'image'));
      o.Add('postedAt', JStr(doc, 'postedAt'));
      if JObj(doc, 'metrics') <> nil then o.Add('metrics', JObj(doc, 'metrics').Clone) else o.Add('metrics', TJSONObject.Create);
      JSetN(o, 'hot', Floor(hot[pos[i]] * 100 + 0.5) / 100);
      k := Soc_IndexOfExact(topicPost, JStr(doc, '_id'));
      if k >= 0 then o.Add('topic', JParse(topicRef[k])) else o.Add('topic', TJSONNull.Create);
      outArr.Add(o);
    end;
    topicPost.Free; topicRef.Free;
    all.Free;
    SendJson(200, outArr);
    exit;
  end;

  // ---------------- admin: /api/admin/social/... ----------------
  if Copy(ReqPath, 1, 18) <> '/api/admin/social/' then exit;
  if Handled then exit;

  if Route('GET', '/api/admin/social/accounts') then
  begin
    RequirePerm('social');
    all := DbLoad('socialaccounts');
    for i := 1 to all.Count - 1 do
    begin
      j := i;
      while (j > 0) and ((CompareStr(JStr(all.Items[j - 1], 'platform'), JStr(all.Items[j], 'platform')) > 0) or
        ((JStr(all.Items[j - 1], 'platform') = JStr(all.Items[j], 'platform')) and (CompareStr(JStr(all.Items[j - 1], 'name'), JStr(all.Items[j], 'name')) > 0))) do
      begin
        all.Exchange(j - 1, j);
        Dec(j);
      end;
    end;
    SendJson(200, all);
    exit;
  end;

  if Route('POST', '/api/admin/social/accounts/probe') then
  begin
    RequirePerm('social');
    parsed := Soc_ParseAccountBody(ReqBody);
    settings := GetSettings;
    targets := TJSONArray.Create;
    fr := nil;
    try
      acc := TJSONObject.Create;
      acc.Add('id', '');
      for i := 0 to parsed.Count - 1 do acc.Add(parsed.Names[i], parsed.Items[i].Clone);
      o := TJSONObject.Create;
      o.Add('account', acc);
      o.Add('since', Soc_IsoMs(Soc_NowMs - Int64(SOC_PROBE_DAYS) * 24 * SOC_HOUR_MS));
      targets.Add(o);
      try
        fr := Soc_FetchPlatformPosts(JStr(parsed, 'platform'), targets, JObj(settings, 'social'), 5);
      except
        on E: EApi do raise EApi.CreateS(400, E.Message);
        on E: Exception do raise EApi.CreateS(400, E.Message);
      end;
      frPosts := JArr(fr, 'posts');
      if (frPosts.Count = 0) and (JArr(fr, 'errors').Count > 0) then raise EApi.CreateS(400, JArr(fr, 'errors').Items[0].AsString);
      samples := TJSONArray.Create;
      for i := 0 to Min(frPosts.Count, 5) - 1 do
      begin
        smp := TJSONObject(frPosts.Items[i].Clone);
        JSetS(smp, 'text', Soc_JsSlice(JStr(smp, 'text'), 300));
        samples.Add(smp);
      end;
      o := TJSONObject.Create;
      o.Add('count', frPosts.Count);
      o.Add('samples', samples);
      SendJson(200, o);
    finally
      fr.Free; targets.Free; settings.Free; parsed.Free;
    end;
    exit;
  end;

  if Route('POST', '/api/admin/social/accounts') then
  begin
    RequirePerm('social');
    parsed := Soc_ParseAccountBody(ReqBody);
    try
      if Soc_DupAccount(JStr(parsed, 'platform'), JStr(parsed, 'handle'), '') then raise EApi.CreateS(409, 'Already exists');
      doc := TJSONObject.Create;
      doc.Add('platform', JStr(parsed, 'platform'));
      doc.Add('handle', JStr(parsed, 'handle'));
      doc.Add('url', JStr(parsed, 'url'));
      doc.Add('name', JStr(parsed, 'name'));
      doc.Add('kind', JStr(parsed, 'kind'));
      JSetN(doc, 'weight', JNum(parsed, 'weight', 1));
      JSetN(doc, 'followers', JNum(parsed, 'followers'));
      doc.Add('baseline', 0);
      doc.Add('enabled', JBool(parsed, 'enabled', True));
      id := DbInsert('socialaccounts', doc);
      doc.Free;
      SendJson(201, DbGet('socialaccounts', id));
    finally
      parsed.Free;
    end;
    exit;
  end;

  if Route('PUT', '/api/admin/social/accounts/:id') then
  begin
    RequirePerm('social');
    parsed := Soc_ParseAccountBody(ReqBody);
    try
      id := P(4);
      if not IsId(id) then raise EApi.CreateS(404, 'Not found');
      acc := DbGet('socialaccounts', id);
      if acc = nil then raise EApi.CreateS(404, 'Not found');
      acc.Free;
      if Soc_DupAccount(JStr(parsed, 'platform'), JStr(parsed, 'handle'), id) then raise EApi.CreateS(409, 'Already exists');
      DbUpdate('socialaccounts', id, parsed);
      SendJson(200, DbGet('socialaccounts', id));
    finally
      parsed.Free;
    end;
    exit;
  end;

  if Route('DELETE', '/api/admin/social/accounts/:id') then
  begin
    RequirePerm('social');
    id := P(4);
    if not IsId(id) or not DbDelete('socialaccounts', id) then raise EApi.CreateS(404, 'Not found');
    all := DbLoad('socialposts');
    ok := False;
    for i := all.Count - 1 downto 0 do
      if JStr(all.Items[i], 'accountId') = id then begin all.Delete(i); ok := True; end;
    if ok then DbSave('socialposts', all);
    all.Free;
    SendNoContent;
    exit;
  end;

  if Route('GET', '/api/admin/social/posts') then
  begin
    RequirePerm('social');
    issues := '';
    platform := Q('platform');
    if (platform <> '') and not Soc_IsPlatform(platform) then Soc_Issue(issues, SOC_PLATFORMS_ENUM, 'platform');
    accountId := Q('accountId');
    page := Soc_QInt('page', 0, 0, High(Int32), issues);
    limit := Soc_QInt('limit', 50, 1, 100, issues);
    if issues <> '' then raise EApi.CreateS(400, issues);
    if (accountId <> '') and not IsId(accountId) then raise EApi.CreateS(404, 'Not found');
    all := DbLoad('socialposts');
    SetLength(idx, 0);
    for i := 0 to all.Count - 1 do
    begin
      if (platform <> '') and (JStr(all.Items[i], 'platform') <> platform) then continue;
      if (accountId <> '') and (JStr(all.Items[i], 'accountId') <> accountId) then continue;
      SetLength(idx, Length(idx) + 1);
      idx[High(idx)] := i;
    end;
    for i := 1 to High(idx) do
    begin
      t := idx[i]; j := i - 1;
      while (j >= 0) and (Soc_PostMs(all.Items[idx[j]]) < Soc_PostMs(all.Items[t])) do begin idx[j + 1] := idx[j]; Dec(j); end;
      idx[j + 1] := t;
    end;
    total := Length(idx);
    posts := TJSONArray.Create;
    for i := page * limit to Min((page + 1) * limit, total) - 1 do posts.Add(all.Items[idx[i]].Clone);
    all.Free;
    o := TJSONObject.Create;
    o.Add('posts', posts);
    o.Add('total', total);
    SendJson(200, o);
    exit;
  end;
end;
