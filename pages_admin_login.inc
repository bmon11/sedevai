// ---- /admin/login (auth/view/sign-in-view.jsx + layouts/auth-split + GuestGuard)
procedure AdmPage_Login;
var
  returnTo: string;
begin
  returnTo := Q('returnTo', '/admin/runs');
  // only local paths, no //evil.com
  if (Copy(returnTo, 1, 1) <> '/') or (Copy(returnTo, 1, 2) = '//') then returnTo := '/admin/runs';
  // GuestGuard: signed in already -> go
  if AdmLoadUser <> nil then
  begin
    AdmRedirect(returnTo);
    exit;
  end;
  AdmH := '';
  AdmHead('Нэвтрэх');
  AW('<style>');
  AW('.auth .form form{display:flex;flex-direction:column;gap:24px}');
  AW('.auth .desc{color:var(--txt2);margin:0 0 40px}');
  AW('.auth .art circle{transition:transform .6s}');
  AW('</style>');
  AW('</head><body>');
  AW('<div class="auth">');
  AW('<section class="side">');
  AW('<div><h3>Сэдэв.мн удирдлага</h3><p>Ажиллагаа, эх сурвалж, тохиргоо</p></div>');
  // hand drawn illustration instead of the template's dashboard picture
  AW('<svg class="art" viewBox="0 0 360 260" xmlns="http://www.w3.org/2000/svg">');
  AW('<rect x="20" y="20" width="320" height="220" rx="18" fill="#fff" stroke="#DFE3E8"/>');
  AW('<rect x="20" y="20" width="80" height="220" rx="18" fill="#F4F6F8"/>');
  AW('<rect x="36" y="44" width="48" height="10" rx="5" fill="#007BFF"/>');
  AW('<rect x="36" y="72" width="40" height="8" rx="4" fill="#C4CDD5"/><rect x="36" y="92" width="44" height="8" rx="4" fill="#C4CDD5"/>');
  AW('<rect x="36" y="112" width="36" height="8" rx="4" fill="#C4CDD5"/><rect x="36" y="132" width="42" height="8" rx="4" fill="#C4CDD5"/>');
  AW('<rect x="120" y="44" width="200" height="14" rx="7" fill="#1C252E" opacity=".85"/>');
  AW('<rect x="120" y="76" width="92" height="64" rx="12" fill="#CCE5FF"/><rect x="226" y="76" width="94" height="64" rx="12" fill="#FCCED0"/>');
  AW('<path d="M128 220 L160 190 L190 202 L222 166 L252 178 L284 150 L312 160" fill="none" stroke="#007BFF" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/>');
  AW('<circle cx="222" cy="166" r="6" fill="#E50914"/>');
  AW('</svg>');
  AW('</section>');
  AW('<section class="form">');
  AW('<h5>Ажилтны нэвтрэх</h5>');
  AW('<p class="desc">Сэдэв.мн-ийн сэдэв, ажиллагаа, тохиргоог удирдах</p>');
  AW('<div id="err"></div>');
  AW('<form id="lf" onsubmit="return doLogin(event)" novalidate>');
  AW('<div class="fld"><label class="fl">Нэвтрэх нэр</label><input id="un" autocomplete="username" autofocus><div class="ht" id="unE" style="color:var(--err-d);display:none">Нэвтрэх нэр оруулна уу</div></div>');
  AW('<div class="fld pw"><label class="fl">Нууц үг</label><input id="pw" type="password" autocomplete="current-password">');
  AW('<button type="button" class="ib" onclick="togglePw()" aria-label="Нууц үг харах">' + AdmIcon('eye') + '</button></div>');
  AW('<div class="ht" id="pwE" style="color:var(--err-d);display:none;margin-top:-18px">Нууц үг оруулна уу</div>');
  AW('<button type="submit" class="btn dk lg" id="lb">Нэвтрэх</button>');
  AW('</form>');
  AW('</section>');
  AW('</div>');
  AW('<script>');
  AW('window.NO_LOGIN_REDIRECT = true;');
  AW('var RETURN_TO = ' + StringReplace(JEsc(returnTo), '<', '\u003c', [rfReplaceAll]) + ';');
  AW('function togglePw() { var p = $("pw"); p.type = p.type === "password" ? "text" : "password"; }');
  AW('function doLogin(ev) {');
  AW('  ev.preventDefault();');
  AW('  var u = v("un").trim(), p = v("pw");');
  AW('  show("unE", !u); show("pwE", !p);');
  AW('  if (!u || !p) return false;');
  AW('  $("err").innerHTML = "";');
  AW('  var b = $("lb"); busy(b, true, "Нэвтэрч байна...");');
  AW('  api("POST", "/api/auth/login", { username: u, password: p }).then(function () {');
  AW('    return api("GET", "/api/auth/me");');
  AW('  }).then(function () { location.href = RETURN_TO; }).catch(function (e) {');
  AW('    busy(b, false);');
  AW('    $("err").innerHTML = alertBox("error", e.message);');
  AW('  });');
  AW('  return false;');
  AW('}');
  AW('</script>');
  AW('</body></html>');
  SendHtml(AdmH);
  AdmH := '';
end;
