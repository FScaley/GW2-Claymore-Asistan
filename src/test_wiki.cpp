#include "core/GW2Client.h"
#include "core/WikiText.h"
#include "core/ItemIndex.h"
#include "core/FunctionHandler.h"
#include <cstdio>
#include <json.hpp>

using json = nlohmann::json;

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? "OK" : "FAIL", what);
    if (!ok) g_fail++;
}

int main() {
    GW2Client gw2;

    printf("=== A: Beetle Saddle HTML -> text (script/style leak check) ===\n");
    auto saddle = gw2.WikiGetPageHtml("Beetle Saddle");
    if (saddle.found) {
        std::string text = WikiText::HtmlToText(saddle.html);
        printf("html=%zu text=%zu\n--- first 1200 ---\n%.1200s\n--- end ---\n",
               saddle.html.size(), text.size(), text.c_str());
        Check(text.find("tokenvalid") == std::string::npos, "no CSS leak (.tokenvalid)");
        Check(text.find("function(") == std::string::npos, "no JS leak (function()");
        Check(text.find("Inquest Beetle Notes") != std::string::npos, "contains Inquest Beetle Notes");
    } else {
        Check(false, "Beetle Saddle HTML fetched");
    }

    printf("\n=== B: Roller Beetle HTML -> sections ===\n");
    auto rb = gw2.WikiGetPageHtml("Roller Beetle");
    if (rb.found) {
        std::string text = WikiText::HtmlToText(rb.html);
        std::vector<WikiSection> sections;
        std::string lead = WikiText::SplitLead(text, sections);
        printf("text=%zu lead=%zu sections=%zu\n", text.size(), lead.size(), sections.size());
        for (auto& s : sections) printf("  ## %s (%zu)\n", s.title.c_str(), s.body.size());
        Check(sections.size() >= 3, "at least 3 level-2 sections detected");
        Check(text.find("gamelink-") == std::string::npos, "no gamelink JS leak");
    } else {
        Check(false, "Roller Beetle HTML fetched");
    }

    printf("\n=== C: FunctionHandler gw2_wiki(\"Roller Beetle\") ===\n");
    {
        ItemIndex idx;
        FunctionHandler fh(&gw2, &idx);
        FunctionCall call;
        call.id = "t1";
        call.name = "gw2_wiki";
        call.arguments = {{"query", "Roller Beetle"}};
        auto res = fh.Handle(call);
        printf("result bytes=%zu\n", res.resultText.size());

        json j = json::parse(res.resultText, nullptr, false);
        Check(!j.is_discarded(), "result is valid JSON");
        if (!j.is_discarded()) {
            printf("title=%s\n", j.value("title", "?").c_str());
            if (j.contains("sections")) {
                printf("sections:");
                for (auto& s : j["sections"]) printf(" [%s]", s.get<std::string>().c_str());
                printf("\n");
            }
            std::string content = j.value("content", "");
            printf("content bytes=%zu\n", content.size());
            Check(content.find("Beetle Juice") != std::string::npos, "content mentions Beetle Juice");

            size_t subCount = j.contains("sub_collections") ? j["sub_collections"].size() : 0;
            printf("sub_collections=%zu\n", subCount);
            bool hasSaddle = false, hasFeed = false, hasJuice = false, hasNotes = false;
            if (subCount > 0) {
                for (auto& sc : j["sub_collections"]) {
                    std::string name = sc.value("name", "");
                    size_t n = sc.contains("items") ? sc["items"].size() : 0;
                    printf("  - %s: %zu items\n", name.c_str(), n);
                    for (auto& it : sc["items"]) {
                        printf("      * %s -- %s\n", it.value("name", "").c_str(),
                               it.value("hint", "").c_str());
                        if (it.value("name", "") == "Inquest Beetle Notes") hasNotes = true;
                    }
                    if (name == "Beetle Saddle") hasSaddle = true;
                    if (name == "Beetle Feed") hasFeed = true;
                    if (name == "Beetle Juice") hasJuice = true;
                }
            }
            Check(hasSaddle, "sub_collections has Beetle Saddle");
            Check(hasFeed, "sub_collections has Beetle Feed");
            Check(hasJuice, "sub_collections has Beetle Juice");
            Check(hasNotes, "Beetle Saddle items include Inquest Beetle Notes");
            Check(res.resultText.size() < 40 * 1024, "total result under 40KB");
        }
    }

    printf("\n=== D: gw2_wiki section param ===\n");
    {
        ItemIndex idx;
        FunctionHandler fh(&gw2, &idx);
        FunctionCall call;
        call.id = "t2";
        call.name = "gw2_wiki";
        call.arguments = {{"query", "Roller Beetle"}, {"section", "Unlocking"}};
        auto res = fh.Handle(call);
        json j = json::parse(res.resultText, nullptr, false);
        Check(!j.is_discarded() && j.value("section", "") == "Unlocking", "section=Unlocking returned");
        if (!j.is_discarded())
            printf("section content bytes=%zu\n", j.value("content", "").size());
    }

    printf("\n=== E: cancel inside tool ===\n");
    {
        ItemIndex idx;
        FunctionHandler fh(&gw2, &idx);
        FunctionCall call;
        call.id = "t3";
        call.name = "gw2_wiki";
        call.arguments = {{"query", "Roller Beetle"}};
        auto res = fh.Handle(call, [] { return true; });
        Check(res.resultText.find("cancelled") != std::string::npos, "cancel returns cancelled error");
    }

    printf("\n%s (%d failures)\n", g_fail == 0 ? "=== TUMU GECTI ===" : "=== BASARISIZ ===", g_fail);
    return g_fail == 0 ? 0 : 1;
}
