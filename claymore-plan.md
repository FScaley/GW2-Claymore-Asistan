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

**v0.3.0 (13 Eylul 2026):**
- GW2Client — /v2/items, /v2/commerce/prices, /v2/recipes, /v2/recipes/search, Wiki opensearch + parse
- ItemIndex — lazy item isim→ID cache (items_index.json), wiki lookups ile doldurulur
- FunctionHandler — Gemini function_call → API cagri → function_result dongusu
- 3 tool (birlestirilmis): gw2_item_info, gw2_recipe, gw2_wiki
- Fiyat formatlama (copper → Xg Ys Zc)
- Gemini function calling loop: Ask(tools) → requires_action → FunctionHandler → SendFunctionResults
- FC 429 handling: pinned model retry (≤12s), else cooldown + chain fallback restart
- Iptal butonu, Temizle butonu, tool status gosterimi

**v0.3.1–v0.3.5 (13 Eylul 2026) — Hotfix dizisi:**
- v0.3.1: Waypoint hallucination fix (system prompt'a talimat — yetersiz kaldi)
- v0.3.2: Tool dongusu limiti asildiginda tool'suz tekrar sorma (HATA: hallucination kaynagi oldu — v0.3.4'te kaldirdik)
- v0.3.3: gw2_map tool eklendi — /v2/continents API'den gercek waypoint chat_link kodlari
- v0.3.4: Anti-hallucination: chat link provenance check (StripUnverifiedChatLinks), Ingilizce system prompt, tool'suz fallback kaldirildi
- v0.3.5: System prompt dengeleme (genel sorular tool gerektirmesin), MAX_FC_ROUNDS 4→6

