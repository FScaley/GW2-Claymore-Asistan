# CLAUDE.md

This file provides guidance to Claude Code when working with code in this repository.

## What This Is

A Guild Wars 2 in-game AI assistant addon (Nexus/Raidcore framework) powered by Google Gemini API. Provides a chat window where the user asks GW2-related questions and gets AI-powered answers — NPC locations, crafting recipes, TP prices, waypoint info, general game knowledge.

The UI language is Turkish. The design document is `claymore-plan.md` (comprehensive, Turkish). The fizibilite artifact is at: https://claude.ai/code/artifact/5e9600e8-e9ff-4e47-9b6b-cb78782fe816

## Build

**Toolchain:** Visual Studio 2022 (v143), C++17, x64 only. Opens as `GW2-Claymore-Asistan.sln`.

```
# Build via VS Developer Command Prompt or MSBuild:
msbuild src\GW2-Claymore-Asistan.vcxproj /p:Configuration=Release /p:Platform=x64

# Debug build:
msbuild src\GW2-Claymore-Asistan.vcxproj /p:Configuration=Debug /p:Platform=x64
```

Output: `build\Release\claymore-asistan.dll`. Post-build copies to `E:\Guild Wars 2\addons\claymore-asistan.dll` (silent fail if game running — disable addon in-game CTRL+O before rebuild).

**Dependencies:** nlohmann/json (header-only, `include/json.hpp`), WinHTTP (`winhttp.lib`), Dear ImGui (vendored in `src/imgui/`, Nexus-provided version — do NOT update independently). `src/nexus/` and `src/mumble/` are vendored headers.

## Tests

```
# From src/ directory (VS Developer Command Prompt):

# Markdown renderer parser — ZERO cost, no network, no API key:
cl /EHsc /std:c++17 /MT /utf-8 test_markdown.cpp chat/Markdown.cpp

# Wiki path only — ZERO Gemini cost (wiki + GW2 API), no API key needed:
cl /EHsc /std:c++17 /MT /I"../include" test_wiki.cpp core/HttpClient.cpp core/GW2Client.cpp core/WikiText.cpp core/ItemIndex.cpp core/FunctionHandler.cpp /link winhttp.lib

# Full suite — live Gemini + GW2 API:
cl /EHsc /std:c++17 /MT /I"../include" test_gemini.cpp core/HttpClient.cpp core/GeminiClient.cpp core/ConfigManager.cpp core/GW2Client.cpp core/WikiText.cpp core/ItemIndex.cpp core/FunctionHandler.cpp core/Worker.cpp /link winhttp.lib
```

`test_markdown.exe` — pure parser test for the chat renderer (`chat/Markdown.cpp`), no network. Feeds the literal TEST 5 and TEST 6 Gemini answers plus an edge-case block; asserts 3 level-3 headers + 27 bullets on the Roller Beetle answer, `* **bold**` → Bullet with Bold first token, backtick-wrapped `` `[&BLoDAAA=]` `` → one ChatLink token with zero backticks left, `1.`/`2)` numbered, nested indent, `---`/`***` rules, stray `**`/`` ` `` dropped without leaving lone-space tokens. **Regression gate for any change to `Markdown.cpp` or `ChatWindow::RenderFormattedText`.** Layout (indent/wrap/spacing) is NOT covered — that needs an in-game look.

`test_wiki.exe` — wiki data path regression test, zero Gemini RPM. Checks: no CSS/JS leak in Beetle Saddle HTML→text; ≥3 level-2 sections detected on Roller Beetle; `gw2_wiki("Roller Beetle")` returns valid JSON whose `sub_collections` contain Beetle Saddle (9 items), Beetle Feed (8), Beetle Juice (10), including "Inquest Beetle Notes"; total result under 40KB (actual ~15.7KB); `section=Unlocking` works; cancel returns the cancelled error. **This is the regression gate for any change on the wiki path (WikiText, HandleWiki, GW2Client wiki calls) — run it first, before test_gemini.**

`test_gemini.exe` — live Gemini + GW2 API test (requires config with API key at `E:\Guild Wars 2\addons\claymore-asistan\config.json`). Tests: price formatting, GW2 item/price fetch, wiki search, FunctionHandler dispatch, Gemini simple question, Gemini FC round-trip, Worker-driven waypoint FC loop (TEST 5: "Toxic Spider Queen nerede" — verifies gw2_map returns real chat_link codes and StripUnverifiedChatLinks doesn't fire), Worker-driven collection FC loop (TEST 6: "Roller Beetle mount nasil acilir? Hangi koleksiyonlar ve hangi itemler lazim?" — observed: ONE `gw2_wiki` call, all 3 collections and all 27 item names relayed verbatim). Costs ~9 RPM — Flash free tier is 20 RPM, so running the suite twice in a row trips 429 → Lite fallback; that is expected and exercises the chain. **Run this before every DLL handoff.**

## Architecture

### Critical Rule: Render Thread Safety

**HTTP, JSON parsing, and all AI calls happen ONLY on the worker thread. The ImGui render callback ONLY reads snapshots under a mutex. Violating this freezes the game.**

```
[Worker Thread]                    [Render Thread (ImGui)]
   ├─ Gemini API POST               │
   ├─ JSON parse                     │
   └─ mutex.lock() → ChatSnapshot   │
                               mutex.lock() → ChatSnapshot → draw ImGui
