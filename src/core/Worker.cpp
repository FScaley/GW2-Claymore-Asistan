#include "Worker.h"

const std::string Worker::SYSTEM_PROMPT =
    "Sen GW2-Claymore Asistan'sin. Guild Wars 2 hakkinda uzmansin.\n"
    "- Ingilizce ara ve dusun, Turkce cevap ver.\n"
    "- GW2'de Turkce lokalizasyon yok: item/NPC/map isimlerini Ingilizce yaz, aciklamalari Turkce yap.\n"
    "- Waypoint kodlarini [&BxxxxBQ=] formatinda ver (varsa).\n"
    "- Fiyatlari altin/gumus/bakir (g/s/c) olarak goster.\n"
    "- Craft malzemeleri icin maliyet dagilimi goster.\n"
    "- NPC konumlari icin en yakin waypoint ve kisa yol tarifi ver.\n"
    "- Kesin olmayan bilgilerde bunu belirt.\n"
    "- Kisa ve oz cevaplar ver.\n"
    "- Item fiyati, crafting tarifi veya wiki bilgisi gerektiginde uygun tool'u kullan.\n"
    "- Tool sonuclarini kullaniciya Turkce ozet olarak sun, ham JSON gosterme.";

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

    if (!resp.ok && resp.error.empty() && resp.statusCode == 200)
        resp.error = resp.RequiresAction()
            ? "Tool dongusu limiti asildi"
            : "Beklenmeyen yanit: " + resp.status;

    std::lock_guard<std::mutex> lk(m_snapshotMutex);
    m_snapshot.busy = false;
    m_snapshot.generation = gen;

    if (resp.ok) {
        m_snapshot.messages.push_back({ChatMessage::Assistant, resp.text});
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
