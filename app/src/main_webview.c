// main.c — MemePanel v0.7：WebView2 壳架构
// C 核心保留：托盘/热键/单例/索引(store)/缩略图(wicthumb)/CF_HDROP 粘贴/去重/后缀修正。
// UI 层迁移至 HTML（assets/panel.html 嵌资源，图片走虚拟主机映射本地目录，JS↔C 走 WebMessage）。
// 原 GDI+/ULW 自绘实现存档于 main_gdi_legacy.c。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <objbase.h>
#include "util.h"
#include "store.h"
#include "wicthumb.h"
#include "webview.h"

// SDK 26100 把 DROPFILES 挪出 shellapi.h；自带等价定义（成员全 4 字节，ABI 一致）
#include <pshpack1.h>
typedef struct { DWORD pFiles; POINT pt; BOOL fNC; BOOL fWide; } MPDROPFILES;
#include <poppack.h>

#define APP_NAME     L"MemePanel"
#define WM_TRAY      (WM_APP + 1)
#define HOTKEY_ID    1
#define IDM_SHOW     2001
#define IDM_MGR      2200
#define IDM_RESCAN   2002
#define IDM_EXIT     2003
#define FADE_TIMER   2

// ---- 无 CRT 字符串助手 ----
static const char* str_find(const char* s, char c)
{
    while (*s && *s != c) s++;
    return (*s == c) ? s : NULL;
}
static int str_int(const char* s)
{
    int v = 0, neg = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}

// ---- 全局状态 ----
static Store     g_store;
static HINSTANCE g_hInst;
static HWND      g_hwnd, g_lastTarget;
static HICON     g_icon;
static NOTIFYICONDATAW g_nid;
static int       g_dpi = 96;
static BOOL      g_mgr;
static int       g_alpha = 255;
static wchar_t   g_status[128];

static int S(int v) { return MulDiv(v, g_dpi, 96); }

