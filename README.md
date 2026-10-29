# Сэдэв.мн — Delphi edition

The whole of sedevmn (`sedev-backend` Express/Mongo + `sedev-frontend` Next.js/MUI) merged into **one Free Pascal program in Delphi mode**: `sedev.dpr`. The same binary serves the JSON API (`/api/*`), the reader site and the staff admin.

The code is deliberately written as one big program. Every file is `{$I}`-included into `sedev.dpr`, everything shares global variables, and modules copy-paste helpers from each other instead of sharing them. It is also deliberately **moderately slow**:
- the HTTP server handles one request at a time;
- every request waits `SLOWNESS_MS` (default 120 ms);
- the "database" is JSON files that are read in full on every query and written back pretty-printed;
- every outbound HTTP call starts a separate `curl` process.

The original sedevmn repo is untouched.

## Requirements

- Free Pascal 3.2.2 (`brew install fpc`, or `apt install fpc`)
- `curl`, used for all outbound HTTP: scraping, Gemini, social providers
- `lame` (optional): MP3 narration. Without it, audio is served as WAV.
- `jq` + `mongoexport` (optional): only for importing the old Mongo data

## Build and run

```sh
./build.sh                     # → build/main/sedev
cp .env.example .env           # then fill in the keys
build/main/sedev seed          # default sources, social accounts, lenses, people, settings, admin from env
build/main/sedev create-user bilguun 'secret-pass' superadmin
build/main/sedev               # serve on PORT (default 4001)
```

Open `http://localhost:4001/` for the reader and `http://localhost:4001/admin/login` for staff.

## Configuration (`.env` or environment)

| Variable | Default | Meaning |
|---|---|---|
| `PORT` | 4001 | HTTP port |
| `DATA_DIR` | `data` | folder of the JSON-file store (one `<collection>.json` per collection, audio in `audio/`) |
| `JWT_SECRET` | — (required) | signs the `sedev_token` session cookie |
| `SITE_ORIGIN` | `http://localhost:4001` | `https://…` makes the cookie `Secure` |
| `RUN_SCHEDULER` | `true` | run the news/social/publish/world crons (Mongolia time) |
| `SLOWNESS_MS` | 120 | delay added to every request |
| `GEMINI_API_KEY` | — | AI Studio key; or set `GOOGLE_GENAI_USE_VERTEXAI=true` + `GOOGLE_CLOUD_PROJECT` (+ `GOOGLE_CLOUD_LOCATION`), which uses `gcloud auth print-access-token` |
| `TWITTERAPI_IO_KEY`, `SCRAPECREATORS_KEY` | — | social providers; a platform without a key is skipped |
| `ADMIN_USERNAME`, `ADMIN_PASSWORD` | — | `seed` creates this superadmin |
| `USER_AGENT` | `SedevResearch/1.0 (+https://sedev.mn)` | scraper user agent |
| `LAME_BIN` | `/opt/homebrew/bin/lame` | MP3 encoder |

## CLI

`build/main/sedev <command>`:

| Command | What it does |
|---|---|
| *(none)* / `serve` | start the server |
| `seed` | default data (sources, social accounts, lenses, people, settings) and the env admin |
| `create-user <username> <password> <role> [name]` | create or reset a staff account (roles: superadmin, admin, editor, moderator, journalist) |
| `job <news\|social\|publish\|world>` | run one pipeline job now, blocking |
| `sync-sources` | add or update the built-in news sources |
| `recalc-weights [--days N] [--dry-run]` | recompute source weights |
| `probe [slug]` | try collecting each source (or one) and print the result |
| `collect-social` | run social collection once |
| `extract-entities [--days N] [--rescan]` | find tracked people in articles |
| `merge-lenses [--dry-run]` | merge old wrap points into the current defaults |
| `backfill-visuals` | generate missing topic visuals |

## Moving data from sedevmn (Mongo)

```sh
tools/import-mongo.sh "mongodb://…/sedev" data
build/main/sedev create-user <you> <new-password> superadmin   # bcrypt hashes cannot be verified here
```

Audio clips are not imported. They are generated again on the first play.

## Auto check

`./check.sh` does the following:
1. builds the program;
2. starts it on a throwaway data dir (port `CHECK_PORT`, default 4099) and creates a test superadmin;
3. runs every line of `checks.txt` and `checks.d/*.txt` (`METHOD PATH STATUS [JSON]`; `LOGIN` signs in, and each file starts logged out);
4. exits non-zero on any failure or unhandled server error.

It runs automatically in two places:
- **GitHub Actions** (`.github/workflows/check.yml`), on every push and PR;
- **locally before each commit**, after you opt in with `git config core.hooksPath tools/hooks`.

## Layout

| File | Contents |
|---|---|
| `sedev.dpr` | program, request dispatcher, static files, scheduler thread |
| `core.inc` | globals, env, JSON-file DB, JSON helpers, SHA-256/HMAC/JWT, curl, Gemini, cron, roles, settings |
| `libtext.inc` | HTML parser + CSS selectors, text, dates, similarity, regex workarounds |
| `sources.inc`, `collector.inc` | news sources, discovery, extraction, weights |
| `social.inc` (+ `social_defaults.inc`) | social accounts, providers, posts, hot score |
| `auth.inc`, `settings.inc`, `lenses.inc`, `entities.inc`, `articles.inc` | staff, settings, wrap points, people, article search |
| `audio.inc`, `chat.inc`, `reports.inc` | narration (TTS), reader chat, Тайлан analytics |
| `topics.inc`, `pipeline.inc` | editions, topics, feed, visuals, runs and the 6-hour pipeline |
| `cli.inc` | commands above |
| `pages_reader*.inc`, `reader_assets/` | reader site (home tabs, billboard, card deck, charts, chat, audio, people, social, archive) |
| `pages_admin*.inc`, `public/admin.*` | staff admin |
| `docs/USER_GUIDE.md` | how readers and staff use the site |
| `docs/modules/*.md` | per-module port notes and deviations |
