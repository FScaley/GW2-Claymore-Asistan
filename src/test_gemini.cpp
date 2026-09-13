#include "core/HttpClient.h"
#include "core/GeminiClient.h"
#include "core/ConfigManager.h"
#include <cstdio>
#include <cassert>

static const char* SYSTEM_PROMPT =
    "Sen GW2-Claymore Asistan'sin. Guild Wars 2 hakkinda uzmansin.\n"
    "- Ingilizce ara ve dusun, Turkce cevap ver.\n"
    "- Kisa ve oz cevaplar ver.";

int main() {
    ConfigManager config;
    if (!config.Load("E:\\Guild Wars 2\\addons\\claymore-asistan\\config.json")) {
        printf("FAIL: config yuklenemedi\n");
        return 1;
    }
    printf("Config OK: model=%s\n", config.GetModel().c_str());

    GeminiClient gemini;
    gemini.SetApiKey(config.GetApiKey());
    gemini.SetModel(config.GetModel());
    gemini.SetModelFallback(config.GetModelFallback());

    printf("\n=== TEST 1: Tek soru (grounding yok) ===\n");
    auto r1 = gemini.Ask("Dusk nedir? Tek cumle.", SYSTEM_PROMPT);
    printf("ok=%d status=%d\n", r1.ok, r1.statusCode);
    printf("text: %s\n", r1.text.c_str());
    printf("error: %s\n", r1.error.c_str());
    printf("interactionId: %s\n", r1.interactionId.c_str());

    if (!r1.ok) {
        printf("\nFAIL: ilk istek basarisiz\n");
        return 1;
    }

    printf("\n=== TEST 2: Multi-turn (previous_interaction_id) ===\n");
    auto r2 = gemini.Ask("Peki nereden elde edilir?", SYSTEM_PROMPT, r1.interactionId);
    printf("ok=%d status=%d\n", r2.ok, r2.statusCode);
    printf("text: %s\n", r2.text.c_str());
    printf("error: %s\n", r2.error.c_str());

    if (!r2.ok) {
        printf("\nFAIL: multi-turn basarisiz\n");
        return 1;
    }

    printf("\n=== TUMU GECTI ===\n");
    return 0;
}
