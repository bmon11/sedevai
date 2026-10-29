// ===================== PAGES_ADMIN: staff admin html pages (/admin/*) =====================
// port of sedev-frontend/src/app/admin/* + sections/{source,social,entity-admin,lens,run,topic-admin,report,settings,user}
// + layouts/dashboard. Server renders the shell (sidebar nav by permissions, header, dialogs markup),
// vanilla js inside each page calls the same-origin /api/* endpoints (cookie sedev_token).
// Everything below writes into one global string AdmH with AW(). Pages are long and copy-pasted on purpose.

var
  AdmH: string = '';          // the html being built for the current request
  AdmMe: TJSONObject = nil;   // signed in user of this request (alias of CurUser, freed by dispatcher)
  AdmTitle: string = '';

procedure AW(const s: string);
begin
  AdmH := AdmH + s + #10;
end;

procedure AdmRedirect(const url: string);
begin
  Resp.SetCustomHeader('Location', url);
  SendRaw(302, 'text/html; charset=utf-8', '<!doctype html><meta charset="utf-8"><a href="' + HtmlEsc(url) + '">' + HtmlEsc(url) + '</a>');
  Resp.CodeText := 'Found';
end;

// signed in + active user from the cookie, nil otherwise. result is stored in CurUser (dispatcher frees it)
function AdmLoadUser: TJSONObject;
var
  sub: string;
  u: TJSONObject;
begin
  Result := nil;
  sub := JwtVerify(Cookie('sedev_token'));
  if sub = '' then exit;
  u := DbGet('users', sub);
  if u = nil then exit;
  if not JBool(u, 'active', True) then begin u.Free; exit; end;
  if CurUser <> nil then FreeAndNil(CurUser);
  CurUser := u;
  Result := u;
end;

// the "me" json the pages use (like GET /api/auth/me)
function AdmMeJson: string;
var
  o: TJSONObject;
  perms: TStringList;
  i: Integer;
  a: TJSONArray;
begin
  o := TJSONObject.Create;
  o.Add('id', JStr(AdmMe, '_id'));
  o.Add('_id', JStr(AdmMe, '_id'));
  o.Add('username', JStr(AdmMe, 'username'));
  o.Add('name', JStr(AdmMe, 'name'));
  o.Add('role', JStr(AdmMe, 'role'));
  a := TJSONArray.Create;
  perms := TStringList.Create;
  perms.Delimiter := ','; perms.StrictDelimiter := True;
  perms.DelimitedText := PermsOf(JStr(AdmMe, 'role'));
  for i := 0 to perms.Count - 1 do if perms[i] <> '' then a.Add(perms[i]);
  perms.Free;
  o.Add('permissions', a);
  Result := StringReplace(o.AsJSON, '<', '<', [rfReplaceAll]);
  o.Free;
end;

function AdmRoleLabel(const r: string): string;
begin
  if r = 'superadmin' then Result := 'Супер админ'
  else if r = 'admin' then Result := 'Админ'
  else if r = 'editor' then Result := 'Редактор'
  else if r = 'moderator' then Result := 'Модератор'
  else if r = 'journalist' then Result := 'Сэтгүүлч'
  else Result := r;
end;

// hand drawn 24x24 icons (stand-ins for the solar duotone set)
function AdmIcon(const name: string): string;
var
  d: string;
