// ===================== SETTINGS (modules/settings/settings.routes.ts + migrateSettings) =====================
// GetSettings / DEFAULT_SETTINGS / MAX_TOPICS_PER_RUN live in core.inc.

const
  SET_JOB_KINDS: array[0..3] of string = ('news', 'social', 'publish', 'world');
  SET_SOCIAL_CRON = '0 8-22 * * *';
  SET_PUBLISH_CRON = '10 8-22 * * *';
  SET_DEFAULT_CREDIT_USD = 0.00188;
  SET_MODEL_RX = '^gemini-[a-z0-9.\-]+$';
  SET_MODEL_MSG = 'Gemini model id, e.g. gemini-3.5-flash';

// node-cron validate, roughly: 5 or 6 fields of numbers / ranges / steps / lists / names
function Set_CronValid(const expr: string): Boolean;
var
  f, items: TStringList;
  i, j, o, lo, hi, a, b, p: Integer;
  it, rng: string;
  names: string;
begin
  Result := False;
  f := TStringList.Create;
  items := TStringList.Create;
  try
    f.Delimiter := ' '; f.StrictDelimiter := False; f.DelimitedText := Trim(expr);
    if (f.Count <> 5) and (f.Count <> 6) then exit;
    o := 0;
    if f.Count = 6 then o := 1;
    for i := 0 to f.Count - 1 do
    begin
      case i - o of
        -1: begin lo := 0; hi := 59; end;
        0: begin lo := 0; hi := 59; end;
        1: begin lo := 0; hi := 23; end;
        2: begin lo := 1; hi := 31; end;
        3: begin lo := 1; hi := 12; end;
      else
        begin lo := 0; hi := 7; end;
      end;
      names := LowerCase(f[i]);
      // month / weekday names are fine for node-cron
      if (i - o = 3) and RxMatch('^[a-z,\-]+$', names) then continue;
      if (i - o = 4) and RxMatch('^[a-z,\-]+$', names) then continue;
      items.Delimiter := ','; items.StrictDelimiter := True; items.DelimitedText := f[i];
      for j := 0 to items.Count - 1 do
      begin
        it := items[j];
        if it = '' then exit;
        p := Pos('/', it);
        if p > 0 then
        begin
          if StrToIntDef(Copy(it, p + 1, MaxInt), 0) <= 0 then exit;
          rng := Copy(it, 1, p - 1);
        end
        else rng := it;
        if rng = '*' then continue;
        if Pos('-', rng) > 0 then
        begin
          a := StrToIntDef(Copy(rng, 1, Pos('-', rng) - 1), -1);
          b := StrToIntDef(Copy(rng, Pos('-', rng) + 1, MaxInt), -1);
          if (a < lo) or (a > hi) or (b < lo) or (b > hi) or (b < a) then exit;
        end
        else
        begin
          a := StrToIntDef(rng, -1);
          if (a < lo) or (a > hi) then exit;
        end;
      end;
    end;
    Result := True;
  finally
    f.Free;
    items.Free;
  end;
end;

// next start of a job (ISO UTC), '' when off or the scheduler is not running in this process
function Set_NextRun(const cron: string; enabled: Boolean): string;
var
  mn: TDateTime;
  i: Integer;
begin
  Result := '';
  if not enabled then exit;
  if Env('RUN_SCHEDULER', 'true') <> 'true' then exit;
  mn := IncHour(NowUtc, 8);
  mn := RecodeSecond(RecodeMilliSecond(mn, 0), 0);
  for i := 1 to 8 * 24 * 60 do
  begin
    mn := IncMinute(mn, 1);
    if CronMatches(cron, mn) then exit(IsoOf(IncHour(mn, -8)));
  end;
end;

function Set_ProviderOptions: TJSONObject;
begin
  Result := TJSONObject(JParse('{"facebook":[{"id":"scrapecreators","label":"ScrapeCreators"}],' +
    '"x":[{"id":"twitterapiio","label":"twitterapi.io"}],' +
    '"threads":[{"id":"scrapecreators","label":"ScrapeCreators"}]}'));
end;

function Set_ProviderSupports(const id, platform: string): Boolean;
begin
  if id = 'twitterapiio' then Result := platform = 'x'
  else if id = 'scrapecreators' then Result := (platform = 'facebook') or (platform = 'threads')
  else Result := False;