// TEMP DIAG
static const wchar_t xNL[2] = { 13, 10 };
static void dbglog(const wchar_t* s)
{
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(NULL, p, MAX_PATH);
    wchar_t* e = p + wlen(p);
    while (e > p && e[-1] != 92) e--;          // 92 = backslash
    *e = 0;
    lstrcatW(p, L"dg.txt");
    HANDLE f = CreateFileW(p, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD got;
    WriteFile(f, s, wlen(s) * 2, &got, NULL);
    WriteFile(f, xNL, 4, &got, NULL);
    CloseHandle(f);
}

// ============================================================
// 数据推送（C -> JS）。名字/标签进 JS 字符串需转义 ' \ 换行
// ============================================================
static void esc_add(wchar_t* dst, size_t cap, const wchar_t* src)
{
    size_t n = wlen(dst);
    for (; *src && n + 3 < cap; src++) {
        if (*src == L'\'' || *src == L'\\') dst[n++] = L'\\';
        dst[n++] = *src;
    }
    dst[n] = 0;
}

static void push_all(void)
{
    if (!wv_ready()) return;
    size_t cap = 64 + (u64)g_store.count * (MAX_PATH * 2 + 64);
    wchar_t* buf = (wchar_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, cap * 2);
    if (!buf) return;

    // items: "ei|name|gif|maskLo|maskHi\n..."
    wchar_t* w = buf;
    lstrcpynW(w, L"MP.items('", 1024); w += wlen(w);
    for (int i = 0; i < g_store.count; i++) {
        Entry* e = &g_store.entries[i];
        u32 lo = (u32)(e->tagmask & 0xFFFFFFFFull);
        u32 hi = (u32)(e->tagmask >> 32);
        u32 nl = wlen(e->name);
        BOOL gif = nl > 4 && e->name[nl - 4] == L'.' &&
                   (e->name[nl - 3] == L'g' || e->name[nl - 3] == L'G') &&
                   (e->name[nl - 2] == L'i' || e->name[nl - 2] == L'I') &&
                   (e->name[nl - 1] == L'f' || e->name[nl - 1] == L'F');
        wchar_t nm[MAX_PATH * 2];
        nm[0] = 0;
        esc_add(nm, MAX_PATH * 2, e->name);
        wnsprintfW(w, (int)(cap - (u64)(w - buf)), L"%d|%s|%c|%u|%u\\n",
                   i, nm, gif ? L'1' : L'0', lo, hi);
        w += wlen(w);
    }
    lstrcpynW(w, L"')", 8);
    wv_exec(buf);

    // tags
    w = buf;
    lstrcpynW(w, L"MP.tags('", 1024); w += wlen(w);
    for (int i = 0; i < 64; i++) {
        if (!g_store.tagNames[i]) continue;
        wchar_t nm[MAX_PATH * 2];
        nm[0] = 0;
        esc_add(nm, MAX_PATH * 2, g_store.tagNames[i]);
        wnsprintfW(w, (int)(cap - (u64)(w - buf)), L"%d|%s\\n", i, nm);
        w += wlen(w);
    }
    lstrcpynW(w, L"')", 8);
    wv_exec(buf);

    // recents（按 used 降序前 7）
    {
        int idx[7], n = 0;
        for (int i = 0; i < g_store.count && n < 7; i++) {
            if (!g_store.entries[i].used) continue;
            int k = n++;
            idx[k] = i;
            while (k > 0 && g_store.entries[idx[k - 1]].used < g_store.entries[idx[k]].used) {
                int t = idx[k - 1]; idx[k - 1] = idx[k]; idx[k] = t; k--;
            }
        }
        wchar_t js[160], rs[96];
        int off = 0;
        for (int i = 0; i < n; i++)
            off += wnsprintfW(rs + off, 96 - off, i ? L",%d" : L"%d", idx[i]);
        wnsprintfW(js, 160, L"MP.recents('%s')", n ? rs : L"");
        wv_exec(js);
    }
    // 模式 + 状态
    {
        wchar_t js[300], esc[160];
        esc[0] = 0;
        esc_add(esc, 160, g_status);
        wnsprintfW(js, 300, L"MP.mode(%d);MP.status('%s')", g_mgr ? 1 : 0, esc);
        wv_exec(js);
    }
    HeapFree(GetProcessHeap(), 0, buf);
}

static void set_status(const wchar_t* s)
{
    lstrcpynW(g_status, s, 128);
    if (!wv_ready()) return;
    wchar_t js[200], esc[160];
    esc[0] = 0;
    esc_add(esc, 160, s);
    wnsprintfW(js, 200, L"MP.status('%s')", esc);
    wv_exec(js);
}

// ============================================================
// 显示 / 隐藏（普通窗口 + SLWA 淡入；Win11 DWM acrylic/圆角/暗色）
// ============================================================
static void apply_dwm_glass(HWND hwnd)
{
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm) return;
    HRESULT (WINAPI *pSWA)(HWND, DWORD, LPCVOID, DWORD) =
        (HRESULT (WINAPI*)(HWND, DWORD, LPCVOID, DWORD))GetProcAddress(dwm, "DwmSetWindowAttribute");
    if (pSWA) {
        int corner = 2;   // DWMWCP_ROUND
        pSWA(hwnd, 33, &corner, sizeof corner);
        int backdrop = 3; // DWMSBT_TRANSIENTWINDOW（acrylic）
        pSWA(hwnd, 38, &backdrop, sizeof backdrop);
        BOOL dark = TRUE;
        pSWA(hwnd, 20, &dark, sizeof dark);
    }
}

static void mgr_resize(HWND hwnd, BOOL mgr)
{
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    int w, h;
    if (mgr) { w = sw * 60 / 100; h = sh * 66 / 100; }
    else {
        w = sw * 46 / 100; h = sh * 54 / 100;
        if (w < S(700)) w = S(700);
        if (h < S(460)) h = S(460);
    }
    SetWindowPos(hwnd, HWND_TOPMOST, (sw - w) / 2, (sh - h) / (mgr ? 6 : 5), w, h,
                 SWP_NOACTIVATE);
}

static void panel_show(HWND hwnd)
{
    HWND fg = GetForegroundWindow();
    if (fg != hwnd) g_lastTarget = fg;
    g_alpha = 90;
    SetLayeredWindowAttributes(hwnd, 0, (BYTE)g_alpha, LWA_ALPHA);
    mgr_resize(hwnd, g_mgr);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_SHOWWINDOW | SWP_NOMOVE | SWP_NOSIZE);
    wv_set_visible(TRUE);
    wv_notify_moved();
    SetForegroundWindow(hwnd);
    SetTimer(hwnd, FADE_TIMER, 10, NULL);
}

static void panel_hide(HWND hwnd)
{
    KillTimer(hwnd, FADE_TIMER);
    ShowWindow(hwnd, SW_HIDE);
    wv_set_visible(FALSE);
}

