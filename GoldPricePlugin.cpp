#include "pch.h"
#include "GoldPricePlugin.h"
#include "GoldPriceSource.h"
#include <memory>
#include <new>

CGoldPricePlugin& CGoldPricePlugin::GetInstance()
{
    static CGoldPricePlugin instance;
    return instance;
}

CGoldPricePlugin::CGoldPricePlugin()
    : m_londonPrice(0.0), m_londonYesterday(0.0), m_londonUp(false),
    m_zheShangPrice(0.0), m_zheShangYesterday(0.0), m_zheShangUp(false)
{
    InitializeCriticalSection(&m_cs);
    m_pLondonItem = new CLondonGoldItem(*this);
    m_pZheShangItem = new CZheShangGoldItem(*this);
}

CGoldPricePlugin::~CGoldPricePlugin()
{
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

void CGoldPricePlugin::DataRequired()
{
    QueueFetch(true);
    QueueFetch(false);
}

void CGoldPricePlugin::QueueFetch(bool london)
{
    auto& busy = london ? m_londonBusy : m_zheShangBusy;
    auto& lastStart = london ? m_londonLastStart : m_zheShangLastStart;
    if (busy.exchange(true)) return;
    const ULONGLONG now = GetTickCount64();
    if (lastStart != 0 && now - lastStart < 1000)
    {
        busy = false;
        return;
    }
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(&CGoldPricePlugin::FetchCallback), &module))
    {
        busy = false;
        return;
    }
    auto* work = new (std::nothrow) FetchWork{this, london, module};
    lastStart = now;
    if (!work || !TrySubmitThreadpoolCallback(&CGoldPricePlugin::FetchCallback, work, nullptr))
    {
        delete work;
        FreeLibrary(module);
        busy = false;
        return;
    }
}

void CALLBACK CGoldPricePlugin::FetchCallback(PTP_CALLBACK_INSTANCE instance, void* context)
{
    std::unique_ptr<FetchWork> work(static_cast<FetchWork*>(context));
    FreeLibraryWhenCallbackReturns(instance, work->module);
    CallbackMayRunLong(instance);
    CGoldPricePlugin* plugin = work->plugin;
    const bool london = work->london;
    try { plugin->FetchPrice(london); }
    catch (...) { /* A failed request keeps the last valid quote. */ }
    work.reset();
    (london ? plugin->m_londonBusy : plugin->m_zheShangBusy) = false;
}

void CGoldPricePlugin::FetchPrice(bool london)
{
    GoldPriceSource::Quote quote;
#ifdef GOLDPRICE_TESTING
    const std::string body = GoldPriceTestHttpGet(london);
#else
    const std::string body = GoldPriceSource::HttpGet(london ? GoldPriceSource::LondonUrl : GoldPriceSource::ZheShangUrl);
#endif
    if (!(london ? GoldPriceSource::ParseLondon(body, quote) : GoldPriceSource::ParseZheShang(body, quote))) return;
    EnterCriticalSection(&m_cs);
    if (london)
    {
        m_londonPrice = quote.price;
        m_londonYesterday = quote.yesterday;
        m_londonUp = quote.change >= 0;
        m_londonChangeAmt.swap(quote.changeAmount);
        m_londonChangeRate.swap(quote.changeRate);
    }
    else
    {
        m_zheShangPrice = quote.price;
        m_zheShangYesterday = quote.yesterday;
        m_zheShangUp = quote.change >= 0;
        m_zheShangChangeAmt.swap(quote.changeAmount);
        m_zheShangChangeRate.swap(quote.changeRate);
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