begin
  d := '';
  if name = 'doc' then d := '<path d="M6 2h8l5 5v13a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2z" opacity=".32"/><path d="M8 12h8v1.6H8zM8 16h5v1.6H8zM14 2l5 5h-4a1 1 0 0 1-1-1z"/>'
  else if name = 'chart' then d := '<rect x="3" y="3" width="18" height="18" rx="4" opacity=".32"/><path d="M7 17v-5h2v5zM11 17V7h2v10zM15 17v-7h2v7z"/>'
  else if name = 'user' then d := '<rect x="2" y="4" width="20" height="16" rx="3" opacity=".32"/><circle cx="9" cy="10" r="2.5"/><path d="M5 16.5c.6-2 2.2-3 4-3s3.4 1 4 3zM14 9h5v1.5h-5zM14 12.5h4V14h-4z"/>'
  else if name = 'play' then d := '<circle cx="12" cy="12" r="10" opacity=".32"/><path d="M10 8l6 4-6 4z"/>'
  else if name = 'globe' then d := '<circle cx="12" cy="12" r="10" opacity=".32"/><path d="M12 2c2.8 2.6 4 6 4 10s-1.2 7.4-4 10c-2.8-2.6-4-6-4-10s1.2-7.4 4-10zm0 2.6C10.4 6.6 9.6 9 9.6 12s.8 5.4 2.4 7.4c1.6-2 2.4-4.4 2.4-7.4s-.8-5.4-2.4-7.4zM2.5 11.2h19v1.6h-19z"/>'
  else if name = 'share' then d := '<circle cx="12" cy="12" r="10" opacity=".32"/><circle cx="8" cy="12" r="2"/><circle cx="15.5" cy="8" r="2"/><circle cx="15.5" cy="16" r="2"/><path d="M8.6 11.2l6.3-3.6.7 1.2-6.3 3.6zM9.3 12.2l6.3 3.6-.7 1.2-6.3-3.6z"/>'
  else if name = 'layers' then d := '<path d="M12 3l9 5-9 5-9-5z"/><path d="M3 12l9 5 9-5v2l-9 5-9-5z" opacity=".32"/>'
  else if name = 'settings' then d := '<circle cx="12" cy="12" r="10" opacity=".32"/><circle cx="12" cy="12" r="3.2"/><path d="M11 3h2v3h-2zM11 18h2v3h-2zM3 11h3v2H3zM18 11h3v2h-3z"/>'
  else if name = 'users' then d := '<circle cx="9" cy="8" r="3.5"/><path d="M2.5 19c.5-3.5 3.2-5.5 6.5-5.5s6 2 6.5 5.5z"/><circle cx="17" cy="9" r="2.6" opacity=".32"/><path d="M16 14c3 0 5 1.6 5.5 4.5H17c-.2-1.8-.6-3.2-1-4.5z" opacity=".32"/>'
  else if name = 'home' then d := '<path d="M3 10.5L12 3l9 7.5V20a1 1 0 0 1-1 1h-5v-6h-6v6H4a1 1 0 0 1-1-1z" opacity=".32"/><path d="M10 15h4v6h-4z"/>'
  else if name = 'lock' then d := '<rect x="4" y="10" width="16" height="11" rx="3" opacity=".32"/><path d="M8 10V7a4 4 0 0 1 8 0v3h-1.8V7a2.2 2.2 0 0 0-4.4 0v3z"/><circle cx="12" cy="15.5" r="1.6"/>'
  else if name = 'add' then d := '<path d="M11 5h2v14h-2z"/><path d="M5 11h14v2H5z"/>'
  else if name = 'up' then d := '<path d="M12 8l6 6-1.4 1.4L12 10.8l-4.6 4.6L6 14z"/>'
  else if name = 'down' then d := '<path d="M12 16l-6-6 1.4-1.4 4.6 4.6 4.6-4.6L18 10z"/>'
  else if name = 'menu' then d := '<path d="M4 6h16v2H4zM4 11h10v2H4zM4 16h16v2H4z"/>'
  else if name = 'eye' then d := '<path d="M12 5c5 0 8.5 4 9.5 7-1 3-4.5 7-9.5 7s-8.5-4-9.5-7C3.5 9 7 5 12 5zm0 3.2a3.8 3.8 0 1 0 0 7.6 3.8 3.8 0 0 0 0-7.6z"/>'
  else if name = 'fire' then d := '<path d="M12 2c1 3.5 5 5.5 5 11a5 5 0 0 1-10 0c0-2.5 1-4 2.5-5.5C9.8 9.5 11 10 11.5 11 12.5 8 12.3 5 12 2z"/>'
  else if name = 'pie' then d := '<path d="M11 3v10h10A10 10 0 1 1 11 3z" opacity=".32"/><path d="M13 1.5A9.5 9.5 0 0 1 22.5 11H13z"/>'
  else if name = 'cup' then d := '<path d="M7 3h10v6a5 5 0 0 1-10 0z"/><path d="M4 4h3v2H5.2c.2 1.8 1 2.8 2 3.2l-.6 1.8C4.8 10.4 4 8.4 4 6zM17 4h3v2c0 2.4-.8 4.4-2.6 5l-.6-1.8c1-.4 1.8-1.4 2-3.2H17zM11 14h2v4h3v3H8v-3h3z" opacity=".32"/>'
  else if name = 'docs' then d := '<rect x="7" y="2" width="14" height="17" rx="2" opacity=".32"/><path d="M5 5v14a3 3 0 0 0 3 3h10v-2H8a1 1 0 0 1-1-1V5z"/><path d="M10 7h8v1.6h-8zM10 11h8v1.6h-8zM10 15h5v1.6h-5z"/>'
  else if name = 'fb' then d := '<circle cx="12" cy="12" r="10" fill="#1877F2"/><path fill="#fff" d="M13.2 20v-6.2h2.1l.3-2.4h-2.4V9.9c0-.7.2-1.2 1.2-1.2h1.3V6.5a17 17 0 0 0-1.9-.1c-1.9 0-3.2 1.2-3.2 3.3v1.7H8.5v2.4h2.1V20z"/>'
  else if name = 'x' then d := '<path d="M17.8 3h3.1l-6.8 7.7L22 21h-6.2l-4.9-6.3L5.3 21H2.2l7.2-8.3L1.8 3h6.4l4.4 5.8zm-1.1 16.2h1.7L7.4 4.7H5.5z"/>'
  else if name = 'threads' then d := '<path d="M16.9 11.2c-.1-.1-.2-.1-.3-.2-.2-3.1-1.9-4.9-4.7-4.9-1.7 0-3.1.7-4 2l1.6 1.1c.6-1 1.6-1.2 2.4-1.2 1 0 1.7.3 2.2.9.3.4.6 1 .7 1.7-.8-.1-1.7-.2-2.7-.1-2.6.1-4.3 1.7-4.2 3.8.1 1.1.6 2 1.5 2.6.8.5 1.8.7 2.8.7 1.4-.1 2.5-.6 3.2-1.6.6-.7.9-1.7 1-2.9.7.4 1.2.9 1.4 1.6.5 1.1.5 2.9-1 4.3-1.3 1.3-2.8 1.8-5.1 1.9-2.6 0-4.5-.8-5.7-2.4C6.9 17 6.3 15 6.3 12.5s.6-4.5 1.7-5.9c1.2-1.5 3.1-2.3 5.7-2.4 2.6 0 4.6.8 5.9 2.4.6.8 1.1 1.7 1.4 2.8l1.9-.5c-.4-1.4-1-2.6-1.8-3.6C19.4 3.3 16.9 2.2 13.7 2.2c-3.2 0-5.7 1.1-7.3 3.1-1.4 1.8-2.1 4.2-2.2 7.2.1 3 .8 5.4 2.2 7.2 1.6 2 4.1 3.1 7.3 3.1 2.8 0 4.8-.8 6.4-2.4 2.1-2.1 2.1-4.8 1.4-6.4-.5-1.2-1.4-2.1-2.6-2.8zm-4.6 4.4c-1.1.1-2.3-.4-2.3-1.5-.1-.8.6-1.7 2.4-1.8h.6c.7 0 1.3.1 1.9.2-.2 2.6-1.4 3-2.6 3.1z"/>'
  else if name = 'earth' then d := '<circle cx="12" cy="12" r="10"/>'
  else d := '<circle cx="12" cy="12" r="8"/>';
  Result := '<svg viewBox="0 0 24 24" fill="currentColor" width="22" height="22" aria-hidden="true">' + d + '</svg>';