// ============================================================
// 粘贴链路（CF_HDROP 保留 GIF 动画）
// ============================================================
static void paste_entry(HWND hwnd, int ei)
{
    Entry* e = &g_store.entries[ei];
    e->used = GetTickCount64();
    store_save(&g_store);
    push_all();

    wchar_t path[MAX_PATH];
    store_build_path(&g_store, e, path, MAX_PATH);
    panel_hide(hwnd);

    BOOL ok = FALSE;
    for (int t = 0; t < 4 && !ok; t++) {
        if (!OpenClipboard(hwnd)) { Sleep(15); continue; }
        if (EmptyClipboard()) {
            u32 bytes = (u32)(sizeof(MPDROPFILES) + ((u64)wlen(path) + 2) * 2);
            HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (hg) {
                MPDROPFILES* df = (MPDROPFILES*)GlobalLock(hg);
                if (df) {
                    df->pFiles = sizeof(MPDROPFILES);
                    df->fNC = FALSE; df->fWide = TRUE;
                    memcpy(df + 1, path, ((u64)wlen(path) + 1) * 2);
                    ((wchar_t*)(df + 1))[wlen(path) + 1] = 0;
                    GlobalUnlock(hg);
                    ok = SetClipboardData(CF_HDROP, hg) != NULL;
                }
                if (!ok) GlobalFree(hg);
            }
        }
        CloseClipboard();
        if (!ok) Sleep(15);
    }

    if (g_lastTarget && IsWindow(g_lastTarget)) {
        SetForegroundWindow(g_lastTarget);
        Sleep(40);
        INPUT in[4];
        memset(in, 0, sizeof in);
        in[0].type = INPUT_KEYBOARD; in[0].ki.wVk = VK_CONTROL;
        in[1].type = INPUT_KEYBOARD; in[1].ki.wVk = 'V';
        in[2].type = INPUT_KEYBOARD; in[2].ki.wVk = 'V';         in[2].ki.dwFlags = KEYEVENTF_KEYUP;
        in[3].type = INPUT_KEYBOARD; in[3].ki.wVk = VK_CONTROL;  in[3].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(4, in, sizeof(INPUT));
    }
}

// ============================================================
// 管理操作（重命名/删除/去重/修后缀/批量打标）
// ============================================================
static BOOL mgr_rename_entry(int ei, const wchar_t* newName)
{
    if (!newName[0] || wlen(newName) > 200) return FALSE;
    for (const wchar_t* p = newName; *p; p++)
        if (*p < 32 || *p == L'\\' || *p == L'/' || *p == L':' || *p == L'*' ||
            *p == L'?' || *p == L'"' || *p == L'<' || *p == L'>' || *p == L'|')
            return FALSE;
    wchar_t oldp[MAX_PATH], newp[MAX_PATH], oldt[MAX_PATH], newt[MAX_PATH];
    store_build_path(&g_store, &g_store.entries[ei], oldp, MAX_PATH);
    wnsprintfW(newp, MAX_PATH, L"%s\\%s", g_store.memesDir, newName);
    if (GetFileAttributesW(newp) != INVALID_FILE_ATTRIBUTES) return FALSE;
    if (!MoveFileW(oldp, newp)) return FALSE;
    wnsprintfW(oldt, MAX_PATH, L"%s\\%s.jpg", g_store.thumbsDir, g_store.entries[ei].name);
    wnsprintfW(newt, MAX_PATH, L"%s\\%s.jpg", g_store.thumbsDir, newName);
    if (!MoveFileW(oldt, newt)) DeleteFileW(oldt);
    wchar_t* d = wdup(newName);
    if (d) { HeapFree(GetProcessHeap(), 0, g_store.entries[ei].name); g_store.entries[ei].name = d; }
    return TRUE;
}

static void mgr_delete(HWND hwnd, const char* eisCsv)
{
    int n = 1;
    for (const char* p = eisCsv; *p; p++) if (*p == ',') n++;
    wchar_t* buf = (wchar_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                       ((u64)n * MAX_PATH + 2) * 2);
    if (!buf) return;
    wchar_t* w = buf;
    int cnt = 0;
    const char* p = eisCsv;
    while (*p && cnt < n) {
        int ei = str_int(p);
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
        if (ei < 0 || ei >= g_store.count) continue;
        store_build_path(&g_store, &g_store.entries[ei], w, MAX_PATH);
        w += wlen(w) + 1;
        cnt++;
    }
    *w = 0;
    SHFILEOPSTRUCTW fo;
    memset(&fo, 0, sizeof fo);
    fo.hwnd = hwnd;
    fo.wFunc = FO_DELETE;
    fo.pFrom = buf;
    fo.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
    int r = SHFileOperationW(&fo);
    HeapFree(GetProcessHeap(), 0, buf);
    store_scan(&g_store);
    if (r == 0) wnsprintfW(g_status, 128, L"已删除 %d 张（在回收站）", cnt);
    else lstrcpynW(g_status, L"删除失败", 128);
    push_all();
}

