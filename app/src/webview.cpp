// webview.cpp — WebView2 桥（C++ 但无 CRT：不 new/delete，回调对象用静态实例）
// 环境/控制器异步创建链 -> 透明背景 + 虚拟主机(memes.local/thumbs.local) + WebMessage 桥
#include <windows.h>
#include "webview.h"
#include "../sdk/WebView2.h"

// WebView2Loader 动态加载（dll 随 exe 旁置）
typedef HRESULT (WINAPI *PFN_CreateEnv)(PCWSTR browserFolder, PCWSTR userDataFolder,
    ICoreWebView2EnvironmentOptions*, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
static PFN_CreateEnv pCreateEnv;

static ICoreWebView2Environment*      g_env;
static ICoreWebView2Controller*       g_ctl;
static ICoreWebView2*                 g_web;
static HWND     g_parent;

// TEMP DIAG
static const wchar_t xCRLF[2] = { 13, 10 };
static void wvlog(const wchar_t* s)
{
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(NULL, p, MAX_PATH);
    wchar_t* e = p + lstrlenW(p);
    while (e > p && e[-1] != 92) e--;          // 92 = backslash
    *e = 0;
    lstrcatW(p, L"dg.txt");
    HANDLE f = CreateFileW(p, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD got;
    WriteFile(f, s, lstrlenW(s) * 2, &got, NULL);
    WriteFile(f, xCRLF, 4, &got, NULL);
    CloseHandle(f);
}

static WEB_MSG_CB g_msgCb;
static wchar_t g_udDir[MAX_PATH];
static BOOL     g_ready;
static char*    g_pendingHtml;        // 创建完成前的待导航 HTML（HeapAlloc）
static int      g_pendingLen;

// ---- 事件 handler：静态对象，AddRef/Release 计数恒 1（不释放）----
// 标准合规 QI：至少识别 IUnknown；失败必须置 *ppv = NULL（否则调用方误用垃圾指针→崩）
#define WV_QI \
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) { \
        static const GUID IUNK = { 0, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } }; \
        if (!ppv) return E_POINTER; \
        *ppv = NULL; \
        if (!memcmp_(&riid, &IUNK, 16)) { *ppv = this; AddRef(); return S_OK; } \
        return E_NOINTERFACE; \
    }

struct ScriptHandler : public ICoreWebView2ExecuteScriptCompletedHandler {
    WV_QI
    ULONG STDMETHODCALLTYPE AddRef() { return 2; }
    ULONG STDMETHODCALLTYPE Release() { return 1; }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT, PCWSTR) { return S_OK; }
};
static ScriptHandler g_scriptH;

// ---- WebMessage 接收 ----
struct MsgHandler : public ICoreWebView2WebMessageReceivedEventHandler {
    WV_QI
    ULONG STDMETHODCALLTYPE AddRef() { return 2; }
    ULONG STDMETHODCALLTYPE Release() { return 1; }
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* e)
    {
        LPWSTR js = NULL;
        if (SUCCEEDED(e->TryGetWebMessageAsString(&js)) && js) {   // 字符串消息（非 JSON 包装）
            if (g_msgCb) g_msgCb(js);
            CoTaskMemFree(js);
            return S_OK;
        }
        if (SUCCEEDED(e->get_WebMessageAsJson(&js)) && js) {
            if (g_msgCb) g_msgCb(js);
            CoTaskMemFree(js);
        }
        return S_OK;
    }
};
static MsgHandler g_msgH;

// 导航完成日志（TEMP）
struct NavHandler : public ICoreWebView2NavigationCompletedEventHandler {
    WV_QI
    ULONG STDMETHODCALLTYPE AddRef() { return 2; }
    ULONG STDMETHODCALLTYPE Release() { return 1; }
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* e)
    {
        BOOL ok = FALSE;
        e->get_IsSuccess(&ok);
        COREWEBVIEW2_WEB_ERROR_STATUS err = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
        e->get_WebErrorStatus(&err);
        wchar_t m[96];
        wsprintfW(m, L"nav-complete ok=%d err=%d", ok ? 1 : 0, (int)err);
        wvlog(m);
        return S_OK;
    }
};
static NavHandler g_navH;


