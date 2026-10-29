# Module: social

Files: `social.inc` (everything), `social_defaults.inc` (126 default accounts, `platform|url|name|kind|weight|followers`).
Smoke tests: `checks.d/social.txt`.

## Ported (sedev-backend/src/modules/social/*)

- **account.model / post.model** → collections `socialaccounts`, `socialposts` (same fields; dates ISO strings, ids 24-hex).
  `(platform, handle)` and `(platform, externalId)` uniqueness enforced in code (409 `Already exists` for accounts).
- **handleFromUrl** → `Soc_HandleFromUrl`.
- **defaults.ts + seed** → `Soc_DefaultAccounts`, `Soc_SeedAccounts` (inserts missing accounts only).
- **providers** (twitterapi.io for X, ScrapeCreators for Facebook/Threads): same endpoints, params, paging/stop rules,
  credit accounting and normalization. HTTP through core `CurlRequest`; keys `TWITTERAPI_IO_KEY`, `SCRAPECREATORS_KEY`.
  Retries 429/5xx up to 5 times with exponential backoff (Retry-After is not read: curl helper does not expose headers).
- **collect.ts** → `Soc_Collect` (per-platform reports + usage, same log line), `Soc_FetchPlatformPosts`, `Soc_SinceMs`.
  Posts upserted, account followers / lastPostAt / lastPostId / pageId / lastCollectedAt / baseline updated, scores recomputed.
- **score.ts** → engagement, post score, decay, median, articleTime, discussion/trigger boost, topic signals, logNormalize, rankHot.
  Own copy of `lib/similarity.ts` (shingles / jaccard / nearDuplicateGroups) for reprint detection.
  All numbers of `test/score.test.ts` and the social parts of `test/providers.test.ts` were reproduced in a scratch test program.
- **social.routes.ts**
  - public `GET /api/social/trending?platform&hours(1-336,48)&limit(1-100,30)&page(0)` (topic link from the latest non-world edition)
  - admin (perm `social`) under `/api/admin/social`: `GET /accounts`, `POST /accounts` (201), `PUT /accounts/:id`,
    `DELETE /accounts/:id` (204, deletes the account's posts), `POST /accounts/probe`, `GET /posts?platform&accountId&page&limit(1-100,50)`.
  - zod errors reproduced as `z.prettifyError` text (`✖ message\n  → at field`), status 400.
- **scripts/collect-social.ts** → `Soc_CollectSocialScript` (returns exit code; prints one `key=value` line per report instead of `console.table`).

## Deviations

- Sequential: platforms, accounts and twitter batches are fetched one after another (original: parallel, limit 10).
- `savePosts` loads `socialposts` once per platform and writes it back once (instead of one upsert per post).
- Probe and admin list use the file store order rules: byte-wise string sort (Mongo default collation is also binary).
- Number formatting follows fpjson (`1.5000000000000000E+000`), values are identical JSON numbers.
- `Soc_HttpHook` global: optional replacement for curl, used only by tests.

## Not ported

- Mongo indexes (no need, linear scans).
