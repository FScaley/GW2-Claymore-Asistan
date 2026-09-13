#include "core/HttpClient.h"
#include "core/GeminiClient.h"
#include "core/ConfigManager.h"
#include "core/GW2Client.h"
#include "core/ItemIndex.h"
#include "core/FunctionHandler.h"
#include <cstdio>

static const char* SYSTEM_PROMPT =
    "Sen GW2-Claymore Asistan'sin. Guild Wars 2 hakkinda uzmansin.\n"
    "- Ingilizce ara ve dusun, Turkce cevap ver.\n"
    "- Kisa ve oz cevaplar ver.\n"
    "- Item fiyati, crafting tarifi veya wiki bilgisi gerektiginde uygun tool'u kullan.";

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

    printf("\n=== TEST 0: GW2 API + Fiyat Formatlama ===\n");
    {
        printf("FormatPrice(0) = %s\n", GW2Client::FormatPrice(0).c_str());
        printf("FormatPrice(50) = %s\n", GW2Client::FormatPrice(50).c_str());
        printf("FormatPrice(2121) = %s\n", GW2Client::FormatPrice(2121).c_str());
        printf("FormatPrice(8500000) = %s\n", GW2Client::FormatPrice(8500000).c_str());

        GW2Client gw2;
        auto item = gw2.GetItem(19721);
        if (item.found) {
            printf("Item: %s (id=%d, rarity=%s, chat_link=%s)\n",
                   item.name.c_str(), item.id, item.rarity.c_str(), item.chatLink.c_str());
        } else {
            printf("WARN: GW2 API item fetch failed\n");
        }

        auto price = gw2.GetPrice(19721);
        if (price.found) {
            printf("Price: buy=%s sell=%s\n",
                   GW2Client::FormatPrice(price.buyPrice).c_str(),
                   GW2Client::FormatPrice(price.sellPrice).c_str());
        } else {
            printf("WARN: GW2 API price fetch failed\n");
        }

        auto wiki = gw2.WikiSearch("Mystic Coin", 3);
        printf("Wiki search 'Mystic Coin': %zu results\n", wiki.size());
        for (auto& r : wiki)
            printf("  - %s\n", r.title.c_str());
    }

    printf("\n=== TEST 1: FunctionHandler ===\n");
    {
        GW2Client gw2;
        ItemIndex index;
        FunctionHandler handler(&gw2, &index);

        FunctionCall call;
        call.id = "test_1";
        call.name = "gw2_item_info";
        call.arguments = {{"name", "Glob of Ectoplasm"}};
        auto result = handler.Handle(call);
        printf("gw2_item_info result: %s\n", result.resultText.substr(0, 200).c_str());
        printf("Index size after: %zu\n", index.Size());
    }

    GeminiClient gemini;
    gemini.SetApiKey(config.GetApiKey());
    gemini.SetModelChain(chain);
    gemini.SetLogger([](const std::string& msg) {
        printf("  [LOG] %s\n", msg.c_str());
    });

    printf("\n=== TEST 2: Gemini soru (tool'suz) ===\n");
    auto r1 = gemini.Ask("Dusk nedir? Tek cumle.", SYSTEM_PROMPT);
    printf("ok=%d status=%s model=%s fallback=%d\n",
           r1.ok, r1.status.c_str(), r1.activeModel.c_str(), r1.fallbackUsed);
    if (r1.ok) printf("text: %s\n", r1.text.c_str());
    else printf("error: %s\n", r1.error.c_str());

    if (!r1.ok) {
        printf("\nFAIL: ilk istek basarisiz\n");
        return 1;
    }

    printf("\n=== TEST 3: Gemini + function calling ===\n");
    {
        auto tools = FunctionHandler::GetToolDefinitions();
        auto r = gemini.Ask("Glob of Ectoplasm kac altin? Sadece fiyat soyle.",
                            SYSTEM_PROMPT, "", "", tools);
        printf("ok=%d status=%s model=%s fc_count=%zu\n",
               r.ok, r.status.c_str(), r.activeModel.c_str(), r.functionCalls.size());

        if (r.RequiresAction()) {
            printf("Function calls:\n");
            for (auto& fc : r.functionCalls)
                printf("  - %s(%s)\n", fc.name.c_str(), fc.arguments.dump().c_str());

            GW2Client gw2;
            ItemIndex index;
            FunctionHandler handler(&gw2, &index);

            std::vector<std::pair<std::string, std::pair<std::string, std::string>>> results;
            for (auto& fc : r.functionCalls) {
                FunctionCall call;
                call.id = fc.id;
                call.name = fc.name;
                call.arguments = fc.arguments;
                auto result = handler.Handle(call);
                results.push_back({result.callId, {result.name, result.resultText}});
            }

            auto r2 = gemini.SendFunctionResults(r.activeModel, r.interactionId,
                                                  results, tools, SYSTEM_PROMPT);
            printf("After FC: ok=%d status=%s\n", r2.ok, r2.status.c_str());
            if (r2.ok) printf("text: %s\n", r2.text.c_str());
            else printf("error: %s\n", r2.error.c_str());
        } else if (r.ok) {
            printf("Model answered without tools: %s\n", r.text.c_str());
        } else {
            printf("error: %s\n", r.error.c_str());
        }
    }

    printf("\n=== TEST 4: Cooldown kontrolu ===\n");
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
