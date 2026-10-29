// ===================== AUTH + USERS (modules/auth/auth.ts, users.routes.ts, user.model.ts) =====================
// also: utf8 helpers (Ppl*) and the zod-ish validators (Pv*) used by settings/lenses/entities/articles below.
// users collection: {_id, username, name, passwordHash, role, active, lastLoginAt, createdAt, updatedAt}

const
  AUTH_COOKIE = 'sedev_token';
  AUTH_MAX_AGE = 7 * 24 * 3600; // seconds (7 days)
  ROLES_LIST = 'superadmin,admin,editor,moderator,journalist';
  // /^[a-z0-9._-]{3,32}$/ checked by hand (Auth_UsernameOk): TRegExpr ^[class]{n,m}$ also matches shorter strings
  USERNAME_CHARS = 'abcdefghijklmnopqrstuvwxyz0123456789._-';
  USERNAME_MSG = '3–32 chars: a-z, 0-9, . _ -';

var
  PvErrs: string = '';

// ---------- utf8 helpers ----------
// decodes the code point starting at byte i, len = bytes used
function PplCpAt(const s: string; i: Integer; out len: Integer): Integer;
var
  b: Integer;
begin
  b := Ord(s[i]);
  if b < $80 then begin len := 1; exit(b); end;
  if (b and $E0) = $C0 then
  begin
    len := 2;
    if i + 1 > Length(s) then begin len := 1; exit(b); end;
    exit(((b and $1F) shl 6) or (Ord(s[i + 1]) and $3F));
  end;
  if (b and $F0) = $E0 then
  begin
    len := 3;
    if i + 2 > Length(s) then begin len := 1; exit(b); end;
    exit(((b and $0F) shl 12) or ((Ord(s[i + 1]) and $3F) shl 6) or (Ord(s[i + 2]) and $3F));
  end;
  if (b and $F8) = $F0 then
  begin
    len := 4;
    if i + 3 > Length(s) then begin len := 1; exit(b); end;
    exit(((b and $07) shl 18) or ((Ord(s[i + 1]) and $3F) shl 12) or ((Ord(s[i + 2]) and $3F) shl 6) or (Ord(s[i + 3]) and $3F));
  end;
  len := 1;
  Result := b;
end;

// code point that ends right before byte i (-1 when i is the start)
function PplCpBefore(const s: string; i: Integer): Integer;
var
  j, l: Integer;
begin
  Result := -1;
  if i <= 1 then exit;
  j := i - 1;
  while (j > 1) and ((Ord(s[j]) and $C0) = $80) do Dec(j);
  Result := PplCpAt(s, j, l);
end;

// start byte of the code point before byte i (0 when none)
function PplPrevStart(const s: string; i: Integer): Integer;
var
  j: Integer;
begin
  Result := 0;
  if i <= 1 then exit;
  j := i - 1;
  while (j > 1) and ((Ord(s[j]) and $C0) = $80) do Dec(j);
  Result := j;
end;

function PplIsLetterCp(cp: Integer): Boolean;
begin
  Result := ((cp >= Ord('A')) and (cp <= Ord('Z'))) or ((cp >= Ord('a')) and (cp <= Ord('z'))) or
    (cp = $AA) or (cp = $B5) or (cp = $BA) or
    ((cp >= $C0) and (cp <= $24F) and (cp <> $D7) and (cp <> $F7)) or
    ((cp >= $370) and (cp <= $3FF)) or ((cp >= $400) and (cp <= $52F));
end;

function PplIsUpperCp(cp: Integer): Boolean;
begin
  Result := ((cp >= Ord('A')) and (cp <= Ord('Z'))) or
    ((cp >= $C0) and (cp <= $DE) and (cp <> $D7)) or
    ((cp >= $391) and (cp <= $3A9)) or
    ((cp >= $400) and (cp <= $42F)) or
    ((cp >= $460) and (cp <= $52F) and ((cp mod 2) = 0));
end;

function PplCpToStr(cp: Integer): string;
begin
  if cp < $80 then Result := Chr(cp)
  else if cp < $800 then Result := Chr($C0 or (cp shr 6)) + Chr($80 or (cp and $3F))
  else if cp < $10000 then Result := Chr($E0 or (cp shr 12)) + Chr($80 or ((cp shr 6) and $3F)) + Chr($80 or (cp and $3F))
  else Result := Chr($F0 or (cp shr 18)) + Chr($80 or ((cp shr 12) and $3F)) + Chr($80 or ((cp shr 6) and $3F)) + Chr($80 or (cp and $3F));