end;

// settings doc + nextRuns + providerOptions (what GET/PUT answer). caller frees
function Set_Response: TJSONObject;
var
  nr: TJSONObject;
  i: Integer;
  sch: TJSONObject;
  nx: string;
begin
  Result := GetSettings;
  nr := TJSONObject.Create;
  for i := 0 to High(SET_JOB_KINDS) do
  begin
    sch := TJSONObject(JPath(Result, 'schedules.' + SET_JOB_KINDS[i]));
    nx := '';
    if sch <> nil then nx := Set_NextRun(JStr(sch, 'cron'), JBool(sch, 'enabled', True));
    if nx = '' then nr.Add(SET_JOB_KINDS[i], TJSONNull.Create) else nr.Add(SET_JOB_KINDS[i], nx);
  end;
  JSet(Result, 'nextRuns', nr);
  JSet(Result, 'providerOptions', Set_ProviderOptions);
end;

// boot migrations of the settings document (settings.model.ts migrateSettings)
procedure Set_Migrate;
var
  all: TJSONArray;
  i: Integer;
  o, sch, pub, soc, s2: TJSONObject;
  changed: Boolean;
begin
  all := DbLoad('settings');
  changed := False;
  for i := 0 to all.Count - 1 do
  begin
    o := TJSONObject(all.Items[i]);
    if JStr(o, '_id') <> 'global' then continue;
    // pre-split single schedule -> publish job
    if o.Find('scheduleCron') <> nil then
    begin
      sch := JObj(o, 'schedules');
      if sch = nil then begin sch := TJSONObject.Create; JSet(o, 'schedules', sch); end;
      pub := TJSONObject.Create;
      pub.Add('enabled', JBool(o, 'scheduleEnabled', True));
      pub.Add('cron', JStr(o, 'scheduleCron'));
      JSet(sch, 'publish', pub);
      o.Delete('scheduleCron');
      if o.IndexOfName('scheduleEnabled') >= 0 then o.Delete('scheduleEnabled');
      changed := True;
    end;
    // Apify actors -> providers
    soc := JObj(o, 'social');
    if (soc <> nil) and (soc.Find('actors') <> nil) then
    begin
      JSet(soc, 'providers', JParse('{"facebook":"scrapecreators","x":"twitterapiio","threads":"scrapecreators"}'));
      JSetN(soc, 'creditUsd', SET_DEFAULT_CREDIT_USD);
      JSetN(soc, 'overlapHours', 1);
      soc.Delete('actors');
      sch := JObj(o, 'schedules');
      if sch = nil then begin sch := TJSONObject.Create; JSet(o, 'schedules', sch); end;
      s2 := JObj(sch, 'social');
      if s2 = nil then begin s2 := TJSONObject.Create; s2.Add('enabled', True); JSet(sch, 'social', s2); end;
      JSetS(s2, 'cron', SET_SOCIAL_CRON);
      s2 := JObj(sch, 'publish');
      if s2 = nil then begin s2 := TJSONObject.Create; s2.Add('enabled', True); JSet(sch, 'publish', s2); end;
      JSetS(s2, 'cron', SET_PUBLISH_CRON);
      changed := True;
    end;
  end;
  if changed then
  begin
    DbSave('settings', all);
    Log('Settings migrated');
  end;
  all.Free;
end;

// validates one {enabled, cron} schedule into a new object (nil on error)
function Set_VSchedule(parent: TJSONData; const key, path: string): TJSONObject;
var
  o: TJSONObject;
  en: Boolean;
  cr: string;
begin
  Result := nil;
  o := PvObj(parent, key, path);
  if o = nil then exit;
  en := PvBool(o, 'enabled', PvPath(path, key));
  cr := PvStr(o, 'cron', PvPath(path, key), False, '', False, False, -1, -1, '', '');
  if (o.Find('cron') <> nil) and (o.Find('cron').JSONType = jtString) and not Set_CronValid(cr) then
    PvFail(PvPath(PvPath(path, key), 'cron'), 'Invalid cron expression');
  Result := TJSONObject.Create;
  Result.Add('enabled', en);
  Result.Add('cron', cr);
end;