end;

// one nav item if the user may open it
procedure AdmNavItem(const title, path, perm, icon: string; var anyInSection: Boolean; const subheader: string);
var
  cls: string;
begin
  if not Can(JStr(AdmMe, 'role'), perm) then exit;
  if not anyInSection then
  begin
    AW('<div class="sub">' + subheader + '</div>');
    anyInSection := True;
  end;
  cls := 'it';
  if (ReqPath = path) or (Copy(ReqPath, 1, Length(path) + 1) = path + '/') then cls := 'it on';
  AW('<a class="' + cls + '" href="' + path + '">' + AdmIcon(icon) + '<span>' + title + '</span></a>');
end;

// <head> + inlined admin.css / admin.js (read from disk on every request, slow on purpose)
procedure AdmHead(const title: string);
begin
  AW('<!doctype html>');
  AW('<html lang="mn"><head><meta charset="utf-8">');
  AW('<meta name="viewport" content="width=device-width, initial-scale=1">');
  AW('<meta name="robots" content="noindex">');
  AW('<title>' + HtmlEsc(title) + ' · Сэдэв.мн</title>');
  AW('<link rel="icon" href="/favicon.ico">');
  AW('<style>');
  AW(ReadFileStr('public/admin.css'));
  AW('</style>');
  AW('<script>');
  AW(ReadFileStr('public/admin.js'));
  AW('</script>');
end;

// starts a protected dashboard page. False = response already sent (redirect or no permission page)
function AdmPage(const title, perm, wrapCls: string): Boolean;
var
  any: Boolean;
  nm, ini: string;
