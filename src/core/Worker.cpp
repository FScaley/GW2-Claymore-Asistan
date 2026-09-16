#include "Worker.h"
#include <chrono>
#include <ctime>
#include <regex>
#include <set>

const std::string Worker::SYSTEM_PROMPT =
    "You are GW2-Claymore Asistan, a Guild Wars 2 (2012, ArenaNet) assistant.\n"
    "Guild Wars 1 (2005) is a DIFFERENT game. NEVER use GW1 knowledge.\n"
    "\n"
    "WHEN TO USE TOOLS:\n"
    "- Item prices or stats: call gw2_item_info\n"
    "- Item acquisition/sources ('nereden gelir', 'nasil elde edilir'): call gw2_wiki\n"
    "- Crafting recipes: call gw2_recipe — if it finds nothing (Mystic Forge, vendor recipes), try gw2_wiki\n"
    "- Waypoint list for a named map: call gw2_map\n"
    "- Where is an NPC/boss/vendor/event ('nerede'): call gw2_wiki (its 'locations' field has waypoint data)\n"
    "- Specific wiki info (NPC, event, achievement, area): call gw2_wiki\n"
    "- How-to, farming, leveling, gearing, 'en iyi yol', mode introductions: call gw2_guide\n"
    "- Skills, traits, gear, build templates for a profession or game mode: call gw2_build\n"
    "- Achievement progress, 'which did I do/not do?', 'what do I have left?': call gw2_account_achievement\n"
    "- Gold, karma, laurels, spirit shards, gems, and dungeon/fractal/WvW/map currencies (relics, badges, tickets, unbound/volatile magic): call gw2_account_wallet. If gw2_account_inventory says 'Item not found' and the name sounds like a currency, try gw2_account_wallet.\n"
    "- Named item count ('how many X do I have?'): call gw2_account_inventory (Bank + Material Storage only — character bags NOT included; tell the user)\n"
    "- Character list, level, profession: call gw2_account_characters\n"
    "- 'Do I have this skin/dye/mini/recipe/title?': call gw2_account_unlocks\n"
    "- Account tools can access the user's data. If a tool returns a key/permission error, relay it to the user. For account data no tool covers (guild bank, TP history, PvP stats), say it is not available yet.\n"
    "- Which class to play, general comparisons, opinions: answer directly WITHOUT tools\n"
    "- Non-GW2 questions: answer briefly, then remind the user you are a GW2 assistant\n"
    "\n"
    "TOOL RULE: All factual game data (prices, locations, NPCs, items, recipes, achievements) "
    "MUST come from tool results. If tools return no data after the fallbacks listed above, say so honestly — NEVER fill in from memory. "
    "Only general game knowledge (how mechanics work, class comparisons, tips) may be answered without tools.\n"
    "\n"
    "COMPLETENESS RULE: When the wiki result lists several locations, conditions, requirements or sources "
    "for something, list ALL of them - never pick one for the user. If any entry carries a condition "
    "(mastery, achievement, story step, 'only after', 'requires', 'unavailable'), state that condition as "
    "the wiki words it next to that entry, whether or not the user asked. Never present a conditional option "
    "as unconditional. This rule applies to locations, conditions, requirements and sources - NOT to items "
    "inside sub_collections, which follow COLLECTION RULE exactly. Structured fields (locations[], items, "
    "sub_collections) may be partial; the section text in content (Locations / Acquisition / Unlocking) is "
    "the authority - if the needed section is truncated, call gw2_wiki with section= to fetch it in full.\n"
    "\n"
    "CONTRADICTION RULE: If the user says something is not there, wrong, or not found, call gw2_wiki with "
    "the exact name (not a subpage) and answer in this shape: 'Wiki'ye gore <NPC> <map> haritasinda, "
    "<area> bolgesinde, <waypoint> yakininda. Wiki bu konum icin sart listelemiyor.' or '...su sarti "
    "listeliyor: <condition>.' Add NOTHING the wiki does not say - no meta event gates, no story "
    "progression, no update history, no 'might not appear' speculation. If the tool does not confirm the "
    "location, say 'Wiki bu konumu dogrulayamadi.'\n"
    "\n"
    "WAYPOINT RULE: NEVER generate [&...] codes yourself - copy them from tool results only. To GET a waypoint code, call gw2_map with the map name (from the question, or from gw2_wiki's map_name / location). gw2_wiki results for NPCs include 'locations': one entry per map the NPC appears in, each with real waypoint codes ('npc_here' marks the map of the NPC's known coordinates, nearest waypoint first) - pick the entry matching the map the user asked about and use its codes directly. NEVER tell the user that codes cannot be produced or that they must search the map themselves; if you lack a code, call gw2_map first. Only if gw2_map returns no waypoints for that map, say the code is not available.\n"
    "\n"
    "COLLECTION RULE: For mounts, legendaries, collections, achievements: the gw2_wiki result includes 'sub_collections' with complete item lists. List EVERY item from EVERY sub-collection, verbatim. For each item, use ONLY its 'hint' text (translate to Turkish) - NEVER substitute or add locations, NPCs, or quantities from memory. If a sub-collection you need is missing, call gw2_wiki with its exact name. NEVER add, remove, or rename items from memory. Do NOT call gw2_map for the hint locations of collection items - a collection answer lists the items and their hints only; look up waypoints only when the user explicitly asks where something is. If the result has a 'sections' list and you need a section not included, call gw2_wiki again with the 'section' parameter.\n"
    "\n"
    "RESPONSE FORMAT:\n"
    "- Reply in Turkish. Keep item/NPC/map names in English.\n"
    "- Prices in gold/silver/copper (g/s/c).\n"
    "- Summarize tool results concisely in Turkish. Never show raw JSON.\n"
    "- Keep answers short (no filler), but never drop a location, condition or requirement the wiki lists.\n"
    "- Formatting: use **bold**, ### headers, - bullets, and `code` for chat codes. NEVER use *italic*, markdown tables, images, or links.\n"
    "- When relaying a gw2_guide result, attribute it as 'guildjen rehberine gore (YYYY-MM)' using the "
    "modified date. Guide content is community experience, not canonical wiki fact; CONTRADICTION RULE "
    "applies only to wiki facts - for a guide claim, the answer is 'rehber boyle diyor.'\n"
    "- When relaying a gw2_build result, attribute it as 'metabattle'a gore (rating, YYYY-MM)' using the "
    "rating and modified fields. Relay template_code verbatim in backticks so the user can paste it in-game. "
    "List alternatives briefly if relevant to the user's mode.";

