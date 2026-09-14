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
- Rate limit: 20 RPM (free Flash) — Faz 0 tespiti; 13 Eylul: `limit: 20` GUNLUK cikti (v0.3.10 notuna bak)
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
- Model zinciri: gemini-3.5-flash-lite (30 RPM) → gemini-3.5-flash (15 RPM) → gemini-3.8-flash (15 RPM) — o gunku varsayimlar; limitler RPM degil (v0.3.10 notu). Bu Lite-once zincir v0.3.0 kurulumlarinin config.json'unda kalici
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

### Faz 2: Akilli Veri Entegrasyonu — TAMAMLANDI (13 Eylul 2026, v0.3.8; v0.3.9–v0.3.18 bugfix/prompt/kaynak)

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
- Model zinciri: Flash birincil (20 RPM), Lite yedek (30 RPM). Lite halusinasyon orani kabul edilemez. (20/30 RPM sanildi; 13 Eylul: Flash `limit: 20` GUNLUK — free key'de Flash gunde ~8-10 soru, gerisi Lite)
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
- test_gemini TEST 6: Worker ile "Roller Beetle mount nasil acilir? Hangi koleksiyonlar ve hangi itemler lazim?" — gozlem: TEK gw2_wiki cagrisi, Gemini (Flash suite'in kendisi yuzunden cooldown'da oldugu icin Lite'ta) 3 koleksiyonu ve 27 item adini aynen listeledi. test_gemini build satirina core/WikiText.cpp eklendi. ~9 Gemini cagrisi (Flash'in gunluk 20 kotasinin yaklasik yarisi — ayni gun ikinci kosu Lite'ta; beklenen, zinciri test eder)
- Kalan: bazi hint'ler Lite'ta hafizadan suslendi (wiki hint "Dabiji Hollows" derken model "Pogahn Bluffs" yazdi) — model davranisi, veri eksigi degil; prompt kurali ile azaltildi, Flash'ta daha iyi bekleniyor. Icerik butunlugu (item adlari/sayilari) deterministik
- Bilinen limitler: alt koleksiyon fetch'leri seri, ~50KB HTML, cache yok (ayni mount iki kez = 4-5 tam fetch; yalnizca FC logunda tekrarlanan ayni gw2_wiki sorgusu gorulurse sayfa cache eklenir). ExtractTableRows mech1 tablosu icinde ic ice `<table>` desteklemiyor (gorulmedi). ExtractSubCollectionNames yalnizca iki sablonu taniyor — baska sablonla baglanan alt koleksiyonlar icin Gemini gw2_wiki'yi adiyla cagirmali. location→waypoints hala `| location = [[Map]]` formati gerektiriyor

**v0.3.8 (13 Eylul 2026) — Markdown-lite renderer:**
- Problem: v0.3.7 cevaplari uzun yapili listeler (`### 1. Beetle Juice...`, `- Inquest Beetle Notes`, `` `[&BLoDAAA=]` ``) — eski renderer yalnizca `**bold**` + `[&kod]` biliyordu, `###`, `-`, backtick ham karakter olarak gorunuyordu
- imgui_markdown degerlendirildi ve REDDEDILDI: link modeli `[text](url)`, basliklar icin ayri ImFont*, chatlink kopyalama davranisi callback icinde yeniden yazilmali. Mevcut ozel renderer genisletildi
- Yeni modul `src/chat/Markdown.h/.cpp` (namespace Markdown, ImGui bagimsiz → offline test edilebilir): `Parse(text)` satir-bazli siniflandirma (Blank/Plain/Header/Bullet/Numbered/Rule, level, indent = bosluk/2, marker), `TokenizeInline` satir ici kelime-akis token'lari (Bold, InlineCode, ChatLink, Plain). `**` aramasi satir icinde kalir (sarkan `**` mesajin kalanini yutmaz — dusurulur, tek basinaysa takip eden bosluk da). Backtick icindeki `[&...]` → ChatLink (backtick'ler atilir); baska icerik → InlineCode. `IsChatLink` dogrulamasi (`[&` + base64 + `]`)
- ChatWindow: `RenderFormattedText` = Parse + satir yerlesimi. Blank → tek Spacing (NewLine degil — Gemini cok bosluk birakir); Rule → altin Separator; Header → Spacing + altin renk + Spacing (ikinci font YOK — Nexus atlas riski, istenirse ayri release); Bullet/Numbered → soluk isaret (`-` / `1.`), `Indent(isaretGenisligi)` + `SameLine` ile sarilan satirlar metnin altina hizalanir; ic ice indent 4-bosluk-genisligi/seviye. `RenderTokens` ortak kelime-akis (SameLine(0,0), tasma → NewLine; ChatLink = Selectable + PushID + tiklayinca kopyala)
- System prompt FORMAT satiri: `**bold**`, `###`, `-`, `` `kod` `` kullan; markdown tablo/resim/link ASLA — tablolar boru corbasi olarak gorunurdu
- HandleMap: ResolveMapId sonrasi iptal kontrolu eklendi (v0.3.7'de parametre alinip kullanilmiyordu)
- Yeni `src/test_markdown.cpp` → test_markdown.exe (sifir maliyet, ag yok): gercek TEST 5/TEST 6 Gemini cevaplari + edge-case blogu; 3 level-3 baslik + 27 bullet, `* **bold**` → Bullet+Bold, backtick'li chatlink → tek ChatLink ve sifir backtick, numarali/ic ice/rule/sarkan isaret kontrolleri — 23/23 gecti. Build: `cl /EHsc /std:c++17 /MT /utf-8 test_markdown.cpp chat/Markdown.cpp`. Markdown.cpp/RenderFormattedText degisikliklerinin regresyon kapisi
- Dogrulama durumu: parser offline dogrulandi; yerlesim (indent/sarma/bosluk) oyun ici bakis gerektirir — ImGui disarida render edilemez
- Bilinen limitler: tablo/resim/link/italik yok; baslik yalnizca renk+bosluk; ic ice indent kaynaktan genis (kozmetik); satir basi `N. ` duz metin numarali sayilir; backtick icinde `**` kod span'ini bozar (Gemini uretmez)

**v0.3.9 (13 Eylul 2026) — NPC konum → waypoint: alan→harita cozumu + API floor bugu:**
- Rapor: "kourna da gorrik nerede" → Lite "Oyun ici [&...] kodlari dogrudan uretilemediginden haritanizdan aratin" dedi. Kessex Hills eskiden calisiyordu; Domain of Kourna HIC calismamisti — uc bagimsiz sebep:
  1. **API floor bugu (gorunmezdi):** `/v2/maps/1288` `default_floor=1` diyor ama floor 1/3/2/0 → 404; POI'ler yalnizca floor 49'da (Apizmic Grounds WP, Allied Encampment WP). v0.3.8 `/floors/1/` sabit kodluydu → Kourna icin kusursuz bir `gw2_map` cagrisi bile 0 waypoint donduruyordu. Fix: `GetMap` `default_floor`+`floors[]` okur; `GetMapWithWaypoints` default'tan baslayip her floor'u dener, waypoint veren ilk floor kazanir (`floorUsed`). Thunderhead Peaks 4 WP, Kessex Hills 16 WP — her floor'da ayni (dogrulandi)
  2. **NPC infobox formati:** `| location = Thaumanova Reactor Fractal; Allied Encampment; Sun's Refuge; ...` — duz metin ALAN adlari, noktali virgullu, asla `[[Harita]]` linki. v0.3.7'nin `[[...]]` tetikleyicisi NPC sayfalarinda hic ateslenemezdi. Alan sayfasi (`{{Location infobox | type = area | within = Domain of Kourna | sector id = 1647}}`) haritayi `within` ile verir
  3. **Prompt kacisi:** v0.3.4 WAYPOINT RULE "kod yoksa mevcut degil de" — Lite bunu gw2_map cagirmamak icin kullandi
- `ResolveMapId(name, depth)` birlesik cozucu: arama → redirect → Location infobox (lead) → `| id` **yalnizca lead'de** (eski tum-sayfa aramasi alan sayfasindaki vendor tablosundan ITEM id'sini harita id'si sanabilirdi) → yoksa `| within` takibi (derinlik ≤2). Her adim `map:` cache. `gw2_map("Allied Encampment")` de calisir
- HandleWiki: `location_areas` (cap 4; `<br>`/sablon temizlenir), `locations[]` (harita basina 1 giris, cap 3: map_name, map_id, areas, npc_here, waypoints[]; waypoint'siz haritalar (instance/fractal) atilir), `npc_here` = NPC `| coordinates` haritanin `continent_rect` icinde → o girisin waypoint'leri en yakin ilk (`nearest: true`; sayisal mesafe YOK — Lite birim uydurur), npc_here girisler once; `map_name`/`nearby_waypoints` = `locations[0]` aynasi; `other_locations` = cozulemeyen/cap disi alanlar
- Gorrik sonucu: Thunderhead Peaks (npc_here, Observation Deck WP en yakin) + Fractals of the Mists + Domain of Kourna (2 WP) — 14.2KB. Wiki koordinati Thunderhead'i gosteriyor (ep5 Pact Command); Kourna sorusu icin Gemini `locations[]`'tan secer
- WAYPOINT RULE yeniden yazildi: kod uretme; kod ALMAK icin gw2_map cagir; NPC gw2_wiki sonuclarindaki `locations[]`'tan kullanicinin sordugu haritayi sec; "kod uretilemez / haritadan arayin" ASLA deme
- FC log satirina tool suresi (ms) eklendi — NPC aramalari 10-30 GET, olcum lazim
- test_wiki F (Gorrik: Kourna girisi + Allied Encampment WP + npc_here=Thunderhead) ve G (Toxic Spider Queen: noktali virgullu alan listesi → Kessex Hills ≥10 WP) — once KIRMIZI gozlendi (map_name yok / Thunderhead 4 WP), fix sonrasi YESIL. Tani icin her alanin ayri `gw2_map` cozumu yazdirildi: Kourna 1288 dogru cozulmus ama 0 WP → API probe → floor 49
- test_gemini TEST 7: Worker ile "kourna da gorrik nerede, en yakin waypoint?" — Lite `gw2_wiki` + ek `gw2_map("Domain of Kourna")`, cevapta gercek kod, strip yok, red yok. TEST 5'e `checks:` satiri eklendi. ~12 Gemini cagrisi
- Bilinen limitler: NPC gw2_wiki en pahali tool (10-30 GET, 10-30 s worst case; waypoint fetch cache'siz), instance haritalarda 5 bos floor probe, `npc_here` tek koordinata dayanir (cok gorunumlu NPC'de editorun sectigi), test F canli wiki durumuna bagli, Lite gw2_map'i gereksiz tekrar cagirabiliyor, `{{Stub}}` uyarisi content basina siziyor (zararsiz), `items_index.json` diskte hic olusmamis (Save yalnizca AddonUnload'da — oyun cikisinda calismiyor; cache oturum bazli, dogruluk sorunu degil; istenirse Add sonrasi/periyodik Save)

**v0.3.10 (13 Eylul 2026) — Wiki aramasi iki katmanli: baslik oneki + tam metin fallback:**
- Kaynak: kullanicinin arkadasinin Nexus logu (addon GitHub release'lerinden otomatik guncelleniyor — 0.3.0→0.3.9 ayni gun). "Janthir Syntri Renown Tokens" → `No wiki results`, ardindan 5 tur daha kelime ekleme (renown hearts, map currency, vendor…) hepsi bos → 6 tur limiti → "bilgi bulunamadi". Ayni desen 3 soruda; v0.3.6 logunda da `Leviathan farm End of Dragons`, `Gorrik Kourna location`, `Vanguard title Guild Wars 2`
- Kok neden (API probe ile dogrulandi): `action=opensearch` yalnizca **baslik oneki** eslestirir — sayfa "Janthir Syntri Renown Token" (tekil), sorgu cogul → bos. `list=search` (tam metin) ayni sorguda ilk sonuc dogru sayfa. `srwhat=title`/`nearmatch` cogul/ek kelimeli sorgularda da bos — tek ise yarayan `text`
- `WikiSearch`: katman 1 opensearch; **yalnizca 0 sonucta** katman 2 `list=search` (5 sonuc, ns 0) → sorgu kelimeleriyle ortusme skoru (kucuk harf alfanumerik ≥3 karakter, `gw2/guild/wars/wiki` dolgu atilir, onek toleransli `tokens`↔`token`; skor = ortak kelime + baslik kapsama orani → "Renown Heart" beraberlikte "Shard of Janthir Syntri"i gecer), ortusmeyen basliklar atilir (tahmin ≠ eslesme → durust bulunamadi), en fazla 3, `fulltext=true`. HTTP hatasinda katman 2'ye girilmez
- HandleWiki: tam metinden gelen sayfada `search_mode:"fulltext"` + `search_note`; bulunamadiginda `{error, hint}` — "arama baslik tabanli, tam tekil sayfa adiyla veya tek isimle tekrar dene". Tool `query` aciklamasi: tam tekil Ingilizce baslik, dolgu kelime yasak (Guild Wars 2, GW2, location, guide, farm)
- COLLECTION RULE: "ipucu konumlari icin gw2_map cagirma" — bu surumun testinde Lite bir kosuda Roller Beetle ipuclarindaki 5 harita icin sirayla gw2_map cagirip 6 turu doldurdu (once gorulmemisti); cumle sonrasi tekrar tek gw2_wiki
- GeminiClient: 400 govdesinde "retry" ("Model generated invalid JSON syntax … Please retry the request", tool'suz basit soruda gozlendi) → 500/503 gibi zincir fallback
- Testler: test_wiki H once KIRMIZI (hits=0, 62B hata — logdaki `(62B)` ile ayni), fix sonrasi A–H yesil: "Janthir Syntri renown tokens" → Janthir Syntri Renown Token, item_id 102881, search_mode=fulltext; F'e "tam baslik yolunda search_mode yok" negatif kontrolu. Bilgi amacli: "Gorrik Kourna location"→Gorrik, "Dragonite Ore converter"→Bag of Dragonite Ore, "Leviathan farm End of Dragons"→yok. Test tarafi hatasi: `item_id` sayi, `value("item_id","-")` exception atti → `dump()`; stdout tamponsuz yapildi (crash satirlari yutmasin). test_gemini TEST 8 (Worker): Lite tam tekil basligi ilk cagrida kullandi, `writ=1 token=1 notfound=0`; TEST 5/6/7 yesil (3. kosu; 2. kosuda TEST 6 dustu → prompt cumlesi). ~14 Gemini cagrisi, ~6'si Flash denemesi
- Bilinen limitler: katman 2 anlam degil kelime ortusmesiyle secer ("Dragonite Ore converter" → Bag of Dragonite Ore, donusturucular `related`'da); ortusme yoksa dogru sayfa ("Leviathan") icin Gemini'nin tek isimle tekrar denemesi gerekir; ResolveItemId/ResolveMapId de katman 2'yi kullanir (cozulemeyen konumda +1 arama +≤3 sayfa); Quick Access ikon texture'i hic yuklenmiyor (`QA_CLAYMORE … 10 failed attempts`, kozmetik); Lite varyansi gercek — ayni prompt 2 kosuda tek cagri, 1 kosuda 6 tur; **Flash free `limit: 20` = GUNLUK kota** (13 Eylul 20:57–20:59 probe: son testten 7+ dk sonra bos pencerede 3 Flash istegi 429, retry ipucu 24→37→52 s buyudu, 21:04'te 4. yoklama hala 429, Lite 200; 3.8-flash 20:57'de 429 ama 21:03'te 200 → onunki gecici, gunluk degil; govdede quotaId yok → cikarim, sabah ilk soruda model etiketi Flash ise kesinlesir) → gunde ~20 Flash cagrisi (~8–10 soru), gerisi Lite; test_gemini gunluk Flash kotasini yiyor → aksam test sonuclari Lite sonucudur; kota bitince her soru 1 bosa Flash 429 + ~1 s; arkadasin KENDI key'i var (dogrulandi) ama v0.3.0'da kurdugu icin config'inde Lite-once eski zincir kalici (Options UI yok) → hic Flash kullanmadi, logunda 429 yok; gecici-400 fallback'i yalnizca `Ask()`'te, `SendFunctionResults` (pinned) hala "Istek hatasi" gosterir; `SearchWords` 3 harfli stopword'leri ("the","and") atmiyor — kapsama terimi gercek kelimeleri baskin kilar, logda gorulurse stopword listesi

**v0.3.11 (13 Eylul 2026) — Limit karari: ustel 429 cooldown + testler Flash kotasini yemesin:**
- Kullanici karari: "limite takilmayacak en iyi yol ne ise o". Durust cevap: limiti *kaldiran* tek yol key'in Google Cloud projesinde faturalandirma (Tier 1) — key/config degismez, kota otomatik yukselir, etiket beyaza doner; soru basina 2–3 cagri ~10–20K token, Flash sinifinda cent alti–1 cent (guncel fiyat ai.google.dev/pricing, butce uyarisi kur). Anahtar cogaltma/proje rotasyonu ToS disi — onerilmedi. Kod tarafi limitin bedelini keser: kota bitince her soruda bosa Flash denemesi + ~1 s yerine ~4 deneme/gun, sonra 30 dk'da 1
- GeminiClient: `CooldownEntry.streak`; `EscalatedCooldown(retry, k)` = max(retry, 30 s) × 4^(k−1), cap 1800 s (30 s → 2 dk → 8 dk → 30 dk); streak yalnizca GERCEK 429 denemesinde artar (cooldown'da atlanan istekler artirmaz → dakikalik/gunluk ayrimi onemsiz), o modelden herhangi bir basarida sifirlanir; Ask + pinned SendFunctionResults iki 429 noktasi. Log: `Cooldown: model = Ns (k. ardisik 429[ - gunluk kota olabilir])`; `ModelCooldown.streak` disari acildi
- test_gemini: varsayilan **Lite-once** zincir (`config.SetModelChain`), `--flash` ile config zinciri — bir kosu ~6 Flash denemesi = gunluk kotanin 1/3'u idi, bugun bes kosu kotayi yedi. TEST -1: 11 vakalik saf matematik (22 s → 30/120/480/1800; 52 s → 52/208/832/1800; 0 → 30; 5,k=2 → 120). TEST 9 (gozlemsel): Flash-once istemci, 31 s arayla 2 soru → gozlem: `30s (1. ardisik 429)` → `140s (2. ardisik 429)` (retry ipucu 35 s × 4), Lite cevapladi; Flash saglikliyken streak 0 kalir
- Sonuclar: test_wiki A–H yesil (degismedi); test_gemini Lite zincirinde TEST 5/6/7/8 `fallback=0` yesil; Flash saglikli oldugunda `--flash` ile tekrar kosulmali (sabah)
- Bilinen limitler: free key'de turuncu etiket artik NORMAL durum (gunun ~20 Flash cagrisindan sonra) — "neden turuncu" tooltip'i follow-up; streak process icinde, oyun yeniden baslatilinca unutulur (~4 bosa deneme tekrar); 3.8-flash olculmemis aday birincil (limit + kalite kaydi yok)

**v0.3.12 (13 Eylul 2026) — "Baglanti hatasi" nedenini soyluyor + API anahtari temizligi:**
- Saha raporu: ikinci bir arkadas her soruda "baglanti basarisiz" aliyor, Nexus logunda yalnizca "Claymore loaded" var (cogu kiside sorun yok). Kod okuma: `HttpClient::Post` nullopt → GeminiClient "Baglanti hatasi" (statusCode 0, log yok) → `Ask` ilk modelde doner → Worker sadece sohbete yazar. Bos log bu yolun imzasi; WinHTTP hata kodu hic okunmuyordu
- Ayirici deney (scratchpad `probe_header.exe`, sahte anahtarlar, config okunmadi): bos anahtar → 403 (loglanir); satir sonu/CRLF/bosluk/tirnak → 400 "API key not valid" (loglanir); **tek ASCII disi bayt (UTF-8 c-cedilla) → `WinHttpSendRequest` hata 87, 0 ms, cevap yok** = rapor edilen sessiz belirti birebir. Ikinci sinif: DNS/proxy/TLS/guvenlik duvari (12xxx) — yalnizca kodla ayirt edilir
- HttpClient: Get/Post ortak `Request()`; `HttpFailure{stage, code, elapsedMs}` (GetLastError `CloseHandle`'dan ONCE), `LastFailureText()`, `DescribeError()` (FormatMessage winhttp.dll, Ingilizce→sistem dili, UTF-8), `TurkishHint()` (87/12002/12007/12029/12030/sertifika-TLS/12180), `Diagnostics()` (RtlGetVersion + WinHTTP varsayilan proxy + kullanici IE proxy; ag yok). `WinHttpOpen` hatasi kurucuda kaydedilir
- GeminiClient::DoRequest: `[HTTP] model: WinHttpSendRequest: 12007 - The server name ... (50 ms)` logu; mesaj `Baglanti hatasi (kod N): <Turkce ipucu>`; statusCode 0'da zincir denemesi YOK (ag hatasi modelden bagimsiz, bekleme 3x olurdu). GW2Client: tum GET'ler `Fetch()` → cevap yoksa `[HTTP] host/path -> ...` (`SetLogger`; entry ayni logger lambda'sini verir)
- ConfigManager: `SanitizeApiKey` (uclardaki bosluk/NBSP/dar NBSP/ZWSP/ZWNJ/ZWJ/BOM/tirnak; Load + SetApiKey), `ApiKeyProblem` (0x21–0x7E disi ilk karakterin konumu ve turu; anahtar asla yazilmaz). Worker: `m_keyProblem` `Start()`'ta (thread dogmadan — Kaydet config'i `Stop()`'tan once yazar, DoChat config okumaz; ilk taslaktaki `m_config->GetApiKey()` okumasi veri yarisiydi, advisor oncesi kendi incelemede yakalandi), DoChat HTTP gondermeden System mesaji + `API anahtari sorunu:` logu. entry: yuklemede `Ortam:` + `API anahtari: N karakter, gecerli|SORUN` satirlari; Options anahtar sorununu kirmizi gosterir; Kaydet temizlenmis anahtari girdiye geri yazar
- Testler: test_wiki I once KIRMIZI (34 derleme hatasi — API yok), sonra A–I yesil: nonexistent.invalid → 12007 (50 ms), ASCII disi baslik → 87 (0 ms), /v2/build 200 hatayi siler, Diagnostics "Windows 10.0.26200 | WinHTTP proxy: dogrudan | kullanici proxy: otomatik algila=0 ...", sanitize/problem 11 kontrol. test_gemini TEST -1b (bos anahtar → "girilmemis", bozuk anahtar → "5. karakteri", `[HTTP]`/`[4xx]` satiri yok) + -1/0–9 Lite zincirinde yesil (2 kosu; TEST 9: Flash 429 54 s/41 s — aksam kotasi). test_wiki derleme satirina `core/ConfigManager.cpp` eklendi
- Surec notu: ilk commit denemesinde PowerShell here-string'i `git commit -F -` yerine argumana gitti → commit olmadi ama tag+release eski commit'e (7d30f76) atildi; release+tag silindi (`gh release delete --cleanup-tag`), mesaj dosyadan commit (1794dd8), tag/push/release yenilendi; tag deref yerel+uzak dogrulandi. Kural: commit mesajini dosyaya yaz, `-F dosya`; tag atmadan once `git log -1` kontrol et
- Bilinen limitler: HttpClient `WINHTTP_ACCESS_TYPE_DEFAULT_PROXY` — kullanici (tarayici) proxy'sini yok sayar; `AUTOMATIC_PROXY`'ye gecis kanit olmadan yapilmadi (herkese WPAD gecikmesi) → yalnizca log 12029/12002 + `kullanici proxy` dolu gosterirse v0.3.13; `Diagnostics()` proxy dizesini aynen yazar (`user:pass@host` olsa loga girer — nadir, `@` oncesi maskeleme v0.3.13); 87 ipucu anahtari suclar (tek degisken baslik); sanitize yalnizca uclari temizler, icerdeki karakter rapor edilir. Rapor KAPANMADI: arkadas guncelleyip 1 soru sorup uc satiri (`Ortam:`, `API anahtari:`, `[HTTP]`) gonderecek; kod 87 ise anahtari silip yeniden yapistirmasi yeter

**v0.3.13 (13 Eylul 2026) — Bozuk IPv6 yolu: WinHTTP hizli IPv4 geri donusu + baglanma zaman asimi 10 s:**
- v0.3.12 tanilamasi ilk saha sonucunu verdi: ayni kullanicida mesaj "kod 12002: zaman asimi". Ayirici test (Windows'un kendi `curl.exe`'si): `-4` → 404 aninda, `-6` → sessizlik → makinede IPv6 adresi var, yol olu (SYN kara delik). DNS kontrolu (13 Eylul): generativelanguage.googleapis.com 8 A + 8 AAAA; github.com / objects.githubusercontent.com / api.guildwars2.com / wiki.guildwars2.com yalnizca A → oyun, Nexus guncellemesi ve tarayici (Happy Eyeballs) calisirken yalnizca Gemini cagrisi IPv6'yi 45 s bekleyip 12002 aliyordu. "Herkeste calisiyor, onda calismiyor" tablosunun tam aciklamasi
- HttpClient: oturumda `WINHTTP_OPTION_IPV6_FAST_FALLBACK` (SDK basliginda 140, Win 8.1+; IPv6 300 ms'de baglanmazsa IPv4 paralel), `IPv6FastFallback()`; `TimeoutsFor(budget)` = resolve/connect min(budget, 10 s), send/receive budget (POST baglanma 45→10 s; GET 10 s ve wiki HTML 25 s alma degismedi); `Diagnostics()` sonuna "| IPv6 hizli geri donus: acik|desteklenmiyor (kod)"; `TurkishHint(12002)` bozuk IPv6'yi sayar; entry v0.3.13
- Derleme dersi: `std::min` Windows.h `min` makrosuna carpti — MSBuild (`ConformanceMode` = /permissive-) hata verdi, test derlemesi (/permissive- yok) gecti ve yesil kosmustu. Ucl u operatorle cozuldu; test derleme satirlarina `/permissive-` eklendi (CLAUDE.md). vcxproj'a NOMINMAX ayri karar
- Testler: test_wiki J once KIRMIZI (TimeoutsFor/IPv6FastFallback yok), sonra A–J yesil (Diagnostics: "... | IPv6 hizli geri donus: acik"); test_gemini -1/-1b/0–9 Lite yesil (TEST 9 canli: 30 s → 192 s). Durust sinir: gelistirme makinesinde IPv6 yok (`curl -6` "Could not resolve host") → duzeltmenin etkisi burada gozlemlenemez; dogrulama etkilenen kullanicinin guncelleme sonrasi ilk sorusu + logda "acik"
- Kalan belirsizlik: kullanicinin `curl -6` satirinin tam metni ("(28) timed out" = olu yol, duzeltme kapsar; "Could not resolve host" = IPv6 hic yok, sebep baska) ve `[HTTP]` satiri (asama + ms) aynen alinmadi — istendi. Hala 12002 ise process-ici supheliler: oyun trafigini tunelleyen ping dusurucu (ExitLag/WTFast/NoPing — curl oyun process'i disinda calisir, onu gormez), guvenlik duvarinda Gw2-64.exe kurali
- Kullanici tarafi kalici cozum: adaptorde IPv6'yi kapatmak (geri alinabilir) veya router IPv6 ayari — ayni olu yol IPv6 kullanan diger programlari da yavaslatir
- SAHA RAPORU KAPANDI (13 Eylul 2026): kullanici IPv6'yi adaptorden kapatti, "ipv6 kapatinca cozuldu" — WinHTTP fast fallback o makinede ise yaramadi (12002, 19245 ms)

**v0.3.14 (14 Eylul 2026) — COMPLETENESS + CONTRADICTION prompt kurallari:**
- Saha bulgusu: Gharr Leadclaw sohbetinde model 7+ konumdan sadece birini (Wizard's Tower) soyledi, mastery kosulunu belirtmedi; kullanici "burada yok" deyince wiki yerine uydurma aciklama yapti ("meta durumuna gore gorunmeyebiliyor"). Nexus log analizi (FC satirlari): gw2_wiki cagirilmis, Dragon's Stand da dahil dogru veri donmus — sorun modelin sunumunda, veride degil
- Wiki veri probe (probe_gharr.cpp, 0 Gemini): gw2_wiki("Gharr Leadclaw") → locations[] yalnizca 3/7 harita (infobox | location alani eksik); ama content'teki Locations bolumu 7 haritanin tamamini kosullariyla iceriyor. gw2_wiki("Gharr Leadclaw/Dragon's Stand Exchanges") → gercek exchange verisi (achievement gerektirmeyen Bulk Exchanges)
- System prompt COMPLETENESS RULE: wiki sonucu birden fazla secenek listeliyorsa HEPSINI ve her birinin kosulunu (mastery/achievement/story) wiki'nin soyledigi sekilde sun; kosullu secenegi kosulsuz gosterme; yapisal alanlar eksik olabilir, content'teki bolum metni asil kaynak
- System prompt CONTRADICTION RULE: kullanici "yok/yanlis" derse gw2_wiki'yi tam isimle tekrar cagir, sabit sablonla cevapla ("Wiki'ye gore <NPC> <harita>, <bolge> bolgesinde… Wiki bu konum icin sart listelemiyor / su sarti listeliyor: …"); wiki'nin soylemedigi aciklama ekleme (meta event, guncelleme, asama)
- "Keep answers short" → "no filler, but never drop a location, condition or requirement the wiki lists"
- gw2_wiki tool description'a: "locations is derived from the infobox and may be incomplete; the full list with conditions is in the Locations section in content"
- Testler: TEST 10 (Gharr 2-turn Lite, checks: turn1 locations=6 wayfinder=1 dragons_stand=1; turn2 fc_wiki pact_base halluc=0); TEST 11 (Auric Dust 1-turn Lite, checks: sources=4 vendor chest story gather). A–J + -1/-1b/0–11 yesil. Durustluk: turn 2 sablonu 4 kosunun 2'sinde takip edildi, Lite %100 guvenilir degil; fc_wiki_main tum kosuslarda 0 (model ana sayfayi tekrar cagirmiyor, turn 1 baglamini kullaniyor)
- Bilinen: (1) COMPLETENESS vs COLLECTION catismasi — Lite collection itemlerine hafizadan detay ekliyor (Milin, 50 Inscribed Shard; hint yalnizca "heket" diyor); v0.3.14 oncesi de vardi. (2) verifiedLinks DoChat basina sifirlanir — turn 2 turn 1'in gecerli kodlarini soyar ([&BC4EAAA=] → [kod dogrulanamadi]); onceden var, UX hatasi

**v0.3.15 (14 Eylul 2026) — Yeni kaynak: guildjen.com topluluk rehberleri:**
- Yeni tool `gw2_guide(query, section?)` — guildjen.com'dan WordPress REST API ile topluluk rehberleri ceker. How-to, farming, leveling, gearing, mode introduction sorulari icin. Koleksiyon/achievement item listesi DEGIL (gw2_wiki'nin isi — tool description siniri koyar)
- Kesfedilme: `/wp-json/wp/v2/search` (hem post hem page kapasar). Icerik: `/wp-json/wp/v2/posts|pages/{id}?_fields=title,link,modified,content` → `content.rendered` (yalnizca makale HTML'i). HtmlToText ile cevrilir; h1→h2 normalize (WordPress h1 kullanir, SplitLead h2 arar). 12KB metin butcesi, gw2_wiki ile ayni desen. Entity-decoded basliklar (&#8217; → ')
- Host guard: GuideGetContent yalnizca GUIDE_HOST'a baglanir — WordPress JSON'daki `_links.self` rasgele host'a yonlendirmez
- System prompt: WHEN TO USE TOOLS'a `gw2_guide` eklendi; "General advice WITHOUT tools" → yalnizca opinion/comparison. Rehber attributionu: "guildjen rehberine gore (YYYY-MM)". CONTRADICTION RULE wiki facts icin; rehber icin "rehber boyle diyor"
- Feasibility probe: gw2mists.com DUSTU (pure SPA, API 403); snowcrows.com ve metabattle.com calisiyor (SSR) ama ertelendi (kullanici: "buildler cok onemli degil"). guildjen.com Cloudflare (AAAA kaydi var — IPv6 sinifi)
- Testler: test_wiki K (zero Gemini: 8 section, 12KB, sifir sizinti, entity decode, section=, cancel); test_gemini TEST 12 (Lite: balik tutma → fc_guide=1, source_named=1; TEST 6 gw2_guide'a donmedi — sinir tuttu)
- Bilinen: (1) guide chat kodlari verifiedLinks'e girer — guildjen icin guvenilir ama yeni guven yuzeyi. (2) Per-session cache yok (section= iki HTTP cagrisi tekrarlar). (3) Top-hit only, relevance guard yok

**v0.3.16 (14 Eylul 2026) — Konum zenginlestirme + verifiedLinks + guide cache:**
- Locations section → locations[] zenginlestirme: NPC sayfalarin Locations bolumundeki haritalari (infobox'ta olmayanlar) `locations[]`'a ekler. Her ek harita `ResolveMapId` + `GetMapWithWaypoints` ile cozulur, gercek waypoint kodlari eklenir. LOCATION_MAP_CAP=6. Gharr 3→6 (Arborstone, Dragon's Stand, Wizard's Tower eklendi); Gorrik 3→6 (Grothmar Valley, Gyala Delve, Jahai Bluffs). Mythwright Gambit (raid) dogru sekilde cozumsuz kalir ve atlanir
- verifiedLinks multi-turn fix: `m_verifiedLinks` artik Worker member (DoChat basina degil), `ClearHistory`'de sifirlaniyor. Insert `m_snapshotMutex` altinda — Cancel→Temizle sirasinda data race onlendi. TEST 10: turn 2'de sifir Stripped satiri (onceki 5 kosuda 1-2 per turn)
- Guide per-session cache: `m_guideCache` (post id → normalized text). section= cagrisi GuideGetContent'i atlar, yalnizca GuideSearch tekrar calisir (1 HTTP tasarrufu)
- Em-dash literal `\xe2\x80\x94` (ASCII kaynak kurali). Cap sabiti `LOCATION_MAP_CAP = 6`
- Testler: test_wiki L (Gharr ≥5 location + Dragon's Stand Pact Base Camp waypoint); F Gorrik 3→6 regresyon yok; A-L yesil. test_gemini TEST 10 turn 1: 6/6 gercek kodlar, turn 2: 0 stripped; -1/-1b/0-12 yesil

**v0.3.17 (14 Eylul 2026) — NOMINMAX, thread safety, turuncu tooltip, area capture:**
- NOMINMAX vcxproj'a eklendi (Debug + Release). HttpClient::TimeoutsFor'daki ternary workaround `std::min` ile degistirildi. `std::min`/`std::max` artik tum TU'larda guvenle kullanilabilir
- m_interactionId ve m_interactionModel ClearHistory'de m_snapshotMutex altina alindi (v0.3.16 verifiedLinks race fix'inin kardesi)
- Turuncu "(yedek)" model etiketine tooltip: "Birincil model kota asiminda - yedek model kullanildi. Free API key ile normaldir."
- Locations section area capture: section-derived entries artik harita adi yerine gercek alan adini (ikinci `- ` satiri, orn. "Pact Base Camp") aliyor
- Testler: A-L + -1/-1b/0-12 yesil. MSBuild NOMINMAX ile derlendi, `std::min` calisiyor

**v0.3.18 (14 Eylul 2026) — Model chain UI, otomatik proxy, ItemIndex periodic save:**
- Model chain Options UI: Nexus Options panelinde her model icin dropdown secici + yukari/asagi sirala butonlari. Degisiklik aninda config.json'a kaydedilir ve Worker yeniden baslatilir
- AUTOMATIC_PROXY: WinHTTP oturumu artik kullanicinin proxy ayarlarini (PAC, WPAD, IE proxy) otomatik algilar (Win 8.1+). DEFAULT_PROXY'ye fallback eski Windows icin korundu
- ItemIndex periodic save: her 20 yeni item eklendiginde otomatik diske kaydeder. Oyun cikisinda AddonUnload calismazsa bile cache korunur
- Testler: A-L yesil. MSBuild Release derlendi

**v0.3.19 (14 Eylul 2026) — gw2_build tool, metabattle.com entegrasyonu:**
- Ayrintili notlar v0.3.19 commit'inde

**v0.3.20 (14 Eylul 2026) — Faz 2 cilalamasi: ikon, rating-widget, proxy maskeleme, guide relevance, prompt scope:**
- Quick Access ikon duzeltildi: wiki goruntu URL'si yanlisti (`/images/3/37/` → `/images/5/50/`), `Textures_GetOrCreateFromURL` (Nexus cache) ile `QuickAccess_Add`'den once cagriliyor. Tek ikon tanimlayicisi (normal ve hover). Dogrulama yalnizca oyun icinde (log satiri yok + ikon gorunur)
- metabattle rating-widget boilerplate temizlendi: `build-header` HtmlToText skip listesine eklendi. test_wiki M "Our curator" kontrolu eklendi
- Proxy credential maskeleme: `ProxyField()` artik `MaskProxyCredentials` uyguluyor — `user:pass@` iceren proxy dizeleri `****@host` olarak loglanir. Public static `HttpClient::MaskProxyCredentials` testlenebilir (test_wiki I'da 3 kontrol)
- Guide relevance guard: `GuideSearch` artik sonuclari `TitleScore` ile yeniden siralar (herhangi bir sonuc >0 skora sahipse). Skor 0 ise WordPress sirasi korunur (Turkce/paraphrase sorgular icin dusmesin). `SearchWords`+`TitleScore` zaten GW2Client.cpp'de mevcut
- COMPLETENESS RULE scope: "NOT to items inside sub_collections, which follow COLLECTION RULE exactly" eklendi — iki kural arasi oncelik belirginlestirildi
- verifiedLinks guide/build trust surface: kabul edildi — her iki site guvenilir, risk sayfa ele gecirmesi; source bazli ayirma maliyet/fayda oraninda degmez
- FC loop exhaustion: ertelendi — v0.3.2 dersi (tool'suz tekrar sorma = halusinasyon kaynagi) gecerli; API davranisi dogrulanmadan yapilmaz; 6-tur limiti v0.3.10'dan beri nadiren dolur
- Testler: test_wiki A-M yesil (0 failure), test_markdown yesil, MSBuild Release derlendi

**Faz 2 kalan (ertelenmis / kosullu):**
- ~~COLLECTION vs COMPLETENESS catismasi~~ — v0.3.20: COMPLETENESS RULE'a "NOT to items inside sub_collections, which follow COLLECTION RULE exactly" scope cumlesi eklendi. Etki henuz test_gemini ile olculemedi; Lite davranisi %100 guvenilir olmayabilir (prompt kurali, model garantisi degil)
- ~~Guide per-session cache~~ — v0.3.16'da eklendi (search hala tekrar calisir)
- snowcrows.com (PvE raid build) ve metabattle.com (genel build, MediaWiki) entegrasyonu — feasibility dogrulandi (SSR, robots.txt acik), kullanici onceligi dusuk ("buildler cok onemli degil"); gw2mists.com dustu (SPA, API 403)
- verifiedLinks guide/build trust surface: guildjen ve metabattle kodlari otomatik verified — her iki site guvenilir topluluk kaynagi, risk yalnizca sayfa ele gecirmesi; source bazli ayirma maliyet/fayda oraninda degmez. **Karar: kabul edildi (v0.3.20)**
- ~~Guide relevance guard~~ — v0.3.20: `GuideSearch` artik `TitleScore` ile re-rank yapar (herhangi bir sonuc >0 skora sahipse). Skor 0 ise WordPress sirasini korur (Turkce/paraphrase sorgu icin). `SearchWords`+`TitleScore` GW2Client.cpp'de anonymous namespace'te, paylasimli
- Faturalandirma acilirsa: zincir aynen kalir; Pro opsiyonel (yavas/pahali). Acilmazsa: 3.8-flash'i birincil olarak olc (429 govdesi + TEST 5–8), Lite'i fiili birincil kabul edip sertlestirmeye devam
- ~~"Neden turuncu" tooltip~~ — v0.3.17'de eklendi
- Kullanici/sistem proxy destegi (`WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY` veya `WinHttpGetIEProxyConfigForCurrentUser` ile istek basina proxy) — KOSULLU: yalnizca bir `[HTTP]` logu 12029/12002 + `kullanici proxy: proxy=<host>` gosterirse (v0.3.12 notu)
- ~~`Diagnostics()` proxy dizesinde `user:pass@` maskeleme~~ — v0.3.20'de eklendi (`MaskProxyCredentials`, test_wiki I'da 3 kontrol)
- ~~vcxproj'a `NOMINMAX`~~ — v0.3.17'de eklendi; `std::min` workaround'i kaldirildi
- ~~`model_chain` icin Options UI~~ — v0.3.18'de eklendi (dropdown + reorder butonlari)
- ~~Quick Access ikon texture'i (`QA_CLAYMORE`)~~ — v0.3.20: URL duzeltildi (`/images/3/37/` → `/images/5/50/`), `Textures_GetOrCreateFromURL` + `QuickAccess_Add` sirasi duzeltildi
- Tur limiti dolunca "elindeki veriyle simdi cevapla" zarif bitis (function_result yanina text input?) — API davranisi dogrulanmadan yapilmaz; v0.3.2 dersi: tool'suz tekrar sorma YOK
- Wiki icerik cache (`mapId → GW2MapInfo` bellek ici) — yalnizca FC log ms sutunu NPC aramalarinin cok yavas oldugunu gosterirse
- ~~imgui_markdown~~ (v0.3.8'de markdown-lite renderer ile kapatildi — imgui_markdown reddedildi, yukariya bak)
- Baslik icin ikinci (buyuk) font — istenirse ayri release (Nexus Fonts_AddFromFile + atlas rebuild null race)
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
- Model zinciri (v0.3.6+): gemini-3.5-flash (birincil — free key'de gunde ~20 cagri) → gemini-3.5-flash-lite (yedek, fiili is yuku) → gemini-3.8-flash. Config'deki model_chain korunur; varsayilan sadece yeni kurulumlar icin (v0.3.18: Options UI ile degistirilebilir)
- Free tier: Flash `limit: 20` GUNLUK (13 Eylul 2026 kaniti), Lite bugune kadar hic 429 vermedi, grounding kota 0, Pro limit 0. 429 govdesinde quotaId yok; "retry in Xs" ipucu gunluk kotada da kucuk — guvenilmez
- Eski modeller (2.5): yeni kullanicilara kapali
- Key format: AQ. prefix gecerli
- Response parse: steps[].content[].text (type=="model_output")
- 429'da per-model cooldown + zincir fallback (v0.2.0'da degistirildi — rate limit havuzlari model basina)
- Function calling (v0.3.0+): tools[] + requires_action + function_result; sonuc ayni modele previous_interaction_id ile doner
- Faz 2: model_tier=="paid" → Pro model + grounding tools aktif
- 500/503'te de zincir fallback