end;

// number of code points
function PplULen(const s: string): Integer;
var
  i: Integer;
begin
  Result := 0;
  for i := 1 to Length(s) do
    if (Ord(s[i]) and $C0) <> $80 then Inc(Result);
end;

// first n code points
function PplUCut(const s: string; n: Integer): string;
var
  i, c: Integer;
begin
  c := 0;
  for i := 1 to Length(s) do
    if (Ord(s[i]) and $C0) <> $80 then
    begin
      Inc(c);
      if c > n then exit(Copy(s, 1, i - 1));
    end;
  Result := s;
end;

// lower case for ascii, latin-1, cyrillic (incl. Ө Ү Ё)
function PplLower(const s: string): string;
var
  i, cp, l: Integer;
begin
  Result := '';
  i := 1;
  while i <= Length(s) do
  begin
    cp := PplCpAt(s, i, l);
    if (cp >= Ord('A')) and (cp <= Ord('Z')) then cp := cp + 32
    else if (cp >= $C0) and (cp <= $DE) and (cp <> $D7) then cp := cp + 32
    else if (cp >= $410) and (cp <= $42F) then cp := cp + 32
    else if (cp >= $400) and (cp <= $40F) then cp := cp + 80
    else if (cp >= $460) and (cp <= $52F) and ((cp mod 2) = 0) then cp := cp + 1;
    if cp < $80 then Result := Result + Chr(cp)
    else if l = 1 then Result := Result + s[i]
    else Result := Result + PplCpToStr(cp);
    Inc(i, l);
  end;
end;

// JS Math.round(v * 100) / 100
function PplRound2(v: Double): Double;
begin
  Result := Floor(v * 100 + 0.5) / 100;
end;

// every byte of s is in allowed (ascii only) and minL <= length <= maxL
function PplCharsOk(const s, allowed: string; minL, maxL: Integer): Boolean;
var
  i: Integer;
begin
  Result := (Length(s) >= minL) and (Length(s) <= maxL);
  if Result then
    for i := 1 to Length(s) do
      if Pos(s[i], allowed) = 0 then exit(False);
end;

function Auth_UsernameOk(const s: string): Boolean;
begin
  Result := PplCharsOk(s, USERNAME_CHARS, 3, 32);
end;

// ---------- zod-ish validation (errors look like z.prettifyError) ----------
procedure PvReset;
begin
  PvErrs := '';
end;

procedure PvFail(const path, msg: string);
begin
  if PvErrs <> '' then PvErrs := PvErrs + #10;
  PvErrs := PvErrs + '✖ ' + msg;
  if path <> '' then PvErrs := PvErrs + #10 + '  → at ' + path;
end;

procedure PvDone;
begin
  if PvErrs <> '' then raise EApi.CreateS(400, PvErrs);
end;

function PvType(d: TJSONData): string;
begin
  if d = nil then exit('undefined');
  case d.JSONType of
    jtNull: Result := 'null';
    jtString: Result := 'string';
    jtNumber: Result := 'number';
    jtBoolean: Result := 'boolean';
    jtArray: Result := 'array';
    jtObject: Result := 'object';
  else
    Result := 'unknown';
  end;
end;

function PvPath(const path, key: string): string;
begin
  if path = '' then Result := key else Result := path + '.' + key;
end;

// object at key (or the body itself when key = ''); nil + error when not an object
function PvObj(o: TJSONData; const key, path: string): TJSONObject;
var
  d: TJSONData;
begin
  Result := nil;
  if key = '' then d := o
  else if (o <> nil) and (o is TJSONObject) then d := TJSONObject(o).Find(key)
  else d := nil;
  if (d <> nil) and (d is TJSONObject) then exit(TJSONObject(d));
  PvFail(PvPath(path, key), 'Invalid input: expected object, received ' + PvType(d));
end;

function PvHas(o: TJSONData; const key: string): Boolean;
begin
  Result := (o <> nil) and (o is TJSONObject) and (TJSONObject(o).Find(key) <> nil);