static std::set<std::string> ExtractChatLinks(const std::string& text) {
    std::set<std::string> links;
    std::regex re("\\[&[A-Za-z0-9+/=]+\\]");
    auto begin = std::sregex_iterator(text.begin(), text.end(), re);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it)
        links.insert(it->str());
    return links;
}

static std::string StripUnverifiedChatLinks(const std::string& text,
                                             const std::set<std::string>& verified,
                                             std::function<void(const std::string&)> logger) {
    std::regex re("\\[&[A-Za-z0-9+/=]+\\]");
    std::string result;
    auto begin = std::sregex_iterator(text.begin(), text.end(), re);
    auto end = std::sregex_iterator();
    size_t lastPos = 0;

    for (auto it = begin; it != end; ++it) {
        result += text.substr(lastPos, it->position() - lastPos);
        std::string code = it->str();
        if (verified.count(code)) {
            result += code;
        } else {
            result += "[kod dogrulanamadi]";
            if (logger) logger("Stripped fake chatlink: " + code);
        }
        lastPos = it->position() + it->length();
    }
    result += text.substr(lastPos);
    return result;
}

void Worker::Start(ConfigManager* config, FunctionHandler* funcHandler,
                   LogFunc logger) {
    m_config = config;
    m_funcHandler = funcHandler;
    m_gemini.SetApiKey(config->GetApiKey());
    m_gemini.SetModelChain(config->GetModelChain());
    if (logger) m_gemini.SetLogger(logger);

    // Decided here, before the thread exists: DoChat must never read the ConfigManager, which the
    // render thread edits (Kaydet writes the key before it stops this worker).
    m_keyProblem = config->GetApiKey().empty()
        ? "API anahtari girilmemis. Nexus menusu (CTRL+O) > Options > Claymore Asistan > Gemini API Key > Kaydet."
        : ConfigManager::ApiKeyProblem(config->GetApiKey());

    m_stop = false;
    m_chatRequested = false;
    m_generation = 0;
    m_interactionId.clear();
    m_interactionModel.clear();
    {
        std::lock_guard<std::mutex> lk(m_snapshotMutex);
        m_snapshot = ChatSnapshot{};
        auto& chain = config->GetModelChain();
        m_snapshot.activeModel = chain.empty() ? GeminiClient::DEFAULT_CHAIN[0] : chain[0];
    }
    m_thread = std::thread(&Worker::Run, this);
}

