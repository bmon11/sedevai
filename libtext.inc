// ===================== libtext: lib/text.ts + lib/dates.ts parseDay + lib/similarity.ts + a hand rolled html/xml dom =====================
// Everything here is plain global functions. Strings are UTF-8 bytes; "length" of text means JS length
// (UTF-16 units) so limits like `text.length >= 80` behave like the original.
// No caching anywhere, selectors are re-parsed on every call (slow on purpose, simple on purpose).

type
  TIntArr = array of Integer;

  // one node of a parsed document. Kind: 0 root, 1 element, 2 text
  THNode = record
    Kind: Integer;
    Tag: string;
    Txt: string;
    AN, AV: array of string;
    Parent: Integer;
    Kids: TIntArr;
  end;

  THDoc = record
    N: array of THNode;
    Cnt: Integer;
    Xml: Boolean;
  end;

  // selector pieces
  THSimple = record
    Tag: string;
    Ids, Cls: array of string;
    AtN, AtOp, AtV: array of string;
    PsN, PsArg: array of string;
  end;

  THAlt = record
    C: array of THSimple;
    Comb: array of Char; // Comb[k] sits between C[k] and C[k+1]: ' ' '>' '+' '~'
  end;

  THSel = array of THAlt;

// ---------------------------------------------------------------- utf-8 / js string stuff

// JS string length (UTF-16 code units) of a UTF-8 string
function U16Len(const s: string): Integer;
var
  i: Integer;
  b: Byte;
begin
  Result := 0;
  for i := 1 to Length(s) do
  begin
    b := Ord(s[i]);
    if (b and $C0) <> $80 then Inc(Result);
    if b >= $F0 then Inc(Result);
  end;
end;

// s.slice(0, n) in UTF-16 units
function U16Copy(const s: string; n: Integer): string;
var
  i, c: Integer;
  b: Byte;
begin
  c := 0;
  i := 1;
  while i <= Length(s) do
  begin
    b := Ord(s[i]);
    if (b and $C0) <> $80 then
    begin
      if b >= $F0 then Inc(c, 2) else Inc(c);
      if c > n then break;
    end;
    Inc(i);
  end;
  Result := Copy(s, 1, i - 1);
end;

function UDecode(const s: string; var i: Integer): Cardinal;
var
  b: Byte;
  n, k: Integer;
begin
  b := Ord(s[i]);
  if b < $80 then begin Result := b; Inc(i); exit; end;
  if (b and $E0) = $C0 then begin Result := b and $1F; n := 1; end
  else if (b and $F0) = $E0 then begin Result := b and $0F; n := 2; end
  else if (b and $F8) = $F0 then begin Result := b and $07; n := 3; end
  else begin Result := $FFFD; Inc(i); exit; end;
  Inc(i);
  for k := 1 to n do
  begin
    if (i > Length(s)) or ((Ord(s[i]) and $C0) <> $80) then begin Result := $FFFD; exit; end;
    Result := (Result shl 6) or (Ord(s[i]) and $3F);
    Inc(i);
  end;
end;

function UEnc(cp: Cardinal): string;
begin
  if cp < $80 then Result := Chr(cp)
  else if cp < $800 then Result := Chr($C0 or (cp shr 6)) + Chr($80 or (cp and $3F))
  else if cp < $10000 then Result := Chr($E0 or (cp shr 12)) + Chr($80 or ((cp shr 6) and $3F)) + Chr($80 or (cp and $3F))
  else Result := Chr($F0 or (cp shr 18)) + Chr($80 or ((cp shr 12) and $3F)) + Chr($80 or ((cp shr 6) and $3F)) + Chr($80 or (cp and $3F));
end;

function ValidUtf8(const s: string): Boolean;
var
  i: Integer;
begin
  Result := True;
  i := 1;
  while i <= Length(s) do
    if UDecode(s, i) = $FFFD then
    begin
      Result := False;
      exit;
    end;
end;

// JS \s
function IsJsSpaceCp(cp: Cardinal): Boolean;
begin
  Result := (cp = 9) or (cp = 10) or (cp = 11) or (cp = 12) or (cp = 13) or (cp = 32) or (cp = $A0) or (cp = $1680) or
    ((cp >= $2000) and (cp <= $200A)) or (cp = $2028) or (cp = $2029) or (cp = $202F) or (cp = $205F) or (cp = $3000) or (cp = $FEFF);
end;

// s.replace(/\s+/g, ' ').trim()
function CleanWs(const s: string): string;
var
  i, j, k, m: Integer;
  cp: Cardinal;
  sp: Boolean;
begin
  SetLength(Result, Length(s));
  k := 0;
  sp := False;
  i := 1;
  while i <= Length(s) do
  begin
    j := i;
    if Ord(s[i]) < $80 then begin cp := Ord(s[i]); Inc(i); end else cp := UDecode(s, i);
    if IsJsSpaceCp(cp) then sp := True
    else
    begin
      if sp and (k > 0) then begin Inc(k); Result[k] := ' '; end;
      sp := False;
      for m := j to i - 1 do begin Inc(k); Result[k] := s[m]; end;
    end;
  end;
  SetLength(Result, k);
end;

// s.trim()
function JsTrim(const s: string): string;
var
  i, a, b, j: Integer;
  cp: Cardinal;
begin
  a := 1;
  i := 1;
  while i <= Length(s) do
  begin
    j := i;
    cp := UDecode(s, i);
    if not IsJsSpaceCp(cp) then begin a := j; break; end;
    a := i;
  end;
  b := Length(s);
  while b >= a do
  begin
    j := b;
    while (j > a) and ((Ord(s[j]) and $C0) = $80) do Dec(j);
    i := j;
    cp := UDecode(s, i);
    if not IsJsSpaceCp(cp) then break;
    b := j - 1;
  end;
  Result := Copy(s, a, b - a + 1);
end;

function ULowerCp(cp: Cardinal): Cardinal;
begin
  Result := cp;
  if (cp >= 65) and (cp <= 90) then Result := cp + 32
  else if cp < 128 then exit
  else if (cp >= $C0) and (cp <= $DE) and (cp <> $D7) then Result := cp + 32
  else if (cp >= $100) and (cp <= $137) and (cp mod 2 = 0) then Result := cp + 1
  else if (cp >= $391) and (cp <= $3AB) and (cp <> $3A2) then Result := cp + 32
  else if (cp >= $410) and (cp <= $42F) then Result := cp + 32
  else if (cp >= $400) and (cp <= $40F) then Result := cp + 80
  else if (((cp >= $460) and (cp <= $481)) or ((cp >= $48A) and (cp <= $4BF)) or ((cp >= $4D0) and (cp <= $52F))) and (cp mod 2 = 0) then Result := cp + 1;
end;

// toLowerCase for latin + cyrillic (Mongolian Ө Ү included)
function ULower(const s: string): string;
var
  i: Integer;
  cp: Cardinal;
begin
  Result := '';
  i := 1;
  while i <= Length(s) do
  begin
    if Ord(s[i]) < $80 then
    begin
      if s[i] in ['A'..'Z'] then Result := Result + Chr(Ord(s[i]) + 32) else Result := Result + s[i];
      Inc(i);
    end
    else
    begin
      cp := UDecode(s, i);
      Result := Result + UEnc(ULowerCp(cp));
    end;
  end;
end;

// \p{L}|\p{N} approximation: ascii alnum + anything non-ascii that is not a known punctuation/symbol block
function IsWordCp(cp: Cardinal): Boolean;
begin
  if cp < $80 then exit(((cp >= 48) and (cp <= 57)) or ((cp >= 65) and (cp <= 90)) or ((cp >= 97) and (cp <= 122)));
  if cp <= $BF then exit((cp = $AA) or (cp = $B2) or (cp = $B3) or (cp = $B5) or (cp = $B9) or (cp = $BA) or ((cp >= $BC) and (cp <= $BE)));
  if (cp = $D7) or (cp = $F7) then exit(False);
  if (cp >= $300) and (cp <= $36F) then exit(False);
  if (cp >= $482) and (cp <= $489) then exit(False);
  if (cp >= $2000) and (cp <= $206F) then exit(False);
  if (cp >= $20A0) and (cp <= $20FF) then exit(False);
  if (cp >= $2100) and (cp <= $214F) then exit(False);
  if (cp >= $2190) and (cp <= $2BFF) then exit(False);
  if (cp >= $2E00) and (cp <= $2E7F) then exit(False);
  if (cp >= $3000) and (cp <= $303F) then exit(False);
  if (cp >= $FE30) and (cp <= $FE6F) then exit(False);
  if (cp >= $FF00) and (cp <= $FF0F) then exit(False);
  if (cp >= $FF1A) and (cp <= $FF20) then exit(False);
  if (cp >= $FF3B) and (cp <= $FF40) then exit(False);
  if (cp >= $FF5B) and (cp <= $FF65) then exit(False);
  if (cp = $FEFF) or (cp = $FFFD) then exit(False);
  if (cp >= $1F000) and (cp <= $1FAFF) then exit(False);
  Result := True;
end;

// lines -> clean -> drop short (length <= minLen) -> unique (first wins) -> join \n
function TxtUniqueJoin(lines: TStrings; minLen: Integer): string;
var
  seen: TStringList;
  i, idx: Integer;
  l: string;
begin
  Result := '';
  seen := TStringList.Create;
  seen.Sorted := True;
  seen.CaseSensitive := True;
  seen.UseLocale := False;
  for i := 0 to lines.Count - 1 do
  begin
    l := CleanWs(lines[i]);
    if U16Len(l) <= minLen then continue;
    if seen.Find(l, idx) then continue;
    seen.Add(l);
    if Result <> '' then Result := Result + #10;
    Result := Result + l;
  end;
  seen.Free;
end;

// String(v) like JS for json values (nil = '')
function JValStr(d: TJSONData): string;
var
  i: Integer;
