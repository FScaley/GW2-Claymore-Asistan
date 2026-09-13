# Faz 0 — Smoke Test Raporu (13 Eylul 2026)

## Sonuclar

| Test | Durum | Detay |
|------|-------|-------|
| Model listesi | OK | 30+ model. En guncel: gemini-3.8-flash, gemini-3.1-pro-preview |
| Interactions API | OK | Interaction ID alindi, status: completed |
| Turkce cevap | OK | GW2 sorusuna dogru Turkce cevap geldi |
| Key tier tespiti | FREE | Pro modeller limit: 0 (kullanilamaz) |
| Eski modeller (2.5) | KAPALI | "no longer available to new users" (404) |
| Google Search grounding | **CALISMAZ** | Free tier'da kota 0 — tools yok iken 200, tools var iken 429. Sadece ucretli key ile aktif. |
| C++ HttpClient::Post | OK | test_gemini.exe ile dogrulanmis — tek soru + multi-turn calisiyor |

## Tespit Edilen Gercek Degerler

| Parametre | Plan'daki | Gercek |
|-----------|-----------|--------|
| Model | gemini-3.8-flash | gemini-3.8-flash (dogru) |
| Endpoint | Interactions API | /v1beta/interactions (dogru) |
| Rate limit (free Flash) | 15 RPM | **20 RPM** |
| Pro erisim (free key) | Bilinmiyor | **limit: 0 — kullanilamaz** |
| Eski modeller (2.5) | Mevcut | **Yeni kullanicilara KAPALI** |
| Fallback model | gemini-3.6-flash | gemini-3.5-flash (test edildi) |

## Response Yapisi (C++ implementasyon icin kritik)

```json
{
  "id": "v1_...",           // -> previous_interaction_id olarak sakla
  "status": "completed",
  "model": "gemini-3.8-flash",
  "service_tier": "standard",
  "usage": {
    "total_tokens": 466,
    "total_input_tokens": 36,
    "total_output_tokens": 57,
    "total_thought_tokens": 373
  },
  "steps": [
    {"type": "thought", "signature": "..."},  // gosterme
    {
      "type": "model_output",
      "content": [
        {"text": "Cevap metni buraya", "type": "text"}
      ]
    }
  ]
}
```

**Parse kurali:** `steps[]` icinde `type == "model_output"` olan step'in `content[].text` alanini birlestir.

## Ornek Basarili Cevap

Soru: "GW2'de Dusk nedir ve nereden elde edilir?"
Cevap: "**Dusk**, efsanevi buyuk kilic Twilight'in oncul (precursor) silahindir; Mystic Forge'dan, rastgele ganimet olarak, koleksiyonunu tamamlayip ureterek ya da Trading Post'tan satin alinarak elde edilir."

## Cift Model Stratejisi (Guncellenmis)

- **Free key:** gemini-3.8-flash (fallback: gemini-3.5-flash)
- **Paid key:** gemini-3.1-pro-preview (fallback: gemini-3.8-flash)
- **Tier tespiti:** Pro model ile deneme cagrisi; 429 + "limit: 0" ise free tier
- **Manuel override:** config.json'da model_tier alani (auto/free/paid)

## Kalan Testler (Faz 2 sirasinda)

- [x] Google Search grounding — FREE TIER'DA CALISMAZ (kota 0, dogrulanmis)
- [ ] Function calling (Faz 2)
- [x] Multi-turn (previous_interaction_id) — test_gemini.exe ile dogrulanmis
- [ ] generateContent fallback endpoint
- [ ] WikiClient (kendi HTTP istekleri ile wiki erisimi — grounding yerine)
