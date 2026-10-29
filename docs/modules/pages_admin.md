# pages_admin — staff admin pages (/admin/*)

Port of `sedev-frontend/src/app/admin/*`, `layouts/dashboard`, `auth/*` (sign-in, guards, account
popover, change password) and `sections/{source,social,entity-admin,lens,run,topic-admin,report,settings,user}`.

## Files

| File | Contents |
|---|---|
| `pages_admin.inc` | globals (`AdmH`, `AdmMe`), `AW()` html writer, auth/redirect helpers, icons, dashboard shell (sidebar nav, header, account popover, password + confirm dialogs), form builders, `Routes_pages_admin` |
| `pages_admin_login.inc` | `/admin/login` |
| `pages_admin_content.inc` | `/admin/topics`, `/admin/people`, `/admin/runs`, `/admin/runs/:id` |
| `pages_admin_config.inc` | `/admin/sources` (+ source dialog, weights dialog), `/admin/social` (+ account dialog, posts table), `/admin/lenses`, `/admin/settings`, `/admin/users` |
| `pages_admin_reports.inc` | `/admin/reports` (Тайлан) with hand-made SVG charts |
| `public/admin.css`, `public/admin.js` | shared CSS and JS helpers; pasted inline into every admin page on each request |
| `checks.d/pages_admin.txt` | smoke tests |

## How it works

- The server builds each page as one HTML string. Each page has inline `<style>`/`<script>` and copy-pasted constants (categories, run stages), as the user asked.
- Vanilla JS calls the same `/api/*` endpoints and JSON shapes as the original (`lib/axios.js` endpoints), with the `sedev_token` cookie.
- Auth is checked on the server. A protected page verifies the cookie (`JwtVerify` + the `users` record, which must be active). Without a valid session it answers `302 /admin/login?returnTo=<path>`.
- Any API `401` in the browser also sends the user to the login page.
- The sidebar shows only the items the role's permissions allow (`PermsOf`). A page the role may not open shows "Хандах эрхгүй" (PermissionGuard).
- `/admin` redirects to `/admin/topics`, as in the original.
- `/admin/login` sends a signed-in user to `returnTo` (default `/admin/runs`, CONFIG.auth.redirectPath). Only local paths are accepted.
- Account popover: "Нүүр сайт", "Нууц үг солих" (`PUT /api/auth/password`), "Гарах" (`POST /api/auth/logout`).
- Polling as in SWR: the runs list refreshes every 4s while a run is running, and run details every 3s.

## Deviations

- No MUI. Plain CSS gives a similar look: sidebar, breadcrumbs, cards, tables, dialogs, labels/chips, switches and tabs.
- Icons are simple hand-drawn SVGs, not Iconify/solar.
- The login side panel uses an SVG illustration instead of the template image.
- Reports charts are hand-made SVG (stacked bars, donut, multi-line, progress bars) with hover tooltips, replacing ApexCharts.
- There is no theme settings drawer (dark mode, nav layout, mini nav). On small screens the sidebar becomes a slide-in drawer.
- Forms are checked by HTML `required` plus the backend's error message. There is no zod on the client.
- The posts table uses simple prev/next paging (50 per page) instead of MUI TablePagination.

## Not ported

- `sections/social/post-card.jsx`: the admin does not use it (it belongs to the reader `/social` page in pages_reader).
- The settings drawer and the nav-horizontal/mini layouts.
