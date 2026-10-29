// ===================== CLI (scripts/create-user.ts, seed.ts, extract-entities.ts, merge-lenses.ts) =====================
// returns True when cmd was a CLI command (program exits after)
//   sedev create-user <username> <password> <role> [name]   (the original order <username> <role> <password> works too)
//   sedev seed
//   sedev extract-entities [--days N] [--rescan]
//   sedev merge-lenses [--dry-run]
//   sedev job news|social|publish|world | sedev job resume <runId>   (run-job.ts, blocking)
//   sedev sync-sources | recalc-weights [--days N] [--dry-run] | probe [slug] | collect-social
//   sedev backfill-visuals [--editions N] [--dry-run]

function Cli_HasFlag(const flag: string): Boolean;
var
  i: Integer;
begin
  Result := False;
  for i := 2 to ParamCount do
    if ParamStr(i) = flag then exit(True);
end;

function Cli_FlagValue(const flag: string): string;
var
  i: Integer;
begin
  Result := '';
  for i := 2 to ParamCount - 1 do
    if ParamStr(i) = flag then exit(ParamStr(i + 1));
end;

function Cli_Main(const cmd: string): Boolean;
var
  un, pw, role, name, tmp: string;
  nLenses, nEnts, nSources, nAccounts: Integer;
  defs: TJSONArray;
  s: TJSONObject;
begin
  Result := False;

  if cmd = 'create-user' then
  begin
    Result := True;
    un := LowerCase(Trim(ParamStr(2)));
    pw := ParamStr(3);
    role := ParamStr(4);
    name := ParamStr(5);
    // original script order: <username> <role> <password>
    if Auth_IsRole(pw) and not Auth_IsRole(role) then begin tmp := pw; pw := role; role := tmp; end;
    if (not Auth_UsernameOk(un)) or (not Auth_IsRole(role)) or (Length(pw) < 8) then
    begin
      WriteLn('Usage: sedev create-user <username> <password (8+ chars)> <' + StringReplace(ROLES_LIST, ',', '|', [rfReplaceAll]) + '> [name]');
      Halt(1);
    end;
    if Auth_UpsertUser(un, pw, role, name) then WriteLn('Created ' + role + ' account: ' + un)
    else WriteLn('Updated ' + role + ' account: ' + un);
    exit;
  end;

  if cmd = 'seed' then
  begin
    Result := True;
    Auth_MigrateLegacy;
    Auth_BootstrapAdmin;
    nSources := Src_SeedSources;
    nLenses := Lens_SeedDefaults;
    Soc_SeedAccounts;
    defs := Soc_DefaultAccounts;
    nAccounts := defs.Count;
    defs.Free;
    nEnts := Ent_SeedDefaults;
    s := GetSettings;
    s.Free;
    WriteLn('Seeded ' + IntToStr(nSources) + ' sources, ' + IntToStr(nLenses) + ' wrap points, ' + IntToStr(nAccounts) +
      ' social accounts, ' + IntToStr(nEnts) + ' people, settings');
    exit;
  end;

  if cmd = 'extract-entities' then
  begin
    Result := True;
    Ent_CliExtract(StrToIntDef(Cli_FlagValue('--days'), 0), Cli_HasFlag('--rescan'));
    exit;
  end;

  if cmd = 'job' then
  begin
    Result := True;
    Halt(Pipe_CliJob(ParamStr(2), ParamStr(3)));
  end;

  if cmd = 'sync-sources' then
  begin
    Result := True;
    Src_SyncSources;
    exit;
  end;

  if cmd = 'recalc-weights' then
  begin
    Result := True;
    Src_RecalcWeights;
    exit;
  end;

  if cmd = 'probe' then
  begin
    Result := True;
    Col_Probe(ParamStr(2));
    exit;
  end;

  if cmd = 'collect-social' then
  begin
    Result := True;
    Halt(Soc_CollectSocialScript);
  end;

  if cmd = 'backfill-visuals' then
  begin
    Result := True;
    if Cli_FlagValue('--editions') <> '' then
      Pipe_BackfillVisuals(StrToIntDef(Cli_FlagValue('--editions'), 4), Cli_HasFlag('--dry-run'))
    else
      Pipe_BackfillVisuals(4, Cli_HasFlag('--dry-run'));
    exit;
  end;

  if cmd = 'merge-lenses' then
  begin
    Result := True;
    Lens_MergeLenses(Cli_HasFlag('--dry-run'));
    exit;
  end;
end;