begin
  Result := False;
  AdmH := '';
  AdmTitle := title;
  AdmMe := AdmLoadUser;
  if AdmMe = nil then
  begin
    AdmRedirect('/admin/login?returnTo=' + UrlEnc(ReqPath));
    exit;
  end;
  nm := JStr(AdmMe, 'name');
  if nm = '' then nm := JStr(AdmMe, 'username');
  ini := UTF8Encode(Copy(UTF8Decode(nm), 1, 1));
  AdmHead(title);
  AW('<script>window.ME = ' + AdmMeJson + ';</script>');
  AW('</head><body>');
  // ---- sidebar (layouts/nav-config-dashboard.jsx)
  AW('<div class="scrim" id="scrim" onclick="toggleNav(false)"></div>');
  AW('<nav class="nav" id="nav">');
  AW('<a class="logo" href="/admin"><b>С</b>Сэдэв.мн</a>');
  any := False;
  AdmNavItem('Сэдвүүд', '/admin/topics', 'topics.view', 'doc', any, 'Агуулга');
  AdmNavItem('Тайлан', '/admin/reports', 'reports', 'chart', any, 'Агуулга');
  AdmNavItem('Хүмүүс', '/admin/people', 'entities', 'user', any, 'Агуулга');
  AdmNavItem('Ажиллагаа', '/admin/runs', 'runs.view', 'play', any, 'Агуулга');
  any := False;
  AdmNavItem('Эх сурвалж', '/admin/sources', 'sources', 'globe', any, 'Тохиргоо');
  AdmNavItem('Сошиал', '/admin/social', 'social', 'share', any, 'Тохиргоо');
  AdmNavItem('Задлах цэгүүд', '/admin/lenses', 'lenses', 'layers', any, 'Тохиргоо');
  AdmNavItem('Тохиргоо', '/admin/settings', 'settings', 'settings', any, 'Тохиргоо');
  AdmNavItem('Хэрэглэгчид', '/admin/users', 'users', 'users', any, 'Тохиргоо');
  AW('</nav>');
  // ---- header with account popover (layouts/components/account-popover.jsx)
  AW('<header class="hdr">');
  AW('<button class="menu ib" onclick="toggleNav(true)" aria-label="Цэс">' + AdmIcon('menu') + '</button>');
  AW('<button class="acct" onclick="toggleAcct(event)" aria-label="Аккаунт"><span class="avatar">' + HtmlEsc(ini) + '</span></button>');
  AW('<div class="pop" id="acctPop">');
  AW('<div class="who"><div style="font-weight:600">' + HtmlEsc(nm) + '</div>');
  AW('<div class="muted">' + HtmlEsc(JStr(AdmMe, 'username')) + ' · ' + AdmRoleLabel(JStr(AdmMe, 'role')) + '</div></div><hr>');
  AW('<div style="padding:4px 0"><a class="mi" href="/">' + AdmIcon('home') + '<span>Нүүр сайт</span></a>');
  AW('<button class="mi" onclick="openPw()">' + AdmIcon('lock') + '<span>Нууц үг солих</span></button></div><hr>');
  AW('<div style="padding:8px"><button class="btn soft-e" onclick="logout()" style="justify-content:flex-start">Гарах</button></div>');
  AW('</div>');
  AW('</header>');
  AW('<main class="main"><div class="wrap ' + wrapCls + '">');
  if (perm <> '') and not Can(JStr(AdmMe, 'role'), perm) then
  begin
    // PermissionGuard
    AW('<div class="empty filled" style="padding:80px 16px"><h6>Хандах эрхгүй</h6>');
    AW('<div>Таны эрхээр энэ хуудсыг үзэх боломжгүй. Шаардлагатай бол админд хандана уу.</div></div>');
    AW('</div></main>');
    AW('</body></html>');
    SendHtml(AdmH);
    exit;
  end;
  Result := True;
end;

// breadcrumbs heading (components/custom-breadcrumbs)
procedure AdmBc(const heading, crumbsHtml, actionHtml: string);
begin
  AW('<div class="bc"><div class="l"><h4 id="bcH">' + heading + '</h4>');
  AW('<div class="links">' + crumbsHtml + '</div></div>');
  if actionHtml <> '' then AW('<div class="act" id="bcAct">' + actionHtml + '</div>');
  AW('</div>');
end;

function AdmCrumb(const name: string): string;
begin
  Result := '<a href="/admin">Админ</a><span class="dot"></span><span class="last">' + name + '</span>';
end;