static u64 fnv_file(const wchar_t* path)
{
    u64 h = 1469598103934665603ull;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    u8* buf = (u8*)HeapAlloc(GetProcessHeap(), 0, 16384);
    if (!buf) { CloseHandle(f); return 0; }
    DWORD got;
    while (ReadFile(f, buf, 16384, &got, NULL) && got)
        for (DWORD i = 0; i < got; i++) { h ^= buf[i]; h *= 1099511628211ull; }
    HeapFree(GetProcessHeap(), 0, buf);
    CloseHandle(f);
    return h;
}

static void mgr_dedup(HWND hwnd)
{
    int n = g_store.count;
    if (n < 2) { set_status(L"库太小，无需去重"); return; }
    HANDLE hp = GetProcessHeap();
    int* head = (int*)HeapAlloc(hp, 0, 4096 * 4);
    if (head) memset(head, 0xFF, 4096 * 4);   // 链尾哨兵必须 -1（零初始化=死循环，血泪）
    int* next = (int*)HeapAlloc(hp, HEAP_ZERO_MEMORY, (u64)n * 4);
    u8* done  = (u8*)HeapAlloc(hp, HEAP_ZERO_MEMORY, n);
    int* grp  = (int*)HeapAlloc(hp, 0, (u64)n * 4);
    int groups = 0, marked = 0;
    wchar_t* selCsv = (wchar_t*)HeapAlloc(hp, HEAP_ZERO_MEMORY, 8192);
    size_t selOff = 0;
    if (!selCsv) { if (head) HeapFree(hp, 0, head); if (next) HeapFree(hp, 0, next); if (done) HeapFree(hp, 0, done); if (grp) HeapFree(hp, 0, grp); return; }
    if (head && next && done && grp) {
        for (int i = 0; i < n; i++) {
            int b = (int)(g_store.entries[i].size & 4095);
            next[i] = head[b]; head[b] = i;
        }
        wchar_t p[MAX_PATH];
        for (int i = 0; i < n; i++) {
            if (done[i]) continue;
            done[i] = TRUE;
            int gn = 0;
            for (int j = head[g_store.entries[i].size & 4095]; j >= 0; j = next[j]) {
                if (j == i || done[j]) continue;
                if (g_store.entries[j].size == g_store.entries[i].size) grp[gn++] = j;
            }
            if (!gn) continue;                    // size 唯一，零 IO
            store_build_path(&g_store, &g_store.entries[i], p, MAX_PATH);
            u64 hi = fnv_file(p);
            BOOL dup = FALSE;
            for (int k = 0; k < gn; k++) {
                int j = grp[k];
                store_build_path(&g_store, &g_store.entries[j], p, MAX_PATH);
                if (fnv_file(p) == hi) {
                    done[j] = TRUE; dup = TRUE; marked++;
                    if (selOff + 12 < 4096)
                        selOff += wnsprintfW(selCsv + selOff, (int)(4096 - selOff),
                                             marked > 1 ? L",%d" : L"%d", j);
                }
            }
            if (dup) groups++;
        }
    }
    if (selCsv) HeapFree(hp, 0, selCsv);
    if (head) HeapFree(hp, 0, head);
    if (next) HeapFree(hp, 0, next);
    if (done) HeapFree(hp, 0, done);
    if (grp)  HeapFree(hp, 0, grp);
    if (groups) {
        wchar_t st[96];
        wnsprintfW(st, 96, L"%d 组重复，已选中 %d 张，确认后删除", groups, marked);
        set_status(st);
        wchar_t* js = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, 16384);
        if (js) {
            wnsprintfW(js, 8192, L"MP.keep('%s')", selCsv);
            wv_exec(js);
            HeapFree(GetProcessHeap(), 0, js);
        }
    } else set_status(L"未发现重复");
    (void)hwnd;
}

static int wendswith_ci(const wchar_t* s, const wchar_t* suf)
{
    u32 n = wlen(s), m = wlen(suf);
    if (n < m) return 0;
    for (u32 i = 0; i < m; i++) {
        wchar_t a = s[n - m + i], b = suf[i];
        if (a >= L'A' && a <= L'Z') a += 32;
        if (b >= L'A' && b <= L'Z') b += 32;
        if (a != b) return 0;
    }
    return 1;
}

