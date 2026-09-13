#include "HttpClient.h"
#include <Windows.h>
#include <winhttp.h>
#include <chrono>
#pragma comment(lib, "winhttp.lib")

namespace {

long ElapsedMs(std::chrono::steady_clock::time_point t0) {
    return static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count());
}

std::string WideToUtf8(const wchar_t* w) {
    if (!w || !*w) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return "";
    std::string out(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), len, nullptr, nullptr);
    return out;
}

std::string ProxyField(LPWSTR s) {
    return (s && *s) ? WideToUtf8(s) : std::string("-");
}

} // namespace

HttpClient::HttpClient()
{
    m_hSession = WinHttpOpen(L"GW2-Claymore-Asistan/1.0",
                             WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                             WINHTTP_NO_PROXY_NAME,
                             WINHTTP_NO_PROXY_BYPASS, 0);
    if (!m_hSession)
        Fail("WinHttpOpen", GetLastError(), 0);
}

HttpClient::~HttpClient()
{
    if (m_hSession)
        WinHttpCloseHandle(m_hSession);
}

void HttpClient::Fail(const char* stage, unsigned long code, long elapsedMs)
{
    m_lastFailure.stage = stage;
    m_lastFailure.code = code;
    m_lastFailure.elapsedMs = elapsedMs;
}

std::optional<HttpResponse> HttpClient::Get(const std::string& host,
                                             const std::string& path,
                                             int timeoutMs)
{
    return Request(L"GET", host, path, L"", "", timeoutMs);
}

std::optional<HttpResponse> HttpClient::Post(const std::string& host,
                                              const std::string& path,
                                              const std::string& body,
                                              const std::string& extraHeaders,
                                              int timeoutMs)
{
    std::wstring wHeaders = L"Content-Type: application/json; charset=utf-8\r\n";
    if (!extraHeaders.empty()) {
        // Byte-to-wchar widening: header values are ASCII by contract. A non-ASCII byte here
        // (a stray NBSP or Turkish letter in the API key) makes WinHttpSendRequest fail with
        // ERROR_INVALID_PARAMETER (87) before anything leaves the machine - see TurkishHint(87).
        std::wstring wExtra(extraHeaders.begin(), extraHeaders.end());
        wHeaders += wExtra;
        if (wHeaders.back() != L'\n') wHeaders += L"\r\n";
    }
    return Request(L"POST", host, path, wHeaders, body, timeoutMs);
}

std::optional<HttpResponse> HttpClient::Request(const wchar_t* method, const std::string& host,
                                                const std::string& path, const std::wstring& headers,
                                                const std::string& body, int timeoutMs)
{
    if (!m_hSession)
        return std::nullopt;            // m_lastFailure still describes the WinHttpOpen failure

    m_lastFailure = HttpFailure{};
    auto t0 = std::chrono::steady_clock::now();

    std::wstring wHost(host.begin(), host.end());
    std::wstring wPath(path.begin(), path.end());

    HINTERNET hConnect = WinHttpConnect(m_hSession, wHost.c_str(),
                                        INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        Fail("WinHttpConnect", GetLastError(), ElapsedMs(t0));
        return std::nullopt;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, method, wPath.c_str(),
                                            nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        DWORD err = GetLastError();     // before CloseHandle, which may overwrite it
        WinHttpCloseHandle(hConnect);
        Fail("WinHttpOpenRequest", err, ElapsedMs(t0));
        return std::nullopt;
    }

    WinHttpSetTimeouts(hRequest, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    BOOL bResult = WinHttpSendRequest(hRequest,
                                       headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                                       headers.empty() ? 0 : static_cast<DWORD>(headers.length()),
                                       body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(),
                                       static_cast<DWORD>(body.size()),
                                       static_cast<DWORD>(body.size()),
                                       0);
    if (!bResult) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        Fail("WinHttpSendRequest", err, ElapsedMs(t0));
        return std::nullopt;
    }

    bResult = WinHttpReceiveResponse(hRequest, nullptr);
    if (!bResult) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        Fail("WinHttpReceiveResponse", err, ElapsedMs(t0));
        return std::nullopt;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX,
                        &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);

    std::string responseBody;
    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
        std::string chunk(bytesAvailable, '\0');
        DWORD bytesRead = 0;
        WinHttpReadData(hRequest, chunk.data(), bytesAvailable, &bytesRead);
        responseBody.append(chunk.data(), bytesRead);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);

    return HttpResponse{ static_cast<int>(statusCode), std::move(responseBody) };
}

std::string HttpClient::LastFailureText() const
{
    if (!m_lastFailure.Any()) return "";
    std::string s = m_lastFailure.stage + ": " + std::to_string(m_lastFailure.code);
    std::string desc = DescribeError(m_lastFailure.code);
    if (!desc.empty()) s += " - " + desc;
    s += " (" + std::to_string(m_lastFailure.elapsedMs) + " ms)";
    return s;
}

std::string HttpClient::DescribeError(unsigned long code)
{
    wchar_t buf[512] = {};
    DWORD flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK;
    HMODULE winhttp = GetModuleHandleW(L"winhttp.dll");   // the 12xxx texts live in winhttp.dll
    if (winhttp) flags |= FORMAT_MESSAGE_FROM_HMODULE;

    // English first so a log from a Turkish Windows stays searchable; fall back to the system language.
    DWORD n = FormatMessageW(flags, winhttp, code, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
                             buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])), nullptr);
    if (n == 0)
        n = FormatMessageW(flags, winhttp, code, 0, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])), nullptr);
    if (n == 0) return "";

    std::string s = WideToUtf8(buf);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
    return s;
}

