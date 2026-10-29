# Module "sources" (libtext.inc, sources.inc, collector.inc)

## What was ported

| Original (sedev-backend/src) | Pascal |
|---|---|
| `lib/text.ts` htmlToText, richText, fillTemplate (pick = core `JPath`) | `libtext.inc` `HtmlToText`, `RichText`, `RichTextStr`, `FillTemplate` |
| `lib/dates.ts` parseDay, dateWindow | `ParseDay`, `ParseDayAt`, `ParseDayJ`, `DateWindow` |
| `lib/similarity.ts` | `Shingles`, `Jaccard`, `NearDuplicateGroups` |
| cheerio / fast-xml-parser | hand-rolled HTML/XML parser `HParse` + CSS selector engine `HSelect` / `HMatches` |
| WHATWG `URL` (the parts discovery needs) | `UrlResolve`, `UrlParseAbs`, `UrlQueryGet`, `UrlHost`, `UrlPath` |
| `modules/sources/defaults.ts`, `world-defaults.ts` | `Src_Defaults` (79 built-ins), `SRC_REMOVED` |
| `source.model.ts` (defaults, regionFilter) | `Src_Canon`, `Src_InRegion` |
| `sources.routes.ts` (zod sourceBody) | `Routes_sources`, `Src_ParseBody`; probe route in `Routes_collector` |
| `weights.ts`, `recalc.ts` | `Src_SourceWeights`, `Src_RecalcSourceWeights` (with its own copy of `linkStories`) |
| `scripts/sync-sources.ts`, `recalc-weights.ts`, seed part of `seed.ts` | `Src_SyncSources`, `Src_RecalcWeights`, `Src_SeedSources` |
| `modules/collector/discover.ts`, `extract.ts`, `collect.ts` | `Col_DiscoverLinks`, `Col_ExtractHtml`, `Col_FetchJsonArticle`, `Col_FetchArticle`, `Col_IsUsable`, `Col_CollectSource` |
| robots-parser | `Col_LoadRobots`, `Col_RobotsAllowed`, `Col_CrawlDelaySec` |
| `scripts/probe.ts` | `Col_Probe(only)` |

Routes (all need permission `sources`, checked for any path under `/api/admin/sources`):
`GET /api/admin/sources`, `POST /api/admin/sources` (201), `PUT /api/admin/sources/:id`,
`DELETE /api/admin/sources/:id` (204), `POST /api/admin/sources/probe`,
`POST /api/admin/sources/recalculate-weights`. Validation errors use the zod `prettifyError` text
("✖ … → at path"), duplicates give 409 `Already exists`, unknown/invalid ids 404 `Not found`.

## How to use (user guide)

- Admin → Эх сурвалж: create/edit sources as before; "Шалгах" (probe) fetches the live site, lists
  link count and three sample articles.
- "Жин дахин тооцох" (recalculate weights): `{"days":30,"dryRun":true}` previews, without `dryRun` saves.
- CLI (wired by the cli/pipeline agents): `sedev sync-sources` → `Src_SyncSources`,
  `sedev recalc-weights [--days N] [--dry-run]` → `Src_RecalcWeights`, `sedev probe [slug]` → `Col_Probe`.

## Deviations

- **detailUrl `""`**: the original zod union `httpsUrl | ''` throws `Invalid URL: ` for an empty
  detailUrl (the refine throws before the literal is tried), so the admin form could not save html
  sources. Here `""` is accepted.
- **Fetching** uses its own curl call (`Col_Fetch`) instead of core `FetchText`, because core does not
  follow redirects: `-L --max-redirs 4 --proto =https`, final host must be the same site (www ignored).
  No DNS public-address check (core `IsPublicHttpsUrl` only rejects private IP literals / local names).
- **Concurrency / politeness**: articles are fetched one by one (original: 3 parallel). The 300 ms
  minimum delay is dropped; a robots.txt `Crawl-delay` is still honoured with `Sleep`.
- **Regex**: FPC 3.2.2 TRegExpr has no lookahead, mishandles `{n,m}` and `(?:`. All module patterns go
  through `RxFix` (expands bounded quantifiers) / `RxCompat` (`(?:` → `(`); `RxFullMatch` emulates a
  lookahead after a literal prefix (the `/(?=[a-z]*\d)[a-z0-9]{3,5}` patterns). `RxValid` checks admin
  patterns with lookaheads stripped.
- **HTML parser**: html5-ish, not parse5: implied `</p>`, `</li>`, `</td>`..., void and raw-text elements,
  no foster parenting / adoption agency. A document without `<body>` uses the root for the body guess.
- **Dates**: ISO / RFC 822 timestamps without a zone are read as UTC (JS used the server's local time).
- **Lowercasing / letters** (shingles): ASCII, Latin-1, Greek, Cyrillic (incl. Ө Ү) case folding; "letter"
  = ASCII alnum or any non-ASCII code point outside known punctuation/symbol blocks.
- Responses have no mongoose `__v`. Floats are printed by fpjson (`2.5E+000` style, valid JSON).
- RSS 1.0 items without `<link>` are skipped (original added the literal `"undefined"`).

## Not ported

- DNS-based SSRF guard of `lib/http.ts`.
- `p-limit` parallelism.
