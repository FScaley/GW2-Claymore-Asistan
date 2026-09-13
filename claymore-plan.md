# GW2-Claymore-Asistan — Gelistirme Plani

## Proje Ozeti

GW2 Nexus addon'u: Gemini AI destekli oyun ici asistan. Sohbet penceresi, TP fiyatlari, craft hesaplama, NPC bulma, harita rotalari.

**Kararlar:**
- Ayri addon (TP-Assistant'tan bagimsiz)
- Cift model: ucretli key → Pro, ucretsiz key → Flash
- Faz 3 (harita) dahil, mimari buna gore tasarlandi
- Turkce (kisisel kullanim)

**Artifact (fizibilite + mimari plan):** https://claude.ai/code/artifact/5e9600e8-e9ff-4e47-9b6b-cb78782fe816

---

## Faz Durumu

### Faz 0: Smoke Test — TAMAMLANDI (13 Eylul 2026)

Detaylar: `faz0-rapor.md`

**Onemli bulgular:**
- Interactions API calisiyor, gemini-3.8-flash calisiyor
- Key tier: FREE — Pro modeller limit 0
- Rate limit: 20 RPM (free Flash)
- **Google Search grounding: FREE TIER'DA CALISMAZ** (kota 0)
  - Grounding yok iken 200, grounding var iken 429
  - Cozum: grounding kapatildi, Faz 2'de WikiClient + function calling ile wiki erisimi
- Eski modeller (2.5): yeni kullanicilara kapali
- Key format: AQ. prefix gecerli
- C++ HttpClient::Post dogrulanmis (test_gemini.exe)

### Faz 1: AI Sohbet Cekirdegi — TAMAMLANDI (13 Eylul 2026)

**Teslim edilenler:**
- Nexus addon iskeleti (entry.cpp, Signature -77043, ALT+C keybind)
- ConfigManager (API key, model, pencere konum/boyut)
- GeminiClient (Interactions API, non-streaming, multi-turn, hata matrisi)
- Worker thread (ChatSnapshot mutex pattern, generation-based cancel)
- ChatWindow (gw2pao temali ImGui penceresi)
- HttpClient POST desteği (45s timeout)
- test_gemini.exe (C++ entegrasyon testi)

**Advisor düzeltmeleri (3 tur):**
1. HttpClient'a POST ekle, Turkce glyph testi, benzersiz Signature, config path
2. AddonOptions keystroke restart sorunu (Kaydet butonu), SetKeyboardFocusHere fix
3. Google Search grounding free tier'da calismaz — grounding kapatildi, 429'da fallback kaldirildi (rate limit tum key icin gecerli)

**v0.1.1 (13 Eylul 2026):**
- RenderFormattedText: word-flow renderer — **bold** altin, [&chatlink] cyan tiklanabilir
- PushID cakismasi duzeltildi, post-build path duzeltildi
- Nexus auto-update: UP_GitHub + UpdateLink

**v0.2.0 (13 Eylul 2026):**
- Model zinciri: gemini-3.5-flash-lite (30 RPM) → gemini-3.5-flash (15 RPM) → gemini-3.8-flash (15 RPM)
- Per-model cooldown: 429'da model sogutuluyor, sonraki deneniyor
- Rate limit havuzlari model basina (test ile dogrulandi: Lite 200, 3.8-flash 429 ayni anda)
- 429 tam hata mesaji Nexus loguna yazilir (APIDefs->Log thread-safe — mutex korumali, dogrulandi)
- Interaction model takibi: fallback'ta ID sifirlanir (cross-model ID test edilemedi)
- ConfigManager: model_chain array (eski model/model_fallback yok sayilir)
- UI: aktif model gosterimi, yedek kullanildiginda turuncu

**Bilinen limitler:**
- Tam markdown render yok (sadece **bold** + [&chatlink] — imgui_markdown Faz 2'de)
- Function calling yok (Gemini kendi bilgisiyle cevap verir)
- Turkce font: v0.2.8 Segoe UI + nullptr config (Nexus pattern). Options + cevap metni calisiyor. Input'ta Turkce karakter ImGui/Nexus sorunu (kontrol disinda)
- v0.2.2-v0.2.5 crash: Nexus hot-reload + ArcDPS korelasyonu. Tam restart ile olmuyor
- Addon unload 45s'ye kadar bekleyebilir (in-flight Gemini cagrisi)
- ClearHistory() cagirici yok (Temizle butonu Faz 2'de)
- model_tier config okunur ama kullanilmaz (Faz 2: paid → Pro + grounding)

### Faz 2: Akilli Veri Entegrasyonu — DEVAM EDIYOR

**v0.3.0 (13 Eylul 2026) — Tamamlandi:**
- GW2Client — /v2/items, /v2/commerce/prices, /v2/recipes, /v2/recipes/search, Wiki opensearch + parse
- ItemIndex — lazy item isim→ID cache (items_index.json), wiki lookups ile doldurulur
- FunctionHandler — Gemini function_call → API cagri → function_result dongusu
- 3 tool (birlestirilmis): gw2_item_info (item+fiyat tek seferde), gw2_recipe (tarif+malzeme+maliyet+kar), gw2_wiki (wiki arama+icerik)
- Fiyat formatlama (copper → Xg Ys Zc)
- Gemini function calling loop: Ask(tools) → requires_action → FunctionHandler → SendFunctionResults → final response. Max 4 round.
- FC 429 handling: pinned model retry (≤12s), else cooldown + chain fallback restart
- Iptal butonu (generation bump ile), Temizle butonu (sohbet gecmisi silme)
- Tool status gosterimi (yesil, "Araniyor: gw2_item_info...")

**Faz 2 kalan (v0.3.1+):**
- gw2_map tool — /v2/maps, waypoint chat_link
- imgui_markdown — zengin metin render (ayri release: font handling + link callback riski)
- ~~Turkce font fix~~ (v0.2.8'de tamamlandi — Options + cevap calisiyor, input ImGui/Nexus siniri)
- Google Search grounding: ucretli key varsa tools ekle (ertelendi — free tier'da test edilemez)

### Faz 3: Harita Isaretcileri ve Rotalar — BEKLIYOR

**Icerik:**
- ProjectionMath — WorldToScreen (GW2-Nexus-Pathing referans)
- TacoParser — pugixml + miniz ile TacO XML/.trl okuma
- MarkerRenderer — ImGui::GetBackgroundDrawList() ile 3D marker
- TrailRenderer — trail/rota cizimi
- RouteManager — paket yukleme, MapID filtreleme
- Topluluk paketleri (Tekkit's All-In-One, Lady Elyssa, Teh's Trails)
- 2D harita marker — API/Wiki koordinatlari minimap/haritada
- Continent ↔ map-space koordinat donusumu (map_rect/continent_rect)
- AI-tetiklenen 2D marker
- Mesafe + yon gostergesi

**Kritik sinir:** API/Wiki koordinatlari 2D — 3D dunya marker sadece TacO paketlerinden mumkun.

### Faz 4: Gelismis Navigasyon — OPSIYONEL

- GPS yon oklari
- Harita tamamlama ilerleme durumu
- Mastery point rota vurgulama

---

## Teknik Referanslar

| Kaynak | Kullanim |
|--------|----------|
| GW2-Nexus-Pathing (YoruDev) | 3D projeksiyon + TacO parse |
| Tyrian Codex (MorningStarGG) | GPS, minimap overlay, wiki render |
| GW2 Assistant (Moisschen) | Gemini entegrasyon referansi |
| imgui_markdown (enkisoftware) | Markdown render (Faz 2) |
| NPC Finder (Abattele) | Wiki API NPC konum cekme |
| gw2pao (SamHurne) | UI renk paleti (PaleGoldenrod #EEE8AA) |
| Tekkit's All-In-One | Marker paketi (49,920 marker) |

## GW2 API Notlari

- Rate limit: 600/dk (gozlemlenmis, 11 Eylul 2026)
- Toplu sorgu: 200 ID/istek
- NPC konumlari: API'da YOK — Wiki'den cekilir
- POI'lerde chat_link alani mevcut (dogrulanmis)
- map_rect/continent_rect ile koordinat donusumu

## Gemini API Notlari

- Endpoint: /v1beta/interactions (Interactions API)
- Model: gemini-3.8-flash (free), fallback: gemini-3.5-flash
- Free tier: 20 RPM, grounding kota 0, Pro limit 0
- Eski modeller (2.5): yeni kullanicilara kapali
- Key format: AQ. prefix gecerli
- Response parse: steps[].content[].text (type=="model_output")
- 429'da per-model cooldown + zincir fallback (v0.2.0'da degistirildi — rate limit havuzlari model basina)
- Model zinciri: gemini-3.5-flash-lite → gemini-3.5-flash → gemini-3.8-flash
- Faz 2: model_tier=="paid" → Pro model + grounding tools aktif
- 500/503'te de zincir fallback