static void mgr_fixext(HWND hwnd)
{
    int fixed = 0;
    wchar_t p[MAX_PATH], nn[MAX_PATH];
    for (int i = 0; i < g_store.count; i++) {
        const wchar_t* name = g_store.entries[i].name;
        store_build_path(&g_store, &g_store.entries[i], p, MAX_PATH);
        HANDLE f = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (f == INVALID_HANDLE_VALUE) continue;
        u8 m[12];
        DWORD got = 0;
        ReadFile(f, m, 12, &got, NULL);
        CloseHandle(f);
        if (got < 12) continue;
        const wchar_t* real = NULL;
        if (m[0]=='G' && m[1]=='I' && m[2]=='F' && m[3]=='8') real = L".gif";
        else if (m[0]==0x89 && m[1]=='P' && m[2]=='N' && m[3]=='G') real = L".png";
        else if (m[0]==0xFF && m[1]==0xD8 && m[2]==0xFF) real = L".jpg";
        else if (m[0]=='R' && m[1]=='I' && m[2]=='F' && m[3]=='F' &&
                 m[8]=='W' && m[9]=='E' && m[10]=='B' && m[11]=='P') real = L".webp";
        if (!real || wendswith_ci(name, real)) continue;
        u32 stem = wlen(name);
        while (stem > 0 && name[stem - 1] != L'.') stem--;
        if (stem) stem--;               // stem = 不含点主名长度（off-by-one 血泪）
        if (stem + wlen(real) + 1 > MAX_PATH) continue;
        lstrcpynW(nn, name, (int)stem + 1);
        lstrcpynW(nn + stem, real, MAX_PATH - (int)stem);
        if (mgr_rename_entry(i, nn)) fixed++;
    }
    if (fixed) {
        store_save(&g_store);
        store_scan(&g_store);
        wchar_t st[96];
        wnsprintfW(st, 96, L"修正了 %d 个后缀", fixed);
        set_status(st);
    } else set_status(L"后缀都正常");
    push_all();
    (void)hwnd;
}

static void mgr_batch_tag(int tagId, const char* eisCsv)
{
    if (tagId < 0 || tagId >= 64) return;
    u64 bit = (u64)1 << tagId;
    int firstHas = -1, cnt = 0;
    const char* p = eisCsv;
    while (*p) {
        int ei = str_int(p);
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
        if (ei < 0 || ei >= g_store.count) continue;
        if (firstHas < 0) firstHas = (g_store.entries[ei].tagmask & bit) ? 1 : 0;
        if (firstHas) g_store.entries[ei].tagmask &= ~bit;
        else g_store.entries[ei].tagmask |= bit;
        cnt++;
    }
    if (cnt) {
        store_save(&g_store);
        wchar_t st[96];
        wnsprintfW(st, 96, L"已%s标签「%s」× %d",
                   firstHas ? L"移除" : L"添加", g_store.tagNames[tagId], cnt);
        set_status(st);
        push_all();
    }
}

// ============================================================
// 桥接的拖放导入（HTML drop -> base64 -> 落盘）
// ============================================================
static void import_dropfile(const char* urlName, const char* b64)
{
    wchar_t name[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, urlName, -1, name, MAX_PATH);
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    u32 blen = lstrlenA(b64);
    u32 cap = blen / 4 * 3 + 4;
    u8* out = (u8*)HeapAlloc(GetProcessHeap(), 0, cap);
    if (!out) return;
    u32 o = 0;
    for (u32 i = 0; i + 3 < blen; i += 4) {
        int v[4]; int n = 0;
        for (int k = 0; k < 4; k++) {
            const char* q = T;
            v[k] = 0;
            if (b64[i + k] != '=') {
                for (; *q; q++) if (*q == b64[i + k]) { v[k] = (int)(q - T); n++; break; }
            }
        }
        if (o + 3 > cap) break;
        out[o++] = (u8)((v[0] << 2) | (v[1] >> 4));
        out[o++] = (u8)((v[1] << 4) | (v[2] >> 2));
        out[o++] = (u8)((v[2] << 6) | v[3]);
        if (n < 4) { o -= (u32)(4 - n); break; }
    }
    wchar_t stem[MAX_PATH], ext[40], dst[MAX_PATH];
    lstrcpynW(stem, name, MAX_PATH);
    ext[0] = 0;
    wchar_t* dot = stem + wlen(stem);
    while (dot > stem && dot[-1] != L'.') dot--;
    if (dot > stem) { lstrcpynW(ext, dot - 1, 40); dot[-1] = 0; }
    for (int k = 0; k < 1000; k++) {
        if (k == 0) wnsprintfW(dst, MAX_PATH, L"%s\\%s", g_store.memesDir, name);
        else        wnsprintfW(dst, MAX_PATH, L"%s\\%s (%d)%s", g_store.memesDir, stem, k, ext);
        if (GetFileAttributesW(dst) == INVALID_FILE_ATTRIBUTES) break;
    }
    HANDLE f = CreateFileW(dst, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD got;
        WriteFile(f, out, o, &got, NULL);
        CloseHandle(f);
    }
    HeapFree(GetProcessHeap(), 0, out);
}

