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

**Bilinen limitler:**
- Tam markdown render yok (sadece **bold** + [&chatlink] — imgui_markdown Faz 2'de)
- Function calling yok (Gemini kendi bilgisiyle cevap verir)
- Turkce ğ/ş/ı karakterleri ? olarak gorunebilir (font sorunu)
- Addon unload 45s'ye kadar bekleyebilir (in-flight Gemini cagrisi)
- ClearHistory() cagirici yok (Temizle butonu Faz 2'de)
- model_tier config okunur ama kullanilmaz (Faz 2: paid → Pro + grounding)

### Faz 2: Akilli Veri Entegrasyonu — BEKLIYOR

**Icerik:**
- GW2DataClient — /v2/items, /v2/commerce/prices, /v2/recipes, /v2/maps, /v2/continents
- WikiClient — MediaWiki API (opensearch + parse)
- ItemIndex — lokal item isim→ID cache (items_index.json, ~100K item)
- FunctionHandler — Gemini function_call → API cagri → function_result dongusu
- 5 tool tanimi: gw2_item_search, gw2_tp_price, gw2_recipe_lookup, gw2_map_info, gw2_wiki_search
- Fiyat formatlama (copper → Xg Ys Zc)
- Waypoint chat_link (API'den dogrudan)
- imgui_markdown (zengin metin render)
- Per-call timeout + iptal butonu
- Turkce font fix (Fonts_AddFromFile, Latin Extended-A glyph range)
- Google Search grounding: ucretli key varsa tools ekle (model_tier == "paid" kontrolu)

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
- 429'da fallback yapilmaz (her deneme kotayi tuketir)
- Faz 2: model_tier=="paid" → Pro model + grounding tools aktif
- 500/503'te fallback model dene
