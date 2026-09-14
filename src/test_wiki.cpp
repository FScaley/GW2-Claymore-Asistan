#include "core/GW2Client.h"
#include "core/WikiText.h"
#include "core/ItemIndex.h"
#include "core/FunctionHandler.h"
#include "core/ConfigManager.h"
#include <cstdio>
#include <algorithm>
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

        Check(HttpClient::MaskProxyCredentials("http://user:pass@proxy.example.com:8080")
              == "http://****@proxy.example.com:8080", "MaskProxyCredentials masks user:pass before @");
        Check(HttpClient::MaskProxyCredentials("proxy.example.com:8080")
              == "proxy.example.com:8080", "MaskProxyCredentials leaves no-@ string alone");
        Check(HttpClient::MaskProxyCredentials("http=user:p@a:80;https=u:p@b:443")
              == "http=****@a:80;https=****@b:443", "MaskProxyCredentials handles multi-segment proxies");
    }

    printf("\n=== J: IPv6 fast fallback + connect timeout cap ===\n");
    {
        // Field case (13 Sep 2026, v0.3.12 log): every Gemini call ended in WinHttpSendRequest 12002 after 45 s.
        // curl -4 -> 404 instantly, curl -6 -> silence: the machine has an IPv6 address but the IPv6 path is dead.
        // Only Google publishes AAAA records among our hosts (GitHub, api/wiki.guildwars2.com are IPv4-only), which
        // is why the game, Nexus and the browser (Happy Eyeballs) all worked while the addon alone timed out.
        auto t = HttpClient::TimeoutsFor(45000);
        Check(t.resolve == 10000 && t.connect == 10000 && t.send == 45000 && t.receive == 45000,
              "POST budget 45 s: resolve/connect capped at 10 s, send/receive keep 45 s");
        t = HttpClient::TimeoutsFor(10000);
        Check(t.resolve == 10000 && t.connect == 10000 && t.send == 10000 && t.receive == 10000,
              "GET budget 10 s: unchanged");
        t = HttpClient::TimeoutsFor(25000);
        Check(t.connect == 10000 && t.receive == 25000, "wiki HTML budget 25 s: connect 10 s, receive 25 s");
        t = HttpClient::TimeoutsFor(5000);
        Check(t.resolve == 5000 && t.connect == 5000, "a budget below the cap is not raised");

        HttpClient http;
        Check(http.IPv6FastFallback(), "session enables WINHTTP_OPTION_IPV6_FAST_FALLBACK (Windows 8.1+)");
        std::string diag = HttpClient::Diagnostics();
        printf("Diagnostics: %s\n", diag.c_str());
        Check(diag.find("IPv6 hizli geri donus: acik") != std::string::npos, "Diagnostics reports the fallback state");
        auto ok = http.Get(GW2Client::API_HOST, "/v2/build", 10000);
        Check(ok.has_value() && ok->statusCode == 200, "GW2 API still answers with the new timeouts");
    }

    printf("\n=== K: gw2_guide — guildjen community guide (zero Gemini) ===\n");
    {
        GW2Client gw2k;
        ItemIndex idxk;
        FunctionHandler fh(&gw2k, &idxk);
        FunctionCall call;
        auto noCancel = [](){ return false; };

        call.id = "k1"; call.name = "gw2_guide";
        call.arguments = {{"query", "wvw beginner"}};
        auto r = fh.Handle(call, noCancel);
        auto j = json::parse(r.resultText, nullptr, false);
        printf("size=%zu\n", r.resultText.size());
        Check(!j.is_discarded() && !j.contains("error"), "gw2_guide returns valid JSON, no error");
        std::string title = j.value("title", "");
        printf("title: %s\n", title.c_str());
        Check(!title.empty(), "title is non-empty");
        Check(title.find("&#") == std::string::npos, "title has no raw HTML entities");
        Check(j.value("source", "") == "guildjen", "source is guildjen");
        Check(!j.value("modified", "").empty(), "modified date present");

        json related = j.value("related", json::array());
        printf("search results: [%s]", title.c_str());
        for (size_t i = 0; i < related.size(); ++i)
            printf(", [%s]", related[i].get<std::string>().c_str());
        printf("\n");

        json secs = j.value("sections", json::array());
        printf("sections: %zu\n", secs.size());
        Check(secs.size() >= 2, "at least 2 sections");
        for (size_t i = 0; i < secs.size() && i < 8; ++i)
            printf("  ## %s\n", secs[i].get<std::string>().c_str());

        std::string content = j.value("content", "");
        printf("content_size=%zu\n", content.size());
        Check(content.size() > 2000 && content.size() < 14 * 1024,
              "content is a real guide (>2KB), not just a link index");
        Check(content.find("wp-block") == std::string::npos, "no wp-block leak");
        Check(content.find("srcset") == std::string::npos, "no srcset leak");
        Check(content.find("tiled-gallery") == std::string::npos, "no gallery leak");
        Check(content.find("function(") == std::string::npos, "no JS leak");

        if (secs.size() >= 1) {
            std::string secName = secs[0].get<std::string>();
            printf("section= fetch: %s\n", secName.c_str());
            call.id = "k2"; call.name = "gw2_guide";
            call.arguments = {{"query", "wvw beginner"}, {"section", secName}};
            auto r2 = fh.Handle(call, noCancel);
            auto j2 = json::parse(r2.resultText, nullptr, false);
            Check(!j2.is_discarded() && !j2.contains("error"), "section= returns valid JSON");
            std::string sc = j2.value("content", "");
            Check(sc.find(secName) != std::string::npos, "section content contains the section title");
        }

        auto cancelled = [](){ return true; };
        call.id = "k3"; call.name = "gw2_guide";
        call.arguments = {{"query", "fishing guide"}};
        auto rc = fh.Handle(call, cancelled);
        Check(rc.resultText.find("cancelled") != std::string::npos, "cancel is honored");
    }

    printf("\n=== L: Locations section -> locations[] enrichment (Gharr Leadclaw) ===\n");
    {
        GW2Client gw2l;
        ItemIndex idxl;
        FunctionHandler fhl(&gw2l, &idxl);
        FunctionCall call;
        call.id = "l1"; call.name = "gw2_wiki";
        call.arguments = {{"query", "Gharr Leadclaw"}};
        auto r = fhl.Handle(call, [](){ return false; });
        auto j = json::parse(r.resultText, nullptr, false);
        Check(!j.is_discarded() && !j.contains("error"), "Gharr Leadclaw returns valid JSON");

        size_t locCount = 0;
        bool hasDragonsStand = false;
        bool dsPactBase = false;
        if (j.contains("locations") && j["locations"].is_array()) {
            locCount = j["locations"].size();
            printf("locations (%zu):\n", locCount);
            for (auto& loc : j["locations"]) {
                std::string mn = loc.value("map_name", "?");
                size_t wc = loc.contains("waypoints") ? loc["waypoints"].size() : 0;
                printf("  %s: %zu waypoints", mn.c_str(), wc);
                if (loc.value("npc_here", false)) printf(" [npc_here]");
                printf("\n");
                if (mn == "Dragon's Stand") {
                    hasDragonsStand = true;
                    for (auto& wp : loc["waypoints"])
                        if (wp.value("name", "").find("Pact Base Camp") != std::string::npos)
                            dsPactBase = true;
                }
            }
        } else {
            printf("locations: NONE\n");
        }
        Check(locCount >= 5, "at least 5 locations (infobox + section)");
        Check(hasDragonsStand, "Dragon's Stand is in locations (from section text)");
        Check(dsPactBase, "Dragon's Stand has Pact Base Camp waypoint");
    }

    printf("\n=== M: gw2_build — metabattle builds (zero Gemini) ===\n");
    {
        GW2Client gw2m;
        ItemIndex idxm;
        FunctionHandler fhm(&gw2m, &idxm);
        FunctionCall call;
        auto noCancel = [](){ return false; };

        call.id = "m1"; call.name = "gw2_build";
        call.arguments = {{"query", "firebrand"}, {"mode", "wvw"}};
        auto r = fhm.Handle(call, noCancel);
        auto j = json::parse(r.resultText, nullptr, false);
        printf("size=%zu\n", r.resultText.size());
        Check(!j.is_discarded() && !j.contains("error"), "gw2_build returns valid JSON, no error");
        Check(j.value("source", "") == "metabattle", "source is metabattle");
        std::string title = j.value("title", "");
        printf("title: %s\n", title.c_str());
        Check(!title.empty(), "title is non-empty");

        std::string tc = j.value("template_code", "");
        printf("template_code: %.40s...\n", tc.c_str());
        Check(tc.size() >= 4 && tc.compare(0, 3, "[&D") == 0, "template_code starts with [&D");

        std::string df = j.value("designed_for", "");
        printf("designed_for: %s\n", df.c_str());
        std::string dfLower = df;
        std::transform(dfLower.begin(), dfLower.end(), dfLower.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        Check(dfLower.find("wvw") != std::string::npos, "designed_for contains wvw");

        std::string rating = j.value("rating", "");
        printf("rating: %s\n", rating.c_str());
        Check(!rating.empty(), "rating is non-empty");
        {
            std::string lr = rating;
            std::transform(lr.begin(), lr.end(), lr.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            Check(lr != "archived" && lr != "draft" && lr != "trash" && lr != "test",
                  "rating is not archived/draft/trash/test");
        }

        json alts = j.value("alternatives", json::array());
        printf("alternatives: %zu\n", alts.size());
        Check(alts.size() >= 1, "at least 1 alternative");

        std::string content = j.value("content", "");
        printf("content_size=%zu\n", content.size());
        printf("--- content preview (first 600) ---\n%.600s\n--- end preview ---\n", content.c_str());
        Check(content.size() > 100 && content.size() < 14 * 1024, "content between 100 and 14KB");
        Check(content.find("data-") == std::string::npos, "no data- attribute leak");
        Check(content.find("tooltip") == std::string::npos, "no tooltip leak");
        Check(content.find("Our curator") == std::string::npos, "no rating-widget boilerplate leak");

        json secs = j.value("sections", json::array());
        printf("sections: %zu\n", secs.size());
        for (size_t i = 0; i < secs.size() && i < 10; ++i)
            printf("  ## %s\n", secs[i].get<std::string>().c_str());

        printf("\n--- M2: gw2_build no mode ---\n");
        call.id = "m2"; call.name = "gw2_build";
        call.arguments = {{"query", "guardian"}};
        auto r2 = fhm.Handle(call, noCancel);
        auto j2 = json::parse(r2.resultText, nullptr, false);
        Check(!j2.is_discarded() && !j2.contains("error"), "no-mode returns valid JSON");
        Check(j2.value("source", "") == "metabattle", "no-mode source is metabattle");
        printf("no-mode title: %s\n", j2.value("title", "").c_str());
        printf("no-mode rating: %s\n", j2.value("rating", "").c_str());

        printf("\n--- M3: cancel ---\n");
        auto cancelled = [](){ return true; };
        call.id = "m3"; call.name = "gw2_build";
        call.arguments = {{"query", "thief"}};
        auto rc = fhm.Handle(call, cancelled);
        Check(rc.resultText.find("cancelled") != std::string::npos, "cancel is honored");
    }

    printf("\n%s (%d failures)\n", g_fail == 0 ? "=== TUMU GECTI ===" : "=== BASARISIZ ===", g_fail);
    return g_fail == 0 ? 0 : 1;
}
