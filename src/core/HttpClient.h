#pragma once
#include <string>
#include <optional>

struct HttpResponse {
    int statusCode = 0;
    std::string body;
};

// Why the last request produced no HTTP response at all. WinHTTP reports this only through
// GetLastError(); until v0.3.12 the code was dropped, so a machine that could not reach the
// server showed "Baglanti hatasi" and an empty Nexus log.
struct HttpFailure {
    std::string stage;          // WinHttpOpen / WinHttpConnect / WinHttpOpenRequest / WinHttpSendRequest / WinHttpReceiveResponse
    unsigned long code = 0;     // GetLastError(), captured before any handle is closed
    long elapsedMs = 0;         // ~0 = rejected locally (bad header, no DNS); ~timeout = something drops the packets
    bool Any() const { return !stage.empty(); }
};

class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    std::optional<HttpResponse> Get(const std::string& host, const std::string& path,
                                     int timeoutMs = 10000);

    std::optional<HttpResponse> Post(const std::string& host, const std::string& path,
                                      const std::string& body,
                                      const std::string& extraHeaders = "",
                                      int timeoutMs = 45000);

    // Set when Get/Post returned nullopt; cleared by the next request that gets any response.
    const HttpFailure& LastFailure() const { return m_lastFailure; }
    std::string LastFailureText() const;    // "WinHttpSendRequest: 12007 - The server name ... (14 ms)"

    static std::string DescribeError(unsigned long code);   // Windows' own text for the code (UTF-8), "" if none
    static std::string TurkishHint(unsigned long code);     // one-line user hint for the common codes, "" otherwise
    static std::string Diagnostics();                       // OS version + WinHTTP/user proxy configuration; no secrets

private:
    std::optional<HttpResponse> Request(const wchar_t* method, const std::string& host,
                                        const std::string& path, const std::wstring& headers,
                                        const std::string& body, int timeoutMs);
    void Fail(const char* stage, unsigned long code, long elapsedMs);

    void* m_hSession = nullptr; // HINTERNET
    HttpFailure m_lastFailure;
};
