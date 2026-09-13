#include "Worker.h"
#include <regex>
#include <set>

const std::string Worker::SYSTEM_PROMPT =
    "You are GW2-Claymore Asistan, a Guild Wars 2 (2012, ArenaNet) assistant.\n"
    "Guild Wars 1 (2005) is a DIFFERENT game. NEVER use GW1 knowledge.\n"
    "\n"
    "WHEN TO USE TOOLS:\n"
    "- Item prices or details: call gw2_item_info\n"
    "- Crafting recipes: call gw2_recipe\n"
    "- Locations, waypoints, map info: call gw2_map\n"
    "- Specific NPC, event, achievement info: call gw2_wiki\n"
    "- General advice, opinions, class/build recommendations: answer directly WITHOUT tools\n"
    "\n"
    "WAYPOINT RULE: NEVER generate [&...] codes yourself. Only use chat_link values from tool results. If no tool provides a code, say the code is not available.\n"
    "\n"
    "RESPONSE FORMAT:\n"
    "- Reply in Turkish. Keep item/NPC/map names in English.\n"
    "- Prices in gold/silver/copper (g/s/c).\n"
    "- Summarize tool results concisely in Turkish. Never show raw JSON.\n"
    "- Keep answers short.";

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
    }
    m_interactionId.clear();
    m_interactionModel.clear();
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

    auto tools = FunctionHandler::GetToolDefinitions();
    std::set<std::string> verifiedLinks;

    GeminiResponse resp = m_gemini.Ask(question, SYSTEM_PROMPT,
                                        m_interactionId, m_interactionModel,
                                        tools);

    if (!IsGenerationCurrent(gen)) return;

    int fcRound = 0;
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
            auto result = m_funcHandler->Handle(call);

            auto links = ExtractChatLinks(result.resultText);
            verifiedLinks.insert(links.begin(), links.end());

            results.push_back({result.callId, {result.name, result.resultText}});
        }

        if (!IsGenerationCurrent(gen)) return;

        SetToolStatus("Cevap hazirlaniyor...");

        resp = m_gemini.SendFunctionResults(
            resp.activeModel, resp.interactionId, results, tools, SYSTEM_PROMPT);

        if (!IsGenerationCurrent(gen)) return;

        if (!resp.ok && !resp.RequiresAction() && resp.statusCode == 429) {
            SetToolStatus("");
            resp = m_gemini.Ask(question, SYSTEM_PROMPT, "", "", tools);
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
        std::string safeText = StripUnverifiedChatLinks(resp.text, verifiedLinks, logger);

        m_snapshot.messages.push_back({ChatMessage::Assistant, safeText});
        m_snapshot.activeModel = resp.activeModel;
        m_snapshot.fallbackUsed = resp.fallbackUsed;
        m_interactionId = resp.interactionId;
        m_interactionModel = resp.activeModel;
        m_snapshot.error.clear();
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