begin
  Result := '';
  if d = nil then exit;
  case d.JSONType of
    jtString: Result := d.AsString;
    jtNumber:
      if (d is TJSONIntegerNumber) or (d is TJSONInt64Number) or (d is TJSONQWordNumber) then Result := d.AsString
      else if Frac(d.AsFloat) = 0 then Result := FloatToStrF(d.AsFloat, ffFixed, 18, 0)
      else Result := FloatToStr(d.AsFloat);
    jtBoolean: if d.AsBoolean then Result := 'true' else Result := 'false';
    jtNull: Result := 'null';
    jtArray:
      for i := 0 to TJSONArray(d).Count - 1 do
      begin
        if i > 0 then Result := Result + ',';
        if TJSONArray(d).Items[i].JSONType <> jtNull then Result := Result + JValStr(TJSONArray(d).Items[i]);
      end;
    jtObject: Result := '[object Object]';
  end;
end;

// JS truthiness of a json value
function JsTruthy(d: TJSONData): Boolean;
begin
  Result := False;
  if d = nil then exit;
  case d.JSONType of
    jtString: Result := d.AsString <> '';
    jtNumber: Result := (d.AsFloat <> 0) and not IsNan(d.AsFloat);
    jtBoolean: Result := d.AsBoolean;
    jtArray, jtObject: Result := True;
  end;
end;

// encodeURIComponent
function EncURIComp(const s: string): string;
var
  i: Integer;