static void wv_apply_bounds(void)
{
    if (!g_ctl || !g_parent) return;
    RECT rc;
    GetClientRect(g_parent, &rc);
    g_ctl->put_Bounds(rc);
}

// 控制器就绪：配置 + 导航
struct CtlHandler : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
    WV_QI
    ULONG STDMETHODCALLTYPE AddRef() { return 2; }
    ULONG STDMETHODCALLTYPE Release() { return 1; }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, ICoreWebView2Controller* ctl)
    {
        wvlog(L"ctl-invoke");
        if (FAILED(hr) || !ctl) { wvlog(L"ctl-failed"); return S_OK; }
        g_ctl = ctl;
        ctl->get_CoreWebView2(&g_web);
        if (!g_web) return S_OK;

        ICoreWebView2Settings* st = NULL;
        if (SUCCEEDED(g_web->get_Settings(&st)) && st) {
            st->put_AreDefaultContextMenusEnabled(FALSE);
            st->put_AreDevToolsEnabled(FALSE);
            st->put_IsZoomControlEnabled(FALSE);
            st->put_AreDefaultScriptDialogsEnabled(FALSE);
            st->Release();
        }
        // 透明背景（配合主窗口 DWM backdrop / 深色底 fallback）
        ICoreWebView2Controller2* c2 = NULL;
        if (SUCCEEDED(ctl->QueryInterface(&c2)) && c2) {
            COREWEBVIEW2_COLOR bg = { 0, 0, 0, 0 };
            c2->put_DefaultBackgroundColor(bg);
            c2->Release();
        }
        // 拖放交给 HTML drop 事件处理（document 级 preventDefault 拦截）
        ICoreWebView2Controller4* c4 = NULL;
        if (SUCCEEDED(ctl->QueryInterface(&c4)) && c4) {
            c4->put_AllowExternalDrop(TRUE);
            c4->Release();
        }

        wvlog(L"ctl-msg");
                {
            HRESULT h1 = g_web->add_WebMessageReceived(&g_msgH, NULL);
            HRESULT h2 = g_web->add_NavigationCompleted(&g_navH, NULL);
            wchar_t m2[96]; wsprintfW(m2, L"add-events msg=%lx nav=%lx", (unsigned long)h1, (unsigned long)h2); wvlog(m2);
        }

        // 虚拟主机：本地目录映射（img src=http://memes.local/xxx）
        ICoreWebView2_3* w3 = NULL;
        if (SUCCEEDED(g_web->QueryInterface(&w3)) && w3) {
            wchar_t dir[MAX_PATH], tdir[MAX_PATH];
            GetModuleFileNameW(NULL, dir, MAX_PATH);
            wchar_t* s = dir + lstrlenW(dir);
            while (s > dir && s[-1] != L'\\') s--;
            *s = 0;
            lstrcpyW(tdir, dir);
            lstrcatW(tdir, L"memes");
            w3->SetVirtualHostNameToFolderMapping(L"memes.local", tdir,
                COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
            lstrcpyW(tdir, dir);
            lstrcatW(tdir, L"thumbs");
            w3->SetVirtualHostNameToFolderMapping(L"thumbs.local", tdir,
                COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
            w3->Release();
        }
        wvlog(L"ctl-vhost");
                wv_apply_bounds();
        ctl->put_IsVisible(TRUE);

        if (g_pendingHtml) {
            // UTF-8 -> UTF-16（NavigateToString 要宽字符）
            int wlen = MultiByteToWideChar(CP_UTF8, 0, g_pendingHtml, g_pendingLen, NULL, 0);
            LPWSTR w = (LPWSTR)CoTaskMemAlloc((wlen + 1) * 2);
            if (w) {
                MultiByteToWideChar(CP_UTF8, 0, g_pendingHtml, g_pendingLen, w, wlen);
                w[wlen] = 0;
                (void)w;
                HRESULT hrn = g_web->Navigate(L"about:blank"); // TEMP DIAG
                wchar_t mn[64]; wsprintfW(mn, L"nav2str hr=%lx", (unsigned long)hrn); wvlog(mn);
                CoTaskMemFree(w);
            }
            HeapFree(GetProcessHeap(), 0, g_pendingHtml);
            g_pendingHtml = NULL;
        }
        wvlog(L"ctl-ready");
                g_ready = TRUE;
        if (g_msgCb) g_msgCb(L"{\"cmd\":\"__wvready\"}");
        g_web->ExecuteScript(L"try{window.chrome.webview.postMessage('probe|'+typeof window.MP+'|'+document.querySelectorAll('.cell').length)}catch(e){window.chrome.webview.postMessage('probe|ERR '+e.message)}", &g_scriptH);
        return S_OK;
    }
};
static CtlHandler g_ctlH;

// 环境就绪：创建控制器
struct EnvHandler : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
    WV_QI
    ULONG STDMETHODCALLTYPE AddRef() { return 2; }
    ULONG STDMETHODCALLTYPE Release() { return 1; }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, ICoreWebView2Environment* env)
    {
        wvlog(L"env-invoke");
        if (FAILED(hr) || !env) { wvlog(L"env-failed"); return S_OK; }
        g_env = env;
        env->CreateCoreWebView2Controller(g_parent, &g_ctlH);
        return S_OK;
    }
};
static EnvHandler g_envH;