end;

function PvStr(o: TJSONData; const key, path: string; hasDef: Boolean; const def: string; trimIt, lowerIt: Boolean;
  minL, maxL: Integer; const re, reMsg: string; const minMsg: string = ''): string;
var
  d: TJSONData;
  n: Integer;
begin
  Result := def;
  d := nil;
  if (o <> nil) and (o is TJSONObject) then d := TJSONObject(o).Find(key);
  if (d = nil) and hasDef then exit(def);
  if (d = nil) or (d.JSONType <> jtString) then
  begin
    PvFail(PvPath(path, key), 'Invalid input: expected string, received ' + PvType(d));
    exit;
  end;
  Result := d.AsString;
  if trimIt then Result := Trim(Result);
  if lowerIt then Result := PplLower(Result);
  n := PplULen(Result);
  if (minL >= 0) and (n < minL) then
  begin
    if minMsg <> '' then PvFail(PvPath(path, key), minMsg)
    else PvFail(PvPath(path, key), 'Too small: expected string to have >=' + IntToStr(minL) + ' characters');
  end;
  if (maxL >= 0) and (n > maxL) then
    PvFail(PvPath(path, key), 'Too big: expected string to have <=' + IntToStr(maxL) + ' characters');
  if (re <> '') and not RxMatch(re, Result) then
  begin
    if reMsg <> '' then PvFail(PvPath(path, key), reMsg)
    else PvFail(PvPath(path, key), 'Invalid string: must match pattern /' + re + '/');
  end;
end;

function PvNum(o: TJSONData; const key, path: string; isInt: Boolean; hasMin: Boolean; minV: Double; hasMax: Boolean;
  maxV: Double; hasDef: Boolean = False; def: Double = 0): Double;
var
  d: TJSONData;
begin
  Result := def;
  d := nil;
  if (o <> nil) and (o is TJSONObject) then d := TJSONObject(o).Find(key);
  if (d = nil) and hasDef then exit(def);
  if (d = nil) or (d.JSONType <> jtNumber) then
  begin
    PvFail(PvPath(path, key), 'Invalid input: expected number, received ' + PvType(d));
    exit;
  end;
  Result := d.AsFloat;
  if isInt and (Frac(Result) <> 0) then
  begin
    PvFail(PvPath(path, key), 'Invalid input: expected int, received number');
    exit;
  end;
  if hasMin and (Result < minV) then PvFail(PvPath(path, key), 'Too small: expected number to be >=' + Fmt2(minV));
  if hasMax and (Result > maxV) then PvFail(PvPath(path, key), 'Too big: expected number to be <=' + Fmt2(maxV));
end;

function PvBool(o: TJSONData; const key, path: string; hasDef: Boolean = False; def: Boolean = False): Boolean;
var
  d: TJSONData;
begin
  Result := def;
  d := nil;
  if (o <> nil) and (o is TJSONObject) then d := TJSONObject(o).Find(key);
  if (d = nil) and hasDef then exit(def);
  if (d = nil) or (d.JSONType <> jtBoolean) then
  begin
    PvFail(PvPath(path, key), 'Invalid input: expected boolean, received ' + PvType(d));
    exit;
  end;
  Result := d.AsBoolean;
end;

// list = comma separated allowed values
function PvEnum(o: TJSONData; const key, path, list: string; hasDef: Boolean = False; const def: string = ''): string;
var
  d: TJSONData;
  opts: string;
begin
  Result := def;
  d := nil;
  if (o <> nil) and (o is TJSONObject) then d := TJSONObject(o).Find(key);
  if (d = nil) and hasDef then exit(def);
  if (d <> nil) and (d.JSONType = jtString) and (Pos(',' + d.AsString + ',', ',' + list + ',') > 0) then
    exit(d.AsString);
  opts := '"' + StringReplace(list, ',', '"|"', [rfReplaceAll]) + '"';
  PvFail(PvPath(path, key), 'Invalid option: expected one of ' + opts);
end;

// ---------- users ----------
function Auth_PublicUser(u: TJSONObject): TJSONObject;
begin
  Result := TJSONObject(u.Clone);
  if Result.IndexOfName('passwordHash') >= 0 then Result.Delete('passwordHash');
end;

