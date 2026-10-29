# Module "listen": audio, chat, reports

Files: `audio.inc`, `chat.inc`, `reports.inc`. Smoke tests: `checks.d/listen.txt`.

## Ported

### Audio (`modules/audio/*`)
- `GET /api/topics/:id/audio` returns `{enabled, parts:[{index,title,chars,seconds|null,url}]}`.
- `GET /api/topics/:id/audio/:index.mp3` generates the part on its first play and serves it after that. It supports `Range` (206/416), `Accept-Ranges` and `Cache-Control: public, max-age=3600`.
- Narration (`narration.ts`) is a straight port. Lengths are counted in UTF-16 units, the same as JavaScript, so `chars`, the part splits and the clip keys (`sha256(model\nvoice\ntext)`) match the node app.
- TTS goes through core `GeminiRaw`. The request uses `generationConfig.responseModalities=["AUDIO"]` and `speechConfig{languageCode:"mn-MN", voiceConfig.prebuiltVoiceConfig.voiceName}`. Gemini returns 16-bit PCM at 24 kHz mono.
- The `lame` CLI encodes the PCM to MP3 (mono, 48 kbps CBR). It uses `LAME_BIN`, by default `/opt/homebrew/bin/lame`, and otherwise looks for `lame` on PATH. If lame is missing or fails, the clip is stored as WAV and served as `audio/wav`.
- Storage: the sound is saved as `DATA_DIR/audio/<key>.mp3|.wav`. The metadata goes in `audioclips` as `{key, topicId, model, voice, chars, seconds, bytes, format, file, usage{inputTokens,outputTokens}, costUsd}`, and `costUsd` comes from `TtsCostUsd`. If a doc's file is missing, the doc is deleted and the clip is generated again.
- Daily limit (`settings.audio.dailyLimit`): when it is reached, new parts get 429 `Daily audio limit reached`. If audio is off, generating a part returns 404 `Audio is turned off`.

### Chat (`modules/chat/*`)
- `POST /api/chat` with `{message, topicId, history?}` returns `{answer, articles[]}`.
- Validation follows the zod rules and uses prettified error text in the same format (`✖ … \n  → at field`).
- Rate limit: 10 questions per minute per client IP, kept in memory. Daily limit: `settings.chat.dailyLimit`, counting logs that have a `model`.
- The prompt matches `CHAT_SYSTEM` and `chatPrompt` exactly, with compact JSON like `JSON.stringify`. The model is called through `GeminiJson` with schema `{answer}` and maxOut 2000.
- Each answer adds a doc to `chatlogs`. Every write also deletes logs older than 90 days, which replaces the Mongo TTL index.

### Reports (`modules/reports/*`)
- `GET /api/admin/reports?days=1..90` (default 14) requires `RequirePerm('reports')`.
- Ports `linkStories`, `buildReport` and `buildCosts`. Output keys and order are the same as the node app.
- Editions: mn region only (`region != "world"`), plus 2 extra days of lead-in. Topics with `hidden` are skipped. Run costs come from `runs.usage` and `runs.socialUsage`.

## Deviations
- The server is single-threaded, so the "pending synthesis" map was dropped.
- Gemini retry and backoff come from core `GeminiRaw`: 4 attempts starting at 5 s, where node used 3 attempts starting at 3 s.
- Provider errors (Gemini non-200) become 500 `Internal error`, the same as node.
- Number formatting in JSON responses uses fpjson output. Float values can look like `2.0000000000000000E+000`; they are still valid JSON with the same value. Reports send whole numbers as integers.
- The chat rate limit uses `Req.RemoteAddress`. Node used `req.ip`, which gives the same value because node had no `trust proxy`.
- A suffix range larger than the file (`bytes=-N`) is clamped to 0, where node returned a negative slice.

## Not ported
- No frontend pieces (audio player, chat UI, Тайлан page). They belong to the `pages_*` modules.
