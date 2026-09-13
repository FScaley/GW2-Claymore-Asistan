#include "core/GW2Client.h"
#include "core/WikiText.h"
#include "core/ItemIndex.h"
#include "core/FunctionHandler.h"
#include "core/ConfigManager.h"
#include <cstdio>
#include <json.hpp>

using json = nlohmann::json;

static bool Markdown_IsChatLinkLike(const std::string& s) {
    return s.size() >= 4 && s[0] == '[' && s[1] == '&' && s.back() == ']';
}

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? "OK" : "FAIL", what);
    if (!ok) g_fail++;
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);   // unbuffered: a crash must not eat the lines before it
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

    printf("\n=== F: NPC location -> map waypoints (Gorrik) ===\n");
    {
        ItemIndex idx;
        FunctionHandler fh(&gw2, &idx);
        FunctionCall call;
        call.id = "t4";
        call.name = "gw2_wiki";
        call.arguments = {{"query", "Gorrik"}};
        auto res = fh.Handle(call);
        json j = json::parse(res.resultText, nullptr, false);
        Check(!j.is_discarded(), "Gorrik result is valid JSON");
        if (!j.is_discarded()) {
            size_t locCount = j.contains("locations") ? j["locations"].size() : 0;
            printf("title=%s primary_map=%s locations=%zu other=%s bytes=%zu\n",
                   j.value("title", "?").c_str(), j.value("map_name", "-").c_str(), locCount,
                   j.value("other_locations", json::array()).dump().c_str(), res.resultText.size());
            bool kournaFound = false, alliedWp = false, thunderheadHere = false;
            if (j.contains("locations")) {
                for (auto& loc : j["locations"]) {
                    std::string mn = loc.value("map_name", "");
                    size_t n = loc.contains("waypoints") ? loc["waypoints"].size() : 0;
                    std::string first = (n > 0) ? loc["waypoints"][0].value("name", "") : "";
                    printf("  - %s npc_here=%d areas=%s waypoints=%zu first=%s\n", mn.c_str(),
                           (int)loc.value("npc_here", false), loc.value("areas", json::array()).dump().c_str(),
                           n, first.c_str());
                    if (mn == "Domain of Kourna") {
                        kournaFound = true;
                        for (auto& w : loc["waypoints"])
                            if (w.value("name", "").find("Allied Encampment") != std::string::npos &&
                                Markdown_IsChatLinkLike(w.value("chat_link", ""))) alliedWp = true;
                    }
                    if (mn == "Thunderhead Peaks" && loc.value("npc_here", false)) thunderheadHere = true;
                }
            }
            Check(!j.contains("search_mode"), "exact-title query has no search_mode (opensearch path untouched)");
            Check(kournaFound, "locations include Domain of Kourna (area 'Allied Encampment' -> within)");
            Check(alliedWp, "Domain of Kourna waypoints include Allied Encampment Waypoint with real chat_link (POIs live on floor 49, not default_floor 1)");
            Check(thunderheadHere, "npc_here marks Thunderhead Peaks (wiki coordinates) and it is listed first");
            Check(j.value("map_name", "") == "Thunderhead Peaks", "primary map_name mirrors the npc_here entry");
        }
    }

    printf("\n=== G: multi-area NPC (Champion Toxic Spider Queen) ===\n");
    {
        ItemIndex idx;
        FunctionHandler fh(&gw2, &idx);
        FunctionCall call;
        call.id = "t5";
        call.name = "gw2_wiki";
        call.arguments = {{"query", "Champion Toxic Spider Queen"}};
        auto res = fh.Handle(call);
        json j = json::parse(res.resultText, nullptr, false);
        Check(!j.is_discarded(), "Toxic Spider Queen result is valid JSON");
        if (!j.is_discarded()) {
            size_t wpCount = j.contains("nearby_waypoints") ? j["nearby_waypoints"].size() : 0;
            printf("map_name=%s waypoints=%zu\n", j.value("map_name", "-").c_str(), wpCount);
            Check(j.value("map_name", "") == "Kessex Hills",
                  "semicolon area list resolves first area to Kessex Hills");
            Check(wpCount >= 10, "Kessex Hills waypoints attached");
        }
    }

    printf("\n=== H: natural-language query -> full-text fallback (Janthir Syntri renown tokens) ===\n");
    {
        // Observed in a user's Nexus log (v0.3.9): 'Janthir Syntri Renown Tokens' -> "No wiki results",
        // then 5 more padded queries, all empty. opensearch is a title-PREFIX match: the plural alone kills it.
        auto hits = gw2.WikiSearch("Janthir Syntri renown tokens", 5);
        printf("WikiSearch hits=%zu first=%s fulltext=%d\n", hits.size(),
               hits.empty() ? "-" : hits[0].title.c_str(), hits.empty() ? 0 : (int)hits[0].fulltext);
        Check(!hits.empty() && hits[0].title == "Janthir Syntri Renown Token",
              "plural/padded query resolves to 'Janthir Syntri Renown Token'");

        ItemIndex idx;
        FunctionHandler fh(&gw2, &idx);
        FunctionCall call;
        call.id = "t6";
        call.name = "gw2_wiki";
        call.arguments = {{"query", "janthir syntri renown tokens"}};
        auto res = fh.Handle(call);
        json j = json::parse(res.resultText, nullptr, false);
        Check(!j.is_discarded(), "renown token result is valid JSON");
        if (!j.is_discarded()) {
            std::string itemId = j.contains("item_id") ? j["item_id"].dump() : "-";   // stored as a number
            printf("title=%s item_id=%s search_mode=%s bytes=%zu\n",
                   j.value("title", "-").c_str(), itemId.c_str(),
                   j.value("search_mode", "-").c_str(), res.resultText.size());
            Check(j.value("title", "") == "Janthir Syntri Renown Token", "page title is Janthir Syntri Renown Token");
            Check(itemId == "102881", "item_id 102881 extracted");
            Check(j.value("content", "").find("Local Writ of Renown") != std::string::npos,
                  "content explains the Local Writ of Renown exchange");
            Check(j.value("search_mode", "") == "fulltext", "search_mode marks the fuzzy path");
        }

        // Padded queries from the same log — informational: what does the fallback pick?
        for (const char* q : {"Leviathan farm End of Dragons", "Gorrik Kourna location", "Dragonite Ore converter"}) {
            auto h = gw2.WikiSearch(q, 5);
            printf("  fallback [%s] -> %s\n", q, h.empty() ? "(none)" : h[0].title.c_str());
        }
        auto none = gw2.WikiSearch("meta farm train Guild Wars 2 Secrets of the Obscure Janthir Wilds", 5);
        printf("  fallback [vague meta-farm query] -> %s\n", none.empty() ? "(none)" : none[0].title.c_str());
    }

    printf("\n=== I: HTTP failure visibility + API key hygiene ===\n");
    {
        // A user saw "Baglanti hatasi" with a Nexus log that held nothing but "loaded": HttpClient returned
        // nullopt and threw the WinHTTP error code away. Probe (13 Sep 2026): an empty key -> 403 and a key with
        // whitespace/quotes -> 400 both reach Google and get logged; a single non-ASCII byte in the header
        // (NBSP copied from a web page, a Turkish letter) is rejected locally by WinHttpSendRequest with 87,
        // in 0 ms, with no response - exactly that silent symptom. The code must now be visible and the key clean.
        HttpClient http;
        Check(!http.LastFailure().Any(), "fresh client has no recorded failure");

        auto r = http.Get("nonexistent.invalid", "/", 5000);
        printf("nonexistent.invalid -> %s\n", http.LastFailureText().c_str());
        Check(!r.has_value(), "unresolvable host yields no response");
        Check(http.LastFailure().stage == "WinHttpSendRequest" && http.LastFailure().code == 12007,
              "failure recorded as WinHttpSendRequest 12007 (ERROR_WINHTTP_NAME_NOT_RESOLVED)");
        Check(!HttpClient::DescribeError(12007).empty(), "DescribeError(12007) has Windows text");
        Check(HttpClient::TurkishHint(12007).find("DNS") != std::string::npos, "TurkishHint(12007) mentions DNS");
        Check(HttpClient::TurkishHint(87).find("anahtar") != std::string::npos, "TurkishHint(87) points at the API key");

        auto bad = http.Post("generativelanguage.googleapis.com", "/v1beta/interactions", "{}",
                             "x-goog-api-key: FAKE\xc3\xa7" "123\r\n", 5000);
        printf("non-ASCII header byte -> %s\n", http.LastFailureText().c_str());
        Check(!bad.has_value() && http.LastFailure().code == 87,
              "non-ASCII byte in a header -> WinHttpSendRequest 87 (ERROR_INVALID_PARAMETER), no response");
        Check(http.LastFailure().elapsedMs < 2000, "header rejection is local (no network round trip)");

        auto ok = http.Get(GW2Client::API_HOST, "/v2/build", 10000);
        Check(ok.has_value() && ok->statusCode == 200, "GW2 API /v2/build answers 200");
        Check(!http.LastFailure().Any(), "a successful request clears the previous failure");

        std::string diag = HttpClient::Diagnostics();
        printf("Diagnostics: %s\n", diag.c_str());
        Check(diag.find("Windows") != std::string::npos && diag.find("proxy") != std::string::npos,
              "Diagnostics reports OS version and proxy configuration");

        Check(ConfigManager::SanitizeApiKey("  FAKE123\r\n") == "FAKE123", "SanitizeApiKey trims ASCII whitespace");
        Check(ConfigManager::SanitizeApiKey("\xc2\xa0" "FAKE123\xc2\xa0") == "FAKE123", "SanitizeApiKey trims NBSP");
        Check(ConfigManager::SanitizeApiKey("\"FAKE123\"") == "FAKE123", "SanitizeApiKey strips wrapping quotes");
        Check(ConfigManager::SanitizeApiKey("\xef\xbb\xbf" "FAKE123\xe2\x80\x8b") == "FAKE123",
              "SanitizeApiKey strips BOM and zero-width space");
        Check(ConfigManager::SanitizeApiKey("FAKE123") == "FAKE123", "SanitizeApiKey leaves a clean key alone");
        Check(ConfigManager::ApiKeyProblem("FAKE123").empty(), "clean key reports no problem");
        Check(ConfigManager::ApiKeyProblem("").empty(), "empty key is not a character problem (handled as 'missing')");
        std::string problem = ConfigManager::ApiKeyProblem("FAKE\xc3\xa7" "123");
        printf("problem text: %s\n", problem.c_str());
        Check(!problem.empty() && problem.find("5.") != std::string::npos, "inner non-ASCII byte reported at position 5");
        Check(!ConfigManager::ApiKeyProblem("FAKE 123").empty(), "inner space reported");
        Check(problem.find("FAKE") == std::string::npos, "problem text never echoes the key");
    }

    printf("\n%s (%d failures)\n", g_fail == 0 ? "=== TUMU GECTI ===" : "=== BASARISIZ ===", g_fail);
    return g_fail == 0 ? 0 : 1;
}
