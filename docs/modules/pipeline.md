# Module "pipeline" (topics.inc, pipeline.inc, cli.inc additions)

## What was ported

| Original (sedev-backend/src) | Pascal |
|---|---|
| `modules/topics/categories.ts` | `TOP_CATEGORIES`, `Top_ToCategory`, `Top_CategorySchema` |
| `modules/topics/visuals.ts` (rules, cleanVisuals; the schema lives in lenses.inc) | `Top_VisualsRules`, `Top_CleanVisuals` |
| `modules/topics/feed.ts` | `Top_FeedEntries`, `Top_FeedCmp`, `Top_Cursor` (uses `Rep_LinkStories` from reports.inc) |
| `modules/topics/topics.routes.ts`, `topics.admin.routes.ts` | `Routes_topics` |
| `modules/topics/topic.model.ts`, `edition.model.ts` | collections `topics`, `editions` (same fields) |
| `modules/pipeline/pipeline.ts` | `Pipe_Collect/Extract/Social/Cluster/Select/Wrap/Publish`, `Pipe_ExecuteRun`, `Pipe_StartRun`, `Pipe_ResumeRun`, `Pipe_FailInterruptedRuns` |
| `modules/pipeline/items.ts` | `Pipe_SpreadArticles`, `Pipe_AuthorStem`, `Pipe_PostBatches`, `Pipe_PersonStems`, `Pipe_GroupByPerson` |
| `modules/pipeline/scheduler.ts` | `Pipe_SchedulerTick` (called each minute by the scheduler thread), `Pipe_Boot` |
| `lib/prompts.ts` (SYSTEM, cluster/select/wrap prompts) | `PIPE_SYSTEM`, `Pipe_ClusterPrompt`, `Pipe_SelectPrompt`, `Pipe_WrapPrompt` |
| `modules/runs/run.model.ts`, `runs.routes.ts` | collection `runs`, `Routes_pipeline` |
| `scripts/run-job.ts`, `scripts/backfill-visuals.ts` | `Pipe_CliJob`, `Pipe_BackfillVisuals` |

## API

Public (reader):
- `GET /api/feed?region=mn|world&cursor&limit(1-50,20)`: the Шуурхай feed, `{topics:[card…], nextCursor}`.
- `GET /api/editions?region`: the last 100 editions.
- `GET /api/editions/latest?region`: returns 404 `No edition published yet` when there is none.
- `GET /api/editions/:id`: the edition with cards and `previousRank` (a number, `null` = new, missing when there is no previous edition).
- `GET /api/topics/:id`: the topic without `sourceIds` citations, with `images` (up to 12) and `editionId`/`region`. A hidden topic returns 404.

Admin, with a permission per route:
- `GET /api/admin/topics` (`topics.view`): the last 200 topics.
- `PUT /api/admin/topics/:id` (`topics.edit`): `{title, reason, category}`.
- `PUT /api/admin/topics/:id/visibility` (`topics.hide`): `{hidden}`.
- `GET /api/admin/runs?kind` (`runs.view`): the last 50 runs, without config, events, candidates, selected or sourceReports.
- `GET /api/admin/runs/:id` (`runs.view`): the run without `config.sources` and `candidates`.
- `POST /api/admin/runs` (`runs.start`): `{kind}`, default `publish`. Returns 202 `{_id}`. The run continues in a background thread; poll `GET /:id` for progress.
- `POST /api/admin/runs/:id/resume` (`runs.start`): returns 202, or 404 `No failed run with this id`.

A second run of the same kind while one is running returns 409 `Another run is in progress`. Validation errors use the zod `prettifyError` text. As in the original, `news`/`world` without enabled sources and `publish`/`world` without enabled wrap points return 500.

## How to use (user guide)

- **Admin → Ажиллагаа**: start a job (news / social / publish / world) and open a run to follow its events. A failed run can be resumed: it continues from the stage that failed. Runs that were still running when the server stopped are marked failed at boot ("Interrupted by a server restart…").
- **Scheduler**: every minute it reads `settings.schedules[kind]` (`enabled`, `cron`, in Mongolia time). Changing the settings takes effect from the next minute. Set `RUN_SCHEDULER=false` to turn the scheduler off.
- **Cost savings**:
  - A *scheduled* publish/world run is skipped with a note when fewer than `minNewItems` items are new since the last edition.
  - A topic whose sources did not change copies the previous wrap (`reusedWraps`).
  - `dailyTokenLimit` stops LLM calls for the rest of the day (Mongolia time).
- **Admin → Сэдвүүд**: correct the title, reason or category of a topic (`topics.edit`), or hide or show it (`topics.hide`).
- **CLI**:
  ```
  sedev job news|social|publish|world        # one run now, blocking (manual trigger: publish is never skipped)
  sedev job resume <runId>                    # resume a failed run
  sedev seed                                  # now also seeds the 79 built-in sources and the default social accounts
  sedev sync-sources | recalc-weights [--days N] [--dry-run] | probe [slug] | collect-social
  sedev backfill-visuals [--editions N] [--dry-run]   # visuals for older topics, one LLM call each
  ```

## Deviations

- **Threads.** Manual and scheduled runs execute in a `TPipeRunThread`, and the HTTP server stays single-threaded. Every write to a run, topic or edition is a load-modify-save under the core `DbLock`, which is recursive. The collect and extract stages are serialized by `PipeArticlesLock`, because they write `articles` and the collector keeps global state. An admin source probe that runs at the same moment as a collect stage can still interfere.
- **Extract stage usage** is booked by diffing the shared `GeminiIn/GeminiOut/GeminiCalls` counters, because `Ent_ExtractStage` has no usage callback. A chat or audio request in the same seconds can be counted into the run. The token budget is checked once before the stage, not before each call.
- **Sequential collection.** Sources are collected one after another, where the original ran 4 in parallel.
- **No feed cache.** The feed is rebuilt on every request, where the original cached it per edition.
- **Cost rounding.** Run `costUsd` is rounded to 1e-12, which hides the single-precision noise from core `LlmCostUsd`.
- **JSON in prompts** comes from `Chat_Js` (fpjson escaping, for example `\/`), not from `JSON.stringify`. `groupByPerson` compares keys by bytes, not with `localeCompare`.
- **Names and patterns.** `personStems` and `authorStem` are hand-written over code points, because TRegExpr has no lookbehind and no Cyrillic classes.
- **Run log lines** go to stdout through core `Log`. With stdout redirected, FPC buffers the output per thread.

## Not ported

- `nextRuns()` of scheduler.ts. It is computed by settings.inc (`Set_NextRun`).
- Mongo indexes. The run lock is an explicit check under `DbLock`.
