// admin.js - shared helpers for the Сэдэв.мн staff pages. pages_admin.inc pastes this into every admin page.
// globals everywhere, on purpose. window.ME is set by the page (signed-in user + permissions).
var UB_OFFSET_MS = 8 * 60 * 60 * 1000;
function $(id) { return document.getElementById(id); }
function esc(s) { return String(s == null ? "" : s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;").replace(/'/g, "&#39;"); }
function pad(n) { return String(n).padStart(2, "0"); }
function fDateTimeUB(iso) {
  if (!iso) return "—";
  var d = new Date(new Date(iso).getTime() + UB_OFFSET_MS);
  if (isNaN(d.getTime())) return "—";
  return d.getUTCFullYear() + "." + pad(d.getUTCMonth() + 1) + "." + pad(d.getUTCDate()) + " " + pad(d.getUTCHours()) + ":" + pad(d.getUTCMinutes());
}
function fWindow(w) { return w ? (w.start + " – " + w.end) : "—"; }
function fTokens(u) { u = u || {}; return ((u.inputTokens || 0) + (u.outputTokens || 0)).toLocaleString("en-US"); }
function fUsd(v) { if (typeof v !== "number") return "—"; return "$" + v.toFixed(v < 1 ? 4 : 2); }
function fCount(n) {
  if (!n) return "0";
  if (n >= 1e6) return (n / 1e6).toFixed(1).replace(/\.0$/, "") + "M";
  if (n >= 1e3) return (n / 1e3).toFixed(1).replace(/\.0$/, "") + "K";
  return String(Math.round(n));
}
function can(p) { return !!(window.ME && ME.permissions && ME.permissions.indexOf(p) >= 0); }
var ROLES = {
  superadmin: { label: "Супер админ", color: "error" },
  admin: { label: "Админ", color: "warning" },
  editor: { label: "Редактор", color: "primary" },
  moderator: { label: "Модератор", color: "info" },
  journalist: { label: "Сэтгүүлч", color: "success" }
};
function roleLabel(r) { return ROLES[r] ? ROLES[r].label : r; }
function lbl(text, color) { return "<span class=\"lbl " + (color || "") + "\">" + esc(text) + "</span>"; }

// fetch wrapper like lib/axios.js: rejects with Error(backend {error}) + .status; 401 -> login
function api(method, url, body) {
  var opt = { method: method, credentials: "include", headers: {} };
  if (body !== undefined) { opt.headers["Content-Type"] = "application/json"; opt.body = JSON.stringify(body); }
  return fetch(url, opt).then(function (res) {
    return res.text().then(function (txt) {
      var data = null;
      try { data = txt ? JSON.parse(txt) : null; } catch (e) { data = null; }
      if (!res.ok) {
        if (res.status === 401 && !window.NO_LOGIN_REDIRECT) {
          location.href = "/admin/login?returnTo=" + encodeURIComponent(location.pathname);
        }
        var msg = (data && data.error) || ("Алдаа гарлаа (" + res.status + ")");
        var err = new Error(msg); err.status = res.status; throw err;
      }
      return data;
    });
  }, function (e) { throw new Error(e && e.message ? e.message : "Алдаа гарлаа"); });
}
function apiGet(url) { return api("GET", url); }

function alertBox(sev, text, extraCls) { return "<div class=\"alert " + sev + " " + (extraCls || "") + "\">" + esc(text) + "</div>"; }
function showErr(id, msg) { var el = $(id); if (!el) return; el.innerHTML = msg ? alertBox("error", msg, "pl") : ""; }
function busy(btn, on, text) {
  if (!btn) return;
  if (on) { btn.dataset.txt = btn.innerHTML; btn.classList.add("loading"); if (text) btn.innerHTML = text; }
  else { btn.classList.remove("loading"); if (btn.dataset.txt) btn.innerHTML = btn.dataset.txt; }
}
function openDlg(id) { $(id).classList.add("open"); }
function closeDlg(id) { $(id).classList.remove("open"); }

// confirm dialog (ConfirmDialog): title, content, red action button
function confirmDlg(title, content, actionText, onOk) {
  $("cfT").textContent = title;
  $("cfC").textContent = content;
  $("cfOk").textContent = actionText || "Устгах";
  $("cfOk").onclick = function () { closeDlg("cfDlg"); onOk(); };
  openDlg("cfDlg");
}

// form helpers: values by element id
function v(id) { var el = $(id); return el ? el.value : ""; }
function chk(id) { var el = $(id); return !!(el && el.checked); }
function setv(id, val) { var el = $(id); if (el) el.value = val == null ? "" : val; }
function setchk(id, val) { var el = $(id); if (el) el.checked = !!val; }
function show(id, on) { var el = $(id); if (el) el.style.display = on ? "" : "none"; }

// header: account popover, password dialog, logout, mobile nav
function toggleAcct(ev) { ev.stopPropagation(); $("acctPop").classList.toggle("open"); }
document.addEventListener("click", function (e) {
  var p = $("acctPop");
  if (p && p.classList.contains("open") && !p.contains(e.target)) p.classList.remove("open");
});
document.addEventListener("keydown", function (e) {
  if (e.key === "Escape") { var all = document.querySelectorAll(".dlgbg.open"); if (all.length) all[all.length - 1].classList.remove("open"); }
});
function toggleNav(on) { $("nav").classList.toggle("open", on); $("scrim").classList.toggle("open", on); }
function openPw() {
  $("acctPop").classList.remove("open");
  setv("pwCur", ""); setv("pwNew", ""); $("pwMsg").innerHTML = "";
  openDlg("pwDlg");
}
function submitPw(ev) {
  ev.preventDefault();
  var btn = $("pwBtn");
  $("pwMsg").innerHTML = "";
  busy(btn, true);
  api("PUT", "/api/auth/password", { currentPassword: v("pwCur"), newPassword: v("pwNew") }).then(function () {
    setv("pwCur", ""); setv("pwNew", "");
    $("pwMsg").innerHTML = alertBox("success", "Нууц үг солигдлоо");
  }).catch(function (e) { $("pwMsg").innerHTML = alertBox("error", e.message); }).then(function () { busy(btn, false); });
  return false;
}
function logout() {
  api("POST", "/api/auth/logout").catch(function (e) { console.error(e); }).then(function () {
    location.href = "/admin/login?returnTo=" + encodeURIComponent(location.pathname);
  });
}
