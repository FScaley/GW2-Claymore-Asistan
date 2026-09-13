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

`test_gemini.exe` — live Gemini API test (requires config with API key at `E:\Guild Wars 2\addons\claymore-asistan\config.json`). Tests: single question, multi-turn (previous_interaction_id). Costs 2 of 20 RPM free tier. **Run this before every DLL handoff.**

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

- **`src/entry.cpp`** — Nexus DLL entry, AddonLoad/Unload/Render/Options, all ImGui rendering coordination. Signature: -77043 (unique, TP-Assistant is -77042).
- **`src/core/`** — Infrastructure:
  - `HttpClient` — WinHTTP wrapper. GET (10s timeout) + POST (45s timeout, custom headers). POST added for Gemini API.
  - `GeminiClient` — Gemini Interactions API client. `Ask(question, systemPrompt, prevInteractionId)` → `GeminiResponse{text, interactionId, error, statusCode, ok}`. Fallback model on 500/503 (model-spesifik high demand). 429 rate limit'te fallback yapilmaz (429 body'si per-model olabilir ama her deneme kotayi tuketir). Response parse: `steps[]` where `type=="model_output"` → join `content[].text`.
  - `ConfigManager` — JSON config (API key, model, model_fallback, model_tier, window position/size). Dir: Nexus addon data path (`addons/claymore-asistan/`).
  - `Worker` — Background thread: chat requests, owns ChatSnapshot behind `m_snapshotMutex`. Generation-based cancel. System prompt: Turkish GW2 expert, search in English, answer in Turkish.
- **`src/chat/`** — UI:
  - `ChatWindow` — ImGui chat window with gw2pao-authentic GW2 theme (PaleGoldenrod #EEE8AA accent, dark transparent background, sharp corners). PushGW2Style/PopGW2Style pattern. Scrollable message area, Enter-to-send, animated "Dusunuyor..." dots.
- **`src/map/`** — Reserved for Faz 3 (map markers, TacO pack reading, 3D projection).
- **`src/data/`** — Reserved for Faz 3 (route management).

### Key Patterns

- **Snapshot pattern:** Worker writes ChatSnapshot under mutex. Render copies. Never pass pointers across threads.
- **Generation-based cancel:** `m_generation` atomic incremented per request. If changed after `Ask()` returns, result discarded.
- **Question hand-off:** render→worker via `m_pendingQuestion` under `m_cvMutex`, copied out in `Run()` under lock (same as TP-Assistant's `m_inventoryIdentity` pattern).

### Gemini API Notes

- **Endpoint:** `POST generativelanguage.googleapis.com/v1beta/interactions`
- **Auth:** `x-goog-api-key` header
- **Multi-turn:** `previous_interaction_id` field (server-side state)
- **System instruction:** sent every turn (interaction-scoped)
- **Google Search grounding:** FREE TIER'DA CALISMAZ (kota 0). Sadece ucretli key ile `tools: [{"type": "google_search"}]` gonderilir. Free tier'da Gemini kendi bilgisiyle cevap verir.
- **Rate limit (free tier):** 20 RPM. Pro modeller: limit 0 (kullanilamaz).
- **Fallback:** 500/503'te model_fallback'a gecer. 429'da fallback yapilmaz (her deneme kotayi tuketir).
- **model_tier config:** Okunur ama henuz kullanilmaz. Faz 2'de: `model_tier=="paid"` → Pro model + Google Search grounding aktif.
- **Response parse:** `steps[]` → `type=="model_output"` → `content[].text` birlestir. `thought` step'leri gosterilmez.
- **Key format:** `AQ.` prefix gecerli (eski `AIzaSy` bilgisi yanlis).
- **Eski modeller (2.5):** Yeni kullanıcılara kapali (404).

### UI Theme (gw2pao palette)

- PaleGoldenrod (#EEE8AA / 0.93, 0.91, 0.67) — primary accent (border, button, highlights)
- Window BG: (0.05, 0.04, 0.03, 0.88) — dark, semi-transparent
- Frame BG: (0, 0, 0, 0.37) — pure black, low alpha (gw2pao TextBox)
- Text: pure white
- WindowRounding: 0 (sharp corners, GW2 style)
- User messages: blue (0.55, 0.75, 1.0) — GW2 chat player color
- Assistant messages: PaleGoldenrod

### Known Limitations (Faz 1)

- No full markdown rendering (basic **bold** + [&chatlink] destegi var, imgui_markdown Faz 2'de)
- No function calling (Gemini answers from its own knowledge — Faz 2 will add GW2 API tools)
- Turkish chars (ğ, ş, ı, İ, Ğ, Ş) may render as `?` in ImGui default font atlas — test with "Font testi" line in Options. Fix: `Fonts_AddFromFile` with Latin Extended-A range (Faz 2).
- Addon unload may hang up to 45s if Gemini call is in-flight (WinHTTP sync can't be interrupted).
- Google Search grounding disabled on free tier.

### Data Files (addon directory, gitignored)

- `config.json` — API key (sensitive — never commit!), model settings, window position

### Sister Project

TP-Assistant (`E:\Guild Wars 2\GW2-TP-Assistant\`) — same framework, Trading Post decision support. HttpClient, Worker pattern, ConfigManager template copied from there.