procedure Set_AddNum(o: TJSONObject; const key: string; v: Double);
begin
  if Frac(v) = 0 then o.Add(key, TJSONInt64Number.Create(Trunc(v))) else o.Add(key, TJSONFloatNumber.Create(v));
end;

procedure Routes_settings;
var
  b, sch, mo, so, pr, hw, wo, au, ch, x: TJSONObject;
  nb, stored: TJSONObject;
  i: Integer;
  k, v: string;
  plats: array[0..2] of string;
  emptyBody: TJSONObject;
begin
  if Route('GET', '/api/admin/settings') then
  begin
    RequirePerm('settings');
    SendJson(200, Set_Response);
    exit;
  end;

  if Route('PUT', '/api/admin/settings') then
  begin
    RequirePerm('settings');
    emptyBody := nil;
    if (ReqBody <> nil) and (ReqBody is TJSONObject) then b := TJSONObject(ReqBody)
    else begin emptyBody := TJSONObject.Create; b := emptyBody; end;
    nb := TJSONObject.Create; // the parsed body (only known keys, like zod)
    try
      PvReset;
      // schedules
      sch := PvObj(b, 'schedules', '');
      if sch <> nil then
      begin
        x := TJSONObject.Create;
        for i := 0 to 2 do
        begin
          mo := Set_VSchedule(sch, SET_JOB_KINDS[i], 'schedules');
          if mo <> nil then x.Add(SET_JOB_KINDS[i], mo);
        end;
        if sch.Find('world') <> nil then
        begin
          mo := Set_VSchedule(sch, 'world', 'schedules');
          if mo <> nil then x.Add('world', mo);
        end;
        nb.Add('schedules', x);
      end;
      Set_AddNum(nb, 'minNewItems', PvNum(b, 'minNewItems', '', True, True, 0, False, 0));
      Set_AddNum(nb, 'windowDays', PvNum(b, 'windowDays', '', True, True, 1, True, 60));
      Set_AddNum(nb, 'topicsPerRun', PvNum(b, 'topicsPerRun', '', True, True, 1, True, MAX_TOPICS_PER_RUN));
      Set_AddNum(nb, 'articlesPerSource', PvNum(b, 'articlesPerSource', '', True, True, 10, True, 2000));
      Set_AddNum(nb, 'dailyTokenLimit', PvNum(b, 'dailyTokenLimit', '', True, True, 0, False, 0));
      // models
      mo := PvObj(b, 'models', '');
      if mo <> nil then
      begin
        x := TJSONObject.Create;
        x.Add('cluster', PvStr(mo, 'cluster', 'models', False, '', False, False, -1, -1, SET_MODEL_RX, SET_MODEL_MSG));
        x.Add('select', PvStr(mo, 'select', 'models', False, '', False, False, -1, -1, SET_MODEL_RX, SET_MODEL_MSG));
        x.Add('wrap', PvStr(mo, 'wrap', 'models', False, '', False, False, -1, -1, SET_MODEL_RX, SET_MODEL_MSG));
        x.Add('extract', PvStr(mo, 'extract', 'models', False, '', False, False, -1, -1, SET_MODEL_RX, SET_MODEL_MSG));
        nb.Add('models', x);
      end;
      // social
      so := PvObj(b, 'social', '');
      if so <> nil then
      begin
        x := TJSONObject.Create;
        x.Add('enabled', PvBool(so, 'enabled', 'social'));
        pr := PvObj(so, 'providers', 'social');
        if pr <> nil then
        begin
          plats[0] := 'facebook'; plats[1] := 'x'; plats[2] := 'threads';
          mo := TJSONObject.Create;
          for i := 0 to 2 do
          begin
            v := PvStr(pr, plats[i], 'social.providers', False, '', False, False, -1, -1, '', '');
            if (pr.Find(plats[i]) <> nil) and (pr.Find(plats[i]).JSONType = jtString) and not Set_ProviderSupports(v, plats[i]) then
              PvFail('social.providers.' + plats[i], 'No such provider for ' + plats[i]);
            mo.Add(plats[i], v);
          end;
          x.Add('providers', mo);
        end;
        Set_AddNum(x, 'creditUsd', PvNum(so, 'creditUsd', 'social', False, True, 0, True, 1));
        Set_AddNum(x, 'postsPerAccount', PvNum(so, 'postsPerAccount', 'social', True, True, 1, True, 100));
        Set_AddNum(x, 'postsPerRun', PvNum(so, 'postsPerRun', 'social', True, True, 0, True, 600));
        Set_AddNum(x, 'halfLifeHours', PvNum(so, 'halfLifeHours', 'social', False, True, 1, True, 240));
        Set_AddNum(x, 'overlapHours', PvNum(so, 'overlapHours', 'social', False, True, 0, True, 168));
        nb.Add('social', x);
      end;
      // hot weights
      hw := PvObj(b, 'hotWeights', '');
      if hw <> nil then
      begin
        x := TJSONObject.Create;
        Set_AddNum(x, 'news', PvNum(hw, 'news', 'hotWeights', False, True, 0, True, 1));
        Set_AddNum(x, 'social', PvNum(hw, 'social', 'hotWeights', False, True, 0, True, 1));
        Set_AddNum(x, 'velocity', PvNum(hw, 'velocity', 'hotWeights', False, True, 0, True, 1));
        nb.Add('hotWeights', x);
      end;
      // world (optional)
      if b.Find('world') <> nil then
      begin
        wo := PvObj(b, 'world', '');
        if wo <> nil then
        begin
          x := TJSONObject.Create;
          Set_AddNum(x, 'windowDays', PvNum(wo, 'windowDays', 'world', True, True, 1, True, 14));
          Set_AddNum(x, 'topicsPerRun', PvNum(wo, 'topicsPerRun', 'world', True, True, 1, True, 20));
          Set_AddNum(x, 'minNewItems', PvNum(wo, 'minNewItems', 'world', True, True, 0, False, 0));
          Set_AddNum(x, 'articlesPerSource', PvNum(wo, 'articlesPerSource', 'world', True, True, 10, True, 2000, True, 40));
          nb.Add('world', x);
        end;
      end;
      // audio (optional)
      if b.Find('audio') <> nil then
      begin
        au := PvObj(b, 'audio', '');
        if au <> nil then
        begin
          x := TJSONObject.Create;
          x.Add('enabled', PvBool(au, 'enabled', 'audio'));
          x.Add('model', PvStr(au, 'model', 'audio', False, '', False, False, -1, -1, '^gemini-[a-z0-9.\-]+-tts[a-z0-9.\-]*$', 'Gemini TTS model id'));
          x.Add('voice', PvStr(au, 'voice', 'audio', False, '', False, False, -1, -1, '^[A-Z][a-z]+$', 'Gemini voice name, e.g. Charon'));
          Set_AddNum(x, 'dailyLimit', PvNum(au, 'dailyLimit', 'audio', True, True, 0, False, 0));
          nb.Add('audio', x);
        end;
      end;
      // chat (optional)
      if b.Find('chat') <> nil then
      begin
        ch := PvObj(b, 'chat', '');
        if ch <> nil then
        begin
          x := TJSONObject.Create;
          x.Add('enabled', PvBool(ch, 'enabled', 'chat'));
          x.Add('model', PvStr(ch, 'model', 'chat', False, '', False, False, -1, -1, SET_MODEL_RX, SET_MODEL_MSG));
          Set_AddNum(x, 'dailyLimit', PvNum(ch, 'dailyLimit', 'chat', True, True, 0, False, 0));
          nb.Add('chat', x);
        end;
      end;
      PvDone;
      // like findByIdAndUpdate(body, upsert): every top level key of the body replaces the stored one
      stored := DbGet('settings', 'global');
      if stored = nil then
      begin
        stored := TJSONObject.Create;
        stored.Add('_id', 'global');
        for i := 0 to nb.Count - 1 do stored.Add(nb.Names[i], nb.Items[i].Clone);
        DbInsert('settings', stored);
      end
      else
      begin
        for i := 0 to nb.Count - 1 do
        begin
          k := nb.Names[i];
          JSet(stored, k, nb.Items[i].Clone);
        end;
        DbReplace('settings', stored);
      end;
      stored.Free;
      // applySchedule: the scheduler thread re-reads settings every minute, nothing to do here
      SendJson(200, Set_Response);
    finally
      nb.Free;
      emptyBody.Free;
    end;
    exit;
  end;
end;
