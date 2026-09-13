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
cl /EHsc /std:c++17 /MT /I"../include" test_gemini.cpp core/HttpClient.cpp core/GeminiClient.cpp core/ConfigManager.cpp core/GW2Client.cpp core/ItemIndex.cpp core/FunctionHandler.cpp /link winhttp.lib
```

`test_gemini.exe` — live Gemini + GW2 API test (requires config with API key at `E:\Guild Wars 2\addons\claymore-asistan\config.json`). Tests: price formatting, GW2 item/price fetch, wiki search, FunctionHandler dispatch, Gemini simple question, Gemini function calling round-trip, cooldown status. Costs ~3 of 30 RPM free tier (Lite model). **Run this before every DLL handoff.**

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
  - `GeminiClient` — Gemini Interactions API client. Model chain with per-model cooldown. `Ask(question, systemPrompt, prevInteractionId, interactionModel, tools)` → iterates chain, skips cooled-down models. `SendFunctionResults(model, interactionId, results, tools)` → pinned to one model (function result must go back to the model that issued the call). Returns `GeminiResponse{text, interactionId, error, activeModel, status, retrySeconds, ok, fallbackUsed, functionCalls}`. `ok=true` only on `status=="completed"` with text; `RequiresAction()` when function calls pending.
  - `GW2Client` — GW2 API + Wiki API client. Items (`/v2/items`), prices (`/v2/commerce/prices`), recipes (`/v2/recipes` + `/v2/recipes/search`), wiki opensearch + parse. `FormatPrice(copper)` → "Xg Ys Zc". `ExtractItemIdFromWikitext()` for wiki→item ID resolution.
  - `ItemIndex` — Lazy name→ID cache (`items_index.json`). Populated from wiki lookups + API responses, persisted on addon unload. Keyed by lowercase name. Thread-safe (mutex).
  - `FunctionHandler` — Dispatches Gemini `function_call` to GW2Client/ItemIndex. 3 tools: `gw2_item_info(name)` (wiki→ID→item+prices in one shot), `gw2_recipe(name)` (recipe+ingredients+costs+profit), `gw2_wiki(query)` (wiki search+content, truncated to 4KB). Returns not-found/not-tradeable as text result to Gemini, never as a turn failure.
  - `ConfigManager` — JSON config (API key, model_chain array, window position/size). Dir: Nexus addon data path (`addons/claymore-asistan/`). Old `model`/`model_fallback` keys are ignored; `model_chain` is the single source.
  - `Worker` — Background thread: chat requests, owns ChatSnapshot behind `m_snapshotMutex`. Function calling loop: Ask(tools) → while requires_action: FunctionHandler → SendFunctionResults → repeat (max 4 rounds). On FC 429: retry once if ≤12s, else abandon and restart via Ask(). Generation check after every HTTP call. CancelChat() bumps generation. System prompt: Turkish GW2 expert, use tools for item/recipe/wiki data.
- **`src/chat/`** — UI:
  - `ChatWindow` — ImGui chat window with gw2pao-authentic GW2 theme (PaleGoldenrod #EEE8AA accent, dark transparent background, sharp corners). PushGW2Style/PopGW2Style pattern. Shows active model name (orange when fallback used). Tool status indicator (green, "Araniyor: gw2_item_info..."). Iptal button when busy (red), Temizle button when idle. Scrollable message area, Enter-to-send.
- **`src/map/`** — Reserved for Faz 3 (map markers, TacO pack reading, 3D projection).
- **`src/data/`** — Reserved for Faz 3 (route management).

### Key Patterns

- **Model chain + cooldown:** `GeminiClient` iterates `model_chain` (default: `[gemini-3.5-flash-lite, gemini-3.5-flash, gemini-3.8-flash]`). On 429, model gets cooldown (parsed retry seconds, default 30s). Next model tried. Rate limit pools are per-model (verified: Lite returns 200 while 3.8-flash returns 429).
- **Function calling loop:** Worker sends `Ask()` with tool definitions → Gemini returns `requires_action` + `function_call` steps → FunctionHandler dispatches to GW2 API/Wiki → results sent back via `SendFunctionResults()` → Gemini returns final text. Max 4 rounds. Multiple function calls in one response are batched.
- **FC 429 handling:** `SendFunctionResults` is pinned to the model that issued the call (can't switch models mid-tool-loop). On 429: retry once if wait ≤12s, else set cooldown and abandon — Worker falls back to `Ask()` on next chain model, restarting the question fresh.
- **Tool consolidation:** 3 composite tools instead of granular ones. `gw2_item_info(name)` does wiki→ID→item+prices in one shot (saves Gemini round-trips). Effective throughput ~12-15 questions/min on Lite 30 RPM.
- **Snapshot pattern:** Worker writes ChatSnapshot under mutex. Render copies. Never pass pointers across threads. `toolStatus` field shows active tool name during FC loop.
- **Generation-based cancel:** `m_generation` atomic incremented per request or cancel. Checked after every HTTP call in the FC loop.
- **Interaction model tracking:** Worker stores `(m_interactionId, m_interactionModel)`. Final completed response's ID is stored, not intermediate `requires_action` IDs.

### Gemini API Notes

- **Endpoint:** `POST generativelanguage.googleapis.com/v1beta/interactions`
- **Auth:** `x-goog-api-key` header
- **Multi-turn:** `previous_interaction_id` field (server-side state)
- **System instruction:** sent every turn (interaction-scoped)
- **Google Search grounding:** FREE TIER'DA CALISMAZ (kota 0). Sadece ucretli key ile `tools: [{"type": "google_search"}]` gonderilir. Free tier'da Gemini kendi bilgisiyle cevap verir.
- **Rate limit (free tier, raporlanan):** gemini-3.5-flash-lite: 30 RPM, gemini-3.5-flash/3.8-flash: 15 RPM. RPD: ~1,500. Pro modeller: limit 0 (kullanilamaz).
- **Fallback:** 429'da cooldown + zincir fallback. 500/503'te de fallback. Rate limit havuzlari model basina (dogrulanmis).
- **Cross-model interaction ID:** Test edilemedi (her denemede 429 nedeniyle). Guvenli tasarim: fallback'ta ID sifirlanir, kullanici bir mesaj icin baglam kaybeder ama cevap alir.
- **model_chain config:** `["gemini-3.5-flash-lite", "gemini-3.5-flash", "gemini-3.8-flash"]` — eski `model`/`model_fallback` yok sayilir.
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

### Known Limitations (v0.3.0)

- No full markdown rendering (basic **bold** + [&chatlink] destegi var, imgui_markdown ayri release'da)
- No `gw2_map` tool yet (v0.3.1)
- `TruncateWikitext` strips template braces but not template content — `{{vendor table|...}}` becomes `vendor table|...`. Adequate for v0.3.0.
- Turkish chars (ğ, ş, ı, İ, Ğ, Ş): `Fonts_AddFromFile` ile Segoe UI 16px, `aConfig=nullptr`. Options + Gemini cevaplari dogru gorunuyor. Chat input'ta Turkce karakter yazmak ImGui/Nexus WM_CHAR handling sorunu — bizim kontrolumuz disinda, tum addon'larda ayni. `Fonts_Release` unload'da ilk sirada. Pattern: alter_ego, TyrianCodex.
- Addon unload may hang up to ~100s worst case (Ask 45s + FC retry sleep 12s + SendFunctionResults 45s). WinHTTP sync can't be interrupted.
- `SendFunctionResults` retry sleeps up to 12s without cancel awareness — the retry fires even if user hit Iptal.
- Google Search grounding disabled on free tier. `model_tier` config not yet implemented.
- FC turns are 2+ Gemini API calls → effective throughput ~12-15 questions/min on Lite 30 RPM.
- v0.2.2-v0.2.5 crash'leri: Nexus hot-reload + ArcDPS korelasyonu. "Check for updates" yerine oyunu kapatip acmak gerekiyor.

### Data Files (addon directory, gitignored)

- `config.json` — API key (sensitive — never commit!), model_chain, window position
- `items_index.json` — Lazy item name→ID cache, populated from wiki lookups

### Sister Project

TP-Assistant (`E:\Guild Wars 2\GW2-TP-Assistant\`) — same framework, Trading Post decision support. HttpClient, Worker pattern, ConfigManager template copied from there.