// ============================================================
// JS -> C 消息分发（"cmd|arg1|..."，名称经 encodeURIComponent 所以无 | 冲突）
// ============================================================
static void on_web_msg(const wchar_t* msg)
{
    if (msg && wlen(msg) < 100) dbglog(msg);
    char* m = (char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 16384);
    if (!m) return;
    int ml = WideCharToMultiByte(CP_UTF8, 0, msg, -1, m, 16383, NULL, NULL);
    if (ml <= 0) { HeapFree(GetProcessHeap(), 0, m); return; }
    m[ml] = 0;
    const char* bar = str_find(m, '|');
    char cmd[32] = { 0 };
    if (bar) {
        memcpy(cmd, m, (u64)(bar - m) < 31 ? (u64)(bar - m) : 31);
        bar++;
    } else lstrcpynA(cmd, m, 31);

    if (!lstrcmpA(cmd, "webready")) push_all();
    else if (!lstrcmpA(cmd, "paste")) paste_entry(g_hwnd, str_int(bar));
    else if (!lstrcmpA(cmd, "hide")) panel_hide(g_hwnd);
    else if (!lstrcmpA(cmd, "modemgr")) {
        g_mgr = (str_int(bar) != 0);
        mgr_resize(g_hwnd, g_mgr);
        wv_notify_moved();
        RECT rc; GetClientRect(g_hwnd, &rc);
        wv_resize(rc.right, rc.bottom);
        push_all();
    }
    else if (!lstrcmpA(cmd, "ren")) {
        const char* p2 = str_find(bar, '|');
        if (p2) {
            wchar_t nn[MAX_PATH];
            char a = *(char*)p2; (void)a;
            MultiByteToWideChar(CP_UTF8, 0, p2 + 1, -1, nn, MAX_PATH);
            if (mgr_rename_entry(str_int(bar), nn)) {
                store_save(&g_store);
                store_scan(&g_store);
                set_status(L"已重命名");
            } else set_status(L"重命名失败（重名/非法字符/被占用）");
            push_all();
        }
    }
    else if (!lstrcmpA(cmd, "del")) mgr_delete(g_hwnd, bar);
    else if (!lstrcmpA(cmd, "dedup")) mgr_dedup(g_hwnd);
    else if (!lstrcmpA(cmd, "fixext")) mgr_fixext(g_hwnd);
    else if (!lstrcmpA(cmd, "tag")) {
        const char* p2 = str_find(bar, '|');
        if (p2) mgr_batch_tag(str_int(bar), p2 + 1);
    }
    else if (!lstrcmpA(cmd, "dropfile")) {
        const char* p2 = str_find(bar, '|');
        if (p2) { import_dropfile(bar, p2 + 1); store_scan(&g_store); push_all(); }
    }
    HeapFree(GetProcessHeap(), 0, m);
}

// ============================================================
// 窗口过程
// ============================================================
static LRESULT CALLBACK PanelProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_ERASEBKGND: {
        HDC dc = (HDC)wParam;
        RECT rc; GetClientRect(hwnd, &rc);
        HBRUSH br = CreateSolidBrush(RGB(24, 27, 38));   // Win10 无 acrylic 时的深色兜底
        FillRect(dc, &rc, br);
        DeleteObject(br);
        return 1;
    }
    case WM_SIZE:
        wv_resize(LOWORD(lParam), HIWORD(lParam));
        wv_exec(L"if(window.MP&&MP.render)MP.render()");
        return 0;
    case WM_MOVE:
        wv_notify_moved();
        return 0;
    case WM_TIMER:
        if (wParam == FADE_TIMER) {
            g_alpha += 52;
            if (g_alpha >= 255) { g_alpha = 255; KillTimer(hwnd, FADE_TIMER); }
            SetLayeredWindowAttributes(hwnd, 0, (BYTE)g_alpha, LWA_ALPHA);
            return 0;
        }
        break;
    case WM_HOTKEY:
        if (wParam == HOTKEY_ID) {
            if (IsWindowVisible(hwnd)) panel_hide(hwnd);
            else panel_show(hwnd);
        }
        return 0;
    case WM_ACTIVATEAPP:
        if (!wParam && IsWindowVisible(hwnd)) panel_hide(hwnd);
        return 0;
    case WM_TRAY:
        if (lParam == WM_LBUTTONUP) {
            panel_show(hwnd);
        } else if (lParam == WM_RBUTTONUP) {
            SetForegroundWindow(hwnd);
            POINT pt; GetCursorPos(&pt);
            HMENU m = CreatePopupMenu();
            AppendMenuW(m, MF_STRING, IDM_SHOW,   L"打开面板");
            AppendMenuW(m, MF_STRING, IDM_MGR,    L"管理");
            AppendMenuW(m, MF_STRING, IDM_RESCAN, L"重新扫描");
            AppendMenuW(m, MF_SEPARATOR, 0, NULL);
            AppendMenuW(m, MF_STRING, IDM_EXIT,   L"退出");
            int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                     pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(m);
            if (cmd == IDM_SHOW) panel_show(hwnd);
            else if (cmd == IDM_MGR) { g_mgr = TRUE; panel_show(hwnd); push_all(); }
            else if (cmd == IDM_RESCAN) {
                store_scan(&g_store);
                push_all();
            }
            else if (cmd == IDM_EXIT) DestroyWindow(hwnd);
        }
        return 0;
    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        UnregisterHotKey(hwnd, HOTKEY_ID);
        store_save(&g_store);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================