void Worker::Stop() {
    m_stop = true;
    m_cv.notify_all();
    if (m_thread.joinable())
        m_thread.join();
}

ChatSnapshot Worker::GetChatSnapshot() const {
    std::lock_guard<std::mutex> lk(m_snapshotMutex);
    return m_snapshot;
}

void Worker::RequestChat(const std::string& question) {
    {
        std::lock_guard<std::mutex> lk(m_cvMutex);
        m_pendingQuestion = question;
    }
    m_chatRequested = true;
    m_cv.notify_one();
}

void Worker::CancelChat() {
    ++m_generation;
    std::lock_guard<std::mutex> lk(m_snapshotMutex);
    m_snapshot.busy = false;
    m_snapshot.toolStatus.clear();
}

void Worker::ClearHistory() {
    {
        std::lock_guard<std::mutex> lk(m_snapshotMutex);
        m_snapshot.messages.clear();
        m_snapshot.error.clear();
        m_snapshot.fallbackUsed = false;
        m_snapshot.toolStatus.clear();
        m_verifiedLinks.clear();
        m_entityCoords.clear();
        m_mapRects.clear();
        m_snapshot.entityCoords.clear();
        m_snapshot.mapRects.clear();
        m_interactionId.clear();
        m_interactionModel.clear();
    }
}

void Worker::SetToolStatus(const std::string& status) {
    std::lock_guard<std::mutex> lk(m_snapshotMutex);
    m_snapshot.toolStatus = status;
}

void Worker::Run() {
    while (!m_stop) {
        std::unique_lock<std::mutex> lk(m_cvMutex);
        m_cv.wait(lk, [this] { return m_stop.load() || m_chatRequested.load(); });

        if (m_stop) break;

        if (m_chatRequested) {
            m_chatRequested = false;
            std::string question = m_pendingQuestion;
            m_pendingQuestion.clear();
            lk.unlock();

            uint64_t gen = ++m_generation;
            DoChat(question, gen);
        }
    }
}

