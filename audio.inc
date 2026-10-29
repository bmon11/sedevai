// ===================== AUDIO (modules/audio: tts.ts, narration.ts, clip.model.ts, audio.routes.ts) =====================
// Listen to a topic. GET /api/topics/:id/audio lists its parts; each part's MP3 is generated on its
// first play (Gemini TTS), stored and served from there afterwards. settings.audio.dailyLimit caps new parts a day.
// Storage: metadata doc in "audioclips" (key, topicId, model, voice, chars, seconds, bytes, format, file, usage, costUsd),
// the sound itself in DATA_DIR/audio/<key>.mp3 (or <key>.wav when lame is not installed).
// All lengths are counted in UTF-16 units like JavaScript (UnicodeString), so chars/parts match the node app.

const
  AUDIO_CARD_CHARS = 900;
  AUDIO_PART_CHARS = 320;
  AUDIO_FIRST_PART_CHARS = 160;
  AUDIO_TOTAL_CHARS = 6000;
  AUDIO_SAMPLE_RATE = 24000;
  AUDIO_KBPS = 48;
  AUDIO_STYLE: string = 'Мэдээний хөтлөгчийн тайван, тодорхой, дунд зэргийн хурдтай уншина уу:';
  AUDIO_ELLIPSIS = #$E2#$80#$A6; // … in utf-8

var
  // filled by Audio_BuildNarration (global, sorry)
  NarrTitles: TStringList = nil;
  NarrTexts: TStringList = nil;
  // filled by Audio_Synthesize
  AudioLastSeconds: Double = 0;
  AudioLastIn: Int64 = 0;
  AudioLastOut: Int64 = 0;
  AudioLastFormat: string = 'mp3';

// js String(x) of a field (numbers without exponent garbage)
function Audio_S(o: TJSONData; const name: string): string;
var
  d: TJSONData;
begin
  Result := '';
  if (o = nil) or not (o is TJSONObject) then exit;
  d := TJSONObject(o).Find(name);
  if (d = nil) or (d.JSONType = jtNull) then exit;
  if d.JSONType = jtNumber then
  begin
    if (d is TJSONFloatNumber) then Result := FloatToStr(d.AsFloat) else Result := IntToStr(d.AsInt64);
  end
  else if d.JSONType = jtBoolean then
  begin
    if d.AsBoolean then Result := 'true' else Result := 'false';
  end
  else if d.JSONType = jtString then Result := d.AsString
  else if d.JSONType = jtObject then Result := '[object Object]'
  else Result := '';
end;

function Audio_U16Len(const s: string): Integer;
begin
  Result := Length(UTF8Decode(s));
end;

// first n utf-16 units of s (js s.slice(0, n))
function Audio_U16Slice(const s: string; n: Integer): string;
var
  u: UnicodeString;
begin
  u := UTF8Decode(s);
  if Length(u) <= n then exit(s);
  Result := UTF8Encode(Copy(u, 1, n));
end;

function Audio_Clean(const s: string): string;
begin
  Result := RxReplace(s, '\[\d+\]', '');
  Result := RxReplace(Result, '\s+', ' ');
  Result := Trim(Result);
end;

function Audio_Sentence(const s: string): string;
var
  l: Char;
begin
  if s = '' then exit(s);
  l := s[Length(s)];
  if (l = '.') or (l = '!') or (l = '?') then exit(s);
  if (Length(s) >= 3) and (Copy(s, Length(s) - 2, 3) = AUDIO_ELLIPSIS) then exit(s);
  Result := s + '.';
end;

function Audio_Falsy(v: TJSONData): Boolean;
begin
  Result := (v = nil) or (v.JSONType = jtNull);
  if Result then exit;
  if (v.JSONType = jtString) and (v.AsString = '') then exit(True);
  if (v.JSONType = jtBoolean) and not v.AsBoolean then exit(True);
  if (v.JSONType = jtNumber) and (v.AsFloat = 0) then exit(True);
end;

// speakable text of one wrap point, by its format (see wrap-schema.ts)
function Audio_LensText(const format: string; value: TJSONData): string;
var
  arr, good, bad: TJSONArray;
  i: Integer;
  it: TJSONData;
  bits, s1, s2: string;