// 图标（沿用琉璃版 GDI 绘制）
// ============================================================
static void rr_gdi(HDC dc, LONG l, LONG t, LONG r, LONG b, int rad, HBRUSH fill, HPEN pen)
{
    HGDIOBJ ob = fill ? SelectObject(dc, fill) : SelectObject(dc, GetStockObject(NULL_BRUSH));
    HGDIOBJ op = pen ? SelectObject(dc, pen) : SelectObject(dc, GetStockObject(NULL_PEN));
    RoundRect(dc, l, t, r + 1, b + 1, rad, rad);
    if (fill) SelectObject(dc, ob);
    if (pen)  SelectObject(dc, op);
}

static HICON make_app_icon(void)
{
    HDC sdc = GetDC(NULL);
    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = 64;
    bi.bmiHeader.biHeight = -64;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* colorBits = NULL, *maskBits = NULL;
    HBITMAP color = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, &colorBits, NULL, 0);
    BITMAPINFO mi;
    memset(&mi, 0, sizeof mi);
    mi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    mi.bmiHeader.biWidth = 64;
    mi.bmiHeader.biHeight = 64;
    mi.bmiHeader.biPlanes = 1;
    mi.bmiHeader.biBitCount = 1;
    mi.bmiHeader.biCompression = BI_RGB;
    HBITMAP mask = CreateDIBSection(sdc, &mi, DIB_RGB_COLORS, &maskBits, NULL, 0);
    HICON ic = NULL;
    if (color && mask && colorBits && maskBits) {
        memset(colorBits, 0, 64 * 64 * 4);
        memset(maskBits, 0, 64 * 64 / 8);
        u32* px = (u32*)colorBits;
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 64; x++) {
                float t = (x + y) / 126.0f;
                u8 r = (u8)(46 + (24 - 46) * t), g = (u8)(52 + (27 - 52) * t), b = (u8)(78 + (40 - 78) * t);
                px[y * 64 + x] = 0xFF000000u | (r << 16) | (g << 8) | b;
            }
        HDC mdc = CreateCompatibleDC(sdc);
        HGDIOBJ old = SelectObject(mdc, color);
        HBRUSH glass = CreateSolidBrush(RGB(28, 31, 44));
        HBRUSH glass2 = CreateSolidBrush(RGB(46, 52, 78));
        HPEN edge = CreatePen(PS_SOLID, 3, RGB(122, 162, 255));
        rr_gdi(mdc, 7, 7, 57, 57, 16, glass2, edge);
        rr_gdi(mdc, 11, 11, 53, 53, 13, glass, NULL);
        HPEN wp = CreatePen(PS_SOLID, 4, RGB(255, 255, 255));
        HBRUSH wb = CreateSolidBrush(RGB(255, 255, 255));
        HGDIOBJ op = SelectObject(mdc, wp);
        HGDIOBJ ob = SelectObject(mdc, wb);
        int e = 2;
        Ellipse(mdc, 24 - e, 25 - e, 26 + e, 27 + e);
        Ellipse(mdc, 38 - e, 25 - e, 40 + e, 27 + e);
        SelectObject(mdc, GetStockObject(NULL_BRUSH));
        Arc(mdc, 22, 30, 42, 46, 25, 38, 39, 38);
        SelectObject(mdc, ob);
        SelectObject(mdc, op);
        SelectObject(mdc, old);
        DeleteDC(mdc);
        DeleteObject(glass); DeleteObject(glass2); DeleteObject(edge);
        DeleteObject(wb); DeleteObject(wp);
        for (int i = 0; i < 64 * 64; i++) px[i] |= 0xFF000000u;
        ICONINFO ii;
        ii.fIcon = TRUE; ii.xHotspot = ii.yHotspot = 0;
        ii.hbmMask = mask; ii.hbmColor = color;
        ic = CreateIconIndirect(&ii);
    }
    if (color) DeleteObject(color);
    if (mask) DeleteObject(mask);
    ReleaseDC(NULL, sdc);
    return ic;
}