function Auth_IsRole(const r: string): Boolean;
begin
  Result := (r <> '') and (Pos(',' + r + ',', ',' + ROLES_LIST + ',') > 0);
end;

function Auth_PermsArr(const role: string): TJSONArray;
var
  sl: TStringList;
  i: Integer;
begin
  Result := TJSONArray.Create;
  sl := TStringList.Create;
  sl.Delimiter := ','; sl.StrictDelimiter := True; sl.DelimitedText := PermsOf(role);
  for i := 0 to sl.Count - 1 do
    if sl[i] <> '' then Result.Add(sl[i]);
  sl.Free;
end;

function Auth_FindByUsername(const username: string): TJSONObject;
begin
  Result := DbFindOne('users', 'username', username);
end;

// creates the user or resets role/password/active (create-user script); returns True when created
function Auth_UpsertUser(const username, password, role, name: string): Boolean;
var
  u, patch: TJSONObject;
begin
  u := Auth_FindByUsername(username);
  if u = nil then
  begin
    u := TJSONObject.Create;
    u.Add('username', username);
    u.Add('name', name);
    u.Add('passwordHash', HashPassword(password));
    u.Add('role', role);
    u.Add('active', True);
    DbInsert('users', u);
    u.Free;
    exit(True);
  end;
  patch := TJSONObject.Create;
  patch.Add('role', role);
  patch.Add('active', True);
  patch.Add('passwordHash', HashPassword(password));
  if name <> '' then patch.Add('name', name);
  DbUpdate('users', JStr(u, '_id'), patch);
  patch.Free;
  u.Free;
  Result := False;
end;

// ADMIN_USERNAME / ADMIN_PASSWORD superadmin (seed.ts). only creates, never changes an existing account
procedure Auth_BootstrapAdmin;
var
  un, pw: string;
  u: TJSONObject;
begin
  un := LowerCase(Trim(Env('ADMIN_USERNAME')));
  pw := Env('ADMIN_PASSWORD');
  if (un = '') or (pw = '') then
  begin
    WriteLn('ADMIN_USERNAME/ADMIN_PASSWORD not set: no superadmin created');
    exit;
  end;
  if Length(pw) < 8 then
  begin
    WriteLn('ADMIN_PASSWORD must be at least 8 characters: no superadmin created');
    exit;
  end;
  u := Auth_FindByUsername(un);
  if u = nil then
  begin
    u := TJSONObject.Create;
    u.Add('username', un);
    u.Add('name', '');
    u.Add('passwordHash', HashPassword(pw));
    u.Add('role', 'superadmin');
    u.Add('active', True);
    DbInsert('users', u);
    WriteLn('Superadmin created: ' + un);
  end;
  u.Free;
end;

// legacy accounts with email instead of username (seed.ts)
procedure Auth_MigrateLegacy;
var
  all: TJSONArray;
  i: Integer;
  o: TJSONObject;
  em, un: string;
  changed: Boolean;
begin
  all := DbLoad('users');
  changed := False;
  for i := 0 to all.Count - 1 do
  begin
    o := TJSONObject(all.Items[i]);
    if (o.Find('username') = nil) and (JStr(o, 'email') <> '') then
    begin
      em := JStr(o, 'email');
      un := em;
      if Pos('@', un) > 0 then un := Copy(un, 1, Pos('@', un) - 1);
      un := LowerCase(un);
      JSetS(o, 'username', un);
      JSetB(o, 'active', True);
      o.Delete('email');
      changed := True;
      WriteLn('Legacy account ' + em + ' → username ' + un);
    end;
  end;
  if changed then DbSave('users', all);
  all.Free;
end;

procedure Auth_AssertCanManage(const actor, target: string);
begin
  if not CanManage(actor, target) then raise EApi.CreateS(403, 'Your role cannot manage ' + target + ' accounts');
end;

procedure Routes_auth;
var
  body, u, patch, res: TJSONObject;
  un, pw, cur, nw, name, role, plain, id: string;
  active: Boolean;
  all, list: TJSONArray;
  other: TJSONObject;
  i: Integer;
  emptyBody: TJSONObject;
