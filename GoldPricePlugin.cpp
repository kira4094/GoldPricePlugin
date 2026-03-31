#include "pch.h"
#include "GoldPricePlugin.h"
#include <wininet.h>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "wininet.lib")

// 辅助函数：窄字符串转宽字符串（指定代码页）
static std::wstring NarrowToWide(const std::string& narrow, UINT codePage)
{
    if (narrow.empty()) return L"";
    int len = MultiByteToWideChar(codePage, 0, narrow.c_str(), -1, NULL, 0);
    if (len <= 0) return L"";
    std::vector<wchar_t> buf(len);
    MultiByteToWideChar(codePage, 0, narrow.c_str(), -1, buf.data(), len);
    return std::wstring(buf.data());
}

static std::string HttpGetWithReferer(const wchar_t* url, const wchar_t* referer = nullptr)
{
    HINTERNET hInternet = InternetOpen(L"Mozilla/5.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) return "";

    URL_COMPONENTS urlComp = { 0 };
    urlComp.dwStructSize = sizeof(urlComp);
    wchar_t hostName[256] = { 0 };
    wchar_t urlPath[1024] = { 0 };
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = _countof(hostName);
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = _countof(urlPath);
    if (!InternetCrackUrl(url, 0, 0, &urlComp))
    {
        InternetCloseHandle(hInternet);
        return "";
    }

    HINTERNET hConnect = InternetConnect(hInternet, hostName, urlComp.nPort, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect)
    {
        InternetCloseHandle(hInternet);
        return "";
    }

    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? INTERNET_FLAG_SECURE : 0;
    HINTERNET hRequest = HttpOpenRequest(hConnect, L"GET", urlPath, NULL, NULL, NULL, flags, 0);
    if (!hRequest)
    {
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        return "";
    }

    std::wstring headers = L"Accept: */*\r\nUser-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36\r\n";
    if (referer)
    {
        headers += L"Referer: ";
        headers += referer;
        headers += L"\r\n";
    }

    BOOL bResult = HttpSendRequest(hRequest, headers.c_str(), headers.size(), NULL, 0);
    if (!bResult)
    {
        InternetCloseHandle(hRequest);
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        return "";
    }

    std::string response;
    char buffer[4096];
    DWORD bytesRead = 0;
    while (InternetReadFile(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0)
    {
        response.append(buffer, bytesRead);
    }

    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    return response;
}

// 解析新浪伦敦金（新字段结构）
static bool ParseSinaGold(const std::string& data, double& price, double& yesterday, std::wstring& changeAmt, std::wstring& changeRate)
{
    size_t start = data.find('"');
    if (start == std::string::npos) return false;
    size_t end = data.find('"', start + 1);
    if (end == std::string::npos) return false;
    std::string content = data.substr(start + 1, end - start - 1);

    std::vector<std::string> fields;
    std::stringstream ss(content);
    std::string field;
    while (std::getline(ss, field, ',')) {
        fields.push_back(field);
    }
    if (fields.size() < 8) return false;

    price = atof(fields[0].c_str());
    yesterday = atof(fields[7].c_str());

    double diff = price - yesterday;
    double rate = 0.0;
    if (yesterday != 0.0) {
        rate = diff / yesterday * 100.0;
    }

    wchar_t amtBuf[32];
    swprintf(amtBuf, 32, L"%+.2f", diff);
    changeAmt = amtBuf;

    wchar_t rateBuf[32];
    swprintf(rateBuf, 32, L"%+.2f%%", rate);
    changeRate = rateBuf;

    return true;
}

// 解析京东积存金（UTF-8 编码）
static bool ParseJdGold(const std::string& data, double& price, double& yesterday, std::wstring& changeAmt, std::wstring& changeRate)
{
    size_t pos = data.find("\"price\":\"");
    if (pos == std::string::npos) return false;
    pos += 9;
    size_t end = data.find('"', pos);
    if (end == std::string::npos) return false;
    price = atof(data.substr(pos, end - pos).c_str());

    pos = data.find("\"yesterdayPrice\":\"");
    if (pos == std::string::npos) return false;
    pos += 18;
    end = data.find('"', pos);
    yesterday = atof(data.substr(pos, end - pos).c_str());

    std::string amtStr, rateStr;
    pos = data.find("\"upAndDownAmt\":\"");
    if (pos != std::string::npos) {
        pos += 16;
        end = data.find('"', pos);
        amtStr = data.substr(pos, end - pos);
    }
    pos = data.find("\"upAndDownRate\":\"");
    if (pos != std::string::npos) {
        pos += 17;
        end = data.find('"', pos);
        rateStr = data.substr(pos, end - pos);
    }

    changeAmt = NarrowToWide(amtStr, CP_UTF8);
    changeRate = NarrowToWide(rateStr, CP_UTF8);
    return true;
}

CGoldPricePlugin& CGoldPricePlugin::GetInstance()
{
    static CGoldPricePlugin instance;
    return instance;
}

CGoldPricePlugin::CGoldPricePlugin()
    : m_stop(false),
    m_londonPrice(0.0), m_londonYesterday(0.0), m_londonUp(false),
    m_zheShangPrice(0.0), m_zheShangYesterday(0.0), m_zheShangUp(false)
{
    InitializeCriticalSection(&m_cs);
    m_pLondonItem = new CLondonGoldItem(*this);
    m_pZheShangItem = new CZheShangGoldItem(*this);
    m_worker = std::thread(&CGoldPricePlugin::WorkerThread, this);
}

CGoldPricePlugin::~CGoldPricePlugin()
{
    m_stop = true;
    if (m_worker.joinable())
        m_worker.join();
    DeleteCriticalSection(&m_cs);
    delete m_pLondonItem;
    delete m_pZheShangItem;
}

IPluginItem* CGoldPricePlugin::GetItem(int index)
{
    switch (index)
    {
    case 0: return m_pLondonItem;
    case 1: return m_pZheShangItem;
    default: return nullptr;
    }
}

const wchar_t* CGoldPricePlugin::GetInfo(PluginInfoIndex index)
{
    static wchar_t name[] = L"Gold Price Plugin";
    static wchar_t desc[] = L"Real-time London Gold & ZheShang Gold prices";
    static wchar_t author[] = L"kira4094";
    static wchar_t version[] = L"0.0.1";
    switch (index)
    {
    case TMI_NAME: return name;
    case TMI_DESCRIPTION: return desc;
    case TMI_AUTHOR: return author;
    case TMI_VERSION: return version;
    default: return L"";
    }
}

void CGoldPricePlugin::WorkerThread()
{
    while (!m_stop)
    {
        FetchPrices();
        Sleep(1000);
    }
}

void CGoldPricePlugin::FetchPrices()
{
    std::string londonData = HttpGetWithReferer(L"https://hq.sinajs.cn/list=hf_XAU", L"https://finance.sina.com.cn/futures/quotes/XAU.shtml");
    double londonPrice = 0.0, londonYesterday = 0.0;
    std::wstring londonAmt, londonRate;
    bool londonOk = ParseSinaGold(londonData, londonPrice, londonYesterday, londonAmt, londonRate);

    std::string jdData = HttpGetWithReferer(L"https://api.jdjygold.com/gw2/generic/jrm/h5/m/stdLatestPrice?productSku=1961543816", nullptr);
    double jdPrice = 0.0, jdYesterday = 0.0;
    std::wstring jdAmt, jdRate;
    bool jdOk = ParseJdGold(jdData, jdPrice, jdYesterday, jdAmt, jdRate);

    EnterCriticalSection(&m_cs);
    if (londonOk && londonPrice > 0)
    {
        if (m_londonYesterday > 0)
            m_londonUp = (londonPrice >= londonYesterday);
        m_londonPrice = londonPrice;
        m_londonYesterday = londonYesterday;
        m_londonChangeAmt = londonAmt;
        m_londonChangeRate = londonRate;
    }
    if (jdOk && jdPrice > 0)
    {
        if (m_zheShangYesterday > 0)
            m_zheShangUp = (jdPrice >= jdYesterday);
        m_zheShangPrice = jdPrice;
        m_zheShangYesterday = jdYesterday;
        m_zheShangChangeAmt = jdAmt;
        m_zheShangChangeRate = jdRate;
    }
    LeaveCriticalSection(&m_cs);
}

int CGoldPricePlugin::CLondonGoldItem::GetItemWidthEx(void* hDC) const
{
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    SelectObject((HDC)hDC, hFont);
    SIZE sz;
    GetTextExtentPoint32W((HDC)hDC, L"8888.88 ↑+888.88(+8.88%)", 28, &sz);
    return sz.cx;
}

void CGoldPricePlugin::CLondonGoldItem::DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode)
{
    double price;
    bool isUp;
    std::wstring changeAmt, changeRate;

    EnterCriticalSection(&m_plugin.m_cs);
    price = m_plugin.m_londonPrice;
    isUp = m_plugin.m_londonUp;
    changeAmt = m_plugin.m_londonChangeAmt;
    changeRate = m_plugin.m_londonChangeRate;
    LeaveCriticalSection(&m_plugin.m_cs);

    wchar_t text[256];
    wchar_t arrow = isUp ? L'\u2191' : L'\u2193';
    swprintf(text, 256, L"%.2f %c%s(%s)", price, arrow, changeAmt.c_str(), changeRate.c_str());

    SetBkMode((HDC)hDC, TRANSPARENT);
    COLORREF color;
    if (isUp) {
        color = dark_mode ? RGB(255, 128, 128) : RGB(255, 0, 0);
    }
    else {
        color = dark_mode ? RGB(128, 255, 128) : RGB(0, 255, 0);
    }
    SetTextColor((HDC)hDC, color);

    RECT rect = { x, y, x + w, y + h };
    DrawTextW((HDC)hDC, text, -1, &rect, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_NOCLIP);
}

int CGoldPricePlugin::CZheShangGoldItem::GetItemWidthEx(void* hDC) const
{
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    SelectObject((HDC)hDC, hFont);
    SIZE sz;
    GetTextExtentPoint32W((HDC)hDC, L"888.88 ↓-1.94(-0.19%)", 25, &sz);
    return sz.cx;
}

void CGoldPricePlugin::CZheShangGoldItem::DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode)
{
    double price;
    bool isUp;
    std::wstring changeAmt, changeRate;

    EnterCriticalSection(&m_plugin.m_cs);
    price = m_plugin.m_zheShangPrice;
    isUp = m_plugin.m_zheShangUp;
    changeAmt = m_plugin.m_zheShangChangeAmt;
    changeRate = m_plugin.m_zheShangChangeRate;
    LeaveCriticalSection(&m_plugin.m_cs);

    wchar_t text[256];
    wchar_t arrow = isUp ? L'\u2191' : L'\u2193';
    swprintf(text, 256, L"%.2f %c%s(%s)", price, arrow, changeAmt.c_str(), changeRate.c_str());

    SetBkMode((HDC)hDC, TRANSPARENT);
    COLORREF color;
    if (isUp) {
        color = dark_mode ? RGB(255, 128, 128) : RGB(255, 0, 0);
    }
    else {
        color = dark_mode ? RGB(128, 255, 128) : RGB(0, 255, 0);
    }
    SetTextColor((HDC)hDC, color);

    RECT rect = { x, y, x + w, y + h };
    DrawTextW((HDC)hDC, text, -1, &rect, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_NOCLIP);
}

extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance()
{
    return &CGoldPricePlugin::GetInstance();
}