// closes the page: shared dialogs (password, confirm) + send
procedure AdmFoot;
begin
  AW('</div></main>');
  // change password dialog (layouts/components/change-password-dialog.jsx)
  AW('<div class="dlgbg" id="pwDlg" onclick="if(event.target===this)closeDlg(''pwDlg'')"><div class="dlg xs">');
  AW('<form onsubmit="return submitPw(event)"><h2>Нууц үг солих</h2><div class="bd">');
  AW('<div id="pwMsg"></div>');
  AW('<div class="fld"><label class="fl">Одоогийн нууц үг *</label><input id="pwCur" type="password" autocomplete="current-password" required></div>');
  AW('<div class="fld"><label class="fl">Шинэ нууц үг *</label><input id="pwNew" type="password" autocomplete="new-password" required><div class="ht">Хамгийн багадаа 8 тэмдэгт</div></div>');
  AW('</div><div class="ft"><button type="button" class="btn o" onclick="closeDlg(''pwDlg'')">Хаах</button>');
  AW('<button type="submit" class="btn c" id="pwBtn">Солих</button></div></form></div></div>');
  // confirm dialog (components/custom-dialog ConfirmDialog)
  AW('<div class="dlgbg" id="cfDlg" style="z-index:200" onclick="if(event.target===this)closeDlg(''cfDlg'')"><div class="dlg xs">');
  AW('<h2 id="cfT">Устгах</h2><div class="bd"><div id="cfC"></div></div>');
  AW('<div class="ft"><button class="btn ec" id="cfOk">Устгах</button><button class="btn o" onclick="closeDlg(''cfDlg'')">Болих</button></div></div></div>');
  AW('</body></html>');
  SendHtml(AdmH);
  AdmH := '';
end;

// ---- little form builders (Field.Text / Field.Select / Field.Switch look-alikes)
function AdmFld(const id, lab, typ, helper, extra: string): string;
begin
  Result := '<div class="fld" id="f_' + id + '"><label class="fl">' + lab + '</label>';
  if typ = 'textarea' then Result := Result + '<textarea id="' + id + '" ' + extra + '></textarea>'
  else Result := Result + '<input id="' + id + '" type="' + typ + '" ' + extra + '>';
  if helper <> '' then Result := Result + '<div class="ht" id="h_' + id + '">' + helper + '</div>';
  Result := Result + '</div>';
end;

function AdmSel(const id, lab, optionsHtml, helper, extra: string): string;
begin
  Result := '<div class="fld" id="f_' + id + '"><label class="fl">' + lab + '</label><select id="' + id + '" ' + extra + '>' + optionsHtml + '</select>';
  if helper <> '' then Result := Result + '<div class="ht">' + helper + '</div>';
  Result := Result + '</div>';
end;

function AdmSw(const id, lab, extra: string): string;
begin
  Result := '<label class="swl"><span class="sw"><input type="checkbox" id="' + id + '" ' + extra + '><span></span></span>' + lab + '</label>';
end;

function AdmOpt(const value, lab: string): string;
begin
  Result := '<option value="' + value + '">' + lab + '</option>';
end;

{$I pages_admin_login.inc}
{$I pages_admin_content.inc}
{$I pages_admin_config.inc}
{$I pages_admin_reports.inc}

procedure Routes_pages_admin;
begin
  if Handled then exit;
  if (PCount = 0) or (P(0) <> 'admin') then exit;
  if ReqMethod <> 'GET' then exit;
  if Route('GET', '/admin') then begin AdmRedirect('/admin/topics'); exit; end;
  if Route('GET', '/admin/login') then begin AdmPage_Login; exit; end;
  if Route('GET', '/admin/topics') then begin AdmPage_Topics; exit; end;
  if Route('GET', '/admin/reports') then begin AdmPage_Reports; exit; end;
  if Route('GET', '/admin/people') then begin AdmPage_People; exit; end;
  if Route('GET', '/admin/runs') then begin AdmPage_Runs; exit; end;
  if Route('GET', '/admin/runs/:id') then begin AdmPage_Run(P(2)); exit; end;
  if Route('GET', '/admin/sources') then begin AdmPage_Sources; exit; end;
  if Route('GET', '/admin/social') then begin AdmPage_Social; exit; end;
  if Route('GET', '/admin/lenses') then begin AdmPage_Lenses; exit; end;
  if Route('GET', '/admin/settings') then begin AdmPage_Settings; exit; end;
  if Route('GET', '/admin/users') then begin AdmPage_Users; exit; end;
end;