// ============================================================
// 入口
// ============================================================
void entry(void)
{
    g_hInst = GetModuleHandleW(NULL);
    HDC zdc = GetDC(NULL);
    g_dpi = GetDeviceCaps(zdc, LOGPIXELSX);
    ReleaseDC(NULL, zdc);

    HANDLE mx = CreateMutexW(NULL, TRUE, L"MemePanel_SingleInstance");
    if (mx && GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(NULL, L"MemePanel 已在运行（见系统托盘）。", APP_NAME, MB_ICONINFORMATION);
        ExitProcess(0);
    }

    wic_init();
    store_init(&g_store);
    store_scan(&g_store);

    g_icon = make_app_icon();
    if (!g_icon) g_icon = LoadIconW(NULL, IDI_APPLICATION);

    WNDCLASSW wc;
    memset(&wc, 0, sizeof wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = PanelProc;
    wc.hInstance = g_hInst;
    wc.hIcon = g_icon;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"MemePanelWnd";
    if (!RegisterClassW(&wc)) ExitProcess(1);

    g_hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
                             L"MemePanelWnd", APP_NAME, WS_POPUP,
                             0, 0, S(700), S(460), NULL, NULL, g_hInst, NULL);
    apply_dwm_glass(g_hwnd);
    SetLayeredWindowAttributes(g_hwnd, 0, 255, LWA_ALPHA);

    memset(&g_nid, 0, sizeof g_nid);
    g_nid.cbSize = sizeof g_nid;
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = g_icon;
    lstrcpynW(g_nid.szTip, L"MemePanel — Ctrl+Shift+. 呼出", 128);
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    if (!RegisterHotKey(g_hwnd, HOTKEY_ID,
                        MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, VK_OEM_PERIOD)) {
        lstrcpynW(g_nid.szInfoTitle, APP_NAME, 64);
        lstrcpynW(g_nid.szInfo, L"快捷键注册失败（可能被占用），可点托盘图标打开。", 256);
        g_nid.uFlags = NIF_INFO;
        Shell_NotifyIconW(NIM_MODIFY, &g_nid);
        g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    }

    // WebView2：HTML 从资源（开发时若存在 assets/panel.html 优先读文件，便于热改）
    char* html = NULL;
    DWORD htmlLen = 0;
    wchar_t devPath[MAX_PATH];
    GetModuleFileNameW(NULL, devPath, MAX_PATH);
    wchar_t* s = devPath + wlen(devPath);
    while (s > devPath && s[-1] != L'\\') s--;
    *s = 0;
    lstrcatW(devPath, L"..\\assets\\panel.html");
    HANDLE df = CreateFileW(devPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (df != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER li;
        if (GetFileSizeEx(df, &li) && li.QuadPart > 0 && li.QuadPart < (4 << 20)) {
            html = (char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (u64)li.QuadPart + 1);
            if (html) ReadFile(df, html, (DWORD)li.QuadPart, &htmlLen, NULL);
        }
        CloseHandle(df);
    }
    if (!html) {
        HRSRC r = FindResourceW(NULL, L"PANELHTML", RT_RCDATA);
        if (r) {
            HGLOBAL g = LoadResource(NULL, r);
            if (g) {
                html = (char*)LockResource(g);
                htmlLen = SizeofResource(NULL, r);
            }
        }
    }

    wchar_t ud[MAX_PATH];
    GetModuleFileNameW(NULL, ud, MAX_PATH);
    s = ud + wlen(ud);
    while (s > ud && s[-1] != L'\\') s--;
    *s = 0;
    lstrcatW(ud, L"wv2data");
    dbglog(L"pre-wv");
    if (!wv_init(g_hwnd, ud, on_web_msg)) {
        MessageBoxW(NULL, L"WebView2 初始化失败（需要 exe 旁的 WebView2Loader.dll 与 Edge 运行时）。",
                    APP_NAME, MB_ICONERROR);
        ExitProcess(1);
    }
    dbglog(L"wv-init-ok");
    if (html) wv_navigate(html, (int)htmlLen);
    dbglog(L"nav-done");

    if (wcontains_ci(GetCommandLineW(), L"-mgr")) g_mgr = TRUE;
    if (g_mgr || wcontains_ci(GetCommandLineW(), L"-show"))
        panel_show(g_hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    ExitProcess(0);
}