begin
  // ---------- /api/auth ----------
  if Route('POST', '/api/auth/login') then
  begin
    emptyBody := nil;
    if (ReqBody <> nil) and (ReqBody is TJSONObject) then body := TJSONObject(ReqBody)
    else begin emptyBody := TJSONObject.Create; body := emptyBody; end;
    try
      PvReset;
      un := PvStr(body, 'username', '', False, '', False, False, 1, -1, '', '');
      pw := PvStr(body, 'password', '', False, '', False, False, 1, -1, '', '');
      PvDone;
    finally
      emptyBody.Free;
    end;
    u := Auth_FindByUsername(LowerCase(Trim(un)));
    if (u = nil) or not CheckPassword(pw, JStr(u, 'passwordHash')) then
    begin
      u.Free;
      raise EApi.CreateS(401, 'Invalid username or password');
    end;
    if not JBool(u, 'active', True) then begin u.Free; raise EApi.CreateS(403, 'Account is disabled'); end;
    patch := TJSONObject.Create;
    patch.Add('lastLoginAt', NowIso);
    DbUpdate('users', JStr(u, '_id'), patch);
    patch.Free;
    SetCookieHeader(AUTH_COOKIE, JwtSign(JStr(u, '_id'), AUTH_MAX_AGE), AUTH_MAX_AGE);
    SendJsonStr(200, '{"username":' + JEsc(JStr(u, 'username')) + '}');
    u.Free;
    exit;
  end;

  if Route('POST', '/api/auth/logout') then
  begin
    Resp.SetCustomHeader('Set-Cookie', AUTH_COOKIE + '=; Path=/; Expires=Thu, 01 Jan 1970 00:00:00 GMT');
    SendNoContent;
    exit;
  end;

  if Route('GET', '/api/auth/me') then
  begin
    RequireAuth;
    res := TJSONObject.Create;
    res.Add('id', JStr(CurUser, '_id'));
    res.Add('username', JStr(CurUser, 'username'));
    res.Add('name', JStr(CurUser, 'name'));
    res.Add('role', JStr(CurUser, 'role'));
    res.Add('permissions', Auth_PermsArr(JStr(CurUser, 'role')));
    SendJson(200, res);
    exit;
  end;

  if Route('PUT', '/api/auth/password') then
  begin
    RequireAuth;
    emptyBody := nil;
    if (ReqBody <> nil) and (ReqBody is TJSONObject) then body := TJSONObject(ReqBody)
    else begin emptyBody := TJSONObject.Create; body := emptyBody; end;
    try
      PvReset;
      cur := PvStr(body, 'currentPassword', '', False, '', False, False, 1, -1, '', '');
      nw := PvStr(body, 'newPassword', '', False, '', False, False, 8, 128, '', '', 'Password must be at least 8 characters');
      PvDone;
    finally
      emptyBody.Free;
    end;
    u := DbGet('users', JStr(CurUser, '_id'));
    if (u = nil) or not CheckPassword(cur, JStr(u, 'passwordHash')) then
    begin
      u.Free;
      raise EApi.CreateS(400, 'Current password is wrong');
    end;
    patch := TJSONObject.Create;
    patch.Add('passwordHash', HashPassword(nw));
    DbUpdate('users', JStr(u, '_id'), patch);
    patch.Free;
    u.Free;
    SendNoContent;
    exit;
  end;

  // ---------- /api/admin/users (permission users) ----------
  if Route('GET', '/api/admin/users') then
  begin
    RequirePerm('users');
    all := DbLoad('users');
    JSortBy(all, 'username', False);
    JSortBy(all, 'role', False); // bubble sort is stable: role, then username
    list := TJSONArray.Create;
    for i := 0 to all.Count - 1 do list.Add(Auth_PublicUser(TJSONObject(all.Items[i])));
    all.Free;
    res := TJSONObject.Create;
    res.Add('users', list);
    list := TJSONArray.Create;
    with TStringList.Create do
    begin
      Delimiter := ','; StrictDelimiter := True; DelimitedText := ManageableRoles(JStr(CurUser, 'role'));
      for i := 0 to Count - 1 do if Strings[i] <> '' then list.Add(Strings[i]);
      Free;
    end;
    res.Add('manageableRoles', list);
    SendJson(200, res);
    exit;
  end;

  if Route('POST', '/api/admin/users') then
  begin
    RequirePerm('users');
    emptyBody := nil;
    if (ReqBody <> nil) and (ReqBody is TJSONObject) then body := TJSONObject(ReqBody)
    else begin emptyBody := TJSONObject.Create; body := emptyBody; end;
    try
      PvReset;
      un := PvStr(body, 'username', '', False, '', True, True, -1, -1, '', '');
      if (JPath(body, 'username') <> nil) and (JPath(body, 'username').JSONType = jtString) and not Auth_UsernameOk(un) then
        PvFail('username', USERNAME_MSG);
      name := PvStr(body, 'name', '', True, '', True, False, -1, 100, '', '');
      role := PvEnum(body, 'role', '', ROLES_LIST);
      active := PvBool(body, 'active', '', True, True);
      plain := PvStr(body, 'password', '', False, '', False, False, 8, 128, '', '', 'Password must be at least 8 characters');
      PvDone;
    finally
      emptyBody.Free;
    end;
    Auth_AssertCanManage(JStr(CurUser, 'role'), role);
    other := Auth_FindByUsername(un);
    if other <> nil then begin other.Free; raise EApi.CreateS(409, 'Already exists'); end;
    u := TJSONObject.Create;
    u.Add('username', un);
    u.Add('name', name);
    u.Add('role', role);
    u.Add('active', active);
    u.Add('passwordHash', HashPassword(plain));
    DbInsert('users', u);
    SendJson(201, Auth_PublicUser(u));
    u.Free;
    exit;
  end;

  if Route('PUT', '/api/admin/users/:id') then
  begin
    RequirePerm('users');
    id := P(3);
    emptyBody := nil;
    if (ReqBody <> nil) and (ReqBody is TJSONObject) then body := TJSONObject(ReqBody)
    else begin emptyBody := TJSONObject.Create; body := emptyBody; end;
    try
      PvReset;
      un := PvStr(body, 'username', '', False, '', True, True, -1, -1, '', '');
      if (JPath(body, 'username') <> nil) and (JPath(body, 'username').JSONType = jtString) and not Auth_UsernameOk(un) then
        PvFail('username', USERNAME_MSG);
      name := PvStr(body, 'name', '', True, '', True, False, -1, 100, '', '');
      role := PvEnum(body, 'role', '', ROLES_LIST);
      active := PvBool(body, 'active', '', True, True);
      plain := PvStr(body, 'password', '', True, '', False, False, -1, -1, '', '');
      if (plain <> '') and ((PplULen(plain) < 8) or (PplULen(plain) > 128)) then
        PvFail('password', 'Invalid input');
      PvDone;
    finally
      emptyBody.Free;
    end;
    u := DbGet('users', id);
    if u = nil then raise EApi.CreateS(404, 'Not found');
    try
      Auth_AssertCanManage(JStr(CurUser, 'role'), JStr(u, 'role'));
      Auth_AssertCanManage(JStr(CurUser, 'role'), role);
      if (JStr(u, '_id') = JStr(CurUser, '_id')) and ((role <> JStr(u, 'role')) or not active) then
        raise EApi.CreateS(400, 'You cannot change your own role or disable yourself');
      if un <> JStr(u, 'username') then
      begin
        other := Auth_FindByUsername(un);
        if other <> nil then begin other.Free; raise EApi.CreateS(409, 'Already exists'); end;
      end;
      JSetS(u, 'username', un);
      JSetS(u, 'name', name);
      JSetS(u, 'role', role);
      JSetB(u, 'active', active);
      if plain <> '' then JSetS(u, 'passwordHash', HashPassword(plain));
      DbReplace('users', u);
      SendJson(200, Auth_PublicUser(u));
    finally
      u.Free;
    end;
    exit;
  end;

  if Route('DELETE', '/api/admin/users/:id') then
  begin
    RequirePerm('users');
    id := P(3);
    u := DbGet('users', id);
    if u = nil then raise EApi.CreateS(404, 'Not found');
    try
      if JStr(u, '_id') = JStr(CurUser, '_id') then raise EApi.CreateS(400, 'You cannot delete yourself');
      Auth_AssertCanManage(JStr(CurUser, 'role'), JStr(u, 'role'));
      DbDelete('users', id);
    finally
      u.Free;
    end;
    SendNoContent;
    exit;
  end;
end;
