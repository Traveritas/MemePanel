// wvhello.cpp - control experiment: standard CRT WebView2 with full logging
#include <windows.h>
#include <stdio.h>
#include "../sdk/WebView2.h"

static FILE* g_log;
static void L(const char* s) { if (g_log) { fputs(s, g_log); fputc(10, g_log); fflush(g_log); } }
static void LH(const char* tag, HRESULT hr) {
    if (!g_log) return;
    fprintf(g_log, "%s hr=%lx\n", tag, (unsigned long)hr); fflush(g_log);
}

ICoreWebView2Environment* g_env;
ICoreWebView2Controller* g_ctl;
ICoreWebView2* g_web;
HWND g_hwndMain;

struct NavH : ICoreWebView2NavigationCompletedEventHandler {
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() { return 1; }
    ULONG STDMETHODCALLTYPE Release() { return 1; }
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* e) {
        BOOL ok = FALSE; e->get_IsSuccess(&ok);
        COREWEBVIEW2_WEB_ERROR_STATUS err = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
        e->get_WebErrorStatus(&err);
        char b[96]; sprintf(b, "nav-complete ok=%d err=%d", ok, (int)err); L(b);
        return S_OK;
    }
};
static NavH g_navH;

struct CtlH : ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() { return 1; }
    ULONG STDMETHODCALLTYPE Release() { return 1; }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, ICoreWebView2Controller* ctl) {
        LH("ctl", hr);
        if (FAILED(hr) || !ctl) return S_OK;
        g_ctl = ctl;
        ctl->get_CoreWebView2(&g_web);
        g_web->add_NavigationCompleted(&g_navH, NULL);
        RECT rc; GetClientRect(g_hwndMain, &rc);
        ctl->put_Bounds(rc);
        LH("nav", g_web->NavigateToString(L"<html><body style='background:#112233;color:#fff;font-size:34px'>HELLO WEBVIEW2</body></html>"));
        return S_OK;
    }
};
static CtlH g_ctlH;

struct EnvH : ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() { return 1; }
    ULONG STDMETHODCALLTYPE Release() { return 1; }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, ICoreWebView2Environment* env) {
        LH("env", hr);
        if (FAILED(hr) || !env) return S_OK;
        g_env = env;
        LH("ctl-create", env->CreateCoreWebView2Controller(g_hwndMain, &g_ctlH));
        return S_OK;
    }
};
static EnvH g_envH;

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) PostQuitMessage(0);
    if (m == WM_SIZE && g_ctl) { RECT rc; GetClientRect(h, &rc); g_ctl->put_Bounds(rc); }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int) {
    g_log = fopen("wvh3.log", "a");
    L("start");
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.lpszClassName = L"WvHello";
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    RegisterClassW(&wc);
    g_hwndMain = CreateWindowExW(0, L"WvHello", L"wvhello", WS_OVERLAPPEDWINDOW,
                                 100, 100, 900, 600, NULL, NULL, hi, NULL);
    ShowWindow(g_hwndMain, SW_SHOW);
    LH("create-env", CreateCoreWebView2EnvironmentWithOptions(NULL, L"C:/Users/Traveritas/AppData/Local/Temp/wvtest3", NULL, &g_envH));
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return 0;
}