begin
  Result := '';
  if Audio_Falsy(value) then exit;
  if format = 'text' then exit(Audio_Clean(Audio_S(value, 'text')));
  arr := nil;
  if value is TJSONArray then arr := TJSONArray(value);
  if (format = 'list') or (format = 'keypoints') then
  begin
    if arr = nil then exit('');
    for i := 0 to arr.Count - 1 do
    begin
      it := arr.Items[i];
      if i > 0 then Result := Result + ' ';
      Result := Result + Audio_Sentence(Audio_Clean(Audio_S(it, 'text')));
    end;
    exit;
  end;
  if format = 'facts' then
  begin
    if arr = nil then exit('');
    for i := 0 to arr.Count - 1 do
    begin
      it := arr.Items[i];
      if i > 0 then Result := Result + ' ';
      Result := Result + Audio_Sentence(Audio_Clean(Audio_S(it, 'label')) + ': ' + Audio_Clean(Audio_S(it, 'value')));
    end;
    exit;
  end;
  if format = 'timeline' then
  begin
    if arr = nil then exit('');
    for i := 0 to arr.Count - 1 do
    begin
      it := arr.Items[i];
      bits := '';
      if Audio_Clean(Audio_S(it, 'date')) <> '' then bits := Audio_Clean(Audio_S(it, 'date'));
      if Audio_Clean(Audio_S(it, 'title')) <> '' then
      begin
        if bits <> '' then bits := bits + ', ';
        bits := bits + Audio_Clean(Audio_S(it, 'title'));
      end;
      if Audio_Clean(Audio_S(it, 'event')) <> '' then
      begin
        if bits <> '' then bits := bits + ', ';
        bits := bits + Audio_Clean(Audio_S(it, 'event'));
      end;
      if i > 0 then Result := Result + ' ';
      Result := Result + Audio_Sentence(bits);
    end;
    exit;
  end;
  if format = 'perspectives' then
  begin
    if arr = nil then exit('');
    for i := 0 to arr.Count - 1 do
    begin
      it := arr.Items[i];
      if i > 0 then Result := Result + ' ';
      Result := Result + Audio_Sentence(Audio_Clean(Audio_S(it, 'actor')) + ': ' + Audio_Clean(Audio_S(it, 'position')));
    end;
    exit;
  end;
  if format = 'proscons' then
  begin
    good := JArr(value, 'good');
    bad := JArr(value, 'bad');
    s1 := '';
    if (good <> nil) and (good.Count > 0) then
    begin
      s1 := 'Эерэг тал: ';
      for i := 0 to good.Count - 1 do
      begin
        if i > 0 then s1 := s1 + ' ';
        s1 := s1 + Audio_Sentence(Audio_Clean(Audio_S(good.Items[i], 'text')));
      end;
    end;
    s2 := '';
    if (bad <> nil) and (bad.Count > 0) then
    begin
      s2 := 'Сөрөг тал: ';
      for i := 0 to bad.Count - 1 do
      begin
        if i > 0 then s2 := s2 + ' ';
        s2 := s2 + Audio_Sentence(Audio_Clean(Audio_S(bad.Items[i], 'text')));
      end;
    end;
    Result := s1;
    if s2 <> '' then
    begin
      if Result <> '' then Result := Result + ' ';
      Result := Result + s2;
    end;
    exit;
  end;
end;

// last 1-based position of sub in u (0 = none)
function Audio_LastPos(const sub, u: UnicodeString): Integer;
var
  i: Integer;
begin
  Result := 0;
  for i := Length(u) - Length(sub) + 1 downto 1 do
    if Copy(u, i, Length(sub)) = sub then exit(i);
end;

// cuts text to max utf-16 units at the last sentence end (or word) before it
function Audio_CutAtSentence(const text: string; max: Integer): string;
var
  u, head: UnicodeString;
  e, e2, sp: Integer;