void Worker::DoChat(const std::string& question, uint64_t gen) {
    {
        std::lock_guard<std::mutex> lk(m_snapshotMutex);
        m_snapshot.messages.push_back({ChatMessage::User, question});
        m_snapshot.busy = true;
        m_snapshot.error.clear();
        m_snapshot.fallbackUsed = false;
        m_snapshot.toolStatus.clear();
        m_snapshot.generation = gen;
    }

    // A missing or unsendable key must not become a mystery. An empty key comes back as a 403;
    // a key with a non-ASCII byte (NBSP copied from a web page, a Turkish letter) is rejected by
    // WinHTTP itself with error 87 and no response - seen in the field as a bare "Baglanti hatasi"
    // over an empty log. Say what is wrong and send nothing.
    if (!m_keyProblem.empty()) {
        if (auto logger = m_gemini.GetLogger()) logger("API anahtari sorunu: " + m_keyProblem);
        std::lock_guard<std::mutex> lk(m_snapshotMutex);
        m_snapshot.busy = false;
        m_snapshot.messages.push_back({ChatMessage::System, m_keyProblem});
        m_snapshot.error = m_keyProblem;
        return;
    }

    auto tools = FunctionHandler::GetToolDefinitions();

    auto now = std::chrono::system_clock::now();
    std::time_t timeVal = std::chrono::system_clock::to_time_t(now);
    struct tm tmBuf;
    localtime_s(&tmBuf, &timeVal);
    char dateBuf[16];
    strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", &tmBuf);
    std::string prompt = SYSTEM_PROMPT + "\n\nToday's date: " + dateBuf + ".";

    GeminiResponse resp = m_gemini.Ask(question, prompt,
                                        m_interactionId, m_interactionModel,
                                        tools);

    if (!IsGenerationCurrent(gen)) return;

    bool initialFallback = resp.fallbackUsed;
    int fcRound = 0;
    FunctionHandler::EntityData pendingEntity;
    while (resp.RequiresAction() && fcRound < MAX_FC_ROUNDS && IsGenerationCurrent(gen)) {
        fcRound++;

        std::string toolNames;
        for (auto& fc : resp.functionCalls) {
            if (!toolNames.empty()) toolNames += ", ";
            toolNames += fc.name;
        }
        SetToolStatus("Araniyor: " + toolNames);

        std::vector<std::pair<std::string, std::pair<std::string, std::string>>> results;
        for (auto& fc : resp.functionCalls) {
            if (!IsGenerationCurrent(gen)) return;

            FunctionCall call;
            call.id = fc.id;
            call.name = fc.name;
            call.arguments = fc.arguments;
            auto t0 = std::chrono::steady_clock::now();
            auto result = m_funcHandler->Handle(call,
                [this, gen] { return !IsGenerationCurrent(gen); });
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count();

            auto logger = m_gemini.GetLogger();
            if (logger) {
                std::string preview = result.resultText.substr(0, 120);
                logger("FC " + fc.name + " " + fc.arguments.dump() + " -> ("
                       + std::to_string(result.resultText.size()) + "B, "
                       + std::to_string(elapsedMs) + "ms) " + preview);
            }

            auto links = ExtractChatLinks(result.resultText);
            { std::lock_guard<std::mutex> lk(m_snapshotMutex); m_verifiedLinks.insert(links.begin(), links.end()); }

            auto entityData = m_funcHandler->TakeEntityData();
            if (!entityData.coords.empty()) {
                pendingEntity = std::move(entityData);
            }

            results.push_back({result.callId, {result.name, result.resultText}});
        }

        if (!IsGenerationCurrent(gen)) return;

        SetToolStatus("Cevap hazirlaniyor...");

        resp = m_gemini.SendFunctionResults(
            resp.activeModel, resp.interactionId, results, tools, prompt);

        if (!IsGenerationCurrent(gen)) return;

        if (!resp.ok && !resp.RequiresAction() && resp.statusCode == 429) {
            SetToolStatus("");
            resp = m_gemini.Ask(question, prompt, "", "", tools);
            if (!IsGenerationCurrent(gen)) return;
            continue;
        }
    }

    SetToolStatus("");

    if (resp.RequiresAction()) {
        resp.ok = false;
        resp.error = "Bu soru icin kesin veri bulunamadi. Lutfen farkli sekilde sormayı deneyin.";
    }

    if (!resp.ok && resp.error.empty())
        resp.error = "Cevap alinamadi (status: " + resp.status + ")";

    std::lock_guard<std::mutex> lk(m_snapshotMutex);
    m_snapshot.busy = false;
    m_snapshot.generation = gen;

    if (resp.ok) {
        auto logger = m_gemini.GetLogger();
        std::string safeText = StripUnverifiedChatLinks(resp.text, m_verifiedLinks, logger);

        m_snapshot.messages.push_back({ChatMessage::Assistant, safeText});
        m_snapshot.activeModel = resp.activeModel;
        m_snapshot.fallbackUsed = initialFallback || resp.fallbackUsed;
        m_interactionId = resp.interactionId;
        m_interactionModel = resp.activeModel;
        m_snapshot.error.clear();
        if (!pendingEntity.coords.empty()) {
            m_entityCoords = std::move(pendingEntity.coords);
            m_mapRects = std::move(pendingEntity.rects);
            ++m_entitySeq;
            m_snapshot.entityCoords = m_entityCoords;
            m_snapshot.mapRects = m_mapRects;
            m_snapshot.entitySeq = m_entitySeq;
        }
    } else {
        std::string errText;
        if (resp.statusCode == 429) {
            errText = resp.error;
        } else if (resp.statusCode == 401 || resp.statusCode == 403) {
            errText = "API key gecersiz veya yetkisiz.";
        } else if (resp.statusCode == 400 || resp.statusCode == 404) {
            errText = resp.error.empty() ? "Istek hatasi." : resp.error;
            m_interactionId.clear();
            m_interactionModel.clear();
        } else if (!resp.error.empty()) {
            errText = resp.error;
        } else {
            errText = "Bilinmeyen hata (HTTP " + std::to_string(resp.statusCode) + ")";
        }
        m_snapshot.messages.push_back({ChatMessage::System, errText});
        m_snapshot.error = errText;
    }
}
