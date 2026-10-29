# Module "people": auth, users, settings, lenses, entities, articles, CLI

Files: `auth.inc`, `settings.inc`, `lenses.inc`, `entities.inc`, `articles.inc`, `cli.inc`, `checks.d/people.txt`.

## What was ported

| Original (sedev-backend/src) | Pascal |
|---|---|
| modules/auth/auth.ts, users.routes.ts, user.model.ts | `auth.inc` (`Routes_auth`) |
| modules/settings/settings.routes.ts, `migrateSettings` | `settings.inc` (`Routes_settings`, `Set_Migrate`) |
| modules/lenses/* (defaults, routes, wrap-schema), scripts/merge-lenses.ts | `lenses.inc` (`Routes_lenses`, `Lens_*`) |
| modules/entities/* (defaults, match, extract, metrics, profile, routes), scripts/extract-entities.ts | `entities.inc` (`Routes_entities`, `Ent_*`) |
| modules/articles/articles.routes.ts | `articles.inc` (`Routes_articles`) |
| scripts/create-user.ts, seed.ts | `cli.inc` (`Cli_Main`) |

### API (same paths, status codes and JSON as the original)

- `POST /api/auth/login`, `POST /api/auth/logout` (204), `GET /api/auth/me`, `PUT /api/auth/password` (204)
- `GET|POST /api/admin/users`, `PUT|DELETE /api/admin/users/:id` (permission `users`)
- `GET|PUT /api/admin/settings` (permission `settings`), answer includes `nextRuns` and `providerOptions`
- `GET|POST /api/admin/lenses`, `PUT /api/admin/lenses/order`, `PUT|DELETE /api/admin/lenses/:id` (permission `lenses`)
- `GET /api/entities`, `GET /api/entities/:slug`, `GET /api/entities/:slug/mentions?page&limit&role`
- `GET|POST /api/admin/entities`, `PUT /api/admin/entities/:id` (permission `entities`)
- `GET /api/articles?page&limit&q&source`, `GET /api/articles/sources`

Validation errors are 400 with `{"error": "✖ message\n  → at path"}` (the `z.prettifyError` format). Duplicate
username / lens key / entity slug is 409 `Already exists`. Unknown or malformed id is 404 `Not found`.

### CLI

```
sedev create-user <username> <password> <role> [name]   # also accepts the original order <username> <role> <password>
sedev seed                                              # legacy users, ADMIN_USERNAME/ADMIN_PASSWORD superadmin, lenses, people, settings
sedev extract-entities [--days N] [--rescan]            # find tracked people in collected articles (costs LLM tokens)
sedev merge-lenses [--dry-run]                          # one-off: cause -> trending, impact -> perspectives
```

### Callable from other modules

- `Lens_WrapSchema(lenses, sourceCount)`, `Lens_CleanWraps(...)`, `Lens_LoadSnapshots`, `Lens_SourceLabel(i)`, `Lens_SeedDefaults`
- `Ent_ExtractStage(dayStart, dayEnd, region, model)` → `{scanned, matched, mentions}` or `{error}`; `Ent_ScanArticles` (raises)
- `Set_Migrate` (boot migration), `Set_CronValid`, `Set_NextRun`
- `Auth_UpsertUser`, `Auth_BootstrapAdmin`, `Auth_IsRole`
- helpers: `Ppl*` (UTF-8: `PplLower`, `PplUCut`, `PplULen`, code point/letter tests, `PplRound2`), `Pv*` (zod-like validators: `PvReset/PvStr/PvNum/PvBool/PvEnum/PvObj/PvQInt/PvFail/PvDone`)

## Deviations

- Passwords use core `HashPassword`/`CheckPassword` (iterated SHA-256) instead of bcrypt; existing bcrypt hashes from Mongo cannot sign in (reset with `create-user`).
- Logout clears the cookie with an expired `Set-Cookie` (no `Max-Age`), like `res.clearCookie`.
- `nextRuns` is computed by scanning minutes with core `CronMatches` (null when `RUN_SCHEDULER` is not `true` or the job is off). `PUT` settings does not need `applySchedule`: the scheduler thread re-reads settings each minute.
- Cron validation is a simple field/range check (node-cron also accepts month/weekday names, which pass here too).
- Alias matching (`match.ts`) is hand-written: no lookbehind / `\p{L}` in TRegExpr, so code points are decoded and Latin/Greek/Cyrillic count as letters.
- Article title search lower-cases ASCII, Latin-1 and Cyrillic (incl. Ө, Ү, Ё) instead of a Mongo `$regex` with `i`; it is a plain substring search.
- String lengths and `excerpt` (220) / text slices count code points, not UTF-16 units.
- The extract stage uses `GeminiJson` directly; token usage lands on the core `GeminiIn/GeminiOut/GeminiCalls` counters (the pipeline should diff them to book usage and check its budget) instead of the pipeline's `ask` callback.
- `Lens_WrapSchema` contains its own copy of `topics/visuals.ts visualsSchema` (motif catalogue ids) so it does not depend on the topics module.
- TRegExpr bug: `^[class]{n,m}$` matches too short strings, so username and slug patterns are checked by hand (`PplCharsOk`).

## Not ported / left to others

- `seed` does not seed sources or social accounts yet (`// TODO pipeline agent wires this` in `cli.inc`).
- `Set_Migrate` must be called from `Pipe_Boot` (pipeline module).
- Floats in JSON output come out as `3.3000000000000002E-001` (fpjson default formatting in core); valid JSON, but not the same text as Node.