begin
  u := UTF8Decode(text);
  if Length(u) <= max then exit(text);
  head := Copy(u, 1, max);
  // js lastIndexOf is 0-based, -1 when missing
  e := Audio_LastPos('. ', head) - 1;
  e2 := Audio_LastPos('? ', head) - 1; if e2 > e then e := e2;
  e2 := Audio_LastPos('! ', head) - 1; if e2 > e then e := e2;
  if e > max / 2 then
    Result := UTF8Encode(Copy(head, 1, e + 1))
  else
  begin
    sp := Audio_LastPos(' ', head) - 1;
    if sp < 0 then Result := UTF8Encode(Copy(head, 1, Length(head) - 1)) + AUDIO_ELLIPSIS
    else Result := UTF8Encode(Copy(head, 1, sp)) + AUDIO_ELLIPSIS;
  end;
end;

function Audio_IsTerm(c: WideChar): Boolean;
begin
  Result := (c = '.') or (c = '!') or (c = '?') or (c = #$2026);
end;

// sentences joined into parts of at most max utf-16 units (the first at most first); adds to out list
procedure Audio_SplitParts(const text: string; max, first: Integer; outList: TStrings);
var
  u, cur, sen: UnicodeString;
  i, n, st, limit, cnt: Integer;
  sentences: array of UnicodeString;
begin
  u := UTF8Decode(text);
  n := Length(u);
  SetLength(sentences, 0);
  i := 1;
  while i <= n do
  begin
    if Audio_IsTerm(u[i]) then begin Inc(i); continue; end;
    st := i;
    while (i <= n) and not Audio_IsTerm(u[i]) do Inc(i);
    while (i <= n) and Audio_IsTerm(u[i]) do Inc(i);
    sen := Trim(Copy(u, st, i - st));
    if sen <> '' then
    begin
      SetLength(sentences, Length(sentences) + 1);
      sentences[High(sentences)] := sen;
    end;
  end;
  cur := '';
  cnt := 0;
  for i := 0 to High(sentences) do
  begin
    if cnt > 0 then limit := max else limit := first;
    if (cur <> '') and (Length(cur) + 1 + Length(sentences[i]) > limit) then
    begin
      outList.Add(UTF8Encode(cur));
      Inc(cnt);
      cur := sentences[i];
    end
    else if cur <> '' then cur := cur + ' ' + sentences[i]
    else cur := sentences[i];
  end;
  if cur <> '' then outList.Add(UTF8Encode(cur));
end;

// fills NarrTitles/NarrTexts with the parts of a topic (no LLM call)
procedure Audio_BuildNarration(topic: TJSONObject);
var
  cover, s1, s2, story, body, text, lkey: string;
  cardTitles, cardTexts, tmp: TStringList;
  lenses: TJSONArray;
  wraps: TJSONObject;
  lens: TJSONData;
  total, i, j, mx: Integer;
begin
  if NarrTitles = nil then NarrTitles := TStringList.Create;
  if NarrTexts = nil then NarrTexts := TStringList.Create;
  NarrTitles.Clear; NarrTexts.Clear;
  s1 := Audio_Sentence(Audio_Clean(JStr(topic, 'title')));
  story := JStr(topic, 'storyline');
  if story = '' then story := JStr(topic, 'reason');
  s2 := Audio_Sentence(Audio_Clean(story));
  cover := s1;
  if s2 <> '' then
  begin
    if cover <> '' then cover := cover + ' ';
    cover := cover + s2;
  end;
  cardTitles := TStringList.Create;
  cardTexts := TStringList.Create;
  tmp := TStringList.Create;
  try
    cardTitles.Add(JStr(topic, 'title'));
    cardTexts.Add(cover);
    total := Audio_U16Len(cover);
    lenses := JArr(topic, 'lenses');
    wraps := JObj(topic, 'wraps');
    if lenses <> nil then
      for i := 0 to lenses.Count - 1 do
      begin
        lens := lenses.Items[i];
        lkey := JStr(lens, 'key');
        if wraps <> nil then body := Audio_LensText(JStr(lens, 'format'), wraps.Find(lkey))
        else body := '';
        if (body = '') or (total >= AUDIO_TOTAL_CHARS) then continue;
        mx := AUDIO_TOTAL_CHARS - total;
        if AUDIO_CARD_CHARS < mx then mx := AUDIO_CARD_CHARS;
        text := Audio_CutAtSentence(Audio_Sentence(Audio_Clean(JStr(lens, 'label'))) + ' ' + body, mx);
        cardTitles.Add(JStr(lens, 'label'));
        cardTexts.Add(text);
        total := total + Audio_U16Len(text);
      end;
    for i := 0 to cardTexts.Count - 1 do
    begin
      tmp.Clear;
      if i = 0 then Audio_SplitParts(cardTexts[i], AUDIO_PART_CHARS, AUDIO_FIRST_PART_CHARS, tmp)
      else Audio_SplitParts(cardTexts[i], AUDIO_PART_CHARS, AUDIO_PART_CHARS, tmp);
      for j := 0 to tmp.Count - 1 do
      begin
        NarrTitles.Add(cardTitles[i]);
        NarrTexts.Add(tmp[j]);
      end;
    end;
  finally
    cardTitles.Free; cardTexts.Free; tmp.Free;
  end;
end;

function Audio_ClipKey(const model, voice, text: string): string;
begin
  Result := Sha256Hex(model + #10 + voice + #10 + text);
end;

// visible topic or 404 (also fills the narration globals). caller frees
function Audio_NarrationOf(const topicId: string): TJSONObject;
begin
  Result := nil;
  if IsId(topicId) then Result := DbGet('topics', topicId);
  if (Result = nil) or JBool(Result, 'hidden') then
  begin
    FreeAndNil(Result);
    raise EApi.CreateS(404, 'Not found');
  end;
  Audio_BuildNarration(Result);
end;

// little-endian helpers for the wav header
function LE32(v: LongWord): string;
begin
  Result := Chr(v and $FF) + Chr((v shr 8) and $FF) + Chr((v shr 16) and $FF) + Chr((v shr 24) and $FF);
end;

function LE16(v: Word): string;
begin
  Result := Chr(v and $FF) + Chr((v shr 8) and $FF);
end;

function Audio_PcmToWav(const pcm: string): string;
begin
  Result := 'RIFF' + LE32(36 + Length(pcm)) + 'WAVE' + 'fmt ' + LE32(16) + LE16(1) + LE16(1) +
    LE32(AUDIO_SAMPLE_RATE) + LE32(AUDIO_SAMPLE_RATE * 2) + LE16(2) + LE16(16) + 'data' + LE32(Length(pcm)) + pcm;
end;

function Audio_LameBin: string;
begin
  Result := Env('LAME_BIN', '/opt/homebrew/bin/lame');
  if not FileExists(Result) then Result := ExeSearch('lame', GetEnvironmentVariable('PATH'));
end;

// 16-bit mono 24 kHz pcm -> mp3 48 kbps via the lame CLI. '' when lame is missing or fails
function Audio_PcmToMp3(const pcm: string): string;
var
  lame, inF, outF, outp: string;
  p: TProcess;
  o: TStringList;
begin
  Result := '';
  lame := Audio_LameBin;
  if (lame = '') or not FileExists(lame) then exit;
  inF := TmpName('.pcm');
  outF := TmpName('.mp3');
  WriteFileStr(inF, pcm);
  p := TProcess.Create(nil);
  o := TStringList.Create;
  try
    p.Executable := lame;
    p.Parameters.Add('--quiet');
    p.Parameters.Add('-r');
    p.Parameters.Add('-s'); p.Parameters.Add('24');
    p.Parameters.Add('--bitwidth'); p.Parameters.Add('16');
    p.Parameters.Add('--signed');
    p.Parameters.Add('--little-endian');
    p.Parameters.Add('-m'); p.Parameters.Add('m');
    p.Parameters.Add('-b'); p.Parameters.Add(IntToStr(AUDIO_KBPS));
    p.Parameters.Add('--cbr');
    p.Parameters.Add(inF);
    p.Parameters.Add(outF);
    p.Options := [poWaitOnExit, poUsePipes, poStderrToOutPut];
    try
      p.Execute;
      o.LoadFromStream(p.Output);
      outp := o.Text;
      if (p.ExitStatus = 0) and FileExists(outF) then Result := ReadFileStr(outF)
      else Log('lame failed: ' + Copy(outp, 1, 300));
    except
      on E: Exception do Log('lame failed: ' + E.Message);
    end;
  finally
    p.Free; o.Free;
    if FileExists(inF) then DeleteFile(inF);
    if FileExists(outF) then DeleteFile(outF);
  end;
end;

// Gemini TTS: returns encoded audio (mp3, or wav fallback); sets AudioLastSeconds/In/Out/Format
function Audio_Synthesize(const text, model, voice: string): string;
var
  body, res: TJSONObject;
  parts: TJSONData;
  i: Integer;
  data, pcm, mp3: string;
begin
  Result := '';
  body := TJSONObject(JParse('{"contents":[{"role":"user","parts":[{"text":' + JEsc(AUDIO_STYLE + #10 + text) + '}]}],' +
    '"generationConfig":{"responseModalities":["AUDIO"],"speechConfig":{"languageCode":"mn-MN",' +
    '"voiceConfig":{"prebuiltVoiceConfig":{"voiceName":' + JEsc(voice) + '}}}}}'));
  try
    res := GeminiRaw(model, body.AsJSON);
  finally
    body.Free;
  end;
  try
    data := '';
    parts := JPath(res, 'candidates.0.content.parts');
    if (parts <> nil) and (parts is TJSONArray) then
      for i := 0 to TJSONArray(parts).Count - 1 do
        if JStr(JObj(TJSONArray(parts).Items[i], 'inlineData'), 'data') <> '' then
        begin
          data := JStr(JObj(TJSONArray(parts).Items[i], 'inlineData'), 'data');
          break;
        end;
    if data = '' then raise Exception.Create('Empty speech response');
    pcm := DecodeStringBase64(data);
    AudioLastIn := JInt(JObj(res, 'usageMetadata'), 'promptTokenCount');
    AudioLastOut := JInt(JObj(res, 'usageMetadata'), 'candidatesTokenCount');
  finally
    res.Free;
  end;
  if Odd(Length(pcm)) then SetLength(pcm, Length(pcm) - 1);
  AudioLastSeconds := Length(pcm) / 2 / AUDIO_SAMPLE_RATE;
  mp3 := Audio_PcmToMp3(pcm);
  if mp3 <> '' then
  begin
    AudioLastFormat := 'mp3';
    Result := mp3;
  end
  else
  begin
    Log('lame not available, storing WAV instead of MP3');
    AudioLastFormat := 'wav';
    Result := Audio_PcmToWav(pcm);
  end;
end;

// stored or freshly generated clip bytes for the text; fmt = mp3|wav
function Audio_ClipFor(const topicId, text: string; out fmt: string): string;
var
  st, audio, doc, usage: TJSONObject;
  model, voice, key, fn: string;
  all: TJSONArray;
  i, today, limit: Integer;
  startToday: TDateTime;
begin
  Result := '';
  fmt := 'mp3';
  st := GetSettings;
  try
    audio := JObj(st, 'audio');
    if not JBool(audio, 'enabled', True) then raise EApi.CreateS(404, 'Audio is turned off');
    model := JStr(audio, 'model', 'gemini-2.5-flash-tts');
    voice := JStr(audio, 'voice', 'Charon');
    limit := JInt(audio, 'dailyLimit', 200);
  finally
    st.Free;
  end;
  key := Audio_ClipKey(model, voice, text);
  doc := DbFindOne('audioclips', 'key', key);
  if doc <> nil then
  begin
    fmt := JStr(doc, 'format', 'mp3');
    fn := IncludeTrailingPathDelimiter(DataDir) + 'audio' + PathDelim + JStr(doc, 'file', key + '.' + fmt);
    if FileExists(fn) then
    begin
      doc.Free;
      exit(ReadFileStr(fn));
    end;
    // file vanished: forget the doc and make it again
    DbDelete('audioclips', JStr(doc, '_id'));
    doc.Free;
  end;
  if limit > 0 then
  begin
    startToday := IncHour(ParseIso(TodayMn), -8);
    all := DbLoad('audioclips');
    today := 0;
    for i := 0 to all.Count - 1 do
      if ParseIso(JStr(all.Items[i], 'createdAt')) >= startToday then Inc(today);
    all.Free;
    if today >= limit then raise EApi.CreateS(429, 'Daily audio limit reached');
  end;
  try
    Result := Audio_Synthesize(text, model, voice);
  except
    on E: EApi do raise Exception.Create(E.Message); // provider errors are 500s like in the node app
  end;
  fmt := AudioLastFormat;
  ForceDirectories(IncludeTrailingPathDelimiter(DataDir) + 'audio');
  fn := key + '.' + fmt;
  WriteFileStr(IncludeTrailingPathDelimiter(DataDir) + 'audio' + PathDelim + fn, Result);
  doc := DbFindOne('audioclips', 'key', key);
  if doc = nil then
  begin
    doc := TJSONObject.Create;
    JSetS(doc, 'key', key);
    JSetS(doc, 'topicId', topicId);
    JSetS(doc, 'model', model);
    JSetS(doc, 'voice', voice);
    JSetN(doc, 'chars', Audio_U16Len(text));
    JSetN(doc, 'seconds', AudioLastSeconds);
    JSetN(doc, 'bytes', Length(Result));
    JSetS(doc, 'format', fmt);
    JSetS(doc, 'file', fn);
    usage := TJSONObject.Create;
    usage.Add('inputTokens', AudioLastIn);
    usage.Add('outputTokens', AudioLastOut);
    JSet(doc, 'usage', usage);
    JSet(doc, 'costUsd', TJSONFloatNumber.Create(TtsCostUsd(model, AudioLastIn, AudioLastOut)));
    DbInsert('audioclips', doc);
  end;
  doc.Free;
  Log('Audio clip generated topicId=' + topicId + ' seconds=' + IntToStr(Round(AudioLastSeconds)));
end;

// binary body via ContentStream (Resp.Content goes through TStrings and mangles bytes)
procedure Audio_SendBytes(status: Integer; const ctype, data: string);
var
  ms: TMemoryStream;
begin
  SendRaw(status, ctype, '');
  ms := TMemoryStream.Create;
  if Length(data) > 0 then ms.WriteBuffer(data[1], Length(data));
  ms.Position := 0;
  Resp.ContentStream := ms;
  Resp.FreeContentStream := True;
  Resp.ContentLength := Length(data);
end;

// sends audio with Range support (Safari and iOS refuse to play audio without it)
procedure Audio_SendClip(const data, ctype: string);
var
  range, a, b: string;
  len, start, stop, dash: Int64;
  ok: Boolean;
  i: Integer;
begin
  len := Length(data);
  range := Req.GetHeader(hhRange);
  if range = '' then range := Req.GetFieldByName('Range');
  if range = '' then range := Req.CustomHeaders.Values['Range'];
  ok := Copy(range, 1, 6) = 'bytes=';
  a := ''; b := '';
  if ok then
  begin
    dash := Pos('-', range);
    if dash = 0 then ok := False
    else
    begin
      a := Copy(range, 7, dash - 7);
      b := Copy(range, dash + 1, MaxInt);
      for i := 1 to Length(a) do if not (a[i] in ['0'..'9']) then ok := False;
      for i := 1 to Length(b) do if not (b[i] in ['0'..'9']) then ok := False;
    end;
  end;
  if not ok then
  begin
    Audio_SendBytes(200, ctype, data);
    Resp.SetCustomHeader('Accept-Ranges', 'bytes');
    Resp.SetCustomHeader('Cache-Control', 'public, max-age=3600');
    exit;
  end;
  if a <> '' then start := StrToInt64Def(a, 0) else start := len - StrToInt64Def(b, 0);
  if start < 0 then start := 0;
  if (a <> '') and (b <> '') then
  begin
    stop := StrToInt64Def(b, 0);
    if stop > len - 1 then stop := len - 1;
  end
  else stop := len - 1;
  if (start > stop) or (start >= len) then
  begin
    SendRaw(416, ctype, '');
    Resp.CodeText := 'Range Not Satisfiable';
    Resp.SetCustomHeader('Accept-Ranges', 'bytes');
    Resp.SetCustomHeader('Cache-Control', 'public, max-age=3600');
    Resp.SetCustomHeader('Content-Range', 'bytes */' + IntToStr(len));
    exit;
  end;
  Audio_SendBytes(206, ctype, Copy(data, start + 1, stop - start + 1));
  Resp.CodeText := 'Partial Content';
  Resp.SetCustomHeader('Accept-Ranges', 'bytes');
  Resp.SetCustomHeader('Cache-Control', 'public, max-age=3600');
  Resp.SetCustomHeader('Content-Range', 'bytes ' + IntToStr(start) + '-' + IntToStr(stop) + '/' + IntToStr(len));
end;

procedure Routes_audio;
var
  st, audio, topic: TJSONObject;
  model, voice, id, seg, data, fmt: string;
  keys: TStringList;
  clips, partsArr: TJSONArray;
  part: TJSONObject;
  i, j, idx: Integer;
  found: Boolean;
  enabled: Boolean;
begin
  // GET /api/topics/:id/audio - the parts of a topic and which are ready
  if Route('GET', '/api/topics/:id/audio') then
  begin
    st := GetSettings;
    audio := JObj(st, 'audio');
    enabled := JBool(audio, 'enabled', True);
    model := JStr(audio, 'model', 'gemini-2.5-flash-tts');
    voice := JStr(audio, 'voice', 'Charon');
    st.Free;
    id := P(2);
    topic := Audio_NarrationOf(id);
    topic.Free;
    keys := TStringList.Create;
    for i := 0 to NarrTexts.Count - 1 do keys.Add(Audio_ClipKey(model, voice, NarrTexts[i]));
    clips := DbLoad('audioclips');
    partsArr := TJSONArray.Create;
    for i := 0 to NarrTexts.Count - 1 do
    begin
      part := TJSONObject.Create;
      part.Add('index', i);
      part.Add('title', NarrTitles[i]);
      part.Add('chars', Audio_U16Len(NarrTexts[i]));
      found := False;
      for j := 0 to clips.Count - 1 do
        if JStr(clips.Items[j], 'key') = keys[i] then
        begin
          part.Add('seconds', TJSONFloatNumber.Create(JNum(clips.Items[j], 'seconds')));
          found := True;
          break;
        end;
      if not found then part.Add('seconds', TJSONNull.Create); // null = generated on the first play
      part.Add('url', '/api/topics/' + id + '/audio/' + IntToStr(i) + '.mp3');
      partsArr.Add(part);
    end;
    clips.Free;
    keys.Free;
    SendJson(200, TJSONObject.Create(['enabled', enabled, 'parts', partsArr]));
    exit;
  end;

  // GET /api/topics/:id/audio/:index.mp3 - one part, generated on its first play
  if Route('GET', '/api/topics/:id/audio/:index') and (LowerCase(Copy(P(4), Length(P(4)) - 3, 4)) = '.mp3') and (Length(P(4)) > 4) then
  begin
    id := P(2);
    seg := Copy(P(4), 1, Length(P(4)) - 4);
    topic := Audio_NarrationOf(id);
    topic.Free;
    idx := -1;
    if (seg <> '') and (Length(seg) < 9) then
    begin
      idx := 0;
      for i := 1 to Length(seg) do
        if seg[i] in ['0'..'9'] then idx := idx * 10 + Ord(seg[i]) - 48
        else begin idx := -1; break; end;
    end;
    if (idx < 0) or (idx >= NarrTexts.Count) then raise EApi.CreateS(404, 'Not found');
    data := Audio_ClipFor(id, NarrTexts[idx], fmt);
    if fmt = 'wav' then Audio_SendClip(data, 'audio/wav')
    else Audio_SendClip(data, 'audio/mpeg');
    exit;
  end;
end;