```

### Module Layout

- **`src/entry.cpp`** — Nexus DLL entry, AddonLoad/Unload/Render/Options, all ImGui rendering coordination. Signature: -77043 (unique, TP-Assistant is -77042). Logger lambda injected into Worker for Nexus log access from worker thread. Owns GW2Client, ItemIndex, FunctionHandler lifecycle.
- **`src/core/`** — Infrastructure:
  - `HttpClient` — WinHTTP wrapper. GET (10s timeout) + POST (45s timeout, custom headers). POST added for Gemini API. Each major client (GeminiClient, GW2Client) gets its own instance.
  - `GeminiClient` — Gemini Interactions API client. Model chain with per-model cooldown. `Ask(question, systemPrompt, prevInteractionId, interactionModel, tools)` → iterates chain, skips cooled-down models. `SendFunctionResults(model, interactionId, results, tools)` → pinned to one model (function result must go back to the model that issued the call). `GetLogger()` exposes the log callback for use outside the class. Returns `GeminiResponse{text, interactionId, error, activeModel, status, retrySeconds, ok, fallbackUsed, functionCalls}`. `ok=true` only on `status=="completed"` with text; `RequiresAction()` when function calls pending.
  - `GW2Client` — GW2 API + Wiki API client. Items (`/v2/items`), prices (`/v2/commerce/prices`), recipes (`/v2/recipes` + `/v2/recipes/search`), maps (`/v2/maps` + `/v2/continents/.../maps/.../pois` for waypoints), wiki opensearch + parse. `WikiPage` struct carries both `wikitext` and `html`: `WikiGetPage(title)` fills `wikitext` (used for structure only), `WikiGetPageHtml(title)` (`action=parse&prop=text&disabletoc=1&disableeditsection=1`, 25s timeout) fills `html` (used for content). `FormatPrice(copper)` → "Xg Ys Zc". `ExtractItemIdFromWikitext()` for wiki→item/map ID resolution. `GetMapWithWaypoints(mapId)` returns all waypoints with real `chat_link` codes.
  - `ItemIndex` — Lazy name→ID cache (`items_index.json`). Populated from wiki lookups + API responses, persisted on addon unload. Keyed by lowercase name; map IDs use `"map:"` prefix. Thread-safe (mutex).
  - `WikiText` (`WikiText.h/.cpp`, namespace `WikiText`) — Wiki HTML/wikitext helpers, no external library. `HtmlToText(html)`: small state-machine HTML→text converter — `<tr>`→newline, `<td>/<th>`→` | `, `<li>`→`- `, `<h2..h4>`→`## Title` markdown-style headers, decodes entities (`&amp;`, `&#160;`, `&#xHH;`, ...), skips `<script>`/`<style>` bodies as raw text (jumps straight to the closing tag), skips elements whose class contains toc/navbox/infobox/thumb/mw-editsection/noprint/catlinks/mw-references-wrap and `<sup class="reference">`, collapses whitespace, drops separator-only lines. `ExtractTableRows(html, classSubstring)`: cell arrays for every non-header row of tables whose class contains the substring (`"mech1"` = wiki collection tables); does not handle nested `<table>`. `SplitLead(text, sections)`: splits converted text on `## ` lines into lead + level-2 sections. `ExtractSubCollectionNames(wikitext, max)`: regex for `{{achievement icon|page=X}}` and `{{collection|X}}`, strips `#anchor`, skips names containing "(achievements)" (category pages). `DecodeEntities(s)`.
  - `FunctionHandler` — Dispatches Gemini `function_call` to GW2Client/ItemIndex. `Handle(call, CancelCheck shouldCancel)` with `CancelCheck = std::function<bool()>`; handlers poll it between HTTP calls and return `{"error": "cancelled by user"}` (HandleWiki: before every GET — search, wikitext, HTML, each sub-collection, map lookup; HandleItemInfo/HandleRecipe: after ResolveItemId; HandleMap: after ResolveMapId). 4 tools: `gw2_item_info(name)` (wiki→ID→item+prices in one shot), `gw2_recipe(name)` (recipe+ingredients+costs+profit), `gw2_map(name)` (wiki→map ID→API waypoints with real chat_link codes), `gw2_wiki(query, section?)`. `HandleWiki` flow: opensearch → wikitext (structure only: item ID, `| location =`, redirect, sub-collection templates) → follow `#REDIRECT [[X]]` (strips `#anchor`, saves one Gemini round) → `WikiGetPageHtml` → `WikiText::HtmlToText` → `SplitLead` → `content` = lead + level-2 sections in document order until `WIKI_TEXT_BUDGET` (12KB), with a truncation note telling Gemini to request the section by name. Result JSON: `title`, `url`, `content`, `sections` (section titles), `related` (other opensearch hits), `item_id` (if infobox has one), `items` (if the page itself has a mech1 collection table), `sub_collections` (array of `{name, item_count, items:[{name, hint}]}` — each discovered sub-collection, cap `WIKI_SUBPAGE_CAP`=5, gets its HTML fetched and its mech1 table extracted deterministically; sub-pages yielding no items are dropped silently), `map_name` + `nearby_waypoints` (if infobox lead has `| location = [[Map]]` — appends that map's waypoints with real chat_link codes), `section`/`section_error` (when the optional `section` argument is used: case-insensitive match against section titles, returns that one section in full up to 12KB), `content_source: "wikitext_fallback"` if the HTML fetch failed. `ExtractCollectionItems(html)`: rows with a numeric first cell → `name` = cell[1], `hint` = the cell starting with `Hint:` (prefix stripped); no hint → no `hint` key (no fallback, to avoid wrong values). `ResolveMapId` detects `Location infobox` pages and caches with `"map:"` prefix. Returns not-found/not-tradeable as text result to Gemini, never as a turn failure. (`TruncateWikitext` and the 4KB wikitext truncation were deleted in v0.3.7.)
  - `ConfigManager` — JSON config (API key, model_chain array, window position/size). Dir: Nexus addon data path (`addons/claymore-asistan/`). Old `model`/`model_fallback` keys are ignored; `model_chain` is the single source.
  - `Worker` — Background thread: chat requests, owns ChatSnapshot behind `m_snapshotMutex`. Function calling loop: Ask(tools) → while requires_action: FunctionHandler → SendFunctionResults → repeat (max 6 rounds). On FC 429: retry once if ≤12s, else abandon and restart via Ask(). Generation check after every HTTP call. CancelChat() bumps generation. Passes `[this, gen]{ return !IsGenerationCurrent(gen); }` as the `CancelCheck` into `FunctionHandler::Handle`, so cancel also takes effect inside tools (within one HTTP GET). Chat link provenance check: `StripUnverifiedChatLinks` replaces any `[&...]` code in the final response that was not found in any tool result with `[kod dogrulanamadi]` — makes fabricated waypoint codes impossible regardless of model behavior. FC log line (`FC name args -> (NB) result_preview`, N = result byte size, e.g. `FC gw2_wiki {"query":"Roller Beetle"} -> (15731B) {"content":...`) for diagnostics. `fallbackUsed` carried from initial `Ask()`, not overwritten by FC responses. System prompt in English for better instruction-following; GW1 explicitly prohibited; tool usage required for factual data, direct answers for opinions/advice. COLLECTION RULE (v0.3.7): for mounts/legendaries/collections/achievements, relay every item from every `sub_collections` entry verbatim; use ONLY the provided `hint` text (translated), never substitute locations/NPCs/quantities from memory; if a sub-collection is missing, call `gw2_wiki` with its exact name; use the `section` param for truncated sections.
- **`src/chat/`** — UI:
  - `Markdown` (`Markdown.h/.cpp`, namespace `Markdown`, **no ImGui dependency** — that is what makes it unit-testable) — markdown-lite line parser for Gemini answers. `Parse(text)` → `vector<Line>`; each `Line` has `kind` (Blank/Plain/Header/Bullet/Numbered/Rule), `level` (header 1-6), `indent` (leading spaces / 2, cap 4), `marker` (`"1."`/`"2)"`), and `tokens`. Line classification by prefix: `#{1,6} ` header (trailing `#`s stripped), `- `/`* `/`+ `/`• ` bullet, `\d{1,3}[.)] ` numbered, `---`/`***`/`___` rule, `> ` blockquote → Plain with indent≥1. `TokenizeInline(line)` → word-flow `Token{style, text}` (each word keeps its trailing space, like the old tokenizer): `**...**` → Bold words (search stays inside the line, so a stray `**` can't swallow the rest of the message — unmatched markers are dropped, along with one following space if the marker stood alone); `` `...` `` → `InlineCode`, or `ChatLink` if the span is exactly a `[&...]` code (backticks dropped); bare `[&...]` → `ChatLink` (validated by `IsChatLink`: `[&` + base64 charset + `]`). Chatlinks and code spans inside bold are still detected. `imgui_markdown` was evaluated and rejected: its link model is `[text](url)`, headers need separate `ImFont*`s, and the chatlink copy behavior would have to be re-implemented inside its callback.
  - `ChatWindow` — ImGui chat window with gw2pao-authentic GW2 theme (PaleGoldenrod #EEE8AA accent, dark transparent background, sharp corners). PushGW2Style/PopGW2Style pattern. Shows active model name (orange when fallback used). Tool status indicator (green, "Araniyor: gw2_item_info..."). Iptal button when busy (red), Temizle button when idle. Scrollable message area, Enter-to-send. `RenderFormattedText(text)` = `Markdown::Parse` + per-line layout: Blank → one `Spacing()` per run (not `NewLine()` — Gemini pads heavily); Rule → gold `Separator()`; Header → `Spacing()` + tokens in `COL_HEADER` (gold; color and spacing only — no second font this release) + `Spacing()`; Bullet/Numbered → dim marker (`-` or `1.`), then `Indent(markerWidth)` + `SameLine(0, spaceW)` so wrapped continuation lines align under the text, `Unindent` after; nested `indent` → `Indent(indentUnit * indent)` where `indentUnit` = width of 4 spaces. `RenderTokens(tokens, maxX, linkId, overrideColor)` is the shared word-flow renderer: `SameLine(0,0)` between tokens, `NewLine()` when `GetCursorPosX() + width > maxX` (both window-relative, so consistent under `Indent`); ChatLink tokens are `Selectable` with `PushID(linkId++)`, click → clipboard, tooltip "Kopyalandi!"; other tokens `TextColored` by style (Bold gold, InlineCode light gray-blue, Plain white) unless `overrideColor` (headers) is set.
- **`src/map/`** — Reserved for Faz 3 (map markers, TacO pack reading, 3D projection).
- **`src/data/`** — Reserved for Faz 3 (route management).

### Key Patterns

- **Model chain + cooldown:** `GeminiClient` iterates `model_chain` (default: `[gemini-3.5-flash, gemini-3.5-flash-lite, gemini-3.8-flash]` — Flash primary for quality, Lite overflow for rate). On 429, model gets cooldown (parsed retry seconds, default 30s). Next model tried. Rate limit pools are per-model (verified).
- **Function calling loop:** Worker sends `Ask()` with tool definitions → Gemini returns `requires_action` + `function_call` steps → FunctionHandler dispatches to GW2 API/Wiki → results sent back via `SendFunctionResults()` → Gemini returns final text. Max 6 rounds. Multiple function calls in one response are batched. If loop exhausts all rounds, shows honest error ("kesin veri bulunamadi") — never re-asks without tools (that was v0.3.2's hallucination source).
- **Chat link provenance:** `StripUnverifiedChatLinks` regex-scans the final text for `[&...]` codes. Any code not found in tool result strings is replaced with `[kod dogrulanamadi]`. Model-independent guard — works regardless of prompt adherence.
- **FC 429 handling:** `SendFunctionResults` is pinned to the model that issued the call (can't switch models mid-tool-loop). On 429: retry once if wait ≤12s, else set cooldown and abandon — Worker falls back to `Ask()` on next chain model, restarting the question fresh.
- **Tool consolidation:** 4 composite tools. `gw2_item_info(name)` does wiki→ID→item+prices in one shot. `gw2_map(name)` does wiki→map ID→API waypoints with real chat_link codes. Effective throughput ~8 questions/min on Flash 20 RPM (with FC).
- **Wiki content: rendered HTML, deterministic extraction (v0.3.7):** Wikitext is used only for *structure* (item ID from infobox, `| location =`, `#REDIRECT`, sub-collection discovery via `{{achievement icon|page=X}}` / `{{collection|X}}`). *Content* comes from rendered HTML (`action=parse&prop=text`) converted by `WikiText::HtmlToText`. Why: collection sub-pages like "Beetle Saddle" are 723 bytes of `{{achievement box}}{{collection table}}` templates in wikitext with ZERO item names — the real table only exists in rendered HTML — and the old section-name whitelist (`TruncateWikitext`: Location/Acquisition/Walkthrough/Contents/Notes) missed `== Unlocking ==`, so Gemini filled the gaps from memory. Collection item lists (`items`, `sub_collections`) are extracted from mech1 tables by code, not by the model, so item names/counts are deterministic; only hint wording is left to the model.
- **FC diagnostics:** Every tool call is logged as `FC name args -> (NB) result_preview` (N = result byte size, preview 120 chars). Stripped fake chatlinks are logged as `Stripped fake chatlink: [&...]`.
- **Snapshot pattern:** Worker writes ChatSnapshot under mutex. Render copies. Never pass pointers across threads. `toolStatus` field shows active tool name during FC loop.
- **Generation-based cancel:** `m_generation` atomic incremented per request or cancel. Checked after every HTTP call in the FC loop, and inside tools via the `CancelCheck` lambda passed to `FunctionHandler::Handle` (HandleWiki checks before every GET) — cancel takes effect within one in-flight GET (≤10-25s).
- **Interaction model tracking:** Worker stores `(m_interactionId, m_interactionModel)`. Final completed response's ID is stored, not intermediate `requires_action` IDs.

### Gemini API Notes

- **Endpoint:** `POST generativelanguage.googleapis.com/v1beta/interactions`
- **Auth:** `x-goog-api-key` header
- **Multi-turn:** `previous_interaction_id` field (server-side state)
- **System instruction:** sent every turn (interaction-scoped)
- **Google Search grounding:** FREE TIER'DA CALISMAZ (kota 0). Sadece ucretli key ile `tools: [{"type": "google_search"}]` gonderilir. Free tier'da Gemini kendi bilgisiyle cevap verir.
- **Rate limit (free tier, raporlanan):** gemini-3.5-flash: 20 RPM (429 body says `limit: 20`), gemini-3.5-flash-lite: 30 RPM, gemini-3.8-flash: 15 RPM. RPD: ~1,500. Pro modeller: limit 0 (kullanilamaz).
- **Fallback:** 429'da cooldown + zincir fallback. 500/503'te de fallback. Rate limit havuzlari model basina (dogrulanmis).
- **Cross-model interaction ID:** Test edilemedi (her denemede 429 nedeniyle). Guvenli tasarim: fallback'ta ID sifirlanir, kullanici bir mesaj icin baglam kaybeder ama cevap alir.
- **model_chain config:** Default `["gemini-3.5-flash", "gemini-3.5-flash-lite", "gemini-3.8-flash"]` (Flash primary — v0.3.6'da degistirildi, Lite halusinasyonlari nedeniyle). `config.json`'daki deger korunur; DEFAULT yalnizca yeni kurulumlar icin. Eski `model`/`model_fallback` yok sayilir.
- **Response parse:** `steps[]` → `type=="model_output"` → `content[].text` birlestir. `thought` step'leri gosterilmez.
- **Key format:** `AQ.` prefix gecerli (eski `AIzaSy` bilgisi yanlis).
- **Eski modeller (2.5):** Yeni kullanicilara kapali (404).
- **Function calling (v0.3.0):** `tools` array in request with `type: "function"`. Response `status: "requires_action"` + `function_call` steps. Results sent back via separate interaction (`previous_interaction_id` + `input: [{type: "function_result", ...}]`). Must resend same `tools` array. Multiple calls possible per response.
- **Diagnostics:** 429 tam hata mesajı Nexus loguna yazılır (`[429] model: ...`). `APIDefs->Log` thread-safe (mutex korumalı — Nexus source'ta doğrulandı). Worker thread'den güvenle çağrılabilir.

### UI Theme (gw2pao palette)

- PaleGoldenrod (#EEE8AA / 0.93, 0.91, 0.67) — primary accent (border, button, highlights)
- Window BG: (0.05, 0.04, 0.03, 0.88) — dark, semi-transparent
- Frame BG: (0, 0, 0, 0.37) — pure black, low alpha (gw2pao TextBox)
- Text: pure white
- WindowRounding: 0 (sharp corners, GW2 style)
- User messages: blue (0.55, 0.75, 1.0) — GW2 chat player color
- Assistant messages: PaleGoldenrod
- Fallback indicator: orange (1.0, 0.75, 0.3) — model tag turns orange when fallback used

### Known Limitations (v0.3.8)

- Markdown rendering is "markdown-lite" (v0.3.8): `**bold**`, `#`-`######` headers, `-`/`*`/`+` bullets, numbered lists, `---` rules, `` `code` ``, `[&chatlink]`. Not supported: tables (the system prompt forbids Gemini from emitting them — they would render as pipe soup), images, `[text](url)` links, `*italic*`, nested bold-inside-code. Headers differ from body by color and spacing only — a larger header font would be a second Nexus `Fonts_AddFromFile` with its own atlas-rebuild null race; separate release if wanted.
- Nested bullets: Gemini indents with 2 spaces per level, renderer indents 4-space-widths per level — wider than the source, cosmetic.
- A prose line starting with `N. ` (e.g. "3. koleksiyon icin...") is classified as a Numbered list item. Acceptable.
- `**` inside a backtick span breaks the code span (the top-level `**` scan runs before the backtick scan). Gemini doesn't emit this.
- Renderer layout (indent/wrap/spacing) is verified only by reasoning + the parser test — ImGui can't be rendered outside the game. Parser is covered by `test_markdown.exe`.
- `HandleWiki` location→waypoints append still depends on `| location = [[Map]]` wiki link format in infobox lead; semicolon-separated area names (e.g. `| location = Earthlord's Gap; Viathan's Arm`) don't trigger it — Gemini must call `gw2_map` separately (which it does reliably on Flash).
- Cancel inside tools now works (v0.3.7 — previously "unresponsive inside HandleWiki, ~90s worst case") but is polled only between HTTP calls: it takes effect within one in-flight GET, i.e. ≤10s (GW2 API / wikitext) or ≤25s (`WikiGetPageHtml`).
- Sub-collection fetches in `HandleWiki` are serial, ~50KB HTML each, no cache — asking about the same mount twice = 4-5 full re-fetches. Add a page cache only if the FC log shows repeated identical `gw2_wiki` queries.
- `WikiText::ExtractTableRows` doesn't handle a nested `<table>` inside a mech1 table (the first `</table>` closes the outer one) — not observed on any tested page.
- `WikiText::ExtractSubCollectionNames` only recognizes `{{achievement icon|page=X}}` and `{{collection|X}}`; sub-collections linked via other templates won't be auto-expanded — Gemini must call `gw2_wiki` for them by name (the COLLECTION RULE instructs it to).
- Hint embellishment on Lite (e.g. wiki hint says "Dabiji Hollows", model wrote "Pogahn Bluffs") is model behavior, not a data gap — content completeness (item names/counts) is deterministic; mitigated by the COLLECTION RULE, expected better on Flash.
- Addon unload worst case well past 100s (Ask 45s + FC retry 12s + SendFunctionResults 45s + HandleWiki chain).
- `SendFunctionResults` retry sleeps up to 12s without cancel awareness.
- `config.json` model_chain persists — `DEFAULT_MODEL_CHAIN` only affects fresh installs.
- Turkish chars (ğ, ş, ı, İ, Ğ, Ş): `Fonts_AddFromFile` ile Segoe UI 16px, `aConfig=nullptr`. Chat input'ta Turkce karakter yazmak ImGui/Nexus WM_CHAR handling sorunu — bizim kontrolumuz disinda.
- Google Search grounding disabled on free tier. `model_tier` config not yet implemented.
- FC turns are 2+ Gemini API calls → effective throughput ~8 questions/min on Flash 20 RPM.
- v0.2.2-v0.2.5 crash'leri: Nexus hot-reload + ArcDPS korelasyonu. "Check for updates" yerine oyunu kapatip acmak gerekiyor.

### Data Files (addon directory, gitignored)

- `config.json` — API key (sensitive — never commit!), model_chain, window position
- `items_index.json` — Lazy item name→ID cache, populated from wiki lookups

### Sister Project

TP-Assistant (`E:\Guild Wars 2\GW2-TP-Assistant\`) — same framework, Trading Post decision support. HttpClient, Worker pattern, ConfigManager template copied from there.