begin
  Result := '';
  for i := 1 to Length(s) do
    if s[i] in ['A'..'Z', 'a'..'z', '0'..'9', '-', '_', '.', '!', '~', '*', '''', '(', ')'] then Result := Result + s[i]
    else Result := Result + '%' + IntToHex(Ord(s[i]), 2);
end;

// decodes %XX only (decodeURIComponent-ish). ok=False when the result is not valid utf-8 (JS would throw)
function PctDecode(const s: string; out ok: Boolean): string;
var
  i, v: Integer;
begin
  Result := '';
  ok := True;
  i := 1;
  while i <= Length(s) do
  begin
    if (s[i] = '%') and (i + 2 <= Length(s)) then
    begin
      v := StrToIntDef('$' + Copy(s, i + 1, 2), -1);
      if v >= 0 then
      begin
        Result := Result + Chr(v);
        Inc(i, 3);
        continue;
      end;
      ok := False;
    end
    else if s[i] = '%' then ok := False;
    Result := Result + s[i];
    Inc(i);
  end;
  if ok then ok := ValidUtf8(Result);
end;

// fillTemplate: `{field}` / `{a|b}` placeholders from values (JPath = pick); URL-encoded unless raw
function FillTemplate(const tpl: string; values: TJSONData; raw: Boolean): string;
var
  i, j: Integer;
  path, v: string;
begin
  Result := '';
  i := 1;
  while i <= Length(tpl) do
  begin
    if tpl[i] = '{' then
    begin
      j := PosEx('}', tpl, i + 1);
      if j = 0 then begin Result := Result + Copy(tpl, i, MaxInt); break; end;
      if j = i + 1 then begin Result := Result + '{'; Inc(i); continue; end;
      path := Copy(tpl, i + 1, j - i - 1);
      v := JValStr(JPath(values, path));
      if raw then Result := Result + v else Result := Result + EncURIComp(v);
      i := j + 1;
    end
    else
    begin
      Result := Result + tpl[i];
      Inc(i);
    end;
  end;
end;

// ---------------------------------------------------------------- regex helpers

// fpc 3.2.2 TRegExpr gets bounded quantifiers wrong ("^a{4,12}$" matches "a"), so every pattern of this
// module goes through RxFix first: atom{n,m} -> atom x n + atom? x (m-n), atom{n,} -> atom x n + atom*
function RxFix(const pattern: string): string;
var
  i, j, k, n, m, lastStart: Integer;
  stack: TIntArr;
  sp: Integer;
  atom, num1, num2: string;
  hasComma: Boolean;
begin
  Result := '';
  lastStart := -1;
  SetLength(stack, 0);
  sp := 0;
  i := 1;
  while i <= Length(pattern) do
  begin
    case pattern[i] of
      '\':
        begin
          lastStart := Length(Result) + 1;
          Result := Result + Copy(pattern, i, 2);
          Inc(i, 2);
          continue;
        end;
      '[':
        begin
          lastStart := Length(Result) + 1;
          j := i + 1;
          if (j <= Length(pattern)) and (pattern[j] = '^') then Inc(j);
          if (j <= Length(pattern)) and (pattern[j] = ']') then Inc(j);
          while (j <= Length(pattern)) and (pattern[j] <> ']') do
          begin
            if pattern[j] = '\' then Inc(j);
            Inc(j);
          end;
          Result := Result + Copy(pattern, i, j - i + 1);
          i := j + 1;
          continue;
        end;
      '(':
        begin
          if sp >= Length(stack) then SetLength(stack, sp * 2 + 8);
          stack[sp] := Length(Result) + 1;
          Inc(sp);
          Result := Result + '(';
          lastStart := -1;
          Inc(i);
          continue;
        end;
      ')':
        begin
          if sp > 0 then begin Dec(sp); lastStart := stack[sp]; end else lastStart := -1;
          Result := Result + ')';
          Inc(i);
          continue;
        end;
      '{':
        begin
          j := i + 1;
          num1 := ''; num2 := ''; hasComma := False;
          while (j <= Length(pattern)) and (pattern[j] in ['0'..'9']) do begin num1 := num1 + pattern[j]; Inc(j); end;
          if (j <= Length(pattern)) and (pattern[j] = ',') then
          begin
            hasComma := True;
            Inc(j);
            while (j <= Length(pattern)) and (pattern[j] in ['0'..'9']) do begin num2 := num2 + pattern[j]; Inc(j); end;
          end;
          if (lastStart > 0) and (num1 <> '') and (j <= Length(pattern)) and (pattern[j] = '}') then
          begin
            atom := Copy(Result, lastStart, MaxInt);
            Delete(Result, lastStart, MaxInt);
            n := StrToInt(num1);
            for k := 1 to n do Result := Result + atom;
            if hasComma and (num2 = '') then Result := Result + atom + '*'
            else if hasComma then
            begin
              m := StrToInt(num2);
              for k := n + 1 to m do Result := Result + atom + '?';
            end;
            i := j + 1;
            if (i <= Length(pattern)) and (pattern[i] = '?') then Inc(i); // lazy marker: dropped
            lastStart := -1;
            continue;
          end;
          lastStart := Length(Result) + 1;
          Result := Result + '{';
          Inc(i);
          continue;
        end;
      '*', '+', '?', '|', '^', '$':
        begin
          lastStart := -1;
          Result := Result + pattern[i];
          Inc(i);
          continue;
        end;
    end;
    // plain char (whole utf-8 sequence)
    lastStart := Length(Result) + 1;
    j := i + 1;
    if Ord(pattern[i]) >= $C0 then
      while (j <= Length(pattern)) and ((Ord(pattern[j]) and $C0) = $80) do Inc(j);
    Result := Result + Copy(pattern, i, j - i);
    i := j;
  end;
end;

function TxRx(const pattern, s: string): Boolean;
begin
  Result := RxMatch(RxFix(pattern), s);
end;

function TxReplace(const s, pattern, repl: string): string;
begin
  Result := RxReplace(s, RxFix(pattern), repl);
end;

// first capture groups of pattern in s; returns False when no match. groups[0] = whole match
function RxGroups(const pattern, s: string; groups: TStrings): Boolean;
var
  r: TRegExpr;
  i: Integer;
begin
  Result := False;
  groups.Clear;
  r := TRegExpr.Create(RxFix(pattern));
  try
    try
      if r.Exec(s) then
      begin
        Result := True;
        for i := 0 to r.SubExprMatchCount do groups.Add(r.Match[i]);
      end;
    except
      Result := False;
    end;
  finally
    r.Free;
  end;
end;

function RxFirst(const pattern, s: string; grp: Integer; out m: string): Boolean;
var
  g: TStringList;
begin
  g := TStringList.Create;
  Result := RxGroups(pattern, s, g);
  m := '';
  if Result and (grp < g.Count) then m := g[grp];
  g.Free;
end;

// finds the ')' that closes the '(' at position p
function RxCloseParen(const pattern: string; p: Integer): Integer;
var
  depth, i: Integer;
  inCls: Boolean;
begin
  Result := 0;
  depth := 0;
  inCls := False;
  i := p;
  while i <= Length(pattern) do
  begin
    if pattern[i] = '\' then begin Inc(i, 2); continue; end;
    if inCls then begin if pattern[i] = ']' then inCls := False; end
    else if pattern[i] = '[' then inCls := True
    else if pattern[i] = '(' then Inc(depth)
    else if pattern[i] = ')' then
    begin
      Dec(depth);
      if depth = 0 then exit(i);
    end;
    Inc(i);
  end;
end;

// fpc 3.2 TRegExpr dislikes (?:...): plain groups instead (only capture numbering changes)
function RxCompat(const pattern: string): string;
begin
  Result := StringReplace(pattern, '(?:', '(', [rfReplaceAll]);
end;

// removes (?=...) / (?!...) groups (TRegExpr in fpc 3.2 has no lookaround)
function RxStripLook(const pattern: string): string;
var
  p, q: Integer;
begin
  Result := pattern;
  while True do
  begin
    p := Pos('(?=', Result);
    if p = 0 then p := Pos('(?!', Result);
    if p = 0 then break;
    q := RxCloseParen(Result, p);
    if q = 0 then break;
    Delete(Result, p, q - p + 1);
  end;
end;

// new RegExp(v) would work
function RxValid(const pattern: string): Boolean;
var
  r: TRegExpr;
begin
  Result := True;
  r := TRegExpr.Create;
  try
    try
      r.Expression := RxFix(RxCompat(RxStripLook(pattern)));
      r.Compile;
    except
      Result := False;
    end;
  finally
    r.Free;
  end;
end;

// new RegExp(`^(?:${pattern})$`).test(s), with a literal-prefix lookahead emulation:
// "PREFIX(?=LOOK)REST" -> s starts with PREFIX, LOOK matches at that point, REST matches the remainder
function RxFullMatch(const pattern, s: string): Boolean;
var
  p, q, i: Integer;
  pre, look, post, lit, rest: string;
  isLit: Boolean;
begin
  p := Pos('(?=', pattern);
  if p = 0 then exit(TxRx('^(' + RxCompat(pattern) + ')$', s));
  q := RxCloseParen(pattern, p);
  if q = 0 then exit(False);
  pre := Copy(pattern, 1, p - 1);
  look := Copy(pattern, p + 3, q - p - 3);
  post := Copy(pattern, q + 1, MaxInt);
  isLit := True;
  lit := '';
  i := 1;
  while i <= Length(pre) do
  begin
    if pre[i] = '\' then
    begin
      if (i < Length(pre)) and not (pre[i + 1] in ['a'..'z', 'A'..'Z', '0'..'9']) then begin lit := lit + pre[i + 1]; Inc(i, 2); continue; end;
      isLit := False; break;
    end;
    if pre[i] in ['^', '$', '.', '|', '?', '*', '+', '(', ')', '[', ']', '{', '}'] then begin isLit := False; break; end;
    lit := lit + pre[i];
    Inc(i);
  end;
  if not isLit then exit(TxRx('^(' + RxCompat(RxStripLook(pattern)) + ')$', s));
  if Copy(s, 1, Length(lit)) <> lit then exit(False);
  rest := Copy(s, Length(lit) + 1, MaxInt);
  Result := TxRx('^(' + RxCompat(RxStripLook(look)) + ')', rest) and RxFullMatch(post, rest);
end;

// ---------------------------------------------------------------- dates (lib/dates.ts)

// dateWindow(days): inclusive window ending today (Mongolia)
procedure DateWindow(days: Integer; out ws, we: string);
begin
  we := TodayMn;
  ws := AddDays(we, -(days - 1));
end;

function ValidDayStr(y, m, d: Integer): string;
begin
  Result := '';
  if (y < 1) or (y > 9999) or (m < 1) or (m > 12) or (d < 1) or (d > 31) then exit;
  if not IsValidDate(y, m, d) then exit;
  Result := Format('%.4d-%.2d-%.2d', [y, m, d]);
end;

function MonthIdx(const m: string): Integer;
const
  MS = 'JanFebMarAprMayJunJulAugSepOctNovDec';
var
  p: Integer;
begin
  Result := 0;
  p := Pos(m, MS);
  if (Length(m) = 3) and (p > 0) and ((p - 1) mod 3 = 0) then Result := (p - 1) div 3 + 1;
end;

// parseDay(value, now): YYYY-MM-DD (Mongolia) or '' (= null)
function ParseDayAt(const value: string; nowU: TDateTime): string;
var
  t, z, fr: string;
  g: TStringList;
  y, mo, d, h, mi, sec, msec, offMin, n: Integer;
  dt: TDateTime;
begin
  Result := '';
  t := JsTrim(value);
  if t = '' then exit;
  g := TStringList.Create;
  try
    if TxRx('^\d{4}-\d\d-\d\dT', t) then
    begin
      // ISO timestamp, any offset
      if not RxGroups('^(\d{4})-(\d\d)-(\d\d)T(\d\d):(\d\d)(.*)$', t, g) then exit;
      y := StrToInt(g[1]); mo := StrToInt(g[2]); d := StrToInt(g[3]); h := StrToInt(g[4]); mi := StrToInt(g[5]);
      // rest by hand: [:SS[.fff]] [Z|+hh[:mm]]
      z := g[6];
      sec := 0; msec := 0; fr := '';
      if (z <> '') and (z[1] = ':') then
      begin
        if (Length(z) < 3) or not (z[2] in ['0'..'9']) or not (z[3] in ['0'..'9']) then exit;
        sec := StrToInt(Copy(z, 2, 2));
        Delete(z, 1, 3);
        if (z <> '') and (z[1] in ['.', ',']) then
        begin
          Delete(z, 1, 1);
          while (z <> '') and (z[1] in ['0'..'9']) do begin fr := fr + z[1]; Delete(z, 1, 1); end;
          if fr = '' then exit;
          msec := StrToInt(Copy(fr + '000', 1, 3));
        end;
      end;
      z := Trim(z);
      if (z <> '') and (z <> 'Z') and (z <> 'z') and not TxRx('^[+\-]\d\d(:?\d\d)?$', z) then exit;
      offMin := 0;
      if (z <> '') and (z <> 'Z') and (z <> 'z') then
      begin
        fr := z;
        offMin := StrToInt(Copy(z, 2, 2)) * 60;
        z := StringReplace(Copy(z, 4, MaxInt), ':', '', []);
        if z <> '' then offMin := offMin + StrToInt(z);
        if fr[1] = '-' then offMin := -offMin;
      end;
      if (h > 24) or (mi > 59) or (sec > 59) or not IsValidDate(y, mo, d) then exit;
      if h = 24 then begin if (mi <> 0) or (sec <> 0) then exit; dt := IncDay(EncodeDate(y, mo, d), 1); end
      else dt := EncodeDateTime(y, mo, d, h, mi, sec, msec);
      dt := IncMinute(dt, -offMin);
      Result := ToDay(dt);
      exit;
    end;
    // RSS pubDate (RFC 822): "Mon, 12 Oct 2026 10:00:00 +0800"
    if TxRx('^([A-Z][a-z]{2}, )?\d{1,2} [A-Z][a-z]{2} 20\d\d \d\d:\d\d', t) then
    begin
      if not RxGroups('^([A-Z][a-z]{2}, )?(\d{1,2}) ([A-Z][a-z]{2}) (20\d\d) (\d\d):(\d\d)(.*)$', t, g) then exit;
      d := StrToInt(g[2]); mo := MonthIdx(g[3]); y := StrToInt(g[4]); h := StrToInt(g[5]); mi := StrToInt(g[6]);
      z := g[7];
      sec := 0;
      if (Length(z) >= 3) and (z[1] = ':') and (z[2] in ['0'..'9']) and (z[3] in ['0'..'9']) then
      begin
        sec := StrToInt(Copy(z, 2, 2));
        Delete(z, 1, 3);
      end;
      if (mo = 0) or (h > 23) or (mi > 59) or (sec > 59) or not IsValidDate(y, mo, d) then exit;
      z := Trim(z);
      if Pos(' ', z) > 0 then z := Copy(z, 1, Pos(' ', z) - 1);
      z := UpperCase(z);
      offMin := 0;
      if (z = 'EST') or (z = 'CDT') then offMin := -300
      else if (z = 'EDT') then offMin := -240
      else if (z = 'CST') or (z = 'MDT') then offMin := -360
      else if (z = 'MST') or (z = 'PDT') then offMin := -420
      else if (z = 'PST') then offMin := -480
      else if RxGroups('^(GMT|UTC|UT)?([+\-])(\d\d):?(\d\d)$', z, g) then
      begin
        offMin := StrToInt(g[3]) * 60 + StrToInt(g[4]);
        if g[2] = '-' then offMin := -offMin;
      end;
      dt := IncMinute(EncodeDateTime(y, mo, d, h, mi, sec, 0), -offMin);
      Result := ToDay(dt);
      exit;
    end;
    if RxGroups('(20\d\d)\s*оны\s*(\d{1,2})\s*(-р\s*)?сарын\s*(\d{1,2})', t, g) then
    begin
      Result := ValidDayStr(StrToInt(g[1]), StrToInt(g[2]), StrToInt(g[4]));
      exit;
    end;
    if RxGroups('(20\d\d)[./-](\d{1,2})[./-](\d{1,2})', t, g) then
    begin
      Result := ValidDayStr(StrToInt(g[1]), StrToInt(g[2]), StrToInt(g[3]));
      exit;
    end;
    if Pos('Өчигдөр', t) > 0 then
    begin
      Result := ToDay(IncDay(nowU, -1));
      exit;
    end;
    if RxGroups('(\d+)\s*(өдрийн|цагийн|цаг|минутын)\s*өмнө', t, g) then
    begin
      n := StrToIntDef(g[1], 0);
      if g[2] = 'өдрийн' then dt := IncDay(nowU, -n)
      else if Pos('цаг', g[2]) = 1 then dt := IncHour(nowU, -n)
      else dt := IncMinute(nowU, -n);
      Result := ToDay(dt);
      exit;
    end;
  finally
    g.Free;
  end;
end;

function ParseDay(const value: string): string;
begin
  Result := ParseDayAt(value, NowUtc);
end;

// parseDay of a json value (nil / null -> '')
function ParseDayJ(d: TJSONData): string;
begin
  Result := '';
  if (d = nil) or (d.JSONType = jtNull) then exit;
  Result := ParseDay(JValStr(d));
end;

// ---------------------------------------------------------------- similarity (lib/similarity.ts)

// set of lowercase word bigrams; single words when only one word. caller frees
function Shingles(const text: string): TStringList;
var
  words: TStringList;
  low, w: string;
  i, j: Integer;
  cp: Cardinal;
begin
  Result := TStringList.Create;
  Result.Sorted := True;
  Result.Duplicates := dupIgnore;
  Result.CaseSensitive := True;
  Result.UseLocale := False;
  words := TStringList.Create;
  low := ULower(text);
  w := '';
  i := 1;
  while i <= Length(low) do
  begin
    j := i;
    cp := UDecode(low, i);
    if IsWordCp(cp) then w := w + Copy(low, j, i - j)
    else if w <> '' then begin words.Add(w); w := ''; end;
  end;
  if w <> '' then words.Add(w);
  if words.Count < 2 then
  begin
    for i := 0 to words.Count - 1 do Result.Add(words[i]);
  end
  else
    for i := 0 to words.Count - 2 do Result.Add(words[i] + ' ' + words[i + 1]);
  words.Free;
end;

// |A n B| / |A u B|, 0 for empty
function Jaccard(a, b: TStringList): Double;
var
  i, idx, shared: Integer;
begin
  Result := 0;
  if (a.Count = 0) or (b.Count = 0) then exit;
  shared := 0;
  for i := 0 to a.Count - 1 do
    if b.Find(a[i], idx) then Inc(shared);
  Result := shared / (a.Count + b.Count - shared);
end;

// for each text, index of its near-duplicate group's first member (union find, transitive)
function NearDuplicateGroups(texts: TStrings; threshold: Double = 0.5): TIntArr;
var
  sets: array of TStringList;
  root: TIntArr;
  i, j, a, b: Integer;

  function Find(x: Integer): Integer;
  begin
    if root[x] = x then exit(x);
    root[x] := Find(root[x]);
    Result := root[x];
  end;

begin
  SetLength(sets, texts.Count);
  SetLength(root, texts.Count);
  for i := 0 to texts.Count - 1 do
  begin
    sets[i] := Shingles(texts[i]);
    root[i] := i;
  end;
  for i := 0 to texts.Count - 1 do
    for j := i + 1 to texts.Count - 1 do
    begin
      a := Find(i);
      b := Find(j);
      if (a <> b) and (Jaccard(sets[i], sets[j]) >= threshold) then root[Max(a, b)] := Min(a, b);
    end;
  SetLength(Result, texts.Count);
  for i := 0 to texts.Count - 1 do Result[i] := Find(i);
  for i := 0 to texts.Count - 1 do sets[i].Free;
end;

// ---------------------------------------------------------------- html / xml dom

const
  HVOID = ',area,base,br,col,embed,hr,img,input,link,meta,param,source,track,wbr,keygen,basefont,bgsound,frame,';
  HRAW = ',script,style,textarea,title,xmp,iframe,noembed,noframes,noscript,';
  HPCLOSE = ',address,article,aside,blockquote,center,details,dialog,dir,div,dl,fieldset,figcaption,figure,footer,form,h1,h2,h3,h4,h5,h6,header,hgroup,hr,main,menu,nav,ol,p,pre,section,summary,table,ul,li,dd,dt,listing,plaintext,xmp,';
  HPSCOPE = ',applet,caption,html,table,td,th,marquee,object,template,button,';
  HWS = [' ', #9, #10, #12, #13];

function InList(const list, t: string): Boolean;
begin
  Result := Pos(',' + t + ',', list) > 0;
end;

function HEntity(const nm: string): string;
const
  NAMES: array[0..63] of string = ('amp', 'lt', 'gt', 'quot', 'apos', 'nbsp', 'laquo', 'raquo', 'ndash', 'mdash',
    'hellip', 'lsquo', 'rsquo', 'ldquo', 'rdquo', 'sbquo', 'bdquo', 'copy', 'reg', 'trade', 'bull', 'middot', 'deg',
    'plusmn', 'times', 'divide', 'euro', 'pound', 'yen', 'cent', 'sect', 'para', 'shy', 'iexcl', 'iquest', 'acute',
    'uml', 'thinsp', 'ensp', 'emsp', 'zwnj', 'zwj', 'lrm', 'rlm', 'prime', 'larr', 'rarr', 'uarr', 'darr', 'frac12',
    'frac14', 'frac34', 'auml', 'ouml', 'uuml', 'eacute', 'egrave', 'aacute', 'oacute', 'iacute', 'uacute', 'ccedil',
    'ntilde', 'szlig');
  CODES: array[0..63] of Cardinal = (38, 60, 62, 34, 39, 160, 171, 187, $2013, $2014,
    $2026, $2018, $2019, $201C, $201D, $201A, $201E, 169, 174, $2122, $2022, 183, 176,
    177, 215, 247, $20AC, 163, 165, 162, 167, 182, 173, 161, 191, 180,
    168, $2009, $2002, $2003, $200C, $200D, $200E, $200F, $2032, $2190, $2192, $2191, $2193, 189,
    188, 190, 228, 246, 252, 233, 232, 225, 243, 237, 250, 231,
    241, 223);
var
  i: Integer;
begin
  Result := '';
  for i := 0 to High(NAMES) do
    if NAMES[i] = nm then exit(UEnc(CODES[i]));
end;

// decodes html entities. inAttr: legacy entities without ';' are kept as is
function HDecode(const s: string; inAttr: Boolean = False): string;
var
  i, j, k: Integer;
  nm, r: string;
  v: Int64;
begin
  if Pos('&', s) = 0 then exit(s);
  Result := '';
  i := 1;
  while i <= Length(s) do
  begin
    if s[i] <> '&' then
    begin
      j := i;
      while (i <= Length(s)) and (s[i] <> '&') do Inc(i);
      Result := Result + Copy(s, j, i - j);
      continue;
    end;
    if (i < Length(s)) and (s[i + 1] = '#') then
    begin
      j := i + 2;
      v := -1;
      if (j <= Length(s)) and (s[j] in ['x', 'X']) then
      begin
        Inc(j);
        k := j;
        while (j <= Length(s)) and (s[j] in ['0'..'9', 'a'..'f', 'A'..'F']) and (j - k < 8) do Inc(j);
        if j > k then v := StrToInt64('$' + Copy(s, k, j - k));
      end
      else
      begin
        k := j;
        while (j <= Length(s)) and (s[j] in ['0'..'9']) and (j - k < 9) do Inc(j);
        if j > k then v := StrToInt64(Copy(s, k, j - k));
      end;
      if v >= 0 then
      begin
        if (j <= Length(s)) and (s[j] = ';') then Inc(j);
        if (v = 0) or (v > $10FFFF) or ((v >= $D800) and (v <= $DFFF)) then v := $FFFD;
        Result := Result + UEnc(v);
        i := j;
        continue;
      end;
      Result := Result + '&';
      Inc(i);
      continue;
    end;
    j := i + 1;
    while (j <= Length(s)) and (s[j] in ['a'..'z', 'A'..'Z', '0'..'9']) and (j - i < 12) do Inc(j);
    nm := Copy(s, i + 1, j - i - 1);
    r := HEntity(nm);
    if (r <> '') and (j <= Length(s)) and (s[j] = ';') then
    begin
      Result := Result + r;
      i := j + 1;
    end
    else if (r <> '') and not inAttr and ((nm = 'amp') or (nm = 'lt') or (nm = 'gt') or (nm = 'quot') or (nm = 'nbsp') or (nm = 'copy')) then
    begin
      Result := Result + r;
      i := j;
    end
    else
    begin
      Result := Result + '&';
      Inc(i);
    end;
  end;
end;

function HAdd(var D: THDoc; kind: Integer; const tag: string; parent: Integer): Integer;
var
  k: Integer;
begin
  if D.Cnt >= Length(D.N) then SetLength(D.N, Length(D.N) * 2 + 64);
  Result := D.Cnt;
  Inc(D.Cnt);
  D.N[Result].Kind := kind;
  D.N[Result].Tag := tag;
  D.N[Result].Txt := '';
  SetLength(D.N[Result].AN, 0);
  SetLength(D.N[Result].AV, 0);
  SetLength(D.N[Result].Kids, 0);
  D.N[Result].Parent := parent;
  if parent >= 0 then
  begin
    k := Length(D.N[parent].Kids);
    SetLength(D.N[parent].Kids, k + 1);
    D.N[parent].Kids[k] := Result;
  end;
end;

procedure HInit(var D: THDoc);
begin
  SetLength(D.N, 0);
  D.Cnt := 0;
  HAdd(D, 0, '#root', -1);
end;

// parses html (forgiving, html5-ish: void elements, raw text, implied </p> </li> ...) or xml
// (namespace prefixes dropped like fast-xml-parser removeNSPrefix, cdata kept as text)
procedure HParse(var D: THDoc; const s: string; xml: Boolean);
var
  st: TIntArr;
  sp, i, j, k, m, L, n, bodyN, htmlN, headN: Integer;
  ls, tag, nm, val, txt: string;
  q, c: Char;
  selfClose, foreign, dup: Boolean;
  an, av: array of string;

  function Cur: Integer;
  begin
    if sp > 0 then Result := st[sp - 1] else Result := 0;
  end;

  procedure AddText(const t: string);
  var
    p, lk, nt: Integer;
  begin
    if t = '' then exit;
    p := Cur;
    lk := Length(D.N[p].Kids);
    if (lk > 0) and (D.N[D.N[p].Kids[lk - 1]].Kind = 2) then
      D.N[D.N[p].Kids[lk - 1]].Txt := D.N[D.N[p].Kids[lk - 1]].Txt + t
    else
    begin
      nt := HAdd(D, 2, '', p); // index first: HAdd may move D.N
      D.N[nt].Txt := t;
    end;
  end;

  procedure Push(x: Integer);
  begin
    if sp >= Length(st) then SetLength(st, sp * 2 + 16);
    st[sp] := x;
    Inc(sp);
  end;

  // index in stack of nearest open `t`, not crossing any of `stops`; -1 if none
  function OpenIdx(const t, stops: string): Integer;
  var
    z: Integer;
  begin
    Result := -1;
    for z := sp - 1 downto 0 do
    begin
      if D.N[st[z]].Tag = t then exit(z);
      if InList(stops, D.N[st[z]].Tag) then exit;
    end;
  end;

begin
  HInit(D);
  D.Xml := xml;
  SetLength(st, 16);
  sp := 0;
  bodyN := -1; htmlN := -1; headN := -1;
  L := Length(s);
  if not xml then ls := LowerCase(s) else ls := s;
  i := 1;
  while i <= L do
  begin
    if s[i] <> '<' then
    begin
      j := PosEx('<', s, i);
      if j = 0 then j := L + 1;
      txt := Copy(s, i, j - i);
      AddText(HDecode(txt));
      i := j;
      continue;
    end;
    if i = L then begin AddText('<'); Inc(i); continue; end;
    c := s[i + 1];
    if Copy(s, i, 4) = '<!--' then
    begin
      j := PosEx('-->', s, i + 4);
      if j = 0 then i := L + 1 else i := j + 3;
      continue;
    end;
    if Copy(s, i, 9) = '<![CDATA[' then
    begin
      j := PosEx(']]>', s, i + 9);
      if j = 0 then j := L + 1;
      if xml then AddText(Copy(s, i + 9, j - i - 9));
      i := j + 3;
      continue;
    end;
    if (c = '!') or (c = '?') then
    begin
      j := PosEx('>', s, i);
      if j = 0 then i := L + 1 else i := j + 1;
      continue;
    end;
    if (c = '/') and (i + 2 <= L) and (s[i + 2] in ['a'..'z', 'A'..'Z']) then
    begin
      // end tag
      j := i + 2;
      while (j <= L) and not (s[j] in HWS + ['/', '>']) do Inc(j);
      tag := Copy(s, i + 2, j - i - 2);
      if xml then
      begin
        if Pos(':', tag) > 0 then tag := Copy(tag, Pos(':', tag) + 1, MaxInt);
      end
      else tag := LowerCase(tag);
      k := PosEx('>', s, j);
      if k = 0 then i := L + 1 else i := k + 1;
      if not xml and ((tag = 'body') or (tag = 'html')) then continue; // parse5 keeps them open
      if not xml and (tag = 'br') then begin HAdd(D, 1, 'br', Cur); continue; end;
      for m := sp - 1 downto 0 do
        if D.N[st[m]].Tag = tag then
        begin
          sp := m;
          break;
        end;
      continue;
    end;
    if not (c in ['a'..'z', 'A'..'Z']) then
    begin
      AddText('<');
      Inc(i);
      continue;
    end;
    // start tag
    j := i + 1;
    while (j <= L) and not (s[j] in HWS + ['/', '>']) do Inc(j);
    tag := Copy(s, i + 1, j - i - 1);
    if xml then
    begin
      if Pos(':', tag) > 0 then tag := Copy(tag, Pos(':', tag) + 1, MaxInt);
    end
    else tag := LowerCase(tag);
    SetLength(an, 0);
    SetLength(av, 0);
    selfClose := False;
    while j <= L do
    begin
      while (j <= L) and (s[j] in HWS) do Inc(j);
      if j > L then break;
      if s[j] = '>' then begin Inc(j); break; end;
      if s[j] = '/' then
      begin
        if (j < L) and (s[j + 1] = '>') then begin selfClose := True; Inc(j, 2); break; end;
        Inc(j);
        continue;
      end;
      k := j;
      while (j <= L) and not (s[j] in HWS + ['/', '>', '=']) do Inc(j);
      if j = k then begin Inc(j); continue; end;
      nm := Copy(s, k, j - k);
      if xml then
      begin
        if (Pos(':', nm) > 0) and (Copy(nm, 1, 6) <> 'xmlns:') then nm := Copy(nm, Pos(':', nm) + 1, MaxInt);
      end
      else nm := LowerCase(nm);
      while (j <= L) and (s[j] in HWS) do Inc(j);
      val := '';
      if (j <= L) and (s[j] = '=') then
      begin
        Inc(j);
        while (j <= L) and (s[j] in HWS) do Inc(j);
        if (j <= L) and (s[j] in ['"', '''']) then
        begin
          q := s[j];
          k := j + 1;
          j := PosEx(q, s, k);
          if j = 0 then j := L + 1;
          val := Copy(s, k, j - k);
          Inc(j);
        end
        else
        begin
          k := j;
          while (j <= L) and not (s[j] in HWS + ['>']) do Inc(j);
          val := Copy(s, k, j - k);
        end;
        val := HDecode(val, True);
      end;
      dup := False;
      for m := 0 to High(an) do if an[m] = nm then dup := True;
      if not dup then
      begin
        SetLength(an, Length(an) + 1); an[High(an)] := nm;
        SetLength(av, Length(av) + 1); av[High(av)] := val;
      end;
    end;
    i := j;
    foreign := False;
    if not xml then
    begin
      // html/head/body only once
      if ((tag = 'html') and (htmlN >= 0)) or ((tag = 'body') and (bodyN >= 0)) or ((tag = 'head') and (headN >= 0)) then
      begin
        if tag = 'html' then n := htmlN else if tag = 'body' then n := bodyN else n := headN;
        for m := 0 to High(an) do
        begin
          dup := False;
          for k := 0 to High(D.N[n].AN) do if D.N[n].AN[k] = an[m] then dup := True;
          if not dup then
          begin
            SetLength(D.N[n].AN, Length(D.N[n].AN) + 1); D.N[n].AN[High(D.N[n].AN)] := an[m];
            SetLength(D.N[n].AV, Length(D.N[n].AV) + 1); D.N[n].AV[High(D.N[n].AV)] := av[m];
          end;
        end;
        continue;
      end;
      if tag = 'body' then
      begin
        // close head if it is still open
        m := OpenIdx('head', '');
        if m >= 0 then sp := m;
      end;
      for m := 0 to sp - 1 do
        if (D.N[st[m]].Tag = 'svg') or (D.N[st[m]].Tag = 'math') then foreign := True;
      if not foreign then
      begin
        if InList(HPCLOSE, tag) then
        begin
          m := OpenIdx('p', HPSCOPE);
          if m >= 0 then sp := m;
        end;
        if tag = 'li' then
        begin
          m := OpenIdx('li', ',ul,ol,table,td,th,body,html,');
          if m >= 0 then sp := m;
        end;
        if (tag = 'dd') or (tag = 'dt') then
        begin
          for m := sp - 1 downto 0 do
          begin
            if (D.N[st[m]].Tag = 'dd') or (D.N[st[m]].Tag = 'dt') then begin sp := m; break; end;
            if InList(',dl,table,td,th,body,html,', D.N[st[m]].Tag) then break;
          end;
        end;
        if (Length(tag) = 2) and (tag[1] = 'h') and (tag[2] in ['1'..'6']) and (sp > 0) then
        begin
          nm := D.N[st[sp - 1]].Tag;
          if (Length(nm) = 2) and (nm[1] = 'h') and (nm[2] in ['1'..'6']) then Dec(sp);
        end;
        if ((tag = 'option') or (tag = 'optgroup')) and (sp > 0) and (D.N[st[sp - 1]].Tag = 'option') then Dec(sp);
        if (tag = 'optgroup') and (sp > 0) and (D.N[st[sp - 1]].Tag = 'optgroup') then Dec(sp);
        if tag = 'tr' then
        begin
          for m := sp - 1 downto 0 do
          begin
            if D.N[st[m]].Tag = 'tr' then begin sp := m; break; end;
            if InList(',table,tbody,thead,tfoot,', D.N[st[m]].Tag) then break;
          end;
        end;
        if (tag = 'td') or (tag = 'th') then
        begin
          for m := sp - 1 downto 0 do
          begin
            if (D.N[st[m]].Tag = 'td') or (D.N[st[m]].Tag = 'th') then begin sp := m; break; end;
            if InList(',tr,table,tbody,thead,tfoot,', D.N[st[m]].Tag) then break;
          end;
        end;
        if (tag = 'tbody') or (tag = 'thead') or (tag = 'tfoot') then
        begin
          for m := sp - 1 downto 0 do
          begin
            if InList(',tbody,thead,tfoot,', D.N[st[m]].Tag) then begin sp := m; break; end;
            if D.N[st[m]].Tag = 'table' then break;
          end;
        end;
        if tag = 'a' then
        begin
          m := OpenIdx('a', ',table,td,th,');
          if m >= 0 then sp := m;
        end;
      end;
    end;
    n := HAdd(D, 1, tag, Cur);
    D.N[n].AN := Copy(an, 0, Length(an));
    D.N[n].AV := Copy(av, 0, Length(av));
    if not xml then
    begin
      if tag = 'html' then htmlN := n;
      if tag = 'body' then bodyN := n;
      if tag = 'head' then headN := n;
    end;
    if xml then
    begin
      if not selfClose then Push(n);
      continue;
    end;
    if InList(HVOID, tag) then continue;
    if selfClose and (foreign or (tag = 'svg') or (tag = 'math')) then continue;
    if InList(HRAW, tag) and not foreign then
    begin
      // raw text up to </tag
      k := i;
      while True do
      begin
        k := PosEx('</' + tag, ls, k);
        if k = 0 then break;
        m := k + 2 + Length(tag);
        if (m > L) or (s[m] in HWS + ['/', '>']) then break;
        k := m;
      end;
      if k = 0 then k := L + 1;
      txt := Copy(s, i, k - i);
      if (tag = 'title') or (tag = 'textarea') then txt := HDecode(txt);
      if txt <> '' then
      begin
        m := HAdd(D, 2, '', n);
        D.N[m].Txt := txt;
      end;
      if k > L then i := L + 1
      else
      begin
        m := PosEx('>', s, k);
        if m = 0 then i := L + 1 else i := m + 1;
      end;
      continue;
    end;
    Push(n);
  end;
end;

// textContent
procedure HTextAcc(var D: THDoc; n: Integer; var acc: string);
var
  k: Integer;
begin
  if D.N[n].Kind = 2 then acc := acc + D.N[n].Txt
  else
    for k := 0 to High(D.N[n].Kids) do HTextAcc(D, D.N[n].Kids[k], acc);
end;

function HText(var D: THDoc; n: Integer): string;
begin
  Result := '';
  if n < 0 then exit;
  HTextAcc(D, n, Result);
end;

// direct text children only
function HOwnText(var D: THDoc; n: Integer): string;
var
  k: Integer;
begin
  Result := '';
  for k := 0 to High(D.N[n].Kids) do
    if D.N[D.N[n].Kids[k]].Kind = 2 then Result := Result + D.N[D.N[n].Kids[k]].Txt;
end;

function HAttr(var D: THDoc; n: Integer; const name: string): string;
var
  k: Integer;
begin
  Result := '';
  if n < 0 then exit;
  for k := 0 to High(D.N[n].AN) do
    if D.N[n].AN[k] = name then exit(D.N[n].AV[k]);
end;

function HHasAttr(var D: THDoc; n: Integer; const name: string): Boolean;
var
  k: Integer;
begin
  Result := False;
  for k := 0 to High(D.N[n].AN) do
    if D.N[n].AN[k] = name then exit(True);
end;

function HHasClass(var D: THDoc; n: Integer; const cls: string): Boolean;
var
  c: string;
  i, j: Integer;
begin
  Result := False;
  c := HAttr(D, n, 'class');
  i := 1;
  while i <= Length(c) do
  begin
    while (i <= Length(c)) and (c[i] in HWS) do Inc(i);
    j := i;
    while (j <= Length(c)) and not (c[j] in HWS) do Inc(j);
    if (j > i) and (Copy(c, i, j - i) = cls) then exit(True);
    i := j;
  end;
end;

// element siblings
function HPrevElem(var D: THDoc; n: Integer): Integer;
var
  p, k: Integer;
begin
  Result := -1;
  p := D.N[n].Parent;
  if p < 0 then exit;
  for k := 0 to High(D.N[p].Kids) do
  begin
    if D.N[p].Kids[k] = n then exit;
    if D.N[D.N[p].Kids[k]].Kind = 1 then Result := D.N[p].Kids[k];
  end;
end;

function HElemIndex(var D: THDoc; n: Integer; fromEnd, sameType: Boolean; out total: Integer): Integer;
var
  p, k, x: Integer;
begin
  Result := 0;
  total := 0;
  p := D.N[n].Parent;
  if p < 0 then begin total := 1; Result := 1; exit; end;
  for k := 0 to High(D.N[p].Kids) do
  begin
    x := D.N[p].Kids[k];
    if D.N[x].Kind <> 1 then continue;
    if sameType and (D.N[x].Tag <> D.N[n].Tag) then continue;
    Inc(total);
    if x = n then Result := total;
  end;
  if fromEnd then Result := total - Result + 1;
end;

// splits on sep outside () [] and quotes
function HSplitTop(const s: string; sep: Char): TStringList;
var
  i, depth, start: Integer;
  q: Char;
begin
  Result := TStringList.Create;
  depth := 0;
  q := #0;
  start := 1;
  for i := 1 to Length(s) do
  begin
    if q <> #0 then begin if s[i] = q then q := #0; continue; end;
    if s[i] in ['"', ''''] then q := s[i]
    else if s[i] in ['(', '['] then Inc(depth)
    else if s[i] in [')', ']'] then Dec(depth)
    else if (s[i] = sep) and (depth = 0) then
    begin
      Result.Add(Trim(Copy(s, start, i - start)));
      start := i + 1;
    end;
  end;
  Result.Add(Trim(Copy(s, start, MaxInt)));
end;

function HUnquote(const s: string): string;
begin
  Result := Trim(s);
  if (Length(Result) >= 2) and (Result[1] in ['"', '''']) and (Result[Length(Result)] = Result[1]) then
    Result := Copy(Result, 2, Length(Result) - 2);
end;

function HReadIdent(const p: string; var i: Integer): string;
begin
  Result := '';
  while i <= Length(p) do
  begin
    if p[i] = '\' then
    begin
      if i < Length(p) then Result := Result + p[i + 1];
      Inc(i, 2);
      continue;
    end;
    if not ((p[i] in ['a'..'z', 'A'..'Z', '0'..'9', '-', '_']) or (Ord(p[i]) >= $80)) then break;
    Result := Result + p[i];
    Inc(i);
  end;
end;

function HParseSel(const sel: string): THSel;
var
  parts: TStringList;
  pi, i, L, k, depth: Integer;
  p, body, nm, op, v: string;
  alt: THAlt;
  sm: THSimple;
  pend: Char;
  q: Char;
begin
  SetLength(Result, 0);
  parts := HSplitTop(sel, ',');
  for pi := 0 to parts.Count - 1 do
  begin
    p := parts[pi];
    if p = '' then continue;
    SetLength(alt.C, 0);
    SetLength(alt.Comb, 0);
    pend := #0;
    i := 1;
    L := Length(p);
    while i <= L do
    begin
      while (i <= L) and (p[i] in HWS) do Inc(i);
      if i > L then break;
      if p[i] in ['>', '+', '~'] then begin pend := p[i]; Inc(i); continue; end;
      if (Length(alt.C) > 0) and (pend = #0) then pend := ' ';
      sm.Tag := '';
      SetLength(sm.Ids, 0); SetLength(sm.Cls, 0);
      SetLength(sm.AtN, 0); SetLength(sm.AtOp, 0); SetLength(sm.AtV, 0);
      SetLength(sm.PsN, 0); SetLength(sm.PsArg, 0);
      while (i <= L) and not (p[i] in HWS + ['>', '+', '~']) do
      begin
        if p[i] = '*' then begin sm.Tag := '*'; Inc(i); end
        else if p[i] = '.' then
        begin
          Inc(i);
          SetLength(sm.Cls, Length(sm.Cls) + 1);
          sm.Cls[High(sm.Cls)] := HReadIdent(p, i);
        end
        else if p[i] = '#' then
        begin
          Inc(i);
          SetLength(sm.Ids, Length(sm.Ids) + 1);
          sm.Ids[High(sm.Ids)] := HReadIdent(p, i);
        end
        else if p[i] = '[' then
        begin
          k := i + 1;
          q := #0;
          while (k <= L) do
          begin
            if q <> #0 then begin if p[k] = q then q := #0; end
            else if p[k] in ['"', ''''] then q := p[k]
            else if p[k] = ']' then break;
            Inc(k);
          end;
          body := Copy(p, i + 1, k - i - 1);
          i := k + 1;
          op := '';
          nm := body;
          v := '';
          k := Pos('=', body);
          if k > 0 then
          begin
            if (k > 1) and (body[k - 1] in ['~', '|', '^', '$', '*', '!']) then
            begin
              op := body[k - 1] + '=';
              nm := Copy(body, 1, k - 2);
            end
            else
            begin
              op := '=';
              nm := Copy(body, 1, k - 1);
            end;
            v := Trim(Copy(body, k + 1, MaxInt));
            if (v <> '') and (v[1] in ['"', '''']) and (PosEx(v[1], v, 2) > 0) then
            begin
              if Trim(Copy(v, PosEx(v[1], v, 2) + 1, MaxInt)) = 'i' then op := op + 'i';
              v := Copy(v, 2, PosEx(v[1], v, 2) - 2);
            end
            else if (Length(v) > 2) and (Copy(v, Length(v) - 1, 2) = ' i') then
            begin
              op := op + 'i';
              v := Trim(Copy(v, 1, Length(v) - 2));
            end;
          end;
          SetLength(sm.AtN, Length(sm.AtN) + 1); sm.AtN[High(sm.AtN)] := LowerCase(Trim(nm));
          SetLength(sm.AtOp, Length(sm.AtOp) + 1); sm.AtOp[High(sm.AtOp)] := op;
          SetLength(sm.AtV, Length(sm.AtV) + 1); sm.AtV[High(sm.AtV)] := v;
        end
        else if p[i] = ':' then
        begin
          Inc(i);
          if (i <= L) and (p[i] = ':') then Inc(i);
          nm := LowerCase(HReadIdent(p, i));
          v := '';
          if (i <= L) and (p[i] = '(') then
          begin
            depth := 0;
            k := i;
            while k <= L do
            begin
              if p[k] = '(' then Inc(depth)
              else if p[k] = ')' then begin Dec(depth); if depth = 0 then break; end;
              Inc(k);
            end;
            v := Copy(p, i + 1, k - i - 1);
            i := k + 1;
          end;
          SetLength(sm.PsN, Length(sm.PsN) + 1); sm.PsN[High(sm.PsN)] := nm;
          SetLength(sm.PsArg, Length(sm.PsArg) + 1); sm.PsArg[High(sm.PsArg)] := v;
        end
        else
        begin
          k := i;
          nm := HReadIdent(p, i);
          if i = k then begin Inc(i); sm.Tag := '#invalid'; end
          else sm.Tag := LowerCase(nm);
        end;
      end;
      if Length(alt.C) > 0 then
      begin
        SetLength(alt.Comb, Length(alt.Comb) + 1);
        alt.Comb[High(alt.Comb)] := pend;
      end;
      SetLength(alt.C, Length(alt.C) + 1);
      alt.C[High(alt.C)] := sm;
      pend := #0;
    end;
    if Length(alt.C) > 0 then
    begin
      SetLength(Result, Length(Result) + 1);
      Result[High(Result)] := alt;
    end;
  end;
  parts.Free;
end;

function HMatches(var D: THDoc; n: Integer; const sel: string): Boolean; forward;
function HFirstDesc(var D: THDoc; root: Integer; const sel: string): Integer; forward;

function HAttrOk(var D: THDoc; n: Integer; const nm, op, v: string): Boolean;
var
  a, vv, o: string;
begin
  if not HHasAttr(D, n, nm) then exit(False);
  if op = '' then exit(True);
  a := HAttr(D, n, nm);
  vv := v;
  o := op;
  if (Length(o) > 0) and (o[Length(o)] = 'i') then
  begin
    a := LowerCase(a);
    vv := LowerCase(vv);
    Delete(o, Length(o), 1);
  end;
  if o = '=' then Result := a = vv
  else if o = '!=' then Result := a <> vv
  else if o = '^=' then Result := (vv <> '') and (Copy(a, 1, Length(vv)) = vv)
  else if o = '$=' then Result := (vv <> '') and (Length(a) >= Length(vv)) and (Copy(a, Length(a) - Length(vv) + 1, MaxInt) = vv)
  else if o = '*=' then Result := (vv <> '') and (Pos(vv, a) > 0)
  else if o = '~=' then Result := (vv <> '') and (Pos(' ' + vv + ' ', ' ' + StringReplace(StringReplace(a, #9, ' ', [rfReplaceAll]), #10, ' ', [rfReplaceAll]) + ' ') > 0)
  else if o = '|=' then Result := (a = vv) or (Copy(a, 1, Length(vv) + 1) = vv + '-')
  else Result := False;
end;

function HMatchSimple(var D: THDoc; n: Integer; const sm: THSimple): Boolean;
var
  k, idx, total, nn: Integer;
  arg: string;
begin
  Result := False;
  if (n < 0) or (D.N[n].Kind <> 1) then exit;
  if (sm.Tag <> '') and (sm.Tag <> '*') then
  begin
    if D.Xml then begin if sm.Tag <> LowerCase(D.N[n].Tag) then exit; end
    else if sm.Tag <> D.N[n].Tag then exit;
  end;
  for k := 0 to High(sm.Ids) do
    if HAttr(D, n, 'id') <> sm.Ids[k] then exit;
  for k := 0 to High(sm.Cls) do
    if not HHasClass(D, n, sm.Cls[k]) then exit;
  for k := 0 to High(sm.AtN) do
    if not HAttrOk(D, n, sm.AtN[k], sm.AtOp[k], sm.AtV[k]) then exit;
  for k := 0 to High(sm.PsN) do
  begin
    arg := Trim(sm.PsArg[k]);
    if sm.PsN[k] = 'not' then begin if HMatches(D, n, arg) then exit; end
    else if sm.PsN[k] = 'has' then
    begin
      if (arg <> '') and (arg[1] = '>') then
      begin
        // direct child form
        idx := -1;
        for nn := 0 to High(D.N[n].Kids) do
          if HMatches(D, D.N[n].Kids[nn], Trim(Copy(arg, 2, MaxInt))) then begin idx := 1; break; end;
        if idx < 0 then exit;
      end
      else if HFirstDesc(D, n, arg) < 0 then exit;
    end
    else if sm.PsN[k] = 'is' then begin if not HMatches(D, n, arg) then exit; end
    else if sm.PsN[k] = 'first-child' then begin if HElemIndex(D, n, False, False, total) <> 1 then exit; end
    else if sm.PsN[k] = 'last-child' then begin if HElemIndex(D, n, True, False, total) <> 1 then exit; end
    else if sm.PsN[k] = 'only-child' then begin HElemIndex(D, n, False, False, total); if total <> 1 then exit; end
    else if sm.PsN[k] = 'first-of-type' then begin if HElemIndex(D, n, False, True, total) <> 1 then exit; end
    else if sm.PsN[k] = 'last-of-type' then begin if HElemIndex(D, n, True, True, total) <> 1 then exit; end
    else if (sm.PsN[k] = 'nth-child') or (sm.PsN[k] = 'nth-of-type') or (sm.PsN[k] = 'nth-last-child') then
    begin
      idx := HElemIndex(D, n, sm.PsN[k] = 'nth-last-child', sm.PsN[k] = 'nth-of-type', total);
      if arg = 'odd' then begin if idx mod 2 <> 1 then exit; end
      else if arg = 'even' then begin if idx mod 2 <> 0 then exit; end
      else if idx <> StrToIntDef(arg, -1) then exit;
    end
    else if sm.PsN[k] = 'empty' then begin if Length(D.N[n].Kids) > 0 then exit; end
    else if sm.PsN[k] = 'contains' then begin if Pos(HUnquote(arg), HText(D, n)) = 0 then exit; end
    else exit; // unknown pseudo: no match
  end;
  Result := True;
end;

function HMatchAlt(var D: THDoc; n: Integer; const alt: THAlt; k: Integer): Boolean;
var
  p: Integer;
begin
  Result := False;
  if not HMatchSimple(D, n, alt.C[k]) then exit;
  if k = 0 then exit(True);
  case alt.Comb[k - 1] of
    '>':
      begin
        p := D.N[n].Parent;
        Result := (p >= 0) and (D.N[p].Kind = 1) and HMatchAlt(D, p, alt, k - 1);
      end;
    '+':
      begin
        p := HPrevElem(D, n);
        Result := (p >= 0) and HMatchAlt(D, p, alt, k - 1);
      end;
    '~':
      begin
        p := HPrevElem(D, n);
        while p >= 0 do
        begin
          if HMatchAlt(D, p, alt, k - 1) then exit(True);
          p := HPrevElem(D, p);
        end;
      end;
  else
    begin
      p := D.N[n].Parent;
      while (p >= 0) and (D.N[p].Kind = 1) do
      begin
        if HMatchAlt(D, p, alt, k - 1) then exit(True);
        p := D.N[p].Parent;
      end;
    end;
  end;
end;

function HMatchParsed(var D: THDoc; n: Integer; const S: THSel): Boolean;
var
  a: Integer;
begin
  Result := False;
  if (n < 0) or (D.N[n].Kind <> 1) then exit;
  for a := 0 to High(S) do
    if HMatchAlt(D, n, S[a], High(S[a].C)) then exit(True);
end;

// $(n).is(sel)
function HMatches(var D: THDoc; n: Integer; const sel: string): Boolean;
var
  S: THSel;
begin
  S := HParseSel(sel);
  Result := HMatchParsed(D, n, S);
end;

procedure HSelWalk(var D: THDoc; n: Integer; const S: THSel; var res: TIntArr; var cnt: Integer; firstOnly: Boolean);
var
  k, x: Integer;
begin
  for k := 0 to High(D.N[n].Kids) do
  begin
    if firstOnly and (cnt > 0) then exit;
    x := D.N[n].Kids[k];
    if D.N[x].Kind <> 1 then continue;
    if HMatchParsed(D, x, S) then
    begin
      if cnt >= Length(res) then SetLength(res, cnt * 2 + 16);
      res[cnt] := x;
      Inc(cnt);
    end;
    HSelWalk(D, x, S, res, cnt, firstOnly);
  end;
end;

// $(root).find(sel) in document order (root itself excluded). root 0 = whole document
function HSelect(var D: THDoc; root: Integer; const sel: string): TIntArr;
var
  S: THSel;
  cnt: Integer;
begin
  SetLength(Result, 0);
  if root < 0 then exit;
  S := HParseSel(sel);
  cnt := 0;
  HSelWalk(D, root, S, Result, cnt, False);
  SetLength(Result, cnt);
end;

function HFirstDesc(var D: THDoc; root: Integer; const sel: string): Integer;
var
  S: THSel;
  cnt: Integer;
  res: TIntArr;
begin
  Result := -1;
  if root < 0 then exit;
  S := HParseSel(sel);
  cnt := 0;
  SetLength(res, 0);
  HSelWalk(D, root, S, res, cnt, True);
  if cnt > 0 then Result := res[0];
end;

procedure HDetach(var D: THDoc; n: Integer);
var
  p, k, m: Integer;
begin
  p := D.N[n].Parent;
  if p < 0 then exit;
  m := 0;
  for k := 0 to High(D.N[p].Kids) do
    if D.N[p].Kids[k] <> n then
    begin
      D.N[p].Kids[m] := D.N[p].Kids[k];
      Inc(m);
    end;
  SetLength(D.N[p].Kids, m);
  D.N[n].Parent := -1;
end;

// $(root).find(sel).remove()
procedure HRemoveSel(var D: THDoc; root: Integer; const sel: string);
var
  ids: TIntArr;
  k: Integer;
begin
  ids := HSelect(D, root, sel);
  for k := 0 to High(ids) do HDetach(D, ids[k]);
end;

procedure HReplaceWithText(var D: THDoc; n: Integer; const txt: string);
var
  p, k, t: Integer;
begin
  p := D.N[n].Parent;
  if p < 0 then exit;
  t := HAdd(D, 2, '', -1);
  D.N[t].Txt := txt;
  D.N[t].Parent := p;
  for k := 0 to High(D.N[p].Kids) do
    if D.N[p].Kids[k] = n then D.N[p].Kids[k] := t;
  D.N[n].Parent := -1;
end;

procedure HAppendText(var D: THDoc; n: Integer; const txt: string);
var
  t: Integer;
begin
  t := HAdd(D, 2, '', n);
  D.N[t].Txt := txt;
end;

// ---- xml navigation (fast-xml-parser-ish: doc.rss.channel.item)
function XChild(var D: THDoc; n: Integer; const name: string): Integer;
var
  k: Integer;
begin
  Result := -1;
  if n < 0 then exit;
  for k := 0 to High(D.N[n].Kids) do
    if (D.N[D.N[n].Kids[k]].Kind = 1) and (D.N[D.N[n].Kids[k]].Tag = name) then exit(D.N[n].Kids[k]);
end;

function XChildren(var D: THDoc; n: Integer; const name: string): TIntArr;
var
  k, c: Integer;
begin
  SetLength(Result, 0);
  if n < 0 then exit;
  c := 0;
  for k := 0 to High(D.N[n].Kids) do
    if (D.N[D.N[n].Kids[k]].Kind = 1) and (D.N[D.N[n].Kids[k]].Tag = name) then
    begin
      SetLength(Result, c + 1);
      Result[c] := D.N[n].Kids[k];
      Inc(c);
    end;
end;

function XText(var D: THDoc; n: Integer): string;
begin
  Result := Trim(HText(D, n));
end;

// ---------------------------------------------------------------- lib/text.ts

// plain text from an html fragment, one line per block element
function HtmlToText(const html: string): string;
var
  D: THDoc;
  ids: TIntArr;
  lines: TStringList;
  k: Integer;
begin
  HParse(D, html, False);
  HRemoveSel(D, 0, 'script,style,iframe,noscript');
  ids := HSelect(D, 0, 'p,h2,h3,li');
  lines := TStringList.Create;
  if Length(ids) > 0 then
    for k := 0 to High(ids) do lines.Add(CleanWs(HText(D, ids[k])))
  else
    lines.Add(CleanWs(HText(D, 0)));
  Result := TxtUniqueJoin(lines, 15);
  lines.Free;
end;

// text from an API "content" value: html string, json string, or rich-text tree (ProseMirror/Slate/GoGo)
function RichText(v: TJSONData): string;
var
  t, sep, part, sub: string;
  p, x: TJSONData;
  i, k: Integer;
  keys: array[0..2] of string;
  node: TJSONObject;
begin
  Result := '';
  if v = nil then exit;
  case v.JSONType of
    jtString:
      begin
        t := JsTrim(v.AsString);
        if (t <> '') and (t[1] in ['{', '[']) then
        begin
          p := JParse(t);
          if p <> nil then
          begin
            Result := RichText(p);
            p.Free;
            exit;
          end;
        end;
        Result := HtmlToText(v.AsString);
      end;
    jtArray:
      for i := 0 to TJSONArray(v).Count - 1 do
      begin
        part := RichText(TJSONArray(v).Items[i]);
        if part = '' then continue;
        if Result <> '' then Result := Result + #10;
        Result := Result + part;
      end;
    jtObject:
      begin
        node := TJSONObject(v);
        x := node.Find('text');
        if (x <> nil) and (x.JSONType = jtString) then exit(x.AsString);
        sep := #10;
        x := node.Find('type');
        if (x <> nil) and (x.JSONType = jtString) and (x.AsString = 'paragraph') then sep := '';
        keys[0] := 'body'; keys[1] := 'content'; keys[2] := 'children';
        for k := 0 to 2 do
        begin
          if node.IndexOfName(keys[k]) < 0 then continue;
          x := node.Find(keys[k]);
          part := '';
          if x is TJSONArray then
          begin
            for i := 0 to TJSONArray(x).Count - 1 do
            begin
              sub := RichText(TJSONArray(x).Items[i]);
              if sub = '' then continue;
              if part <> '' then part := part + sep;
              part := part + sub;
            end;
          end
          else part := RichText(x);
          if part = '' then continue;
          if Result <> '' then Result := Result + #10;
          Result := Result + part;
        end;
      end;
  end;
end;

function RichTextStr(const s: string): string;
var
  d: TJSONString;
begin
  d := TJSONString.Create(s);
  Result := RichText(d);
  d.Free;
end;

// ---------------------------------------------------------------- urls (WHATWG-ish, enough for link discovery)

function UrlIsSpecial(const sc: string): Boolean;
begin
  Result := (sc = 'http') or (sc = 'https') or (sc = 'ws') or (sc = 'wss') or (sc = 'ftp') or (sc = 'file');
end;

function UrlEncPart(const s: string; isQuery: Boolean): string;
var
  i: Integer;
  c: Char;
begin
  Result := '';
  for i := 1 to Length(s) do
  begin
    c := s[i];
    if (Ord(c) <= $20) or (Ord(c) >= $7F) or (c in ['"', '<', '>']) or (not isQuery and (c in ['`', '{', '}'])) or (isQuery and (c = '''')) then
      Result := Result + '%' + IntToHex(Ord(c), 2)
    else
      Result := Result + c;
  end;
end;

function UrlNormPath(const path: string): string;
var
  segs, outp: TStringList;
  i: Integer;
  sg: string;
  trail: Boolean;
begin
  segs := TStringList.Create;
  segs.Delimiter := '/';
  segs.StrictDelimiter := True;
  segs.DelimitedText := Copy(path, 2, MaxInt);
  outp := TStringList.Create;
  trail := False;
  for i := 0 to segs.Count - 1 do
  begin
    sg := segs[i];
    trail := False;
    if (sg = '.') or (LowerCase(sg) = '%2e') then begin trail := True; continue; end;
    if (sg = '..') or (LowerCase(sg) = '.%2e') or (LowerCase(sg) = '%2e.') or (LowerCase(sg) = '%2e%2e') then
    begin
      if outp.Count > 0 then outp.Delete(outp.Count - 1);
      trail := True;
      continue;
    end;
    outp.Add(sg);
  end;
  Result := '';
  for i := 0 to outp.Count - 1 do Result := Result + '/' + outp[i];
  if trail or (Result = '') then Result := Result + '/';
  segs.Free;
  outp.Free;
end;

// absolute url -> parts (special schemes get host/port/path/query; fragment dropped). False = invalid
function UrlParseAbs(const u: string; out sc, host, port, path, query: string): Boolean;
var
  p, i: Integer;
  rest, auth: string;
begin
  Result := False;
  sc := ''; host := ''; port := ''; path := ''; query := '';
  p := Pos(':', u);
  if p < 2 then exit;
  sc := LowerCase(Copy(u, 1, p - 1));
  if not TxRx('^[a-z][a-z0-9+.\-]*$', sc) then exit;
  rest := Copy(u, p + 1, MaxInt);
  p := Pos('#', rest);
  if p > 0 then rest := Copy(rest, 1, p - 1);
  if not UrlIsSpecial(sc) then
  begin
    p := Pos('?', rest);
    if p > 0 then begin query := Copy(rest, p + 1, MaxInt); rest := Copy(rest, 1, p - 1); end;
    path := rest;
    Result := True;
    exit;
  end;
  rest := StringReplace(rest, '\', '/', [rfReplaceAll]);
  while (rest <> '') and (rest[1] = '/') do Delete(rest, 1, 1);
  i := 1;
  while (i <= Length(rest)) and not (rest[i] in ['/', '?']) do Inc(i);
  auth := Copy(rest, 1, i - 1);
  rest := Copy(rest, i, MaxInt);
  p := LastDelimiter('@', auth);
  if p > 0 then auth := Copy(auth, p + 1, MaxInt);
  if (auth <> '') and (auth[1] = '[') then
  begin
    p := Pos(']', auth);
    if p = 0 then exit;
    host := LowerCase(Copy(auth, 1, p));
    auth := Copy(auth, p + 1, MaxInt);
    if (auth <> '') and (auth[1] = ':') then port := Copy(auth, 2, MaxInt);
  end
  else
  begin
    p := Pos(':', auth);
    if p > 0 then begin port := Copy(auth, p + 1, MaxInt); host := LowerCase(Copy(auth, 1, p - 1)); end
    else host := LowerCase(auth);
  end;
  if host = '' then exit;
  for i := 1 to Length(host) do
    if host[i] in [' ', '<', '>', '^', '|', '%', '"', '`', '{', '}', #0..#31] then exit;
  if port <> '' then
  begin
    for i := 1 to Length(port) do if not (port[i] in ['0'..'9']) then exit;
    if StrToIntDef(port, 70000) > 65535 then exit;
    port := IntToStr(StrToInt(port));
    if ((sc = 'https') and (port = '443')) or ((sc = 'http') and (port = '80')) or ((sc = 'wss') and (port = '443')) or ((sc = 'ws') and (port = '80')) or ((sc = 'ftp') and (port = '21')) then port := '';
  end;
  p := Pos('?', rest);
  if p > 0 then begin query := Copy(rest, p + 1, MaxInt); rest := Copy(rest, 1, p - 1); end;
  if rest = '' then rest := '/';
  path := UrlEncPart(UrlNormPath(rest), False);
  query := UrlEncPart(query, True);
  Result := True;
end;

function UrlBuild(const sc, host, port, path, query: string): string;
begin
  if not UrlIsSpecial(sc) then
  begin
    Result := sc + ':' + path;
    if query <> '' then Result := Result + '?' + query;
    exit;
  end;
  Result := sc + '://' + host;
  if port <> '' then Result := Result + ':' + port;
  Result := Result + path;
  if query <> '' then Result := Result + '?' + query;
end;

// new URL(href, base).toString() without the hash; '' when it would throw
function UrlResolve(const href, base: string): string;
var
  h, bsc, bhost, bport, bpath, bquery, sc, host, port, path, query, full: string;
  i: Integer;
begin
  Result := '';
  h := Trim(href);
  h := StringReplace(StringReplace(StringReplace(h, #9, '', [rfReplaceAll]), #10, '', [rfReplaceAll]), #13, '', [rfReplaceAll]);
  if TxRx('^[a-zA-Z][a-zA-Z0-9+.\-]*:', h) then
  begin
    if UrlParseAbs(h, sc, host, port, path, query) then Result := UrlBuild(sc, host, port, path, query);
    exit;
  end;
  if not UrlParseAbs(base, bsc, bhost, bport, bpath, bquery) then exit;
  if not UrlIsSpecial(bsc) then exit;
  h := StringReplace(h, '\', '/', [rfReplaceAll]);
  i := Pos('#', h);
  if i > 0 then h := Copy(h, 1, i - 1);
  if Copy(h, 1, 2) = '//' then full := bsc + ':' + h
  else
  begin
    full := bsc + '://' + bhost;
    if bport <> '' then full := full + ':' + bport;
    if h = '' then
    begin
      full := full + bpath;
      if bquery <> '' then full := full + '?' + bquery;
    end
    else if h[1] = '?' then full := full + bpath + h
    else if h[1] = '/' then full := full + h
    else full := full + Copy(bpath, 1, LastDelimiter('/', bpath)) + h;
  end;
  if UrlParseAbs(full, sc, host, port, path, query) then Result := UrlBuild(sc, host, port, path, query);
end;

// url.searchParams.get(name) ('' when missing)
function UrlQueryGet(const query, name: string): string;
var
  parts: TStringList;
  i, p: Integer;
  k, v: string;
  ok: Boolean;
begin
  Result := '';
  parts := TStringList.Create;
  parts.Delimiter := '&';
  parts.StrictDelimiter := True;
  parts.DelimitedText := query;
  for i := 0 to parts.Count - 1 do
  begin
    p := Pos('=', parts[i]);
    if p > 0 then begin k := Copy(parts[i], 1, p - 1); v := Copy(parts[i], p + 1, MaxInt); end
    else begin k := parts[i]; v := ''; end;
    k := PctDecode(StringReplace(k, '+', ' ', [rfReplaceAll]), ok);
    if k = name then
    begin
      Result := PctDecode(StringReplace(v, '+', ' ', [rfReplaceAll]), ok);
      break;
    end;
  end;
  parts.Free;
end;

function UrlHost(const u: string): string;
var
  sc, port, path, query: string;
begin
  if not UrlParseAbs(u, sc, Result, port, path, query) then Result := '';
end;

function UrlPath(const u: string): string;
var
  sc, host, port, query: string;
begin
  if not UrlParseAbs(u, sc, host, port, Result, query) then Result := '';
end;

procedure Routes_libtext;
begin
  // nothing routes here
end;
