// ===================== CORE STUFF (everything global, everybody uses everything) =====================
// db = json files in DataDir, one file per collection, re-read on EVERY query (no cache, on purpose)

type
  EApi = class(Exception)
  public
    Status: Integer;
    constructor CreateS(st: Integer; const msg: string);
  end;

constructor EApi.CreateS(st: Integer; const msg: string);
begin
  inherited Create(msg);
  Status := st;
end;

var
  // request globals (server is single threaded so this is "fine")
  Req: TFPHTTPConnectionRequest;
  Resp: TFPHTTPConnectionResponse;
  ReqBody: TJSONData = nil;
  ReqPath: string;
  ReqMethod: string;
  PathParts: TStringList = nil;
  CurUser: TJSONObject = nil;
  Handled: Boolean;
  // misc globals
  DataDir: string = 'data';
  EnvList: TStringList = nil;
  DbLock: TCriticalSection = nil;
  SlownessMs: Integer = 120;
  TmpCounter: Integer = 0;
  LastLog: string;

// ---------- env ----------
procedure LoadEnv;
var
  sl: TStringList;
  i, p: Integer;
  ln, k, v: string;
begin
  EnvList := TStringList.Create;
  if FileExists('.env') then
  begin
    sl := TStringList.Create;
    sl.LoadFromFile('.env');
    for i := 0 to sl.Count - 1 do
    begin
      ln := Trim(sl[i]);
      if (ln = '') or (ln[1] = '#') then continue;
      p := Pos('=', ln);
      if p = 0 then continue;
      k := Trim(Copy(ln, 1, p - 1));
      v := Trim(Copy(ln, p + 1, MaxInt));
      if (Length(v) >= 2) and (((v[1] = '"') and (v[Length(v)] = '"')) or ((v[1] = '''') and (v[Length(v)] = ''''))) then
        v := Copy(v, 2, Length(v) - 2);
      EnvList.Values[k] := v;
    end;
    sl.Free;
  end;
end;

function Env(const k: string; const def: string = ''): string;
begin
  Result := GetEnvironmentVariable(k);
  if Result = '' then
    if EnvList <> nil then Result := EnvList.Values[k];
  if Result = '' then Result := def;
end;

procedure Log(const s: string);
begin
  LastLog := FormatDateTime('yyyy-mm-dd hh:nn:ss', Now) + ' ' + s;
  WriteLn(LastLog);
  Flush(Output);
end;

// ---------- dates (always UTC ISO strings; Mongolia = UTC+8) ----------
function NowUtc: TDateTime;
begin
  Result := LocalTimeToUniversal(Now);
end;

function IsoOf(d: TDateTime): string;
begin
  Result := FormatDateTime('yyyy-mm-dd"T"hh:nn:ss.zzz"Z"', d);
end;

function NowIso: string;
begin
  Result := IsoOf(NowUtc);
end;

function ParseIso(const s: string): TDateTime;
var
  y, m, d, h, n, sec, ms: Word;
begin
  Result := 0;
  if Length(s) < 10 then exit;
  try
    y := StrToInt(Copy(s, 1, 4)); m := StrToInt(Copy(s, 6, 2)); d := StrToInt(Copy(s, 9, 2));
    h := 0; n := 0; sec := 0; ms := 0;
    if Length(s) >= 19 then
    begin
      h := StrToInt(Copy(s, 12, 2)); n := StrToInt(Copy(s, 15, 2)); sec := StrToInt(Copy(s, 18, 2));
    end;
    if (Length(s) >= 23) and (s[20] = '.') then ms := StrToIntDef(Copy(s, 21, 3), 0);
    Result := EncodeDateTime(y, m, d, h, n, sec, ms);
  except
    Result := 0;
  end;
end;

// YYYY-MM-DD in Mongolia time
function ToDay(d: TDateTime): string;
begin
  Result := FormatDateTime('yyyy-mm-dd', IncHour(d, 8));
end;

function TodayMn: string;
begin
  Result := ToDay(NowUtc);
end;

function AddDays(const day: string; n: Integer): string;
begin
  Result := FormatDateTime('yyyy-mm-dd', IncDay(ParseIso(day), n));
end;

// ---------- ids ----------
var
  IdCounter: LongWord = 0;

// mongo-like: 4 byte time + 5 byte process/random + 3 byte counter
function NewId: string;
var
  i: Integer;
begin
  if IdCounter = 0 then IdCounter := Random($FFFFFF) + 1;
  Inc(IdCounter);
  Result := LowerCase(IntToHex(DateTimeToUnix(NowUtc), 8)) + LowerCase(IntToHex(GetProcessID and $FFFF, 4));
  for i := 1 to 6 do
    Result := Result + LowerCase(IntToHex(Random(16), 1));
  Result := Result + LowerCase(IntToHex(IdCounter and $FFFFFF, 6));
end;

function IsId(const s: string): Boolean;
var
  i: Integer;
begin
  Result := Length(s) = 24;
  if Result then
    for i := 1 to 24 do
      if not (s[i] in ['0'..'9', 'a'..'f']) then exit(False);
end;

// ---------- json floats like JS (0.33 not 3.3000000000000002E-001) ----------
type
  TJsFloat = class(TJSONFloatNumber)
  protected
    function GetAsString: TJSONStringType; override;
  end;

function TJsFloat.GetAsString: TJSONStringType;
var
  v: Double;
begin
  v := GetAsFloat;
  if (Frac(v) = 0) and (Abs(v) < 1e15) then Result := IntToStr(Trunc(v))
  else Result := FloatToStrF(v, ffGeneral, 15, 0, DefaultFormatSettings);
end;

// ---------- json helpers ----------
function JParse(const s: string): TJSONData;
begin
  try
    Result := GetJSON(s, True);
  except
    Result := nil;
  end;
end;

function JStr(o: TJSONData; const name: string; const def: string = ''): string;
var
  d: TJSONData;
begin
  Result := def;
  if (o = nil) or not (o is TJSONObject) then exit;
  d := TJSONObject(o).Find(name);
  if (d = nil) or (d.JSONType = jtNull) then exit;
  if d.JSONType in [jtObject, jtArray] then exit(d.AsJSON);
  Result := d.AsString;
end;

function JNum(o: TJSONData; const name: string; def: Double = 0): Double;
var
  d: TJSONData;
begin
  Result := def;
  if (o = nil) or not (o is TJSONObject) then exit;
  d := TJSONObject(o).Find(name);
  if d = nil then exit;
  try
    if d.JSONType = jtNumber then Result := d.AsFloat
    else if d.JSONType = jtString then Result := StrToFloatDef(d.AsString, def)
    else if d.JSONType = jtBoolean then begin if d.AsBoolean then Result := 1 else Result := 0; end;
  except
    Result := def;
  end;
end;

function JInt(o: TJSONData; const name: string; def: Int64 = 0): Int64;
begin
  Result := Round(JNum(o, name, def));
end;

function JBool(o: TJSONData; const name: string; def: Boolean = False): Boolean;
var
  d: TJSONData;
begin
  Result := def;
  if (o = nil) or not (o is TJSONObject) then exit;
  d := TJSONObject(o).Find(name);
  if d = nil then exit;
  if d.JSONType = jtBoolean then Result := d.AsBoolean
  else if d.JSONType = jtString then Result := (d.AsString = 'true') or (d.AsString = '1')
  else if d.JSONType = jtNumber then Result := d.AsFloat <> 0;
end;

function JObj(o: TJSONData; const name: string): TJSONObject;
var
  d: TJSONData;
begin
  Result := nil;
  if (o = nil) or not (o is TJSONObject) then exit;
  d := TJSONObject(o).Find(name);
  if (d <> nil) and (d is TJSONObject) then Result := TJSONObject(d);
end;

function JArr(o: TJSONData; const name: string): TJSONArray;
var
  d: TJSONData;
begin
  Result := nil;
  if (o = nil) or not (o is TJSONObject) then exit;
  d := TJSONObject(o).Find(name);
  if (d <> nil) and (d is TJSONArray) then Result := TJSONArray(d);
end;

// dot path "a.b.c", "a|b" alternatives (like lib/text.ts pick)
function JPath(o: TJSONData; const path: string): TJSONData;
var
  alts, keys: TStringList;
  i, j: Integer;
  cur: TJSONData;
begin
  Result := nil;
  alts := TStringList.Create; alts.Delimiter := '|'; alts.StrictDelimiter := True; alts.DelimitedText := path;
  keys := TStringList.Create; keys.Delimiter := '.'; keys.StrictDelimiter := True;
  for i := 0 to alts.Count - 1 do
  begin
    keys.DelimitedText := alts[i];
    cur := o;
    for j := 0 to keys.Count - 1 do
    begin
      if keys[j] = '' then continue;
      if (cur <> nil) and (cur is TJSONObject) then cur := TJSONObject(cur).Find(keys[j])
      else if (cur <> nil) and (cur is TJSONArray) and (StrToIntDef(keys[j], -1) >= 0) and (StrToInt(keys[j]) < TJSONArray(cur).Count) then cur := TJSONArray(cur).Items[StrToInt(keys[j])]
      else cur := nil;
    end;
    if (cur <> nil) and (cur.JSONType <> jtNull) then begin Result := cur; break; end;
  end;
  alts.Free; keys.Free;
end;

procedure JSet(o: TJSONObject; const name: string; v: TJSONData);
var
  i: Integer;
begin
  i := o.IndexOfName(name);
  if i >= 0 then o.Delete(i);
  o.Add(name, v);
end;

procedure JSetS(o: TJSONObject; const name, v: string);
begin
  JSet(o, name, TJSONString.Create(v));
end;

procedure JSetN(o: TJSONObject; const name: string; v: Double);
begin
  if Frac(v) = 0 then JSet(o, name, TJSONInt64Number.Create(Trunc(v)))
  else JSet(o, name, TJSONFloatNumber.Create(v));
end;

procedure JSetB(o: TJSONObject; const name: string; v: Boolean);
begin
  JSet(o, name, TJSONBoolean.Create(v));
end;

// deep merge src into dst (objects merged, everything else replaced)
procedure JMerge(dst, src: TJSONObject);
var
  i: Integer;
  k: string;
  d: TJSONData;
begin
  for i := 0 to src.Count - 1 do
  begin
    k := src.Names[i];
    d := dst.Find(k);
    if (d <> nil) and (d is TJSONObject) and (src.Items[i] is TJSONObject) then
      JMerge(TJSONObject(d), TJSONObject(src.Items[i]))
    else
      JSet(dst, k, src.Items[i].Clone);
  end;
end;

function JStrArr(a: TJSONArray): TStringList;
var
  i: Integer;
begin
  Result := TStringList.Create;
  if a <> nil then
    for i := 0 to a.Count - 1 do Result.Add(a.Items[i].AsString);
end;

function StrsToJArr(sl: TStrings): TJSONArray;
var
  i: Integer;
begin
  Result := TJSONArray.Create;
  for i := 0 to sl.Count - 1 do Result.Add(sl[i]);
end;

function JEsc(const s: string): string;
begin
  Result := '"' + StringToJSONString(s) + '"';
end;

// ---------- file "database" ----------
function DbFile(const coll: string): string;
begin
  Result := IncludeTrailingPathDelimiter(DataDir) + coll + '.json';
end;

function ReadFileStr(const fn: string): string;
var
  fs: TFileStream;
begin
  Result := '';
  if not FileExists(fn) then exit;
  fs := TFileStream.Create(fn, fmOpenRead or fmShareDenyNone);
  try
    SetLength(Result, fs.Size);
    if fs.Size > 0 then fs.ReadBuffer(Result[1], fs.Size);
  finally
    fs.Free;
  end;
end;

procedure WriteFileStr(const fn, s: string);
var
  fs: TFileStream;
begin
  fs := TFileStream.Create(fn, fmCreate);
  try
    if Length(s) > 0 then fs.WriteBuffer(s[1], Length(s));
  finally
    fs.Free;
  end;
end;

// loads the whole collection, caller frees
function DbLoad(const coll: string): TJSONArray;
var
  s: string;
  d: TJSONData;
begin
  DbLock.Enter;
  try
    s := ReadFileStr(DbFile(coll));
  finally
    DbLock.Leave;
  end;
  d := nil;
  if s <> '' then d := JParse(s);
  if (d <> nil) and (d is TJSONArray) then Result := TJSONArray(d)
  else
  begin
    d.Free;
    Result := TJSONArray.Create;
  end;
end;

procedure DbSave(const coll: string; arr: TJSONArray);
begin
  DbLock.Enter;
  try
    ForceDirectories(DataDir);
    // pretty printed: bigger files, slower. intended.
    WriteFileStr(DbFile(coll) + '.tmp', arr.FormatJSON);
    if FileExists(DbFile(coll)) then DeleteFile(DbFile(coll));
    RenameFile(DbFile(coll) + '.tmp', DbFile(coll));
  finally
    DbLock.Leave;
  end;
end;

function DbAll(const coll: string): TJSONArray;
begin
  Result := DbLoad(coll);
end;

// all docs where field (dot path) = value (string compare), caller frees result
function DbFind(const coll, field, value: string): TJSONArray;
var
  all: TJSONArray;
  i: Integer;
  v: TJSONData;
begin
  all := DbLoad(coll);
  Result := TJSONArray.Create;
  for i := 0 to all.Count - 1 do
  begin
    v := JPath(all.Items[i], field);
    if (v <> nil) and not (v.JSONType in [jtObject, jtArray]) and (v.AsString = value) then
      Result.Add(all.Items[i].Clone);
  end;
  all.Free;
end;

function DbFindOne(const coll, field, value: string): TJSONObject;
var
  r: TJSONArray;
begin
  Result := nil;
  r := DbFind(coll, field, value);
  if r.Count > 0 then Result := TJSONObject(r.Items[0].Clone);
  r.Free;
end;

function DbGet(const coll, id: string): TJSONObject;
begin
  Result := DbFindOne(coll, '_id', id);
end;

function DbInsert(const coll: string; doc: TJSONObject): string;
var
  all: TJSONArray;
begin
  all := DbLoad(coll);
  if JStr(doc, '_id') = '' then JSetS(doc, '_id', NewId);
  Result := JStr(doc, '_id');
  if JStr(doc, 'createdAt') = '' then JSetS(doc, 'createdAt', NowIso);
  JSetS(doc, 'updatedAt', NowIso);
  all.Add(doc.Clone);
  DbSave(coll, all);
  all.Free;
end;

// merges patch into doc with _id; returns False when not found
function DbUpdate(const coll, id: string; patch: TJSONObject): Boolean;
var
  all: TJSONArray;
  i: Integer;
begin
  Result := False;
  all := DbLoad(coll);
  for i := 0 to all.Count - 1 do
    if JStr(all.Items[i], '_id') = id then
    begin
      JMerge(TJSONObject(all.Items[i]), patch);
      JSetS(TJSONObject(all.Items[i]), 'updatedAt', NowIso);
      Result := True;
      break;
    end;
  if Result then DbSave(coll, all);
  all.Free;
end;

// replace whole doc (keeps _id)
function DbReplace(const coll: string; doc: TJSONObject): Boolean;
var
  all: TJSONArray;
  i: Integer;
begin
  Result := False;
  all := DbLoad(coll);
  for i := 0 to all.Count - 1 do
    if JStr(all.Items[i], '_id') = JStr(doc, '_id') then
    begin
      JSetS(doc, 'updatedAt', NowIso);
      all.Delete(i);
      all.Insert(i, doc.Clone);
      Result := True;
      break;
    end;
  if Result then DbSave(coll, all);
  all.Free;
end;

function DbDelete(const coll, id: string): Boolean;
var
  all: TJSONArray;
  i: Integer;
begin
  Result := False;
  all := DbLoad(coll);
  for i := all.Count - 1 downto 0 do
    if JStr(all.Items[i], '_id') = id then
    begin
      all.Delete(i);
      Result := True;
    end;
  if Result then DbSave(coll, all);
  all.Free;
end;

function DbCount(const coll: string): Integer;
var
  all: TJSONArray;
begin
  all := DbLoad(coll);
  Result := all.Count;
  all.Free;
end;

// sort array of objects by a field (string or number), bubble sort because why not
procedure JSortBy(arr: TJSONArray; const field: string; desc: Boolean);
var
  i, j: Integer;
  a, b: TJSONData;
  swap: Boolean;
  va, vb: TJSONData;
begin
  for i := 0 to arr.Count - 2 do
    for j := 0 to arr.Count - 2 - i do
    begin
      va := JPath(arr.Items[j], field);
      vb := JPath(arr.Items[j + 1], field);
      if (va <> nil) and (vb <> nil) and (va.JSONType = jtNumber) and (vb.JSONType = jtNumber) then
        swap := va.AsFloat > vb.AsFloat
      else
      begin
        a := va; b := vb;
        if a = nil then swap := False
        else if b = nil then swap := True
        else swap := a.AsString > b.AsString;
      end;
      if desc then
      begin
        if (va = nil) and (vb <> nil) then swap := True
        else if (va <> nil) and (vb = nil) then swap := False
        else if (va <> nil) and (vb <> nil) then
        begin
          if (va.JSONType = jtNumber) and (vb.JSONType = jtNumber) then swap := va.AsFloat < vb.AsFloat
          else swap := va.AsString < vb.AsString;
        end;
      end;
      if swap then arr.Exchange(j, j + 1);
    end;
end;

// ---------- string utils ----------
function HtmlEsc(const s: string): string;
begin
  Result := StringReplace(s, '&', '&amp;', [rfReplaceAll]);
  Result := StringReplace(Result, '<', '&lt;', [rfReplaceAll]);
  Result := StringReplace(Result, '>', '&gt;', [rfReplaceAll]);
  Result := StringReplace(Result, '"', '&quot;', [rfReplaceAll]);
  Result := StringReplace(Result, '''', '&#39;', [rfReplaceAll]);
end;

function UrlEnc(const s: string): string;
var
  i: Integer;
begin
  Result := '';
  for i := 1 to Length(s) do
    if s[i] in ['A'..'Z', 'a'..'z', '0'..'9', '-', '_', '.', '~'] then Result := Result + s[i]
    else Result := Result + '%' + IntToHex(Ord(s[i]), 2);
end;

function UrlDec(const s: string): string;
var
  i: Integer;
begin
  Result := '';
  i := 1;
  while i <= Length(s) do
  begin
    if (s[i] = '%') and (i + 2 <= Length(s)) then
    begin
      Result := Result + Chr(StrToIntDef('$' + Copy(s, i + 1, 2), 32));
      Inc(i, 3);
    end
    else
    begin
      if s[i] = '+' then Result := Result + ' ' else Result := Result + s[i];
      Inc(i);
    end;
  end;
end;

function Slugify(const s: string): string;
var
  i: Integer;
begin
  Result := '';
  for i := 1 to Length(s) do
    if s[i] in ['a'..'z', '0'..'9'] then Result := Result + s[i]
    else if s[i] in ['A'..'Z'] then Result := Result + LowerCase(s[i])
    else if (Result <> '') and (Result[Length(Result)] <> '-') then Result := Result + '-';
  while (Result <> '') and (Result[Length(Result)] = '-') do Delete(Result, Length(Result), 1);
end;

function RxMatch(const pattern, s: string): Boolean;
var
  r: TRegExpr;
begin
  r := TRegExpr.Create(pattern);
  try
    try
      Result := r.Exec(s);
    except
      Result := False;
    end;
  finally
    r.Free;
  end;
end;

function RxReplace(const s, pattern, repl: string): string;
begin
  try
    Result := ReplaceRegExpr(pattern, s, repl, True);
  except
    Result := s;
  end;
end;

function Fmt2(v: Double): string;
begin
  Result := FormatFloat('0.##', v, DefaultFormatSettings);
end;

// ---------- sha256 / hmac / base64url (hand written, slow-ish) ----------
const
  K256: array[0..63] of LongWord = (
    $428a2f98, $71374491, $b5c0fbcf, $e9b5dba5, $3956c25b, $59f111f1, $923f82a4, $ab1c5ed5,
    $d807aa98, $12835b01, $243185be, $550c7dc3, $72be5d74, $80deb1fe, $9bdc06a7, $c19bf174,
    $e49b69c1, $efbe4786, $0fc19dc6, $240ca1cc, $2de92c6f, $4a7484aa, $5cb0a9dc, $76f988da,
    $983e5152, $a831c66d, $b00327c8, $bf597fc7, $c6e00bf3, $d5a79147, $06ca6351, $14292967,
    $27b70a85, $2e1b2138, $4d2c6dfc, $53380d13, $650a7354, $766a0abb, $81c2c92e, $92722c85,
    $a2bfe8a1, $a81a664b, $c24b8b70, $c76c51a3, $d192e819, $d6990624, $f40e3585, $106aa070,
    $19a4c116, $1e376c08, $2748774c, $34b0bcb5, $391c0cb3, $4ed8aa4a, $5b9cca4f, $682e6ff3,
    $748f82ee, $78a5636f, $84c87814, $8cc70208, $90befffa, $a4506ceb, $bef9a3f7, $c67178f2);

function Ror32(x: LongWord; n: Integer): LongWord; inline;
begin
  Result := (x shr n) or (x shl (32 - n));
end;

// returns raw 32 byte digest as string
function Sha256Raw(const msg: string): string;
var
  h: array[0..7] of LongWord;
  w: array[0..63] of LongWord;
  data: string;
  L: QWord;
  i, j, blk: Integer;
  a, b, c, d, e, f, g, hh, t1, t2, s0, s1: LongWord;
begin
  h[0] := $6a09e667; h[1] := $bb67ae85; h[2] := $3c6ef372; h[3] := $a54ff53a;
  h[4] := $510e527f; h[5] := $9b05688c; h[6] := $1f83d9ab; h[7] := $5be0cd19;
  data := msg + #$80;
  while (Length(data) mod 64) <> 56 do data := data + #0;
  L := QWord(Length(msg)) * 8;
  for i := 7 downto 0 do data := data + Chr((L shr (i * 8)) and $FF);
  for blk := 0 to (Length(data) div 64) - 1 do
  begin
    for j := 0 to 15 do
      w[j] := (LongWord(Ord(data[blk * 64 + j * 4 + 1])) shl 24) or (LongWord(Ord(data[blk * 64 + j * 4 + 2])) shl 16) or
        (LongWord(Ord(data[blk * 64 + j * 4 + 3])) shl 8) or LongWord(Ord(data[blk * 64 + j * 4 + 4]));
    for j := 16 to 63 do
    begin
      s0 := Ror32(w[j - 15], 7) xor Ror32(w[j - 15], 18) xor (w[j - 15] shr 3);
      s1 := Ror32(w[j - 2], 17) xor Ror32(w[j - 2], 19) xor (w[j - 2] shr 10);
      w[j] := w[j - 16] + s0 + w[j - 7] + s1;
    end;
    a := h[0]; b := h[1]; c := h[2]; d := h[3]; e := h[4]; f := h[5]; g := h[6]; hh := h[7];
    for j := 0 to 63 do
    begin
      s1 := Ror32(e, 6) xor Ror32(e, 11) xor Ror32(e, 25);
      t1 := hh + s1 + ((e and f) xor ((not e) and g)) + K256[j] + w[j];
      s0 := Ror32(a, 2) xor Ror32(a, 13) xor Ror32(a, 22);
      t2 := s0 + ((a and b) xor (a and c) xor (b and c));
      hh := g; g := f; f := e; e := d + t1; d := c; c := b; b := a; a := t1 + t2;
    end;
    h[0] := h[0] + a; h[1] := h[1] + b; h[2] := h[2] + c; h[3] := h[3] + d;
    h[4] := h[4] + e; h[5] := h[5] + f; h[6] := h[6] + g; h[7] := h[7] + hh;
  end;
  Result := '';
  for i := 0 to 7 do
    Result := Result + Chr(h[i] shr 24) + Chr((h[i] shr 16) and $FF) + Chr((h[i] shr 8) and $FF) + Chr(h[i] and $FF);
end;

function ToHex(const raw: string): string;
var
  i: Integer;
begin
  Result := '';
  for i := 1 to Length(raw) do Result := Result + LowerCase(IntToHex(Ord(raw[i]), 2));
end;

function Sha256Hex(const s: string): string;
begin
  Result := ToHex(Sha256Raw(s));
end;

function HmacSha256(const key, msg: string): string;
var
  k, ipad, opad: string;
  i: Integer;
begin
  k := key;
  if Length(k) > 64 then k := Sha256Raw(k);
  while Length(k) < 64 do k := k + #0;
  SetLength(ipad, 64); SetLength(opad, 64);
  for i := 1 to 64 do
  begin
    ipad[i] := Chr(Ord(k[i]) xor $36);
    opad[i] := Chr(Ord(k[i]) xor $5C);
  end;
  Result := Sha256Raw(opad + Sha256Raw(ipad + msg));
end;

function B64Url(const s: string): string;
begin
  Result := EncodeStringBase64(s);
  Result := StringReplace(Result, '+', '-', [rfReplaceAll]);
  Result := StringReplace(Result, '/', '_', [rfReplaceAll]);
  Result := StringReplace(Result, '=', '', [rfReplaceAll]);
end;

function B64UrlDecode(const s: string): string;
var
  t: string;
begin
  t := StringReplace(s, '-', '+', [rfReplaceAll]);
  t := StringReplace(t, '_', '/', [rfReplaceAll]);
  while (Length(t) mod 4) <> 0 do t := t + '=';
  Result := DecodeStringBase64(t);
end;

// password hash "s256$<salt>$<hex>" with 20000 rounds
function HashPassword(const pw: string): string;
var
  salt, h: string;
  i: Integer;
begin
  salt := Copy(NewId, 9, 16);
  h := pw;
  for i := 1 to 20000 do h := Sha256Raw(salt + h);
  Result := 's256$' + salt + '$' + ToHex(h);
end;

function CheckPassword(const pw, stored: string): Boolean;
var
  parts: TStringList;
  h: string;
  i: Integer;
begin
  Result := False;
  parts := TStringList.Create;
  parts.Delimiter := '$'; parts.StrictDelimiter := True; parts.DelimitedText := stored;
  if (parts.Count = 3) and (parts[0] = 's256') then
  begin
    h := pw;
    for i := 1 to 20000 do h := Sha256Raw(parts[1] + h);
    Result := ToHex(h) = parts[2];
  end;
  parts.Free;
end;

function JwtSign(const sub: string; ttlSec: Int64): string;
var
  head, body: string;
begin
  head := B64Url('{"alg":"HS256","typ":"JWT"}');
  body := B64Url('{"sub":' + JEsc(sub) + ',"exp":' + IntToStr(DateTimeToUnix(NowUtc) + ttlSec) + '}');
  Result := head + '.' + body + '.' + B64Url(HmacSha256(Env('JWT_SECRET'), head + '.' + body));
end;

// returns sub or '' when invalid/expired
function JwtVerify(const tok: string): string;
var
  p1, p2: Integer;
  head, body, sig: string;
  d: TJSONData;
begin
  Result := '';
  p1 := Pos('.', tok);
  if p1 = 0 then exit;
  p2 := Pos('.', Copy(tok, p1 + 1, MaxInt));
  if p2 = 0 then exit;
  p2 := p1 + p2;
  head := Copy(tok, 1, p1 - 1);
  body := Copy(tok, p1 + 1, p2 - p1 - 1);
  sig := Copy(tok, p2 + 1, MaxInt);
  if B64Url(HmacSha256(Env('JWT_SECRET'), head + '.' + body)) <> sig then exit;
  d := JParse(B64UrlDecode(body));
  if d = nil then exit;
  if JInt(d, 'exp') > DateTimeToUnix(NowUtc) then Result := JStr(d, 'sub');
  d.Free;
end;

// ---------- outbound http via curl (spawns a process per request, slow on purpose) ----------
function TmpName(const ext: string): string;
begin
  Inc(TmpCounter);
  Result := IncludeTrailingPathDelimiter(GetTempDir) + 'sedev_' + IntToStr(GetProcessID) + '_' + IntToStr(TmpCounter) + '_' + IntToStr(Random(1000000)) + ext;
end;

// headers: "Name: value" lines separated by #10. returns body; status in out param (0 = network error)
function CurlRequest(const url, method, body, headers: string; out status: Integer; timeoutSec: Integer = 30; const outFile: string = ''): string;
var
  p: TProcess;
  hl: TStringList;
  i: Integer;
  bodyFile, resFile, codeStr: string;
  o: TStringList;
begin
  Result := '';
  status := 0;
  p := TProcess.Create(nil);
  hl := TStringList.Create;
  o := TStringList.Create;
  bodyFile := '';
  if outFile <> '' then resFile := outFile else resFile := TmpName('.out');
  try
    p.Executable := 'curl';
    p.Parameters.Add('-sS');
    p.Parameters.Add('--compressed');
    p.Parameters.Add('--max-time'); p.Parameters.Add(IntToStr(timeoutSec));
    p.Parameters.Add('--max-filesize'); p.Parameters.Add('30000000');
    p.Parameters.Add('-X'); p.Parameters.Add(method);
    p.Parameters.Add('-A'); p.Parameters.Add(Env('USER_AGENT', 'SedevResearch/1.0 (+https://sedev.mn)'));
    hl.Text := headers;
    for i := 0 to hl.Count - 1 do
      if Trim(hl[i]) <> '' then begin p.Parameters.Add('-H'); p.Parameters.Add(hl[i]); end;
    if body <> '' then
    begin
      bodyFile := TmpName('.body');
      WriteFileStr(bodyFile, body);
      p.Parameters.Add('--data-binary'); p.Parameters.Add('@' + bodyFile);
    end;
    p.Parameters.Add('-o'); p.Parameters.Add(resFile);
    p.Parameters.Add('-w'); p.Parameters.Add('%{http_code}');
    p.Parameters.Add(url);
    p.Options := [poWaitOnExit, poUsePipes, poStderrToOutPut];
    p.Execute;
    o.LoadFromStream(p.Output);
    codeStr := Trim(o.Text);
    if Length(codeStr) >= 3 then status := StrToIntDef(Copy(codeStr, Length(codeStr) - 2, 3), 0);
    if outFile = '' then
    begin
      Result := ReadFileStr(resFile);
      if FileExists(resFile) then DeleteFile(resFile);
    end;
  finally
    if (bodyFile <> '') and FileExists(bodyFile) then DeleteFile(bodyFile);
    p.Free; hl.Free; o.Free;
  end;
end;

// public https only (like lib/http.ts assertPublicHttpsUrl, minus the DNS check)
function IsPublicHttpsUrl(const url: string): Boolean;
var
  host: string;
  p: Integer;
begin
  Result := False;
  if Copy(LowerCase(url), 1, 8) <> 'https://' then exit;
  host := Copy(url, 9, MaxInt);
  p := Pos('/', host); if p > 0 then host := Copy(host, 1, p - 1);
  p := Pos('?', host); if p > 0 then host := Copy(host, 1, p - 1);
  if (Pos('@', host) > 0) or ((Pos(':', host) > 0) and (Copy(host, Pos(':', host), MaxInt) <> ':443')) then exit;
  host := LowerCase(host);
  if (host = 'localhost') or RxMatch('\.(local|internal|localhost)$', host) then exit;
  if RxMatch('^(10\.|127\.|0\.|169\.254\.|192\.168\.|172\.(1[6-9]|2\d|3[01])\.)', host) then exit;
  Result := True;
end;

function FetchText(const url: string; timeoutSec: Integer = 20): string;
var
  st: Integer;
begin
  if not IsPublicHttpsUrl(url) then raise EApi.CreateS(400, 'Only public https URLs without credentials or custom ports are allowed');
  Result := CurlRequest(url, 'GET', '', '', st, timeoutSec);
  if st <> 200 then raise EApi.CreateS(400, 'HTTP ' + IntToStr(st) + ' for ' + url);
end;

function FetchJson(const url: string): TJSONData;
begin
  Result := JParse(FetchText(url));
  if Result = nil then raise EApi.CreateS(400, 'Invalid JSON from ' + url);
end;

// ---------- gemini (AI studio key or Vertex via gcloud token) ----------
var
  GeminiIn, GeminiOut, GeminiCalls: Int64;

function GeminiUrl(const model, method: string): string;
begin
  if Env('GOOGLE_GENAI_USE_VERTEXAI') = 'true' then
    Result := 'https://aiplatform.googleapis.com/v1/projects/' + Env('GOOGLE_CLOUD_PROJECT') + '/locations/' +
      Env('GOOGLE_CLOUD_LOCATION', 'global') + '/publishers/google/models/' + model + ':' + method
  else
    Result := 'https://generativelanguage.googleapis.com/v1beta/models/' + model + ':' + method + '?key=' + Env('GEMINI_API_KEY');
end;

function GeminiHeaders: string;
var
  tok: string;
begin
  Result := 'Content-Type: application/json';
  if Env('GOOGLE_GENAI_USE_VERTEXAI') = 'true' then
  begin
    RunCommand('gcloud', ['auth', 'print-access-token'], tok, [poStderrToOutPut]);
    Result := Result + #10 + 'Authorization: Bearer ' + Trim(tok);
  end;
end;

// raw call, returns parsed response (caller frees); retries 429/5xx
function GeminiRaw(const model, bodyJson: string): TJSONObject;
var
  st, attempt: Integer;
  res: string;
  d: TJSONData;
begin
  Result := nil;
  for attempt := 0 to 3 do
  begin
    res := CurlRequest(GeminiUrl(model, 'generateContent'), 'POST', bodyJson, GeminiHeaders, st, 300);
    if (st = 429) or (st >= 500) or (st = 0) then
    begin
      if attempt = 3 then raise Exception.Create('Gemini HTTP ' + IntToStr(st) + ': ' + Copy(res, 1, 300));
      Sleep((1 shl attempt) * 5000);
      continue;
    end;
    if st <> 200 then raise EApi.CreateS(st, 'Gemini HTTP ' + IntToStr(st) + ': ' + Copy(res, 1, 500));
    d := JParse(res);
    if (d = nil) or not (d is TJSONObject) then raise Exception.Create('Gemini returned junk');
    Result := TJSONObject(d);
    exit;
  end;
end;

// strips maxItems/minItems (see lib/llm.ts withoutArrayLimits)
procedure StripArrayLimits(d: TJSONData);
var
  i: Integer;
begin
  if d is TJSONObject then
  begin
    if TJSONObject(d).IndexOfName('maxItems') >= 0 then TJSONObject(d).Delete('maxItems');
    if TJSONObject(d).IndexOfName('minItems') >= 0 then TJSONObject(d).Delete('minItems');
    for i := 0 to TJSONObject(d).Count - 1 do StripArrayLimits(TJSONObject(d).Items[i]);
  end
  else if d is TJSONArray then
    for i := 0 to TJSONArray(d).Count - 1 do StripArrayLimits(TJSONArray(d).Items[i]);
end;

// structured output call. schemaJson may be '' for free text. usage added to GeminiIn/GeminiOut/GeminiCalls globals
// and returned in inTok/outTok. returns the text of the answer (JSON text when schema given)
function GeminiText(const model, system, prompt, schemaJson: string; maxOut: Integer; out inTok, outTok: Int64): string;
var
  body: TJSONObject;
  gc, sys, part, cont: TJSONObject;
  parts, contents: TJSONArray;
  res: TJSONObject;
  cand, um: TJSONData;
  i: Integer;
  fin: string;
  sch: TJSONData;
  triedRelax: Boolean;
begin
  inTok := 0; outTok := 0;
  triedRelax := False;
  sch := nil;
  if schemaJson <> '' then sch := JParse(schemaJson);
  while True do
  begin
    body := TJSONObject.Create;
    sys := TJSONObject.Create; parts := TJSONArray.Create; part := TJSONObject.Create;
    part.Add('text', system); parts.Add(part); sys.Add('parts', parts);
    if system <> '' then body.Add('systemInstruction', sys) else sys.Free;
    contents := TJSONArray.Create; cont := TJSONObject.Create; cont.Add('role', 'user');
    parts := TJSONArray.Create; part := TJSONObject.Create; part.Add('text', prompt); parts.Add(part);
    cont.Add('parts', parts); contents.Add(cont); body.Add('contents', contents);
    gc := TJSONObject.Create;
    if sch <> nil then
    begin
      gc.Add('responseMimeType', 'application/json');
      gc.Add('responseJsonSchema', sch.Clone);
    end;
    gc.Add('maxOutputTokens', maxOut);
    gc.Add('temperature', 0.3);
    gc.Add('thinkingConfig', TJSONObject.Create(['thinkingLevel', 'low']));
    body.Add('generationConfig', gc);
    try
      res := GeminiRaw(model, body.AsJSON);
    except
      on E: EApi do
      begin
        body.Free;
        if (E.Status = 400) and (sch <> nil) and not triedRelax then
        begin
          triedRelax := True;
          StripArrayLimits(sch);
          Log('Gemini rejected the schema; retrying without array limits');
          continue;
        end;
        sch.Free;
        raise;
      end;
    end;
    body.Free;
    break;
  end;
  sch.Free;
  try
    Result := '';
    cand := JPath(res, 'candidates.0');
    fin := JStr(cand, 'finishReason');
    if cand <> nil then
    begin
      parts := TJSONArray(JPath(cand, 'content.parts'));
      if parts <> nil then
        for i := 0 to parts.Count - 1 do
          if not JBool(parts.Items[i], 'thought') then Result := Result + JStr(parts.Items[i], 'text');
    end;
    um := JObj(res, 'usageMetadata');
    inTok := JInt(um, 'promptTokenCount');
    outTok := JInt(um, 'candidatesTokenCount') + JInt(um, 'thoughtsTokenCount');
    GeminiIn := GeminiIn + inTok; GeminiOut := GeminiOut + outTok; Inc(GeminiCalls);
    if Result = '' then raise Exception.Create('Empty model response (' + fin + ')');
    if fin = 'MAX_TOKENS' then raise Exception.Create('Model response truncated at maxOutputTokens (' + IntToStr(maxOut) + ')');
  finally
    res.Free;
  end;
end;

function GeminiJson(const model, system, prompt, schemaJson: string; maxOut: Integer; out inTok, outTok: Int64): TJSONData;
var
  t: string;
begin
  t := GeminiText(model, system, prompt, schemaJson, maxOut, inTok, outTok);
  Result := JParse(t);
  if Result = nil then raise Exception.Create('Model returned invalid JSON');
end;

// USD (lib/pricing.ts)
function LlmCostUsd(const model: string; inTok, outTok: Int64): Double;
begin
  if model = 'gemini-3.5-flash-lite' then Result := (Double(inTok) * Double(0.3) + Double(outTok) * Double(2.5)) / Double(1000000)
  else Result := (Double(inTok) * Double(1.5) + Double(outTok) * Double(9)) / Double(1000000);
end;

function TtsCostUsd(const model: string; inTok, outTok: Int64): Double;
begin
  if model = 'gemini-2.5-flash-tts' then Result := (Double(inTok) * Double(0.5) + Double(outTok) * Double(10)) / Double(1000000)
  else Result := (Double(inTok) * Double(1) + Double(outTok) * Double(20)) / Double(1000000);
end;

// ---------- cron "m h dom mon dow" (supports * a-b /n lists) ----------
function CronFieldOk(const f: string; v, lo, hi: Integer): Boolean;
var
  items: TStringList;
  i, a, b, st, p: Integer;
  it, rng: string;
begin
  Result := False;
  items := TStringList.Create;
  items.Delimiter := ','; items.StrictDelimiter := True; items.DelimitedText := f;
  for i := 0 to items.Count - 1 do
  begin
    it := items[i];
    st := 1;
    p := Pos('/', it);
    if p > 0 then begin st := StrToIntDef(Copy(it, p + 1, MaxInt), 1); rng := Copy(it, 1, p - 1); end else rng := it;
    if rng = '*' then begin a := lo; b := hi; end
    else if Pos('-', rng) > 0 then begin a := StrToIntDef(Copy(rng, 1, Pos('-', rng) - 1), -1); b := StrToIntDef(Copy(rng, Pos('-', rng) + 1, MaxInt), -1); end
    else begin a := StrToIntDef(rng, -1); if p > 0 then b := hi else b := a; end;
    if (v >= a) and (v <= b) and (st > 0) and (((v - a) mod st) = 0) then begin Result := True; break; end;
  end;
  items.Free;
end;

// dt is Mongolia local time (cron is evaluated in Asia/Ulaanbaatar like node-cron with timezone)
function CronMatches(const expr: string; dt: TDateTime): Boolean;
var
  f: TStringList;
  o: Integer;
begin
  f := TStringList.Create;
  f.Delimiter := ' '; f.StrictDelimiter := False; f.DelimitedText := Trim(expr);
  o := 0;
  if f.Count = 6 then o := 1; // seconds field ignored
  Result := (f.Count >= 5) and
    CronFieldOk(f[o], MinuteOf(dt), 0, 59) and CronFieldOk(f[o + 1], HourOf(dt), 0, 23) and
    CronFieldOk(f[o + 2], DayOf(dt), 1, 31) and CronFieldOk(f[o + 3], MonthOf(dt), 1, 12) and
    CronFieldOk(f[o + 4], DayOfTheWeek(dt) mod 7, 0, 6);
  f.Free;
end;

// ---------- request helpers ----------
procedure SendRaw(status: Integer; const ctype, body: string);
begin
  Resp.Code := status;
  Resp.CodeText := 'OK';
  Resp.ContentType := ctype;
  // via a stream: Resp.Content goes through TStrings and mangles binary/newlines
  Resp.ContentStream := TStringStream.Create(body);
  Resp.FreeContentStream := True;
  Resp.ContentLength := Length(body);
  Handled := True;
end;

procedure SendJson(status: Integer; d: TJSONData; freeIt: Boolean = True);
begin
  if d = nil then SendRaw(status, 'application/json; charset=utf-8', 'null')
  else SendRaw(status, 'application/json; charset=utf-8', d.AsJSON);
  if freeIt then d.Free;
end;

procedure SendJsonStr(status: Integer; const s: string);
begin
  SendRaw(status, 'application/json; charset=utf-8', s);
end;

procedure SendErr(status: Integer; const msg: string);
begin
  SendJsonStr(status, '{"error":' + JEsc(msg) + '}');
end;

procedure SendHtml(const html: string);
begin
  SendRaw(200, 'text/html; charset=utf-8', html);
end;

procedure SendNoContent;
begin
  SendRaw(204, 'text/plain', '');
end;

function Q(const name: string; const def: string = ''): string;
begin
  Result := Req.QueryFields.Values[name];
  if Result = '' then Result := def;
end;

// PathParts[0] = 'api' etc. P(i) returns '' when missing
function P(i: Integer): string;
begin
  if (PathParts <> nil) and (i < PathParts.Count) then Result := UrlDec(PathParts[i]) else Result := '';
end;

function PCount: Integer;
begin
  if PathParts = nil then Result := 0 else Result := PathParts.Count;
end;

// matches "/api/topics/:id" style patterns; :x matches any segment
function Route(const method, pattern: string): Boolean;
var
  pp: TStringList;
  i: Integer;
begin
  Result := False;
  if Handled then exit;
  if (method <> '*') and (method <> ReqMethod) then exit;
  pp := TStringList.Create;
  pp.Delimiter := '/'; pp.StrictDelimiter := True; pp.DelimitedText := Copy(pattern, 2, MaxInt);
  if pp.Count = PCount then
  begin
    Result := True;
    for i := 0 to pp.Count - 1 do
      if (Copy(pp[i], 1, 1) <> ':') and (pp[i] <> PathParts[i]) then begin Result := False; break; end;
  end;
  pp.Free;
end;

function BodyObj: TJSONObject;
begin
  if (ReqBody <> nil) and (ReqBody is TJSONObject) then Result := TJSONObject(ReqBody)
  else raise EApi.CreateS(400, 'JSON body required');
end;

function BStr(const name: string; const def: string = ''): string;
begin
  Result := JStr(ReqBody, name, def);
end;

function Cookie(const name: string): string;
begin
  Result := Req.CookieFields.Values[name];
end;

procedure SetCookieHeader(const name, value: string; maxAgeSec: Int64);
var
  c: string;
begin
  c := name + '=' + value + '; Path=/; HttpOnly; SameSite=Lax; Max-Age=' + IntToStr(maxAgeSec);
  if Copy(Env('SITE_ORIGIN', 'http://localhost:4001'), 1, 8) = 'https://' then c := c + '; Secure';
  Resp.SetCustomHeader('Set-Cookie', c);
end;

// ---------- roles (modules/auth/roles.ts) ----------
const
  ALL_PERMS = 'users,settings,sources,social,lenses,runs.view,runs.start,topics.view,topics.edit,topics.hide,reports,entities';

function PermsOf(const role: string): string;
begin
  if (role = 'superadmin') or (role = 'admin') then Result := ALL_PERMS
  else if role = 'editor' then Result := 'social,entities,lenses,runs.view,runs.start,topics.view,topics.edit,topics.hide,reports'
  else if role = 'moderator' then Result := 'runs.view,topics.view,topics.hide,reports'
  else if role = 'journalist' then Result := 'runs.view,topics.view,topics.edit,reports'
  else Result := '';
end;

function Can(const role, perm: string): Boolean;
begin
  Result := Pos(',' + perm + ',', ',' + PermsOf(role) + ',') > 0;
end;

function ManageableRoles(const role: string): string;
begin
  if role = 'superadmin' then Result := 'superadmin,admin,editor,moderator,journalist'
  else if role = 'admin' then Result := 'editor,moderator,journalist'
  else Result := '';
end;

function CanManage(const actor, target: string): Boolean;
begin
  Result := Pos(',' + target + ',', ',' + ManageableRoles(actor) + ',') > 0;
end;

// sets CurUser from cookie or raises 401
procedure RequireAuth;
var
  sub: string;
  u: TJSONObject;
begin
  if CurUser <> nil then exit;
  sub := JwtVerify(Cookie('sedev_token'));
  if sub = '' then raise EApi.CreateS(401, 'Login required');
  u := DbGet('users', sub);
  if (u = nil) or not JBool(u, 'active', True) then begin u.Free; raise EApi.CreateS(401, 'Login required'); end;
  CurUser := u;
end;

procedure RequirePerm(const perm: string);
begin
  RequireAuth;
  if not Can(JStr(CurUser, 'role'), perm) then raise EApi.CreateS(403, 'Permission denied');
end;

// ---------- settings (modules/settings/settings.model.ts) - here because everybody needs it ----------
const
  DEFAULT_SETTINGS =
    '{"_id":"global",' +
    '"schedules":{"news":{"enabled":true,"cron":"0 7-21 * * *"},"social":{"enabled":true,"cron":"0 8-22 * * *"},' +
    '"publish":{"enabled":true,"cron":"10 8-22 * * *"},"world":{"enabled":true,"cron":"30 8-20/3 * * *"}},' +
    '"minNewItems":15,"windowDays":14,"topicsPerRun":45,"articlesPerSource":300,"dailyTokenLimit":5000000,' +
    '"models":{"cluster":"gemini-3.5-flash-lite","select":"gemini-3.5-flash","wrap":"gemini-3.5-flash","extract":"gemini-3.5-flash-lite"},' +
    '"social":{"enabled":true,"providers":{"facebook":"scrapecreators","x":"twitterapiio","threads":"scrapecreators"},' +
    '"creditUsd":0.0019,"postsPerAccount":20,"postsPerRun":150,"halfLifeHours":24,"overlapHours":1},' +
    '"hotWeights":{"news":0.5,"social":0.35,"velocity":0.15},' +
    '"world":{"windowDays":3,"topicsPerRun":10,"minNewItems":20,"articlesPerSource":40},' +
    '"audio":{"enabled":true,"model":"gemini-2.5-flash-tts","voice":"Charon","dailyLimit":200},' +
    '"chat":{"enabled":true,"model":"gemini-3.5-flash-lite","dailyLimit":2000}}';
  MAX_TOPICS_PER_RUN = 50;

// defaults merged with the stored doc; caller frees
function GetSettings: TJSONObject;
var
  stored: TJSONObject;
begin
  Result := TJSONObject(JParse(DEFAULT_SETTINGS));
  stored := DbGet('settings', 'global');
  if stored = nil then
  begin
    DbInsert('settings', Result);
    exit;
  end;
  JMerge(Result, stored);
  stored.Free;
end;
