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
cl /EHsc /std:c++17 /I"../include" test_gemini.cpp core/HttpClient.cpp core/GeminiClient.cpp core/ConfigManager.cpp /link winhttp.lib
```

`test_gemini.exe` — live Gemini API test (requires config with API key at `E:\Guild Wars 2\addons\claymore-asistan\config.json`). Tests: single question, multi-turn with interaction ID + model tracking, cooldown status. Costs 2 of 30 RPM free tier (Lite model). **Run this before every DLL handoff.**

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

- **`src/entry.cpp`** — Nexus DLL entry, AddonLoad/Unload/Render/Options, all ImGui rendering coordination. Signature: -77043 (unique, TP-Assistant is -77042). Logger lambda injected into Worker for Nexus log access from worker thread.
- **`src/core/`** — Infrastructure:
  - `HttpClient` — WinHTTP wrapper. GET (10s timeout) + POST (45s timeout, custom headers). POST added for Gemini API.
  - `GeminiClient` — Gemini Interactions API client. Model chain with per-model cooldown. `Ask(question, systemPrompt, prevInteractionId, interactionModel)` → iterates chain, skips cooled-down models, returns `GeminiResponse{text, interactionId, error, activeModel, retrySeconds, ok, fallbackUsed}`. On 429: sets cooldown from parsed retry seconds, tries next model. On 500/503: tries next. Interaction ID only sent to the model that created it (cross-model ID transfer test edilemedi — her denemede 429 nedeniyle). Full 429 message logged for diagnostics.
  - `ConfigManager` — JSON config (API key, model_chain array, window position/size). Dir: Nexus addon data path (`addons/claymore-asistan/`). Old `model`/`model_fallback` keys are ignored; `model_chain` is the single source.
  - `Worker` — Background thread: chat requests, owns ChatSnapshot behind `m_snapshotMutex`. Generation-based cancel. Tracks `m_interactionId` + `m_interactionModel` for context-aware model switching. System prompt: Turkish GW2 expert, search in English, answer in Turkish.
- **`src/chat/`** — UI:
  - `ChatWindow` — ImGui chat window with gw2pao-authentic GW2 theme (PaleGoldenrod #EEE8AA accent, dark transparent background, sharp corners). PushGW2Style/PopGW2Style pattern. Shows active model name (orange when fallback used). Scrollable message area, Enter-to-send, animated "Dusunuyor..." dots.
- **`src/map/`** — Reserved for Faz 3 (map markers, TacO pack reading, 3D projection).
- **`src/data/`** — Reserved for Faz 3 (route management).

### Key Patterns

- **Model chain + cooldown:** `GeminiClient` iterates `model_chain` (default: `[gemini-3.5-flash-lite, gemini-3.5-flash, gemini-3.8-flash]`). On 429, model gets cooldown (parsed retry seconds, default 30s). Next model tried. Rate limit pools are per-model (verified: Lite returns 200 while 3.8-flash returns 429).
- **429'da fallback:** v0.2.0'da etkinlestirildi. Eski karar "429'da fallback yapilmaz" tersi yonde degistirildi — rate limit havuzlari model basina oldugu icin bir modelin 429'u diger modeli etkilemiyor.
- **Snapshot pattern:** Worker writes ChatSnapshot under mutex. Render copies. Never pass pointers across threads.
- **Generation-based cancel:** `m_generation` atomic incremented per request. If changed after `Ask()` returns, result discarded.
- **Question hand-off:** render→worker via `m_pendingQuestion` under `m_cvMutex`, copied out in `Run()` under lock.
- **Interaction model tracking:** Worker stores `(m_interactionId, m_interactionModel)`. `Ask()` only sends the interaction ID to the model that created it. On fallback, context resets (fresh turn on the fallback model).

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
- **Diagnostics:** 429 aldiginda tam hata mesaji Nexus loguna yazilir (`[429] model: ...`). Bu mesaj hangi limitin asildigini gosterir (RPM/RPD/kota).

### UI Theme (gw2pao palette)

- PaleGoldenrod (#EEE8AA / 0.93, 0.91, 0.67) — primary accent (border, button, highlights)
- Window BG: (0.05, 0.04, 0.03, 0.88) — dark, semi-transparent
- Frame BG: (0, 0, 0, 0.37) — pure black, low alpha (gw2pao TextBox)
- Text: pure white
- WindowRounding: 0 (sharp corners, GW2 style)
- User messages: blue (0.55, 0.75, 1.0) — GW2 chat player color
- Assistant messages: PaleGoldenrod
- Fallback indicator: orange (1.0, 0.75, 0.3) — model tag turns orange when fallback used

### Known Limitations (Faz 1)

- No full markdown rendering (basic **bold** + [&chatlink] destegi var, imgui_markdown Faz 2'de)
- No function calling (Gemini answers from its own knowledge — Faz 2 will add GW2 API tools)
- Turkish chars (ğ, ş, ı, İ, Ğ, Ş): Fixed in v0.2.1. Segoe UI 16px with Latin Extended-A range (U+0100-U+017F) via `Fonts_AddFromFile`. Static `ImFontConfig` + `g_turkishRanges` (async atlas rebuild gerektirir). `PushFont/PopFont` in AddonRender + AddonOptions.
- Addon unload may hang up to 45s if Gemini call is in-flight (WinHTTP sync can't be interrupted).
- Google Search grounding disabled on free tier.

### Data Files (addon directory, gitignored)

- `config.json` — API key (sensitive — never commit!), model_chain, window position

### Sister Project

TP-Assistant (`E:\Guild Wars 2\GW2-TP-Assistant\`) — same framework, Trading Post decision support. HttpClient, Worker pattern, ConfigManager template copied from there.
