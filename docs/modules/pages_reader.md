# pages_reader: public reader site

Port of the sedev-frontend reader: `app/(reader)/*`, `app/(deck)/topics/[id]`, the root layout and metadata, `manifest.js`, and `sections/home`, `billboard`, `edition`, `reader`, `chat`, `audio`, `people` and `social`. It also includes the PWA install prompt, the service worker and the «Хөдөлгөөн» (motion) setting.

## Files
- `pages_reader.inc`: routes and the HTML of every page. This includes the inline JS for `/archive`, `/people`, `/people/:slug` and `/social`, and for the home page's Сошиал tab.
- `reader_assets/reader.js`: shared browser code with no framework. It covers helpers, the motion setting, the PWA, the billboard, rank arrows, stories and reels, the edition view, the home tabs, the Bloomberg-style SVG charts, the motifs and photo scenes, the story slides, the card-deck reader, chat and audio, and the `/topics/:id` deck.
- `reader_assets/reader.css`: every style, in plain CSS that reproduces the Minimal/MUI look (Inter + Noto Serif, brand blue and red).
- `reader_assets/sw.js`: copied unchanged from the original.
- `reader_assets/ic-content.svg`: the empty-state illustration.
- `public/logo`, `public/pwa`, `public/favicon.ico`, `public/fonts/*.woff2`: copied from the original (the fonts come from @fontsource-variable).

The files in `reader_assets/` are served by this module with real MIME types, because `ServeStatic` sends files without a known type as `application/octet-stream`. A stylesheet, a service worker or an SVG served that way does not work.

## Routes
| Path | Page |
|---|---|
| `/` | Home with four tabs. **Шуурхай** (default): the `/api/feed` stream, newest first, infinite scroll by `nextCursor`. **Халуун**: billboard of the latest edition (top 20 by `hot`). **Гадаад**: `/api/editions/latest?region=world`. **Сошиал**: trending posts. |
| `/archive` | List from `/api/editions`. |
| `/editions/:id` | Edition billboard with the heading «Архив: start – end». |
| `/people`, `/people/:slug` | People list; profile with KPIs, an SVG trend chart (bars for mentions, a line for tone) and the tabs Мэдээ / Мэдэгдэл / Хэвлэл мэдээлэл / Холбоо / Сэдвүүд. |
| `/social` | Trending posts with platform tabs and infinite scroll by page. |
| `/topics/:id` | Full-screen card deck over the topic's edition, ranked by `hot`. The address follows the open topic. |
| `/manifest.webmanifest`, `/sw.js`, `/reader.css`, `/reader.js`, `/assets/icons/empty/ic-content.svg` | Assets. |

All data is fetched in the browser from the same `/api/*` endpoints as the original. A 404 from the API shows an empty state ("Хуудас олдсонгүй", "Сэдэв олдсонгүй" and so on).

## Behaviour kept
- **Billboard.** Hero, two runner-up cards and the rows below, each row with a hot bar. Rank arrows (▲n, ▼n, ШИНЭ, –) spring in and count up when they scroll into view. In Шуурхай the billboard has no ranks and shows "N минутын өмнө".
- **Stories and reels.** The stories row marks seen topics in `localStorage` `sedev:seen-topics`. Reels show a 9 s bar per topic and support tap, swipe, hold to pause, the arrow keys, Space and Esc.
- **Topic reader.** The slide list is a direct port of `story-slides.js`, including the visual plan, the motif keywords, and photo and chart alternation. Readers swipe or tap between slides, and swipe up or down (or use ↑↓) to change topic. Text shrinks until it fits. Share uses `navigator.share` or the clipboard. Esc closes the reader.
- **Charts.** All 14 Bloomberg Visual Data kinds are drawn as hand-made SVG and redrawn every frame by one `requestAnimationFrame` loop. Photo scenes use a Ken Burns move and a colour grade.
- **Chat drawer.** `POST /api/chat` with the last 4 messages as history. Citation badges and cited-article cards are shown, the conversation is kept in `sessionStorage` per topic, and ⌘J / Ctrl+J toggles the drawer.
- **Audio FAB.** Plays `/api/topics/:id/audio/:i.mp3` parts in order and prefetches 2 parts ahead. A progress ring and a ⏮ ⏭ ✕ panel are shown.
- **PWA.** The service worker is registered and the install card works as in the original (Chrome prompt, iOS hint, in-app browser link). The card is dismissed for 14 days.

## Deviations
- There is no React, MUI, framer-motion, Remotion or SWR. Morph transitions are simple CSS scale and slide animations, and the rank badge uses a CSS spring instead of a Remotion player.
- Icons come from the `iconify-icon@2.1.0` web component (jsdelivr), which fetches icon data from the Iconify API at runtime.
- **Motion setting.** The original stores «Хөдөлгөөн» in the settings drawer, which readers never see. Here it is a ▶/⏸ button in the reader header, stored in `localStorage` `app-settings.motion`. When it is off, scenes are still frames and badges do not animate.
- Topic effects (`effects/catalog.js`) were left out. The story view renders scenes "bare", so the original never showed them either.
- There is no server-side rendering of content. Only the `<title>` of `/topics/:id` (from `DbGet('topics')`) and `/people/:slug` (from `DbFindOne('entities','slug')`) is looked up on the server.
- `/topics/:id`, `/editions/:id` and `/people/:slug` with an unknown id return a 200 page with a not-found empty state, not an HTTP 404.
- Only the light colour scheme is supported (readers got the defaults in the original too).

## Not ported
- The Minimal template boilerplate: theme, settings drawer, illustrations and the progress bar.
- The admin pages, which belong to the pages_admin module.