**v0.3.6 (13 Eylul 2026) — Gozleme dayali kalite iyilestirmesi:**
- Model zinciri: Flash birincil (20 RPM), Lite yedek (30 RPM). Lite halusinasyon orani kabul edilemez.
- TEST 5: Worker ile Toxic Spider Queen waypoint testi — gercek kodlar dogrulandi
- Wiki redirect otomatik takibi (#REDIRECT [[X]]) — 1 Gemini round tasarrufu
- HandleWiki: | location = [[Map]] algilanirsa o map'in waypointleri otomatik eklenir
- ResolveMapId cache ("map:" prefix ile items_index.json)
- Wiki TruncateWikitext: section-based (Location, Acquisition, Walkthrough, Contents, Notes) (v0.3.7'de kaldirildi — rendered HTML'e gecildi)
- FC log: her tool cagrisi loglanir (name, args, result preview)
- fallbackUsed: ilk Ask()'tan tasinir, FC loop icinde kaybolmaz

**v0.3.7 (13 Eylul 2026) — Wiki yeniden yazimi: rendered HTML + deterministik koleksiyon cikarimi:**
- Problem: "Roller Beetle mount nasil acilir?" sorusunda Gemini 3 koleksiyondan 2'sini, yanlis malzemelerle listeledi
- Kok neden (dogrulandi): (a) gw2_wiki ham *wikitext* cekiyordu — "Beetle Saddle" gibi koleksiyon alt sayfalari 723 byte `{{achievement box}}{{collection table}}` sablonu, SIFIR item adi; gercek tablo yalnizca render edilmis HTML'de var. (b) Eski TruncateWikitext bolum-adi whitelist'i (Location/Acquisition/Walkthrough/Contents/Notes) `== Unlocking ==` bolumunu kaciriyordu. (c) Gemini boslugu hafizadan doldurdu
- Mimari: wiki *icerigi* icin wikitext → rendered HTML (`action=parse&prop=text`); wikitext yalnizca *yapi* icin (infobox item ID, `| location =`, `#REDIRECT`, `{{achievement icon|page=X}}` / `{{collection|X}}` regex ile alt koleksiyon kesfi)
- Yeni modul `src/core/WikiText.h/.cpp` (namespace WikiText): HtmlToText (kutuphanesiz state-machine HTML→text; tr→satir, td/th→` | `, li→`- `, h2-h4→`## Baslik`, entity decode, script/style govdesi + toc/navbox/infobox/thumb/mw-editsection/noprint/catlinks/reference elementleri atlanir, whitespace toplanir), ExtractTableRows (class'i verilen tablolarin baslik disi satirlari — "mech1" = koleksiyon tablolari), SplitLead (lead + level-2 bolumler), ExtractSubCollectionNames (#anchor atilir, "(achievements)" kategori sayfalari atlanir), DecodeEntities
- Offline testte bulunan ve duzeltilen bug: ilk HtmlToText surumu atlanan elementlerin tag'lerini atliyor ama *metnini* atlamiyordu — script/style govdeleri metne sizdi (Beetle Saddle 15KB → fix sonrasi 2.7KB; 12KB JavaScript'ti) ve JS icindeki `<` karakterleri tag parse'ini bozup 12 bolumden yalnizca 1'ini algilatiyordu. Fix: skipDepth>0 iken metin bastirilir, script/style icin kapanis tag'i dogrudan aranir
- GW2Client: WikiPage'e `html` alani, yeni `WikiGetPageHtml(title)` (`action=parse&prop=text&disabletoc=1&disableeditsection=1`, 25s timeout). WikiGetPage wikitext dondurmeye devam ediyor
- HandleWiki yeniden yazildi: opensearch → wikitext (yapi) → `#REDIRECT [[X]]` takibi (#anchor atilir) → HTML → HtmlToText → SplitLead → content = lead + belge sirasiyla level-2 bolumler, WIKI_TEXT_BUDGET (12KB) dolunca kesilir ve Gemini'ye bolumu adiyla istemesi soylenir. TruncateWikitext silindi
- Sonuc JSON alanlari: title, url, content, sections (bolum basliklari), related (diger opensearch sonuclari), item_id (infobox'ta varsa), items (sayfanin kendi mech1 tablosu varsa), sub_collections (`{name, item_count, items:[{name, hint}]}` — kesfedilen her alt koleksiyon icin, WIKI_SUBPAGE_CAP=5, HTML cekilip mech1 tablosu deterministik cikarilir; item vermeyen alt sayfalar sessizce atilir), map_name + nearby_waypoints (mevcut: `| location = [[Map]]` varsa gercek chat_link kodlariyla), section / section_error (opsiyonel `section` parametresi kullanildiginda), content_source="wikitext_fallback" (HTML cekilemezse)
- ExtractCollectionItems(html): ilk hucresi sayisal satirlar → name = hucre[1], hint = `Hint:` ile baslayan hucre (prefix atilir). Hint yoksa `hint` anahtari yok (yanlis deger vermemek icin fallback yok)
- Yeni opsiyonel tool parametresi `section` (string): bolum basliklariyla buyuk/kucuk harf duyarsiz eslesme → o bolum tam olarak (12KB'a kadar) doner
- Tool icinde iptal: `FunctionHandler::Handle(call, CancelCheck shouldCancel)` (`CancelCheck = std::function<bool()>`). Worker `[this, gen]{ return !IsGenerationCurrent(gen); }` gecirir. HandleWiki her HTTP GET arasinda kontrol eder (search, wikitext, HTML, her alt koleksiyon, map lookup) ve `{"error": "cancelled by user"}` dondurur; HandleItemInfo/HandleRecipe ResolveItemId sonrasi kontrol eder. Eski "HandleWiki icinde iptal tepkisiz (~90s)" limiti kapandi — iptal artik tek GET icinde (≤10-25s) etkili
- Worker: FC log satirina sonuc byte boyutu eklendi (`FC gw2_wiki {"query":"Roller Beetle"} -> (15731B) {"content":...`). System prompt'a COLLECTION RULE: mount/legendary/koleksiyon/achievement sorularinda her sub_collections girdisindeki her item aynen aktarilir; yalnizca verilen hint metni (cevrilerek) kullanilir, hafizadan konum/NPC/miktar eklenmez; eksik alt koleksiyon icin gw2_wiki tam adiyla cagrilir; kesilen bolumler icin `section` parametresi kullanilir
- Yeni `src/test_wiki.cpp` → test_wiki.exe: SIFIR Gemini maliyeti (wiki + GW2 API). Kontroller: Beetle Saddle metninde CSS/JS sizintisi yok; Roller Beetle'da ≥3 bolum; gw2_wiki("Roller Beetle") gecerli JSON, sub_collections'ta Beetle Saddle (9 item), Beetle Feed (8), Beetle Juice (10) — "Inquest Beetle Notes" dahil; toplam <40KB (gercek 15.7KB); section=Unlocking calisiyor; iptal cancelled hatasi doner. Wiki yolundaki her degisiklik icin regresyon kapisi — ONCE bu calistirilir. Build: `cl /EHsc /std:c++17 /MT /I"../include" test_wiki.cpp core/HttpClient.cpp core/GW2Client.cpp core/WikiText.cpp core/ItemIndex.cpp core/FunctionHandler.cpp /link winhttp.lib` (src/ icinden, VS Developer Command Prompt)
- test_gemini TEST 6: Worker ile "Roller Beetle mount nasil acilir? Hangi koleksiyonlar ve hangi itemler lazim?" — gozlem: TEK gw2_wiki cagrisi, Gemini (Flash suite'in kendisi yuzunden cooldown'da oldugu icin Lite'ta) 3 koleksiyonu ve 27 item adini aynen listeledi. test_gemini build satirina core/WikiText.cpp eklendi. RPM maliyeti ~9 (Flash free tier 20 RPM — suite arka arkaya iki kez calistirilirsa 429 → Lite fallback; beklenen, zinciri test eder)
- Kalan: bazi hint'ler Lite'ta hafizadan suslendi (wiki hint "Dabiji Hollows" derken model "Pogahn Bluffs" yazdi) — model davranisi, veri eksigi degil; prompt kurali ile azaltildi, Flash'ta daha iyi bekleniyor. Icerik butunlugu (item adlari/sayilari) deterministik
- Bilinen limitler: alt koleksiyon fetch'leri seri, ~50KB HTML, cache yok (ayni mount iki kez = 4-5 tam fetch; yalnizca FC logunda tekrarlanan ayni gw2_wiki sorgusu gorulurse sayfa cache eklenir). ExtractTableRows mech1 tablosu icinde ic ice `<table>` desteklemiyor (gorulmedi). ExtractSubCollectionNames yalnizca iki sablonu taniyor — baska sablonla baglanan alt koleksiyonlar icin Gemini gw2_wiki'yi adiyla cagirmali. location→waypoints hala `| location = [[Map]]` formati gerektiriyor

**Faz 2 kalan:**
- imgui_markdown — zengin metin render (ayri release: font handling + link callback riski)
- ~~gw2_map tool~~ (v0.3.3'te tamamlandi)
- ~~Turkce font fix~~ (v0.2.8'de tamamlandi)
- Google Search grounding: ucretli key varsa tools ekle (ertelendi — free tier'da test edilemez)
- Wiki sayfa cache (kosullu: yalnizca FC logunda tekrarlanan ayni gw2_wiki sorgulari gorulurse — alt koleksiyon fetch'leri seri ve cache'siz)

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
- Model zinciri (v0.3.6+): gemini-3.5-flash (birincil, 20 RPM) → gemini-3.5-flash-lite (yedek, 30 RPM) → gemini-3.8-flash. Config'deki model_chain korunur; varsayilan sadece yeni kurulumlar icin.
- Free tier: 20 RPM, grounding kota 0, Pro limit 0
- Eski modeller (2.5): yeni kullanicilara kapali
- Key format: AQ. prefix gecerli
- Response parse: steps[].content[].text (type=="model_output")
- 429'da per-model cooldown + zincir fallback (v0.2.0'da degistirildi — rate limit havuzlari model basina)
- Function calling (v0.3.0+): tools[] + requires_action + function_result; sonuc ayni modele previous_interaction_id ile doner
- Faz 2: model_tier=="paid" → Pro model + grounding tools aktif
- 500/503'te de zincir fallback
