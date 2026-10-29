# Changelog

## 2026-10-29 — Delphi edition (initial)

- `sedev-backend` (Express/Mongo) and `sedev-frontend` (Next.js/MUI) from sedevmn are merged into one Free Pascal program in Delphi mode (`sedev.dpr` + `{$I}` includes), at the user's request.
- **Code style:** deliberately spaghetti — shared global state, copy-pasted helpers, giant HTML-string procedures.
- **Performance:** deliberately moderate. There are four slowdowns:
  - a single-threaded HTTP server;
  - a `SLOWNESS_MS` delay on every request (default 120 ms);
  - the JSON-file store is re-read on every query;
  - every outbound HTTP call starts a `curl` process.
- **Storage:** MongoDB is replaced by a JSON-file store in `DATA_DIR`. `tools/import-mongo.sh` imports existing data.
- **Passwords:** bcrypt is replaced by salted iterated SHA-256. JWT HS256, SHA-256 and HMAC are implemented by hand.
- **Ported:** all API routes, the pipeline (news/social/publish/world, resume, run lock, token budget, wrap reuse), the scheduler, the CLI scripts, the reader site and the staff admin.
- **Auto check:** `check.sh` builds, boots the server and runs 131 endpoint checks. It runs on GitHub Actions for every push and PR. An opt-in pre-commit hook is in `tools/hooks`.
- **Docs:** `README.md`, `docs/USER_GUIDE.md`, `docs/ADMIN_MANUAL.md` and `docs/modules/*.md`.