BOOL wv_init(HWND parent, const wchar_t* userDataDir, WEB_MSG_CB cb)
{
    HMODULE lib = LoadLibraryW(L"WebView2Loader.dll");
    if (!lib) return FALSE;
    pCreateEnv = (PFN_CreateEnv)GetProcAddress(lib, "CreateCoreWebView2EnvironmentWithOptions");
    if (!pCreateEnv) return FALSE;
    g_parent = parent;
    g_msgCb = cb;
    lstrcpyW(g_udDir, userDataDir);
    return SUCCEEDED(pCreateEnv(NULL, userDataDir, NULL, &g_envH));
}

void wv_navigate(const char* html, int len)
{
    if (!g_web) {
        if (g_pendingHtml) HeapFree(GetProcessHeap(), 0, g_pendingHtml);
        g_pendingHtml = (char*)HeapAlloc(GetProcessHeap(), 0, len + 1);
        if (g_pendingHtml) {
            memcpy(g_pendingHtml, html, len);
            g_pendingHtml[len] = 0;
            g_pendingLen = len;
        }
        return;
    }
    int wlen = MultiByteToWideChar(CP_UTF8, 0, html, len, NULL, 0);
    LPWSTR w = (LPWSTR)CoTaskMemAlloc((wlen + 1) * 2);
    if (!w) return;
    MultiByteToWideChar(CP_UTF8, 0, html, len, w, wlen);
    w[wlen] = 0;
    g_web->NavigateToString(w);
    CoTaskMemFree(w);
}

void wv_exec(const wchar_t* js)
{
    if (g_web) g_web->ExecuteScript(js, &g_scriptH);
}

void wv_exec_a(const char* jsUtf8)
{
    if (!g_web) return;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, jsUtf8, -1, NULL, 0);
    LPWSTR w = (LPWSTR)CoTaskMemAlloc(wlen * 2);
    if (!w) return;
    MultiByteToWideChar(CP_UTF8, 0, jsUtf8, -1, w, wlen);
    g_web->ExecuteScript(w, &g_scriptH);
    CoTaskMemFree(w);
}

void wv_resize(int cx, int cy)
{
    if (!g_ctl) return;
    RECT rc = { 0, 0, cx, cy };
    g_ctl->put_Bounds(rc);
}

void wv_set_visible(BOOL on)
{
    if (g_ctl) g_ctl->put_IsVisible(on ? TRUE : FALSE);
}

void wv_notify_moved(void)
{
    if (g_ctl) {
        ICoreWebView2Controller3* c3 = NULL;
        if (SUCCEEDED(g_ctl->QueryInterface(&c3)) && c3) {
            c3->NotifyParentWindowPositionChanged();
            c3->Release();
        } else {
            g_ctl->NotifyParentWindowPositionChanged();
        }
    }
}

BOOL wv_ready(void) { return g_ready; }
