#include "core/HttpClient.h"
#include "core/GeminiClient.h"
#include "core/ConfigManager.h"
#include <cstdio>

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

    auto& chain = config.GetModelChain();
    printf("Config OK: model_chain=[");
    for (size_t i = 0; i < chain.size(); ++i) {
        if (i > 0) printf(", ");
        printf("%s", chain[i].c_str());
    }
    printf("]\n");

    GeminiClient gemini;
    gemini.SetApiKey(config.GetApiKey());
    gemini.SetModelChain(chain);
    gemini.SetLogger([](const std::string& msg) {
        printf("  [LOG] %s\n", msg.c_str());
    });

    printf("\n=== TEST 1: Tek soru ===\n");
    auto r1 = gemini.Ask("Dusk nedir? Tek cumle.", SYSTEM_PROMPT);
    printf("ok=%d status=%d model=%s fallback=%d\n",
           r1.ok, r1.statusCode, r1.activeModel.c_str(), r1.fallbackUsed);
    if (r1.ok) printf("text: %s\n", r1.text.c_str());
    else printf("error: %s\n", r1.error.c_str());
    printf("interactionId: %.50s\n", r1.interactionId.c_str());

    if (!r1.ok) {
        printf("\nFAIL: ilk istek basarisiz\n");
        return 1;
    }

    printf("\n=== TEST 2: Multi-turn ===\n");
    auto r2 = gemini.Ask("Peki nereden elde edilir?", SYSTEM_PROMPT, r1.interactionId, r1.activeModel);
    printf("ok=%d status=%d model=%s fallback=%d\n",
           r2.ok, r2.statusCode, r2.activeModel.c_str(), r2.fallbackUsed);
    if (r2.ok) printf("text: %s\n", r2.text.c_str());
    else printf("error: %s\n", r2.error.c_str());

    if (!r2.ok) {
        printf("\nFAIL: multi-turn basarisiz\n");
        return 1;
    }

    printf("\n=== TEST 3: Cooldown kontrolu ===\n");
    auto cds = gemini.GetCooldowns();
    if (cds.empty()) {
        printf("Aktif cooldown yok (iyi)\n");
    } else {
        for (auto& cd : cds)
            printf("Cooldown: %s = %d sn\n", cd.model.c_str(), cd.waitSeconds);
    }

    printf("\n=== TUMU GECTI ===\n");
    return 0;
}