std::string HttpClient::TurkishHint(unsigned long code)
{
    switch (code) {
    case ERROR_INVALID_PARAMETER:                 // 87: WinHTTP rejected our own header - the API key is the only variable in it
        return "istek basligi gecersiz - API anahtarinda ASCII disi veya gorunmez bir karakter olabilir; "
               "Ayarlardan anahtari silip yeniden yapistirin";
    case ERROR_WINHTTP_TIMEOUT:                   // 12002
        return "zaman asimi - guvenlik duvari veya ag istegi dusuruyor olabilir";
    case ERROR_WINHTTP_INVALID_URL:               // 12005
    case ERROR_WINHTTP_UNRECOGNIZED_SCHEME:       // 12006
        return "gecersiz adres";
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:         // 12007
        return "sunucu adi cozulemedi (DNS) - internet baglantisini, DNS ayarini veya DNS filtresini kontrol edin";
    case ERROR_WINHTTP_LOGIN_FAILURE:             // 12015
        return "proxy kimlik dogrulamasi basarisiz";
    case ERROR_WINHTTP_OPERATION_CANCELLED:       // 12017
        return "istek iptal edildi";
    case ERROR_WINHTTP_CANNOT_CONNECT:            // 12029
        return "sunucuya baglanilamadi - guvenlik duvari, proxy veya erisim engeli";
    case ERROR_WINHTTP_CONNECTION_ERROR:          // 12030
        return "baglanti kesildi";
    case ERROR_WINHTTP_SECURE_CERT_DATE_INVALID:  // 12037
        return "sunucu sertifikasi tarih hatasi - bilgisayarin saat/tarih ayarini kontrol edin";
    case ERROR_WINHTTP_SECURE_CERT_CN_INVALID:    // 12038
    case ERROR_WINHTTP_SECURE_INVALID_CA:         // 12045
    case ERROR_WINHTTP_SECURE_INVALID_CERT:       // 12169
    case ERROR_WINHTTP_SECURE_CERT_REVOKED:       // 12170
        return "sunucu sertifikasi guvenilmiyor - antivirus/proxy HTTPS taramasi araya giriyor olabilir";
    case ERROR_WINHTTP_SECURE_CERT_REV_FAILED:    // 12057
        return "sertifika iptal kontrolu yapilamadi - ag kisitli olabilir";
    case ERROR_WINHTTP_SECURE_CHANNEL_ERROR:      // 12157
    case ERROR_WINHTTP_SECURE_FAILURE:            // 12175
        return "guvenli baglanti (TLS) kurulamadi - antivirus HTTPS taramasi veya eski Windows/TLS ayari";
    case ERROR_WINHTTP_AUTODETECTION_FAILED:      // 12180
        return "proxy otomatik algilama basarisiz";
    default:
        return "";
    }
}

std::string HttpClient::Diagnostics()
{
    std::string out = "Ortam: ";

    // RtlGetVersion tells the truth; GetVersionEx lies to un-manifested callers.
    typedef LONG (WINAPI *RtlGetVersionFn)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW ver = {};
    ver.dwOSVersionInfoSize = sizeof(ver);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    auto rtlGetVersion = ntdll ? reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
    if (rtlGetVersion && rtlGetVersion(&ver) == 0) {
        out += "Windows " + std::to_string(ver.dwMajorVersion) + "." + std::to_string(ver.dwMinorVersion)
             + "." + std::to_string(ver.dwBuildNumber);
    } else {
        out += "Windows (surum okunamadi)";
    }

    // The proxy WinHTTP actually uses under WINHTTP_ACCESS_TYPE_DEFAULT_PROXY (netsh winhttp).
    WINHTTP_PROXY_INFO def = {};
    if (WinHttpGetDefaultProxyConfiguration(&def)) {
        if (def.dwAccessType == WINHTTP_ACCESS_TYPE_NAMED_PROXY)
            out += " | WinHTTP proxy: " + ProxyField(def.lpszProxy) + " (bypass: " + ProxyField(def.lpszProxyBypass) + ")";
        else
            out += " | WinHTTP proxy: dogrudan";
        if (def.lpszProxy) GlobalFree(def.lpszProxy);
        if (def.lpszProxyBypass) GlobalFree(def.lpszProxyBypass);
    } else {
        out += " | WinHTTP proxy: okunamadi (" + std::to_string(GetLastError()) + ")";
    }

    // The user's (browser/system) proxy, which DEFAULT_PROXY ignores - if this is set and the
    // WinHTTP one is "dogrudan", the addon goes direct while the browser goes through the proxy.
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG ie = {};
    if (WinHttpGetIEProxyConfigForCurrentUser(&ie)) {
        out += " | kullanici proxy: otomatik algila=" + std::string(ie.fAutoDetect ? "1" : "0")
             + ", PAC=" + ProxyField(ie.lpszAutoConfigUrl)
             + ", proxy=" + ProxyField(ie.lpszProxy)
             + ", bypass=" + ProxyField(ie.lpszProxyBypass);
        if (ie.lpszAutoConfigUrl) GlobalFree(ie.lpszAutoConfigUrl);
        if (ie.lpszProxy) GlobalFree(ie.lpszProxy);
        if (ie.lpszProxyBypass) GlobalFree(ie.lpszProxyBypass);
    } else {
        out += " | kullanici proxy: okunamadi (" + std::to_string(GetLastError()) + ")";
    }

    return out;
}
