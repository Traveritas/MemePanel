// main.c — MemePanel v0.6「霓虹终端 NEON-TERM」：CRT 磷光屏（06-terminal 变体）
// 架构：UpdateLayeredWindow 整窗提交 32bpp 预乘 DIB；GDI+（flat C，见 gp.h）负责全部
//       矢量/文字/位图合成（GDI 直写 DIB 会破坏 alpha 通道，故一律 GDI+）。
//       静态底图（近黑绿磷屏 + 3px 扫描线 + 边缘磷光辉光 + 双线框/四角加强）resize 时
//       预渲染一次；每帧 = memcpy 底图 + 动态件绘制 + 一次 ULW 提交。
//       无 EDIT 子窗口（layered 主窗口下不可见）：检索行自绘命令行（> 前缀 + 块状
//       caret），中文走 WM_IME_COMPOSITION。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <objbase.h>
#include <imm.h>
#include "util.h"
#include "store.h"
#include "wicthumb.h"
#include "gp.h"

// SDK 26100 把 DROPFILES 挪出 shellapi.h；自带等价定义（成员全 4 字节，ABI 一致）
#include <pshpack1.h>
typedef struct { DWORD pFiles; POINT pt; BOOL fNC; BOOL fWide; } MPDROPFILES;
#include <poppack.h>

#define APP_NAME     L"MemePanel"
#define WM_TRAY      (WM_APP + 1)
#define WM_GIFDONE   (WM_APP + 2)   // 后台 GIF 解码完成（LPARAM = GifFrames* 或 NULL）
#define HOTKEY_ID    1
#define IDM_SHOW     2001
#define IDM_RESCAN   2002
#define IDM_EXIT     2003
#define IDM_TAG_BASE 2100
#define ANIM_TIMER   2
#define CARET_TIMER  3
#define PREVIEW_TMR  4
#define GIF_TIMER    5

// ---- 「霓虹终端 NEON-TERM」tokens（BRIEF.md：CRT 磷光屏）----
#define T_PH_R   42                 // PHOSPHOR #2AFF88 磷绿主色
#define T_PH_G   255
#define T_PH_B   136
#define T_DIM_R  22                 // 暗绿 #16A852（网格线 / 弱化件）
#define T_DIM_G  168
#define T_DIM_B  82
#define T_AMB_R  255                // AMBER #FFB000（仅删除/警示/GIF，强调色只干一件事）
#define T_AMB_G  176
#define T_AMB_B  0

static u32 ARGB(int a, int r, int g, int b)
{
    return ((u32)(a & 255) << 24) | ((u32)(r & 255) << 16) | ((u32)(g & 255) << 8) | (u32)(b & 255);
}
#define C_PH(a)    ARGB(a, T_PH_R, T_PH_G, T_PH_B)   // 磷绿
#define C_DIM(a)   ARGB(a, T_DIM_R, T_DIM_G, T_DIM_B) // 暗绿
#define C_AMB(a)   ARGB(a, T_AMB_R, T_AMB_G, T_AMB_B) // 琥珀
#define C_TXT(a)   ARGB(a, 42, 255, 136)              // 文字主（磷绿 α0.92 系）
#define C_PALE(a)  ARGB(a, 208, 255, 228)             // 输入行「白绿」

// ---- 全局状态 ----
static Store     g_store;
static HINSTANCE g_hInst;
static HWND      g_hwnd, g_lastTarget;
static BOOL      g_headless;   // -shot 无头渲染：不显示窗口、无托盘/热键，直接导出合成帧 PNG
static HICON     g_icon;
static NOTIFYICONDATAW g_nid;

// 合成表面（窗口 = 面板 + 四周 S(BLEED) 阴影出血）
static HDC     g_memDC;
static HBITMAP g_dcStockBmp;    // 内存 DC 初始 1x1 stock 位图（换出判定用）
static HBITMAP g_dib;
static u32*    g_bits;          // 32bpp 预乘，直接像素访问
static int     g_winW, g_winH;
static GpBitmap*  g_gpbmp;      // g_dib 的 GDI+ 视图
static GpGraphics* g_gfx;
static u32*    g_base;          // 静态底图缓存（同尺寸，每帧 memcpy）
static u32*    g_fadeSnap;      // 淡入动画的全亮度帧快照（动画期间免全量合成）
static int     g_fade = 255;

// 质感件（一次生成）
static GpBitmap* g_glowSoft, * g_glowStrong;
static u8*   g_glowSoftBuf, * g_glowStrongBuf;

// 字体 / 字符串格式 / 笔刷（全站 Consolas，中文 fallback 微软雅黑；标题 Impact）
static GpFont* g_fText, * g_fUi, * g_fSmall, * g_fTiny, * g_fMono, * g_fTitle;
static GpStringFormat *g_sfL, * g_sfC, * g_sfR, * g_sfCF;

// 布局（窗口坐标）
static int  g_dpi = 96;
static int  g_bleed;
static RECT g_panel;
static RECT g_searchBox, g_closeRc;
static RECT g_recent[7];
static int  g_recentIdx[7], g_nRecent;
static RECT g_chipRc[MP_MAX_TAGS + 1];
static int  g_chipId[MP_MAX_TAGS + 1], g_nChips;
static RECT g_grid, g_footer, g_preview;
static RECT g_titleRc;                    // 标题条（MEME://TERMINAL / [N] ONLINE / [X]）
static int  g_chipY, g_opY;               // 标签行 / 管理操作行 y（分隔线与绘制共用）
static int  g_cell, g_gap, g_cols, g_rows;

// 数据 / 交互状态
static wchar_t g_search[128]; static int g_slen, g_caret;
static wchar_t g_comp[128];   static int g_complen;   // IME 组合串
static BOOL g_caretOn;
static u64  g_tagFilter;
static int* g_filt; static int g_nFilt, g_capFilt;
static int  g_first, g_hover = -1, g_previewIdx = -1;
static POINT g_mouse; static BOOL g_tracking;

// ---- 管理模式（v0.6：同窗口双模式，右键/托盘进入，Esc 返回速发）----
static BOOL    g_mgr;                 // 管理模式激活
static u8*     g_selBits;             // entry 索引选中位图
static int     g_nSel;
static int     g_renameEi;            // >=0：输入框处于重命名编辑（g_search 暂存新名）
static BOOL    g_delArm;              // 删除二次确认武装
static wchar_t g_status[96];          // 管理状态行（功能性反馈）
static RECT g_btnRc[4];               // 操作按钮命中：0重命名 1删除 2去重 3修后缀
#define BTN_RENAME 0
#define BTN_DELETE 1
#define BTN_DEDUP  2
#define BTN_FIXEXT 3
#define IDM_MGR    2100

static void sel_reserve(int count)
{
    int bytes = count / 8 + 1;
    if (!g_selBits) g_selBits = (u8*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes);
    else {
        u8* nb = (u8*)HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, g_selBits, bytes);
        if (nb) g_selBits = nb;
    }
}
static BOOL sel_has(int ei)  { return g_selBits && (g_selBits[ei >> 3] & (1 << (ei & 7))) != 0; }
static void sel_set(int ei, BOOL on)
{
    if (!g_selBits) return;
    u8 m = (u8)(1 << (ei & 7));
    if (on && !(g_selBits[ei >> 3] & m)) { g_selBits[ei >> 3] |= m; g_nSel++; }
    if (!on && (g_selBits[ei >> 3] & m)) { g_selBits[ei >> 3] &= (u8)~m; g_nSel--; }
}
static void sel_clear(void)
{
    if (g_selBits && g_nSel) memset(g_selBits, 0, g_store.count / 8 + 1);
    g_nSel = 0;
}

static int S(int v) { return MulDiv(v, g_dpi, 96); }

// ============================================================
// 数学：无 CRT 的 sqrt / exp（底图距离场用，仅生成期跑）
// ============================================================
static float fabsf_(float x) { return x < 0 ? -x : x; }
static float fmin_(float a, float b) { return a < b ? a : b; }
static float fmax_(float a, float b) { return a > b ? a : b; }

static float xsqrt(float x)
{
    if (x <= 0.0f) return 0.0f;
    union { float f; u32 i; } u; u.f = x;
    u.i = (u.i >> 1) + 0x1FC00000u;          // 指数减半做初值
    float r = u.f;
    for (int k = 0; k < 3; k++) r = 0.5f * (r + x / r);
    return r;
}

// 点到圆角矩形的带符号距离（内负外正；纯内部走无 sqrt 快路径）
static float sd_round(float px, float py, float cx, float cy, float hw, float hh, float r)
{
    float dx = fabsf_(px - cx) - (hw - r);
    float dy = fabsf_(py - cy) - (hh - r);
    if (dx < 0 && dy < 0) return (dx > dy ? dx : dy) - r;   // 深内部：角区约束不生效
    float ax = fmax_(dx, 0), ay = fmax_(dy, 0);
    return xsqrt(ax * ax + ay * ay) + fmin_(fmax_(dx, dy), 0) - r;
}

// ============================================================
// GDI+ 绘制助手
// ============================================================
static GpPath* rr_path(int l, int t, int r, int b, int rad)
{
    GpPath* p = NULL;
    if (GdipCreatePath(GP_FILL_WINDING, &p)) return NULL;
    int w = r - l, h = b - t;
    if (rad * 2 > w) rad = w / 2;
    if (rad * 2 > h) rad = h / 2;
    if (rad < 1) rad = 1;
    GdipAddPathArcI(p, l, t, rad * 2, rad * 2, 180, 90);
    GdipAddPathArcI(p, r - rad * 2, t, rad * 2, rad * 2, 270, 90);
    GdipAddPathArcI(p, r - rad * 2, b - rad * 2, rad * 2, rad * 2, 0, 90);
    GdipAddPathArcI(p, l, b - rad * 2, rad * 2, rad * 2, 90, 90);
    GdipClosePathFigure(p);
    return p;
}

static void gtext(const wchar_t* s, int len, GpFont* f, RECT rc, GpStringFormat* sf, u32 argb)
{
    GpSolidFill* b = NULL;
    if (GdipCreateSolidFill(argb, &b)) return;
    GpRectF rf = { (float)rc.left, (float)rc.top,
                   (float)(rc.right - rc.left), (float)(rc.bottom - rc.top) };
    GdipDrawString(g_gfx, s, len, f, &rf, sf, (GpBrush*)b);
    GdipDeleteBrush((GpBrush*)b);
}

static float gtext_w(const wchar_t* s, int len, GpFont* f)
{
    GpRectF rf = { 0, 0, 40000, 400 }, bb = { 0,0,0,0 };
    GdipMeasureString(g_gfx, s, len, f, &rf, g_sfL, &bb, NULL, NULL);
    return bb.x + bb.width;
}

// ---- 终端方角绘制件：整数像素矩形（SmoothingMode=None 硬边，终端像素感；
//      AA 模式下 GDI+ 会把 1px 整数矩形劈成两个半透明像素，边框直接减半）----
static void fill_rect(int l, int t, int w, int h, u32 col)
{
    GpSolidFill* b = NULL;
    if (GdipCreateSolidFill(col, &b) == 0) {
        GdipSetSmoothingMode(g_gfx, 3);            // SmoothingModeNone
        GdipFillRectangleI(g_gfx, (GpBrush*)b, l, t, w, h);
        GdipSetSmoothingMode(g_gfx, GP_SMOOTH_AA);
        GdipDeleteBrush((GpBrush*)b);
    }
}

static void hline(int x1, int x2, int y, u32 col) { fill_rect(x1, y, x2 - x1, 1, col); }

// 方角描边（w px，四条填充矩形拼接）
static void frame_rect(RECT rc, u32 col, int w)
{
    fill_rect(rc.left, rc.top, rc.right - rc.left, w, col);
    fill_rect(rc.left, rc.bottom - w, rc.right - rc.left, w, col);
    fill_rect(rc.left, rc.top + w, w, (rc.bottom - rc.top) - w * 2, col);
    fill_rect(rc.right - w, rc.top + w, w, (rc.bottom - rc.top) - w * 2, col);
}

// 终端格三态：0 = 暗绿 1px 细框；1 = hover 磷激发（整格提亮 + 亮绿 2px 框）；2 = 选中
// 拆成 fill（图前）/ frame（图后）：满幅缩略图会盖住先画的框，框必须压在图上
static void term_fill(RECT rc, int state)
{
    fill_rect(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
              state == 2 ? ARGB(36, T_PH_R, T_PH_G, T_PH_B)
              : state == 1 ? ARGB(28, T_PH_R, T_PH_G, T_PH_B)
              : ARGB(9,  T_PH_R, T_PH_G, T_PH_B));
}

static void term_frame(RECT rc, int state)
{
    if (state >= 1) frame_rect(rc, C_PH(235), 2);
    else            frame_rect(rc, ARGB(92, T_DIM_R, T_DIM_G, T_DIM_B), 1);
}

// 径向柔光（预渲染图拉伸）
static void draw_glow(GpBitmap* glow, RECT rc, int grow)
{
    GdipDrawImageRectI(g_gfx, (GpImage*)glow,
                       rc.left - grow, rc.top - grow,
                       rc.right - rc.left + grow * 2, rc.bottom - rc.top + grow * 2);
}

// ============================================================
// 静态件生成
// ============================================================
static void make_glow(GpBitmap** out, u8** bufOut, float peak)
{
    const int N = 64;
    u8* buf = (u8*)HeapAlloc(GetProcessHeap(), 0, (u64)N * N * 4);
    if (!buf) return;
    u32* px = (u32*)buf;
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            float dx = (x + 0.5f - N / 2) / (N / 2), dy = (y + 0.5f - N / 2) / (N / 2);
            float e = dx * dx + dy * dy;
            int a = e >= 1.0f ? 0 : (int)(peak * (1 - e) * (1 - e) * 255);
            px[y * N + x] = ARGB(a, T_PH_R * a / 255, T_PH_G * a / 255, T_PH_B * a / 255);
        }
    GdipCreateBitmapFromScan0(N, N, N * 4, GP_PF_32PARGB, buf, out);
    *bufOut = buf;
}

static void free_statics(void)
{
    if (g_glowSoft)  { GdipDisposeImage((GpImage*)g_glowSoft);  g_glowSoft = NULL; }
    if (g_glowStrong){ GdipDisposeImage((GpImage*)g_glowStrong);g_glowStrong = NULL; }
    if (g_glowSoftBuf)  { HeapFree(GetProcessHeap(), 0, g_glowSoftBuf);  g_glowSoftBuf = NULL; }
    if (g_glowStrongBuf){ HeapFree(GetProcessHeap(), 0, g_glowStrongBuf); g_glowStrongBuf = NULL; }
}

// 底图：近黑绿磷屏 rgba(6,12,8,0.94) + 3px 周期扫描线（rgba(0,0,0,0.22)）+
//       边缘磷光辉光 vignette + 双线框（外 PH α160 / 内 α60）+ 四角十字加强。
//       全部预渲染进 g_base，每帧 memcpy（无逐像素逐帧计算）。
static void gen_base(void)
{
    int W = g_winW, H = g_winH;
    int L = g_panel.left, T = g_panel.top, R = g_panel.right, B = g_panel.bottom;
    float pw = (float)(R - L), ph = (float)(B - T);
    float cx = (L + R) * 0.5f, cy = (T + B) * 0.5f;
    float hw = pw / 2, hh = ph / 2;
    float vig = (float)S(26);              // 边缘辉光带宽（px）
    memset(g_bits, 0, (u64)W * H * 4);
    for (int y = 0; y < H; y++) {
        float fy = y + 0.5f;
        if (fy < (float)T - 1 || fy > (float)B + 1) continue;
        int scan = ((y - T) % 3) == 2;     // 扫描线：每 3px 一条
        for (int x = 0; x < W; x++) {
            float fx = x + 0.5f;
            if (fx < (float)L - 1 || fx > (float)R + 1) continue;
            float sd = sd_round(fx, fy, cx, cy, hw, hh, 0.0f);   // 方角（终端无圆角）
            if (sd > 0.7f) continue;
            float v = (fy - T) / ph;       // 极轻微上亮下暗
            float r = 7 - 2 * v, g = 13 - 3 * v, b = 9 - 2 * v;
            float dE = fmin_(fmin_(fx - L, (float)R - fx), fmin_(fy - T, (float)B - fy));
            if (dE < vig) {                // 边缘磷光：向 PHOSPHOR 拉约 α8+
                float t = 1.0f - dE / vig;
                float gl = 0.05f * t * t;
                r += (T_PH_R - r) * gl; g += (T_PH_G - g) * gl; b += (T_PH_B - b) * gl;
            }
            if (scan) { r *= 0.78f; g *= 0.78f; b *= 0.78f; }   // 扫描暗纹
            float aP = 0.94f * fmin_(0.7f - sd, 1.0f);          // 边缘 1px 软化
            if (aP < 0) aP = 0;
            g_bits[(u64)y * W + x] =
                (((u32)(aP * 255)) << 24) |
                (((u32)(u8)(r * aP)) << 16) | (((u32)(u8)(g * aP)) << 8) | (u32)(u8)(b * aP);
        }
    }
    // 双线框（外亮内暗）+ 四角十字加强（box-drawing corner joint 风，向外伸 S(5)）
    int in = S(4), e = S(5);
    frame_rect((RECT){ L, T, R, B }, C_PH(160), 1);
    frame_rect((RECT){ L + in, T + in, R - in, B - in }, C_PH(60), 1);
    u32 tick = C_PH(235);
    fill_rect(L - e, T - 1, e + 2, 2, tick);  fill_rect(R - 2, T - 1, e + 2, 2, tick);
    fill_rect(L - e, B - 1, e + 2, 2, tick);  fill_rect(R - 2, B - 1, e + 2, 2, tick);
    fill_rect(L - 1, T - e, 2, e + 2, tick);  fill_rect(L - 1, B - 2, 2, e + 2, tick);
    fill_rect(R - 1, T - e, 2, e + 2, tick);  fill_rect(R - 1, B - 2, 2, e + 2, tick);
    memcpy(g_base, g_bits, (u64)W * H * 4);
}

// ============================================================
// 表面管理（DIB / GDI+ 视图 / 底图）
// ============================================================
static void destroy_surface(void)
{
    if (g_gfx)    { GdipDeleteGraphics(g_gfx); g_gfx = NULL; }
    if (g_gpbmp)  { GdipDisposeImage((GpImage*)g_gpbmp); g_gpbmp = NULL; }
    if (g_memDC && g_dib) SelectObject(g_memDC, g_dcStockBmp);   // 先换出才能删
    if (g_dib)    { DeleteObject(g_dib); g_dib = NULL; }
    if (g_base)   { HeapFree(GetProcessHeap(), 0, g_base); g_base = NULL; }
    if (g_fadeSnap) { HeapFree(GetProcessHeap(), 0, g_fadeSnap); g_fadeSnap = NULL; }
    g_bits = NULL;
}

static void rebuild_surface(void)
{
    destroy_surface();
    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = g_winW;
    bi.bmiHeader.biHeight = -g_winH;          // 顶朝下
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    g_dib = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, (void**)&g_bits, NULL, 0);
    if (!g_dib) return;
    HGDIOBJ old = SelectObject(g_memDC, g_dib);
    if (old && old != (HGDIOBJ)g_dcStockBmp && old != (HGDIOBJ)g_dib) DeleteObject(old);
    g_base = (u32*)HeapAlloc(GetProcessHeap(), 0, (u64)g_winW * g_winH * 4);
    GdipCreateBitmapFromScan0(g_winW, g_winH, g_winW * 4, GP_PF_32PARGB,
                              (u8*)g_bits, &g_gpbmp);
    GdipGetImageGraphicsContext((GpImage*)g_gpbmp, &g_gfx);
    if (g_gfx) {
        GdipSetSmoothingMode(g_gfx, GP_SMOOTH_AA);
        GdipSetInterpolationMode(g_gfx, GP_INTERP_BICUBIC);
        GdipSetTextRenderingHint(g_gfx, GP_TEXT_AA_GRIDFIT);
    }
    if (g_base) gen_base();
}

// 淡入：整帧预乘 alpha 同乘系数
static void fade_mul(void)
{
    if (g_fade >= 255) return;
    u32 f = (u32)g_fade;
    u64 n = (u64)g_winW * g_winH;
    u32* p = g_bits;
    for (u64 i = 0; i < n; i++) {
        u32 px = p[i];
        u32 lo = px & 0x00FF00FF, hi = (px >> 8) & 0x00FF00FF;
        p[i] = (((lo * f) >> 8) & 0x00FF00FF) | (((((hi * f) >> 8)) << 8) & 0xFF00FF00);
    }
}

static void submit_ulw(void)
{
    if (!g_dib || g_headless) return;
    RECT wr; GetWindowRect(g_hwnd, &wr);
    POINT src = { 0, 0 }, dst = { wr.left, wr.top };
    SIZE sz = { g_winW, g_winH };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    HDC scr = GetDC(NULL);
    UpdateLayeredWindow(g_hwnd, scr, &dst, &sz, g_memDC, &src, 0, &bf, ULW_ALPHA);
    ReleaseDC(NULL, scr);
}

// ============================================================
// 缩略图环形缓存（HBITMAP 拥有像素 + GpBitmap 零拷贝视图）
// ============================================================
#define TCACHE 768
typedef struct { int idx; HBITMAP hb; GpBitmap* pb; int w, h; } TEnt;
static TEnt g_tc[TCACHE];
static int  g_tcNext;

static void tc_clear(void)
{
    for (int i = 0; i < TCACHE; i++) {
        if (g_tc[i].pb) GdipDisposeImage((GpImage*)g_tc[i].pb);
        if (g_tc[i].hb) DeleteObject(g_tc[i].hb);
        g_tc[i].hb = NULL; g_tc[i].pb = NULL; g_tc[i].idx = -1;
    }
    g_tcNext = 0;
}

static GpBitmap* tc_get(int idx, int* ow, int* oh)
{
    for (int i = 0; i < TCACHE; i++)
        if (g_tc[i].pb && g_tc[i].idx == idx) {
            *ow = g_tc[i].w; *oh = g_tc[i].h;
            return g_tc[i].pb;
        }
    wchar_t p[MAX_PATH];
    store_build_thumb(&g_store, &g_store.entries[g_filt[idx]], p, MAX_PATH);
    HBITMAP hb = wic_load_bitmap(p);
    if (!hb) return NULL;
    GpBitmap* pb = NULL;
    BITMAP bm;
    int w = 1, h = 1;
    if (GetObjectW(hb, sizeof bm, &bm) && bm.bmBits) {
        w = bm.bmWidth; h = bm.bmHeight;
        GdipCreateBitmapFromScan0(w, h, w * 4, GP_PF_32RGB, (u8*)bm.bmBits, &pb);
    }
    if (!pb) { DeleteObject(hb); return NULL; }
    int slot = g_tcNext;
    g_tcNext = (g_tcNext + 1) % TCACHE;
    if (g_tc[slot].pb) GdipDisposeImage((GpImage*)g_tc[slot].pb);
    if (g_tc[slot].hb) DeleteObject(g_tc[slot].hb);
    g_tc[slot].hb = hb; g_tc[slot].pb = pb; g_tc[slot].idx = idx;
    g_tc[slot].w = w; g_tc[slot].h = h;
    *ow = w; *oh = h;
    return pb;
}

// 单张解码缓存（最近格 ×7 / 悬停大图共用模式）：ei 命中即复用，避免每帧解 JPEG
typedef struct { int ei; HBITMAP hb; GpBitmap* pb; int w, h; } OneShot;
static void oneshot_clear(OneShot* o)
{
    if (o->pb) GdipDisposeImage((GpImage*)o->pb);
    if (o->hb) DeleteObject(o->hb);
    o->pb = NULL; o->hb = NULL; o->ei = -1;
}
static GpBitmap* oneshot_get(OneShot* o, int ei, int* ow, int* oh)
{
    if (o->ei == ei && o->pb) { *ow = o->w; *oh = o->h; return o->pb; }
    oneshot_clear(o);
    wchar_t p[MAX_PATH];
    store_build_thumb(&g_store, &g_store.entries[ei], p, MAX_PATH);
    o->hb = wic_load_bitmap(p);
    BITMAP bm;
    if (o->hb && GetObjectW(o->hb, sizeof bm, &bm) && bm.bmBits) {
        o->w = bm.bmWidth; o->h = bm.bmHeight;
        GdipCreateBitmapFromScan0(o->w, o->h, o->w * 4, GP_PF_32RGB,
                                  (u8*)bm.bmBits, &o->pb);
    }
    if (!o->pb) { oneshot_clear(o); return NULL; }
    o->ei = ei;
    *ow = o->w; *oh = o->h;
    return o->pb;
}

static OneShot g_recentImg[7];
static OneShot g_previewImg;

// ============================================================
// v0.5：悬停 GIF 动画（仅 hover 的格子/大图播放，其余静态首帧）
// 解码在后台线程（自建 MTA WIC 工厂），结果 PostMessage 回 UI；
// 帧为整幅画布快照，UI 侧零拷贝包装 GpBitmap，播放只做 DrawImage
// ============================================================
typedef struct {
    int   want;                 // 期望播放的 entry 索引（UI 写）
    BOOL  running;              // 解码线程在跑（UI 维护）
    int   pending;              // 线程忙时排队的下一请求
    // 播放态（UI 拥有；每帧 = DIB（像素拥有者）+ GpBitmap 32RGB 零拷贝视图，
    // 与静态缩略图同一条已验证路径；GIF 帧合成后不透明，可安全忽略 alpha）
    int      ei;
    int      nFrames, cur, w, h;
    int*     delays;
    GpBitmap** pb;
    HBITMAP* hbmps;
    u8**     rawFrames;         // 线程产出的原始帧 buffer（拷入 DIB 后即释放）
} AnimState;
static AnimState g_anim;

typedef struct { wchar_t path[MAX_PATH]; int ei; HWND hwnd; } AnimReq;

static int  is_gif_name(const wchar_t* name);
static void panel_repaint(void);

static void anim_free(void)
{
    KillTimer(g_hwnd, GIF_TIMER);
    if (g_anim.pb) {
        for (int i = 0; i < g_anim.nFrames; i++)
            if (g_anim.pb[i]) GdipDisposeImage((GpImage*)g_anim.pb[i]);
        HeapFree(GetProcessHeap(), 0, g_anim.pb);
    }
    if (g_anim.hbmps) {
        for (int i = 0; i < g_anim.nFrames; i++)
            if (g_anim.hbmps[i]) DeleteObject(g_anim.hbmps[i]);
        HeapFree(GetProcessHeap(), 0, g_anim.hbmps);
    }
    if (g_anim.rawFrames) {
        for (int i = 0; i < g_anim.nFrames; i++)
            if (g_anim.rawFrames[i]) HeapFree(GetProcessHeap(), 0, g_anim.rawFrames[i]);
        HeapFree(GetProcessHeap(), 0, g_anim.rawFrames);
    }
    if (g_anim.delays) HeapFree(GetProcessHeap(), 0, g_anim.delays);
    // 只清播放态；want/pending/running 是请求簿记，不能动（否则解码完成回调永远错配）
    g_anim.pb = NULL; g_anim.hbmps = NULL; g_anim.rawFrames = NULL; g_anim.delays = NULL;
    g_anim.nFrames = 0; g_anim.cur = 0; g_anim.w = g_anim.h = 0;
    g_anim.ei = -1;
}

static void anim_start_thread(HWND hwnd, int ei);

static void anim_request(HWND hwnd, int ei)
{
    g_anim.want = ei;
    if (ei < 0 || ei >= g_store.count || !is_gif_name(g_store.entries[ei].name)) {
        anim_free();
        return;
    }
    if (g_anim.ei == ei) return;                     // 已在播放
    anim_free();
    if (!g_anim.running) anim_start_thread(hwnd, ei);
    else g_anim.pending = ei;                        // 线程忙，排队
}

static DWORD WINAPI anim_thread(LPVOID arg)
{
    AnimReq* req = (AnimReq*)arg;
    GifFrames* res = NULL;
    wic_decode_gif_bg(req->path, &res);
    PostMessageW(req->hwnd, WM_GIFDONE, (WPARAM)req->ei, (LPARAM)res);
    HeapFree(GetProcessHeap(), 0, req);
    return 0;
}

static void anim_start_thread(HWND hwnd, int ei)
{
    AnimReq* req = (AnimReq*)HeapAlloc(GetProcessHeap(), 0, sizeof(AnimReq));
    if (!req) return;
    store_build_path(&g_store, &g_store.entries[ei], req->path, MAX_PATH);
    req->ei = ei; req->hwnd = hwnd;
    HANDLE t = CreateThread(NULL, 0, anim_thread, req, 0, NULL);
    if (t) { g_anim.running = TRUE; CloseHandle(t); }
    else HeapFree(GetProcessHeap(), 0, req);
}

// UI 线程：解码完成消息
static void anim_on_done(HWND hwnd, int ei, GifFrames* res)
{
    g_anim.running = FALSE;
    if (res && res->nFrames > 0 && g_anim.want == ei) {
        g_anim.ei = ei;
        g_anim.nFrames = res->nFrames;
        g_anim.w = res->w; g_anim.h = res->h;
        g_anim.cur = 0;
        g_anim.delays = res->delaysMs;
        g_anim.pb = (GpBitmap**)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                          (u64)res->nFrames * sizeof(GpBitmap*));
        g_anim.hbmps = (HBITMAP*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                           (u64)res->nFrames * sizeof(HBITMAP));
        g_anim.rawFrames = res->frames;   // 拷入 DIB 后统一释放
        if (g_anim.pb && g_anim.hbmps) {
            BITMAPINFO bi;
            memset(&bi, 0, sizeof bi);
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = res->w;
            bi.bmiHeader.biHeight = -(LONG)res->h;   // 顶朝下
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            u32 cb = (u32)res->w * res->h * 4;
            for (int i = 0; i < res->nFrames; i++) {
                void* bits = NULL;
                g_anim.hbmps[i] = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
                if (g_anim.hbmps[i] && bits) {
                    memcpy(bits, res->frames[i], cb);
                    GdipCreateBitmapFromScan0(res->w, res->h, res->w * 4,
                                              GP_PF_32RGB, (u8*)bits, &g_anim.pb[i]);
                }
            }
        }
        // 原始帧 buffer 的使命完成
        for (int i = 0; i < res->nFrames; i++)
            HeapFree(GetProcessHeap(), 0, res->frames[i]);
        HeapFree(GetProcessHeap(), 0, res->frames);
        g_anim.rawFrames = NULL;
        HeapFree(GetProcessHeap(), 0, res);
        if (g_anim.nFrames > 1) SetTimer(hwnd, GIF_TIMER, g_anim.delays[0], NULL);
        panel_repaint();
    } else {
        if (res) { wic_free_gif(res); HeapFree(GetProcessHeap(), 0, res); }
    }
    // 排队请求
    if (g_anim.pending >= 0 && g_anim.pending == g_anim.want && g_anim.ei != g_anim.pending) {
        int p = g_anim.pending;
        g_anim.pending = -1;
        anim_start_thread(hwnd, p);
    } else {
        g_anim.pending = -1;
    }
}

// contain 绘制（保持比例、居中、clip 到圆角）
static void draw_image_contain(GpBitmap* img, int iw, int ih, RECT rc, int rad)
{
    if (!img) return;
    GpPath* clip = rr_path(rc.left, rc.top, rc.right, rc.bottom, rad);
    if (clip) GdipSetClipPath(g_gfx, clip, 0);
    int aw = rc.right - rc.left, ah = rc.bottom - rc.top;
    double sc = (iw && ih) ? (double)aw / iw : 1.0;
    if ((double)ah / ih < sc) sc = (double)ah / ih;
    int dw = (int)(iw * sc), dh = (int)(ih * sc);
    if (dw < 1) dw = 1; if (dh < 1) dh = 1;
    GdipDrawImageRectI(g_gfx, (GpImage*)img,
                       rc.left + (aw - dw) / 2, rc.top + (ah - dh) / 2, dw, dh);
    GdipResetClip(g_gfx);
    if (clip) GdipDeletePath(clip);
}

// ============================================================
// 筛选 / 最近使用
// ============================================================
static void refilter(void)
{
    g_nFilt = 0; g_first = 0; g_hover = -1; g_previewIdx = -1;
    for (int i = 0; i < g_store.count; i++) {
        Entry* e = &g_store.entries[i];
        if (g_tagFilter && !(e->tagmask & g_tagFilter)) continue;
        if (g_search[0] && !wcontains_ci(e->name, g_search)) continue;
        if (g_nFilt == g_capFilt) {
            g_capFilt = g_capFilt ? g_capFilt * 2 : 256;
            int* np = g_filt
                ? (int*)HeapReAlloc(GetProcessHeap(), 0, g_filt, (u64)g_capFilt * 4)
                : (int*)HeapAlloc(GetProcessHeap(), 0, (u64)g_capFilt * 4);
            if (np) g_filt = np;
        }
        g_filt[g_nFilt++] = i;
    }
    tc_clear();
}

static void recalc_recents(void)
{
    g_nRecent = 0;
    for (int i = 0; i < g_store.count && g_nRecent < 7; i++) {
        if (!g_store.entries[i].used) continue;
        int k = g_nRecent++;
        g_recentIdx[k] = i;
        while (k > 0 && g_store.entries[g_recentIdx[k - 1]].used < g_store.entries[g_recentIdx[k]].used) {
            int t = g_recentIdx[k - 1]; g_recentIdx[k - 1] = g_recentIdx[k]; g_recentIdx[k] = t;
            k--;
        }
    }
}

static void clamp_first(void)
{
    int vis = g_cols * g_rows;
    int maxF = g_nFilt - vis;
    if (maxF < 0) maxF = 0;
    if (g_first > maxF) g_first = maxF;
    if (g_first < 0) g_first = 0;
}

// ============================================================
// 布局（终端结构：标题条 / > 检索行 / [标签] / 最近7格 / 网格 / 状态行）
// ============================================================
static void panel_layout(HWND hwnd)
{
    RECT rc; GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;
    if (W <= 0 || H <= 0) return;
    BOOL resized = (W != g_winW || H != g_winH || !g_bits);   // hide 时表面已释放，show 重建
    g_winW = W; g_winH = H;
    // 面板矩形必须先于 rebuild_surface 更新：gen_base 画底图读的就是 g_panel，
    // 顺序反了会把「上一次尺寸」的面板烤进新底图（右下大片透明的元凶）
    g_panel.left = g_bleed; g_panel.top = g_bleed;
    g_panel.right = W - g_bleed; g_panel.bottom = H - g_bleed;
    if (resized) rebuild_surface();
    if (!g_bits) return;

    int padX = S(18), padT = S(10), padB = S(8);

    // 标题条：MEME://TERMINAL v0.6 ······ [N] ONLINE · [X]
    g_titleRc.left = g_panel.left + padX;
    g_titleRc.top = g_panel.top + padT;
    g_titleRc.right = g_panel.right - padX;
    g_titleRc.bottom = g_titleRc.top + S(24);
    g_closeRc.right = g_titleRc.right;
    g_closeRc.left = g_closeRc.right - S(40);
    g_closeRc.top = g_titleRc.top + (S(24) - S(22)) / 2;
    g_closeRc.bottom = g_closeRc.top + S(22);

    // 命令行检索行：`> ` 前缀，无框，靠上下分隔线分区
    g_searchBox.left = g_panel.left + padX;
    g_searchBox.top = g_titleRc.bottom + S(12);
    g_searchBox.right = g_panel.right - padX;
    g_searchBox.bottom = g_searchBox.top + S(30);

    // 状态行（速发 READY / 管理 SYS:）左右界（top/bottom 按模式在下面定）
    g_footer.left = g_panel.left + padX;
    g_footer.right = g_panel.right - padX;

    if (g_mgr) {
        // 管理模式：操作行（[标签] + [按钮]）→ 96px 大格网格 → SYS: 状态行
        g_opY = g_searchBox.bottom + S(12);
        g_cell = S(96); g_gap = S(10);
        g_grid.left = g_panel.left + padX;
        g_grid.top = g_opY + S(30) + S(12);
        g_grid.right = g_panel.right - padX;
        g_footer.bottom = g_panel.bottom - padB;
        g_footer.top = g_footer.bottom - S(20);
        g_grid.bottom = g_footer.top - S(8);
        int avail = g_grid.right - g_grid.left;
        g_cols = avail / (g_cell + g_gap); if (g_cols < 1) g_cols = 1;
        int gh = g_grid.bottom - g_grid.top; if (gh < g_cell) gh = g_cell;
        g_rows = (gh + g_gap) / (g_cell + g_gap); if (g_rows < 1) g_rows = 1;
    } else {
        // 速发模式：> 检索行 → [标签] token → 最近 7 格 → 网格 → READY 状态行
        g_chipY = g_searchBox.bottom + S(12);
        int ry = g_chipY + S(26) + S(10);
        g_cell = S(56); g_gap = S(8);
        for (int i = 0; i < 7; i++) {
            g_recent[i].left = g_panel.left + padX + i * (g_cell + g_gap);
            g_recent[i].right = g_recent[i].left + g_cell;
            g_recent[i].top = ry;
            g_recent[i].bottom = ry + g_cell;
        }
        g_grid.left = g_panel.left + padX;
        g_grid.top = ry + g_cell + S(14);
        g_grid.right = g_panel.right - padX;
        g_footer.bottom = g_panel.bottom - padB;
        g_footer.top = g_footer.bottom - S(20);
        g_grid.bottom = g_footer.top - S(8);
        int avail = g_grid.right - g_grid.left;
        g_cols = avail / (g_cell + g_gap); if (g_cols < 1) g_cols = 1;
        int gh = g_grid.bottom - g_grid.top; if (gh < g_cell) gh = g_cell;
        g_rows = (gh + g_gap) / (g_cell + g_gap); if (g_rows < 1) g_rows = 1;
    }

    // 悬停大图（右下，方角双线框；上方 VIEW: 标签行、下方帧信息行）
    g_preview.right = g_panel.right - S(18);
    g_preview.bottom = g_footer.top - S(24);
    g_preview.left = g_preview.right - S(168);
    g_preview.top = g_preview.bottom - S(168);
    clamp_first();
}

// ============================================================
// 显示 / 隐藏
// ============================================================
static void panel_repaint(void);

static void panel_show(HWND hwnd)
{
    HWND fg = GetForegroundWindow();
    if (fg != hwnd) g_lastTarget = fg;
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    int pw, ph;
    if (g_mgr) { pw = sw * 60 / 100; ph = sh * 66 / 100; }
    else        { pw = sw * 46 / 100; ph = sh * 54 / 100;
                  if (pw < S(700)) pw = S(700);
                  if (ph < S(460)) ph = S(460); }
    int w = pw + g_bleed * 2, h = ph + g_bleed * 2;
    g_fade = 0;
    g_caretOn = TRUE;
    // 关键顺序：隐藏状态下先定尺寸/重建表面、合成 alpha=0 的首帧并 ULW 提交，
    // 之后才显示窗口——否则 SWP_SHOWWINDOW 会先亮出上一会话残留的不透明旧帧（闪烁）
    SetWindowPos(hwnd, HWND_TOPMOST, (sw - w) / 2, (sh - h) / 5, w, h, SWP_NOACTIVATE);
    panel_layout(hwnd);
    g_fade = 0;
    panel_repaint();
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_SHOWWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    submit_ulw();   // 个别系统对隐藏窗口的 ULW 不落地，显示后再提交一次首帧
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    SetTimer(hwnd, ANIM_TIMER, 10, NULL);
    SetTimer(hwnd, CARET_TIMER, 530, NULL);
}

static void panel_hide(HWND hwnd)
{
    KillTimer(hwnd, ANIM_TIMER);
    KillTimer(hwnd, CARET_TIMER);
    KillTimer(hwnd, PREVIEW_TMR);
    KillTimer(hwnd, GIF_TIMER);
    g_fade = 255;
    g_hover = -1;
    g_previewIdx = -1;
    anim_free();
    ShowWindow(hwnd, SW_HIDE);
    destroy_surface();   // 释放 ~9MB 合成表面（下次 show 重建，gen_base 代价 ~5ms）
}

// ============================================================
// 粘贴链路（CF_HDROP 保留 GIF 动画）
// ============================================================
static void paste_entry(HWND hwnd, int ei)
{
    Entry* e = &g_store.entries[ei];
    e->used = GetTickCount64();
    store_save(&g_store);
    recalc_recents();

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
// 拖放导入（文件夹自动建同名标签并批量打标）
// ============================================================
typedef struct { wchar_t name[MAX_PATH]; int tag; } Imported;

static BOOL copy_into(Store* s, const wchar_t* src)
{
    const wchar_t* base = src + wlen(src);
    while (base > src && base[-1] != L'\\') base--;
    wchar_t stem[MAX_PATH]; wchar_t ext[40];
    lstrcpynW(stem, base, MAX_PATH);
    ext[0] = 0;
    wchar_t* dot = stem + wlen(stem);
    while (dot > stem && dot[-1] != L'.') dot--;
    if (dot > stem) { lstrcpynW(ext, dot - 1, 40); dot[-1] = 0; }
    wchar_t dst[MAX_PATH];
    for (int k = 0; k < 1000; k++) {
        if (k == 0) wnsprintfW(dst, MAX_PATH, L"%s\\%s", s->memesDir, base);
        else        wnsprintfW(dst, MAX_PATH, L"%s\\%s (%d)%s", s->memesDir, stem, k, ext);
        if (GetFileAttributesW(dst) == INVALID_FILE_ATTRIBUTES) break;
    }
    return CopyFileW(src, dst, FALSE);
}

static void import_drop(HWND hwnd, HDROP hd)
{
    UINT n = DragQueryFileW(hd, 0xFFFFFFFF, NULL, 0);
    Imported* got = (Imported*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (u64)(n ? n : 1) * sizeof(Imported));
    int nGot = 0, tagId = -1;
    wchar_t src[MAX_PATH], pat[MAX_PATH];

    for (UINT i = 0; i < n && got; i++) {
        if (!DragQueryFileW(hd, i, src, MAX_PATH)) continue;
        DWORD at = GetFileAttributesW(src);
        if (at == INVALID_FILE_ATTRIBUTES) continue;
        if (at & FILE_ATTRIBUTE_DIRECTORY) {
            const wchar_t* base = src + wlen(src);
            while (base > src && base[-1] != L'\\') base--;
            tagId = store_tag_add(&g_store, base);
            wnsprintfW(pat, MAX_PATH, L"%s\\*", src);
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW(pat, &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                    wchar_t full[MAX_PATH];
                    wnsprintfW(full, MAX_PATH, L"%s\\%s", src, fd.cFileName);
                    if (copy_into(&g_store, full) && nGot < (int)n) {
                        lstrcpynW(got[nGot].name, fd.cFileName, MAX_PATH);
                        got[nGot].tag = tagId; nGot++;
                    }
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
        } else {
            if (copy_into(&g_store, src)) {
                const wchar_t* base = src + wlen(src);
                while (base > src && base[-1] != L'\\') base--;
                if (nGot < (int)n) {
                    lstrcpynW(got[nGot].name, base, MAX_PATH);
                    got[nGot].tag = -1; nGot++;
                }
            }
        }
    }
    DragFinish(hd);

    store_scan(&g_store);
    for (int k = 0; k < nGot; k++) {
        if (got[k].tag < 0) continue;
        int ei = store_find(&g_store, got[k].name);
        if (ei >= 0) g_store.entries[ei].tagmask |= (u64)1 << got[k].tag;
    }
    store_save(&g_store);
    if (got) HeapFree(GetProcessHeap(), 0, got);

    refilter();
    recalc_recents();
    panel_repaint();
    wchar_t info[128];
    wnsprintfW(info, 128, L"已导入 %d 张（库共 %d 张）", nGot, g_store.count);
    lstrcpynW(g_nid.szInfoTitle, APP_NAME, 64);
    lstrcpynW(g_nid.szInfo, info, 256);
    g_nid.uFlags = NIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
}

// ============================================================
// v0.6 管理模式：重命名 / 删除（回收站）/ 去重 / 后缀修正
// ============================================================
#define ARM_TMR 6

static void panel_repaint(void);
static void mgr_refresh(HWND hwnd)
{
    sel_reserve(g_store.count);
    g_first = 0;
    panel_layout(hwnd);
    panel_repaint();
}

static void mgr_enter(HWND hwnd)
{
    if (g_mgr) return;
    g_mgr = TRUE;
    sel_reserve(g_store.count);
    g_nSel = 0;
    g_renameEi = -1;
    g_delArm = FALSE;
    g_status[0] = 0;
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    int w = sw * 60 / 100 + g_bleed * 2, h = sh * 66 / 100 + g_bleed * 2;
    SetWindowPos(hwnd, HWND_TOPMOST, (sw - w) / 2, (sh - h) / 6, w, h, SWP_NOACTIVATE);
    mgr_refresh(hwnd);
}

static void mgr_exit(HWND hwnd)
{
    if (!g_mgr) return;
    g_mgr = FALSE;
    g_renameEi = -1;
    g_delArm = FALSE;
    sel_clear();
    g_search[0] = 0; g_slen = 0; g_caret = 0; g_complen = 0;
    refilter();
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    int w = sw * 46 / 100, h = sh * 54 / 100;
    if (w < S(700)) w = S(700);
    if (h < S(460)) h = S(460);
    w += g_bleed * 2; h += g_bleed * 2;
    SetWindowPos(hwnd, HWND_TOPMOST, (sw - w) / 2, (sh - h) / 5, w, h, SWP_NOACTIVATE);
    panel_layout(hwnd);
    panel_repaint();
}

// 改名 + 缩略图迁移 + 索引同步；返回 TRUE 成功
static BOOL mgr_rename_entry(int ei, const wchar_t* newName)
{
    wchar_t oldp[MAX_PATH], newp[MAX_PATH], oldt[MAX_PATH], newt[MAX_PATH];
    store_build_path(&g_store, &g_store.entries[ei], oldp, MAX_PATH);
    wnsprintfW(newp, MAX_PATH, L"%s\\%s", g_store.memesDir, newName);
    if (GetFileAttributesW(newp) != INVALID_FILE_ATTRIBUTES) return FALSE;
    if (!MoveFileW(oldp, newp)) {
        return FALSE;
    }
    wnsprintfW(oldt, MAX_PATH, L"%s\\%s.jpg", g_store.thumbsDir, g_store.entries[ei].name);
    wnsprintfW(newt, MAX_PATH, L"%s\\%s.jpg", g_store.thumbsDir, newName);
    if (!MoveFileW(oldt, newt)) DeleteFileW(oldt);   // 缩略图丢失则由扫描重建
    wchar_t* d = wdup(newName);
    if (d) { HeapFree(GetProcessHeap(), 0, g_store.entries[ei].name); g_store.entries[ei].name = d; }
    return TRUE;
}

static BOOL name_valid(const wchar_t* s)
{
    if (!s || !s[0] || wlen(s) > 200) return FALSE;
    for (const wchar_t* p = s; *p; p++)
        if (*p < 32 || *p == L'\\' || *p == L'/' || *p == L':' || *p == L'*' ||
            *p == L'?' || *p == L'"' || *p == L'<' || *p == L'>' || *p == L'|')
            return FALSE;
    return TRUE;
}

static void mgr_rename_begin(void)
{
    int ei = -1;
    for (int i = 0; i < g_store.count; i++)
        if (sel_has(i)) { ei = i; break; }
    if (ei < 0) return;
    g_renameEi = ei;
    lstrcpynW(g_search, g_store.entries[ei].name, 127);
    g_slen = (int)wlen(g_search);
    g_caret = g_slen;
    g_complen = 0;
    g_caretOn = TRUE;
    lstrcpynW(g_status, L"重命名：回车确认 · Esc 取消", 96);
    panel_repaint();
}

static void mgr_rename_end(HWND hwnd, BOOL commit)
{
    int ei = g_renameEi;
    g_renameEi = -1;
    if (commit && ei >= 0 && ei < g_store.count && name_valid(g_search)) {
        if (mgr_rename_entry(ei, g_search)) {
            store_save(&g_store);
            lstrcpynW(g_status, L"已重命名", 96);
        } else {
            lstrcpynW(g_status, L"重命名失败（重名或文件被占用）", 96);
        }
    } else if (commit) {
        lstrcpynW(g_status, L"文件名非法", 96);
    }
    g_search[0] = 0; g_slen = 0; g_caret = 0; g_complen = 0;
    sel_clear();
    refilter();
    panel_repaint();
    (void)hwnd;
}

static void mgr_delete(HWND hwnd)
{
    if (!g_nSel) return;
    if (!g_delArm) {
        g_delArm = TRUE;
        SetTimer(hwnd, ARM_TMR, 3000, NULL);
        lstrcpynW(g_status, L"再点一次确认删除（送回收站）", 96);
        panel_repaint();
        return;
    }
    g_delArm = FALSE;
    KillTimer(hwnd, ARM_TMR);
    // SHFileOperation 的 pFrom 需要 "p1\0p2\0\0"
    wchar_t* buf = (wchar_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                       ((u64)g_nSel * MAX_PATH + 2) * 2);
    int n = 0;
    if (buf) {
        wchar_t* w = buf;
        for (int i = 0; i < g_store.count && n < g_nSel; i++) {
            if (!sel_has(i)) continue;
            store_build_path(&g_store, &g_store.entries[i], w, (int)(MAX_PATH - (w - buf)));
            w += wlen(w) + 1;
            n++;
        }
        SHFILEOPSTRUCTW fo;
        memset(&fo, 0, sizeof fo);
        fo.hwnd = hwnd;
        fo.wFunc = FO_DELETE;
        fo.pFrom = buf;
        fo.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
        int r = SHFileOperationW(&fo);
        wnsprintfW(g_status, 96, r == 0 ? L"已删除 %d 张（在回收站）" : L"删除失败 (%d)", r == 0 ? n : r);
        HeapFree(GetProcessHeap(), 0, buf);
    }
    store_scan(&g_store);
    recalc_recents();
    sel_clear();
    refilter();
    panel_repaint();
}

// FNV-1a 64 流式哈希（去重足够；非对抗场景）
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

// 按 size 分桶 -> 同 size 组内哈希 -> 每组保留一个，其余预选中（用户再决定删否）
static void mgr_dedup(HWND hwnd)
{
    int n = g_store.count;
    sel_clear();
    if (n < 2) { lstrcpynW(g_status, L"库太小，无需去重", 96); panel_repaint(); return; }
    HANDLE hp = GetProcessHeap();
    int* head = (int*)HeapAlloc(hp, 0, 4096 * 4);
    if (head) memset(head, 0xFF, 4096 * 4);   // 链尾哨兵必须是 -1；零初始化会指向索引 0 形成死环（曾致卡死）
    int* next = (int*)HeapAlloc(hp, HEAP_ZERO_MEMORY, (u64)n * 4);
    u8* done  = (u8*)HeapAlloc(hp, HEAP_ZERO_MEMORY, n);
    int* grp  = (int*)HeapAlloc(hp, 0, (u64)n * 4);
    int groups = 0, marked = 0;
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
            if (!gn) continue;                        // size 唯一，跳过（零 IO）
            store_build_path(&g_store, &g_store.entries[i], p, MAX_PATH);
            u64 hi = fnv_file(p);
            BOOL dup = FALSE;
            for (int k = 0; k < gn; k++) {
                int j = grp[k];
                store_build_path(&g_store, &g_store.entries[j], p, MAX_PATH);
                if (fnv_file(p) == hi) { sel_set(j, TRUE); marked++; done[j] = TRUE; dup = TRUE; }
            }
            if (dup) groups++;
        }
    }
    if (head) HeapFree(hp, 0, head);
    if (next) HeapFree(hp, 0, next);
    if (done) HeapFree(hp, 0, done);
    if (grp)  HeapFree(hp, 0, grp);
    if (groups) wnsprintfW(g_status, 96, L"%d 组重复，已选中 %d 张，确认后删除", groups, marked);
    else       lstrcpynW(g_status, L"未发现重复", 96);
    panel_repaint();
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

// QQNT 下载表情常见后缀错乱：按文件头 magic 修正扩展名
static void mgr_fixext(HWND hwnd)
{
    static const u8 PNGM[] = { 0x89, 'P', 'N', 'G' };
    static const u8 JPGM[] = { 0xFF, 0xD8, 0xFF };
    int fixed = 0;
    wchar_t p[MAX_PATH];
    wchar_t nn[MAX_PATH];
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
        if (stem) stem--;               // stem = 不含点的主名长度；无扩展名则整名
        if (stem + wlen(real) + 1 > MAX_PATH) continue;
        lstrcpynW(nn, name, (int)stem + 1);
        lstrcpynW(nn + stem, real, MAX_PATH - (int)stem);
        if (mgr_rename_entry(i, nn)) fixed++;
    }
    if (fixed) {
        store_save(&g_store);
        refilter();
        wnsprintfW(g_status, 96, L"修正了 %d 个后缀", fixed);
    } else {
        lstrcpynW(g_status, L"后缀都正常", 96);
    }
    panel_repaint();
    (void)hwnd;
}


// ============================================================
// 命中检测
// ============================================================
static int cell_from_point(POINT pt)
{
    if (!PtInRect(&g_grid, pt)) return -1;
    int col = (pt.x - g_grid.left) / (g_cell + g_gap); if (col < 0) col = 0;
    int row = (pt.y - g_grid.top) / (g_cell + g_gap);  if (row < 0) row = 0;
    // 网格右/下方的残余条带（不足一个格子宽）没有画格子，不得命中
    if (col >= g_cols || row >= g_rows) return -1;
    int idx = g_first + row * g_cols + col;
    return (idx < g_nFilt) ? idx : -1;
}

static int is_gif_name(const wchar_t* name)
{
    u32 n = wlen(name);
    return n > 4 && name[n - 4] == L'.' &&
           (name[n - 3] == L'g' || name[n - 3] == L'G') &&
           (name[n - 2] == L'i' || name[n - 2] == L'I') &&
           (name[n - 1] == L'f' || name[n - 1] == L'F');
}

// ============================================================
// 绘制：动态件
// ============================================================
// 标题条：Impact「MEME://TERMINAL」+ 版本号 + 右侧 [N] ONLINE● + [X]，下接双线分隔
static void draw_titlebar(void)
{
    RECT rc = g_titleRc;
    int midY = (rc.top + rc.bottom) / 2;
    gtext(L"MEME://TERMINAL", -1, g_fTitle,
          (RECT){ rc.left, rc.top, rc.left + S(230), rc.bottom }, g_sfL, C_PH(230));
    float tw = gtext_w(L"MEME://TERMINAL", -1, g_fTitle);
    gtext(g_mgr ? L"v0.6//SYS" : L"v0.6", -1, g_fSmall,
          (RECT){ rc.left + (int)tw + S(8), rc.top + S(3),
                  rc.left + (int)tw + S(72), rc.bottom }, g_sfL, C_PH(110));

    // 右：[N] ONLINE + 绿点（闪烁感用静态 α 近似）
    wchar_t cnt[32];
    wnsprintfW(cnt, 32, L"[%d]", g_store.count);
    float cw = gtext_w(cnt, -1, g_fUi);
    float ow = gtext_w(L"ONLINE", -1, g_fUi);
    int xr = g_closeRc.left - S(12);
    fill_rect(xr - S(6), midY - S(3), S(6), S(6), C_PH(235));
    int xOn = xr - S(6) - S(7) - (int)ow;
    gtext(L"ONLINE", -1, g_fUi, (RECT){ xOn, rc.top, xOn + (int)ow + 2, rc.bottom }, g_sfL, C_PH(150));
    int xCnt = xOn - S(7) - (int)cw;
    gtext(cnt, -1, g_fUi, (RECT){ xCnt, rc.top, xCnt + (int)cw + 2, rc.bottom }, g_sfL, C_PH(200));

    // [X] 关闭（hover 反色）
    BOOL chov = PtInRect(&g_closeRc, g_mouse);
    if (chov) {
        fill_rect(g_closeRc.left, g_closeRc.top,
                  g_closeRc.right - g_closeRc.left, g_closeRc.bottom - g_closeRc.top, C_PH(225));
        gtext(L"[X]", -1, g_fUi, g_closeRc, g_sfC, ARGB(255, 4, 9, 6));
    } else {
        gtext(L"[X]", -1, g_fUi, g_closeRc, g_sfC, C_PH(175));
    }

    // 标题下双线分隔（╞═）
    int y0 = rc.bottom + S(4);
    hline(g_panel.left + S(18), g_panel.right - S(18), y0, C_PH(110));
    hline(g_panel.left + S(18), g_panel.right - S(18), y0 + S(3), C_PH(45));
}

// 命令行检索：`> ` 前缀绿亮 + 输入白绿 + 块状 caret（磷绿底黑字），无框
static void draw_search_box(void)
{
    RECT rc = g_searchBox;
    int midY = (rc.top + rc.bottom) / 2;
    // 与标签区之间的分隔线
    hline(rc.left, rc.right, rc.bottom + S(4), C_PH(52));

    int tx = rc.left + S(2);
    gtext(L">", 1, g_fText, (RECT){ tx, rc.top, tx + S(24), rc.bottom }, g_sfL, C_PH(255));
    int ix = tx + (int)gtext_w(L">", 1, g_fText) + S(6);

    // 文本区（前缀 + 组合串 + 剩余 + 块状 caret）
    RECT tr = { ix, rc.top, rc.right - S(8), rc.bottom };
    GdipSetClipRectI(g_gfx, tr.left - 2, tr.top, tr.right - tr.left + 8, tr.bottom - tr.top, 0);
    if (g_search[0] || g_complen) {
        if (g_caret > 0)
            gtext(g_search, g_caret, g_fText, tr, g_sfL, C_PALE(235));
        float preW = g_caret ? gtext_w(g_search, g_caret, g_fText) : 0.0f;
        int compX = ix + (int)preW;
        if (g_complen) {                       // IME 组合串 + 下划线
            RECT cr = { compX, rc.top, tr.right, rc.bottom };
            gtext(g_comp, g_complen, g_fText, cr, g_sfL, C_PALE(235));
            float cw2 = gtext_w(g_comp, g_complen, g_fText);
            hline(compX, compX + (int)cw2, midY + S(9), C_PH(210));
            compX += (int)cw2;
        }
        if (g_caret < g_slen) {
            RECT rr2 = { compX, rc.top, tr.right, rc.bottom };
            gtext(g_search + g_caret, g_slen - g_caret, g_fText, rr2, g_sfL, C_PALE(235));
        }
    }
    // 块状 caret：~10x16 实心磷绿块；盖住字符时反色重绘黑字（终端反色块）
    if (g_caretOn && !g_complen) {
        float preW = g_caret ? gtext_w(g_search, g_caret, g_fText) : 0.0f;
        int cxx = ix + (int)preW;
        int ch = S(16);
        int cw2 = S(9);
        if (g_caret < g_slen) {
            float chw = gtext_w(&g_search[g_caret], 1, g_fText);
            if (chw + 0.99f > (float)cw2) cw2 = (int)chw + 1;
        }
        fill_rect(cxx, midY - ch / 2, cw2, ch, C_PH(215));
        if (g_caret < g_slen)
            gtext(&g_search[g_caret], 1, g_fText,
                  (RECT){ cxx, rc.top, cxx + cw2 + 2, rc.bottom }, g_sfL, ARGB(255, 4, 9, 6));
    }
    GdipResetClip(g_gfx);
}

static void draw_recents(void)
{
    // 最近快捷行：与网格同款终端格（hover = 磷激发 + glow，不浮动位移——终端不搞悬浮）
    for (int i = 0; i < 7; i++) {
        int ei = (i < g_nRecent && g_recentIdx[i] < g_store.count) ? g_recentIdx[i] : -1;
        if (ei < 0) continue;
        RECT rc = g_recent[i];
        BOOL hov = PtInRect(&rc, g_mouse);
        if (hov) draw_glow(g_glowSoft, rc, S(10));
        term_fill(rc, hov ? 1 : 0);
        int drew = 0;
        if (hov && g_anim.ei == ei && g_anim.pb) {
            draw_image_contain(g_anim.pb[g_anim.cur], g_anim.w, g_anim.h, rc, 1);
            drew = 1;
        }
        if (!drew) {
            int iw, ih;
            GpBitmap* pb = oneshot_get(&g_recentImg[i], ei, &iw, &ih);
            if (pb) draw_image_contain(pb, iw, ih, rc, 1);
        }
        term_frame(rc, hov ? 1 : 0);
    }
}

// 标签 = 方括号 token：[全部] / [动物]…；激活 = 反色（磷绿底黑字 + glow）
static void draw_chip(int* px, int y, const wchar_t* txt, int id)
{
    wchar_t buf[128];
    wnsprintfW(buf, 128, L"[%s]", txt);
    float w = gtext_w(buf, -1, g_fUi) + S(10);
    int h = S(26);
    RECT rc = { *px, y, *px + (int)w, y + h };
    if (rc.right > g_panel.right - S(18)) return;    // 放不下的不画
    int active = (id == -1) ? (g_tagFilter == 0) : (g_tagFilter == ((u64)1 << id));
    int hov = PtInRect(&rc, g_mouse);

    if (active) {
        draw_glow(g_glowSoft, rc, S(8));             // 磷光在「通电」处才亮
        fill_rect(rc.left, rc.top, rc.right - rc.left, h, C_PH(228));
        gtext(buf, -1, g_fUi, rc, g_sfC, ARGB(255, 4, 9, 6));
    } else {
        if (hov) fill_rect(rc.left, rc.top, rc.right - rc.left, h, C_PH(26));
        gtext(buf, -1, g_fUi, rc, g_sfC, C_PH(hov ? 235 : 150));
    }

    if (g_nChips < MP_MAX_TAGS + 1) {
        g_chipRc[g_nChips] = rc;
        g_chipId[g_nChips] = id;
        g_nChips++;
    }
    *px = rc.right + S(8);
}

static void draw_empty(void)
{
    static const wchar_t MSG[] = L"> 空库：把表情图拖到这里";
    int cx = (g_grid.left + g_grid.right) / 2;
    int cy = (g_grid.top + g_grid.bottom) / 2;
    gtext(MSG, -1, g_fUi,
          (RECT){ g_grid.left, cy - S(12), g_grid.right, cy + S(8) }, g_sfC, C_PH(150));
    if (g_caretOn) {                                  // 命令行等待态：闪烁块
        float w = gtext_w(MSG, -1, g_fUi);
        fill_rect(cx + (int)(w / 2) + S(3), cy - S(6), S(7), S(12), C_PH(190));
    }
}

static void draw_grid(void)
{
    if (g_nFilt == 0) { draw_empty(); return; }
    for (int k = 0; k < g_cols * g_rows; k++) {
        int idx = g_first + k;
        if (idx >= g_nFilt) break;
        int col = k % g_cols, row = k / g_cols;
        RECT rc = { g_grid.left + col * (g_cell + g_gap),
                    g_grid.top + row * (g_cell + g_gap),
                    0, 0 };
        rc.right = rc.left + g_cell; rc.bottom = rc.top + g_cell;
        BOOL hov = (idx == g_hover);
        BOOL sel = g_mgr && sel_has(g_filt[idx]);
        if (hov) draw_glow(g_glowSoft, rc, S(10));   // 磷激发辉光（预渲染图）
        int st = sel ? 2 : (hov ? 1 : 0);
        term_fill(rc, st);

        Entry* e = &g_store.entries[g_filt[idx]];
        int iw, ih;
        if (hov && g_anim.ei == g_filt[idx] && g_anim.pb) {
            // 悬停中的 GIF：播放动画帧（解码未就绪时走静态 fallback）
            draw_image_contain(g_anim.pb[g_anim.cur], g_anim.w, g_anim.h, rc, 1);
        } else {
            GpBitmap* pb = tc_get(idx, &iw, &ih);
            if (pb) draw_image_contain(pb, iw, ih, rc, 1);
        }
        // 框压在图上（满幅缩略图会盖住先画的框）
        term_frame(rc, st);

        // 管理选中：琥珀 X 记号（图上方：中心短线交叉 + 黑衬底）
        if (sel) {
            int cx2 = (rc.left + rc.right) / 2, cy2 = (rc.top + rc.bottom) / 2;
            int a = S(8);
            fill_rect(cx2 - S(12), cy2 - S(12), S(24), S(24), ARGB(165, 4, 8, 6));
            GpPen* xp = NULL;
            if (GdipCreatePen1(C_AMB(255), 2.0f, GP_UNIT_PIXEL, &xp) == 0) {
                GdipDrawLineI(g_gfx, xp, cx2 - a, cy2 - a, cx2 + a, cy2 + a);
                GdipDrawLineI(g_gfx, xp, cx2 + a, cy2 - a, cx2 - a, cy2 + a);
                GdipDeletePen(xp);
            }
        }
        // GIF 角标：~GIF~ 琥珀小字 + 黑衬条（琥珀只给 GIF/警示）
        if (is_gif_name(e->name)) {
            RECT gc = { rc.right - S(36), rc.bottom - S(14), rc.right - S(2), rc.bottom - S(2) };
            fill_rect(gc.left - S(2), gc.top - S(2),
                      (gc.right - gc.left) + S(4), (gc.bottom - gc.top) + S(4), ARGB(175, 4, 8, 6));
            gtext(L"~GIF~", -1, g_fTiny, gc, g_sfC, C_AMB(235));
        }
        // hover 名字条：黑条 + `> filename` 磷绿
        if (hov) {
            RECT nb = { rc.left, rc.bottom - S(18), rc.right, rc.bottom };
            fill_rect(nb.left, nb.top, nb.right - nb.left, nb.bottom - nb.top, ARGB(205, 3, 6, 5));
            frame_rect((RECT){ nb.left, nb.top, nb.right, nb.bottom }, C_PH(140), 1);
            wchar_t line[160];
            wnsprintfW(line, 160, L"> %s", e->name);
            gtext(line, -1, g_fTiny,
                  (RECT){ nb.left + S(5), nb.top, nb.right - S(4), nb.bottom - S(1) },
                  g_sfL, C_PH(235));
        }
    }
    // 右缘琥珀块状滚动指示（▓ 字符块风：矩形串，非字符字面量）
    int vis = g_cols * g_rows;
    if (g_nFilt > vis) {
        int trackH = g_grid.bottom - g_grid.top;
        int thumbH = trackH * vis / g_nFilt;
        if (thumbH < S(24)) thumbH = S(24);
        int maxF = g_nFilt - vis;
        int thumbY = g_grid.top + (int)((trackH - thumbH) * (maxF ? (double)g_first / maxF : 0));
        int sx = g_grid.right + S(6);
        for (int yy = thumbY; yy + S(7) <= thumbY + thumbH; yy += S(9))
            fill_rect(sx, yy, S(5), S(7), C_AMB(205));
    }
}

// 状态行右段：▓ 块状进度（滚动位置，画矩形串）+ 计数（速发/管理共用）
static void draw_status_right(const wchar_t* cntTxt)
{
    RECT rc = g_footer;
    float cw = gtext_w(cntTxt, -1, g_fMono);
    int bx = rc.right - (int)cw - S(8);               // 块串右端
    int nB = 5;
    int vis = g_cols * g_rows, maxF = g_nFilt - vis;
    int fillN = maxF <= 0 ? nB : 1 + (int)((double)g_first / maxF * (nB - 1) + 0.5);
    if (fillN < 1) fillN = 1; if (fillN > nB) fillN = nB;
    int midY = (rc.top + rc.bottom) / 2;
    for (int i = 0; i < nB; i++) {
        int x = bx - (nB - i) * S(10);
        fill_rect(x, midY - S(5), S(7), S(10), i < fillN ? C_PH(215) : C_PH(40));
    }
    gtext(cntTxt, -1, g_fMono,
          (RECT){ bx, rc.top, rc.right, rc.bottom }, g_sfR, C_PH(170));
}

static void draw_footer(void)
{
    // 左：悬停 = `> 文件名 · 标签` 功能反馈；平时 READY + 闪烁块
    RECT rc = g_footer;
    if (g_hover >= 0 && g_hover < g_nFilt) {
        Entry* e = &g_store.entries[g_filt[g_hover]];
        wchar_t line[160];
        int n = wnsprintfW(line, 160, L"> %s", e->name);
        int first = 1;
        for (int t = 0; t < MP_MAX_TAGS && n < 155; t++) {
            if (!(e->tagmask & ((u64)1 << t)) || !g_store.tagNames[t]) continue;
            n += wnsprintfW(line + n, 160 - n, first ? L" · %s" : L" / %s", g_store.tagNames[t]);
            first = 0;
        }
        gtext(line, -1, g_fSmall,
              (RECT){ rc.left, rc.top, rc.right - S(160), rc.bottom }, g_sfL, C_PH(200));
    } else {
        gtext(L"READY", -1, g_fSmall,
              (RECT){ rc.left, rc.top, rc.left + S(80), rc.bottom }, g_sfL, C_PH(180));
        if (g_caretOn) {
            float w = gtext_w(L"READY", -1, g_fSmall);
            int midY = (rc.top + rc.bottom) / 2;
            fill_rect(rc.left + (int)w + S(4), midY - S(5), S(7), S(10), C_PH(190));
        }
    }
    wchar_t cnt[32];
    wnsprintfW(cnt, 32, L"%d", g_nFilt);
    draw_status_right(cnt);
}

static void draw_preview(void)
{
    if (g_previewIdx < 0 || g_previewIdx >= g_nFilt) return;
    RECT rc = g_preview;
    Entry* e = &g_store.entries[g_filt[g_previewIdx]];
    int ei = g_filt[g_previewIdx];

    // 黑底 + 绿色双线框（方角）
    fill_rect(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, ARGB(215, 4, 8, 6));
    frame_rect(rc, C_PH(190), 1);
    frame_rect((RECT){ rc.left + S(3), rc.top + S(3), rc.right - S(3), rc.bottom - S(3) }, C_PH(70), 1);

    int iw = 0, ih = 0;
    if (g_anim.ei == ei && g_anim.pb) {
        iw = g_anim.w; ih = g_anim.h;
        draw_image_contain(g_anim.pb[g_anim.cur], iw, ih,
                           (RECT){ rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1 }, 1);
    } else {
        GpBitmap* pb = oneshot_get(&g_previewImg, ei, &iw, &ih);
        if (pb)
            draw_image_contain(pb, iw, ih,
                               (RECT){ rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1 }, 1);
    }
    // 框上标签：VIEW: 文件名（右对齐）
    RECT lab = { rc.left - S(8), rc.top - S(21), rc.right, rc.top - S(5) };
    float vw = gtext_w(L"VIEW:", -1, g_fTiny);
    float nw = gtext_w(e->name, -1, g_fTiny);
    int maxW = rc.right - rc.left;
    if ((int)(vw + S(7) + nw) > maxW) nw = (float)(maxW - (int)vw - S(7));
    if (nw < (float)S(30)) nw = (float)S(30);
    RECT nr = { rc.right - (int)nw - 2, lab.top, rc.right, lab.bottom };
    gtext(e->name, -1, g_fTiny, nr, g_sfR, C_PH(205));
    gtext(L"VIEW:", -1, g_fTiny,
          (RECT){ nr.left - S(7) - (int)vw - 2, lab.top, nr.left - S(7), lab.bottom },
          g_sfL, C_PH(120));
    // 框下帧信息：GIF 播放中 = `WxH NFR`（琥珀）；否则 `WxH IMG`
    RECT inf = { rc.left - S(8), rc.bottom + S(5), rc.right, rc.bottom + S(21) };
    BOOL anim = (g_anim.ei == ei && g_anim.pb && g_anim.nFrames > 1);
    wchar_t line[96];
    wnsprintfW(line, 96, L"%dx%d IMG", iw, ih);
    if (anim) wnsprintfW(line, 96, L"%dx%d %dFR", iw, ih, g_anim.nFrames);
    gtext(line, -1, g_fTiny, inf, g_sfR, anim ? C_AMB(220) : C_PH(120));
}

// 管理按钮 = 方括号 token：[重命名]…；hover 反色（绿底黑字）；删除武装 = 琥珀反色
// （借 caret 闪烁节拍做「闪烁感」的静态近似）。武装态 rect 固定按武装文案宽度。
static void draw_mgr_button(int* pright, int y, const wchar_t* txt, int id, BOOL danger)
{
    wchar_t buf[96], arm[96];
    wnsprintfW(buf, 96, L"[%s]", txt);
    wnsprintfW(arm, 96, L"[%s]", L"确认删除");   // 武装文案定宽：rect 不随武装态变化
    float w = gtext_w(id == BTN_DELETE ? arm : buf, -1, g_fUi) + S(10);
    RECT rc = { *pright - (int)w, y, *pright, y + S(30) };
    BOOL hov = PtInRect(&rc, g_mouse);
    BOOL on = (id == BTN_DELETE) && g_delArm;
    if (on) {
        fill_rect(rc.left, rc.top, rc.right - rc.left, S(30), C_AMB(g_caretOn ? 238 : 172));
        gtext(buf, -1, g_fUi, rc, g_sfC, ARGB(255, 8, 5, 0));
    } else if (hov) {
        fill_rect(rc.left, rc.top, rc.right - rc.left, S(30),
                  danger ? C_AMB(225) : C_PH(225));
        gtext(buf, -1, g_fUi, rc, g_sfC, ARGB(255, 4, 9, 6));
    } else {
        frame_rect(rc, danger ? C_AMB(130) : ARGB(120, T_DIM_R, T_DIM_G, T_DIM_B), 1);
        gtext(buf, -1, g_fUi, rc, g_sfC, danger ? C_AMB(205) : C_PH(190));
    }
    g_btnRc[id] = rc;
    *pright = rc.left - S(10);
}

// 管理模式：操作行（[标签] + [按钮]）+ 大格网格 + SYS: 状态行
static void draw_mgr(void)
{
    int opY = g_opY;
    g_nChips = 0;
    int cx = g_panel.left + S(18);
    draw_chip(&cx, opY + S(2), L"全部", -1);
    for (int i = 0; i < MP_MAX_TAGS; i++)
        if (g_store.tagNames[i]) draw_chip(&cx, opY + S(2), g_store.tagNames[i], i);

    // 按钮组右界必须让开关闭按钮（重叠会让点击被 closeRc 抢先命中=静默退出管理）
    int pr = g_closeRc.left - S(14);
    draw_mgr_button(&pr, opY, L"修后缀", BTN_FIXEXT, FALSE);
    draw_mgr_button(&pr, opY, L"去重", BTN_DEDUP, FALSE);
    draw_mgr_button(&pr, opY, g_delArm ? L"确认删除" : L"删除", BTN_DELETE, TRUE);
    draw_mgr_button(&pr, opY, L"重命名", BTN_RENAME, FALSE);

    // 网格（draw_grid 已按 g_mgr 画选中态）
    draw_grid();

    // SYS: 状态行（武装删除时琥珀警示）
    wchar_t line[160];
    if (g_status[0]) wnsprintfW(line, 160, L"SYS: %s", g_status);
    else             lstrcpynW(line, L"SYS: READY", 160);
    gtext(line, -1, g_fSmall,
          (RECT){ g_footer.left, g_footer.top, g_footer.right - S(170), g_footer.bottom },
          g_sfL, g_delArm ? C_AMB(225) : C_PH(185));
    wchar_t cnt[48];
    wnsprintfW(cnt, 48, L"%d/%d", g_nSel, g_nFilt);
    draw_status_right(cnt);
}


// 每帧合成 + 提交。淡入期间合成一次全亮度快照，动画帧只做拷贝+缩放+提交
static void panel_repaint(void)
{
    if (!g_bits || !g_gfx) return;
    memcpy(g_bits, g_base, (u64)g_winW * g_winH * 4);
    draw_titlebar();
    draw_search_box();
    if (g_mgr) {
        draw_mgr();
    } else {
        draw_recents();
        g_nChips = 0;
        int cx = g_panel.left + S(18);
        draw_chip(&cx, g_chipY, L"全部", -1);
        for (int i = 0; i < MP_MAX_TAGS; i++)
            if (g_store.tagNames[i]) draw_chip(&cx, g_chipY, g_store.tagNames[i], i);
        // 标签行下分隔线
        hline(g_panel.left + S(18), g_panel.right - S(18), g_chipY + S(30), C_PH(52));
        draw_grid();
        draw_footer();
    }
    draw_preview();
    if (g_fade < 255) {
        if (!g_fadeSnap)
            g_fadeSnap = (u32*)HeapAlloc(GetProcessHeap(), 0, (u64)g_winW * g_winH * 4);
        if (g_fadeSnap) memcpy(g_fadeSnap, g_bits, (u64)g_winW * g_winH * 4);
    } else if (g_fadeSnap) {
        HeapFree(GetProcessHeap(), 0, g_fadeSnap);
        g_fadeSnap = NULL;
    }
    fade_mul();
    submit_ulw();
}

// 动画帧快路径：不重新合成
static void fade_frame(void)
{
    if (!g_fadeSnap || !g_bits) return;
    memcpy(g_bits, g_fadeSnap, (u64)g_winW * g_winH * 4);
    fade_mul();
    submit_ulw();
}

// ============================================================
// 自绘输入框：编辑 / IME
// ============================================================
static void search_changed(void)
{
    if (g_renameEi >= 0) {   // 重命名编辑不触发过滤
        panel_repaint();
        return;
    }
    refilter();
    panel_repaint();
}

static void search_insert(const wchar_t* s, int n)
{
    if (g_slen + g_complen + n >= 126) return;
    memmove(g_search + g_caret + n, g_search + g_caret, (g_slen - g_caret) * 2);
    memcpy(g_search + g_caret, s, n * 2);
    g_caret += n; g_slen += n;
    g_search[g_slen] = 0;
    search_changed();
}

static void ime_update_position(HWND hwnd)
{
    HIMC im = ImmGetContext(hwnd);
    if (!im) return;
    float preW = g_caret ? gtext_w(g_search, g_caret, g_fText) : 0.0f;
    int cxx = g_searchBox.left + S(2) + (int)gtext_w(L">", 1, g_fText) + S(6) + (int)preW;
    int cyy = (g_searchBox.top + g_searchBox.bottom) / 2;
    POINT sp = { cxx, cyy + S(9) };
    ClientToScreen(hwnd, &sp);
    COMPOSITIONFORM cf;
    cf.dwStyle = CFS_FORCE_POSITION;
    cf.ptCurrentPos = sp;
    cf.rcArea.left = 0; cf.rcArea.top = 0; cf.rcArea.right = 0; cf.rcArea.bottom = 0;
    ImmSetCompositionWindow(im, &cf);
    CANDIDATEFORM cad;
    cad.dwIndex = 0;
    cad.dwStyle = CFS_CANDIDATEPOS;
    cad.ptCurrentPos.x = sp.x; cad.ptCurrentPos.y = sp.y + S(18);
    ImmSetCandidateWindow(im, &cad);
    ImmReleaseContext(hwnd, im);
}

// ============================================================
// 窗口过程
// ============================================================
static LRESULT CALLBACK PanelProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {   // ULW 窗口不走 WM_PAINT，防御性应答
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE:
        panel_layout(hwnd);
        if (IsWindowVisible(hwnd)) panel_repaint();
        return 0;

    case WM_TIMER:
        if (wParam == ANIM_TIMER) {
            g_fade += 52;
            if (g_fade >= 255) {
                g_fade = 255;
                KillTimer(hwnd, ANIM_TIMER);
                panel_repaint();          // 末帧全量（并释放快照）
            } else {
                fade_frame();             // 快路径：快照 + alpha 缩放 + 提交
            }
            return 0;
        }
        if (wParam == CARET_TIMER) {
            g_caretOn = !g_caretOn;
            if (IsWindowVisible(hwnd)) panel_repaint();
            return 0;
        }
        if (wParam == PREVIEW_TMR) {
            KillTimer(hwnd, PREVIEW_TMR);
            if (g_hover >= 0) { g_previewIdx = g_hover; panel_repaint(); }
            return 0;
        }
        if (wParam == ARM_TMR) {   // 删除二次确认超时复位
            KillTimer(hwnd, ARM_TMR);
            if (g_delArm) { g_delArm = FALSE; panel_repaint(); }
            return 0;
        }
        if (wParam == GIF_TIMER) {
            if (g_anim.ei >= 0 && g_anim.pb && g_anim.nFrames > 1) {
                g_anim.cur = (g_anim.cur + 1) % g_anim.nFrames;
                SetTimer(hwnd, GIF_TIMER, g_anim.delays[g_anim.cur], NULL);
                panel_repaint();
            }
            return 0;
        }
        break;

    case WM_COMMAND:
        return 0;

    case WM_GIFDONE:
        anim_on_done(hwnd, (int)wParam, (GifFrames*)lParam);
        return 0;

    case WM_HOTKEY:
        if (wParam == HOTKEY_ID) {
            if (IsWindowVisible(hwnd)) panel_hide(hwnd);
            else panel_show(hwnd);
        }
        return 0;

    case WM_SETFOCUS:
        g_caretOn = TRUE;
        SetTimer(hwnd, CARET_TIMER, 530, NULL);
        return 0;

    case WM_KILLFOCUS:
        KillTimer(hwnd, CARET_TIMER);
        return 0;

    // ---- 键盘：导航 + 自绘输入框编辑 ----
    case WM_KEYDOWN:
        if (g_complen) break;   // IME 组合中：Enter/Esc/方向键/退格全部交给输入法，不触发面板行为
        if (g_renameEi >= 0) {  // 重命名编辑：仅确认/取消，其余走编辑键
            if (wParam == VK_RETURN) { mgr_rename_end(hwnd, TRUE); return 0; }
            if (wParam == VK_ESCAPE) { mgr_rename_end(hwnd, FALSE); return 0; }
        } else if (g_mgr) {
            if (wParam == VK_ESCAPE) { mgr_exit(hwnd); return 0; }
            if (wParam == VK_F2 && g_nSel == 1) { mgr_rename_begin(); return 0; }
            if (wParam == VK_DELETE && g_nSel > 0) { mgr_delete(hwnd); return 0; }
        }
        switch (wParam) {
        case VK_ESCAPE:
            panel_hide(hwnd); return 0;
        case VK_RETURN:
            if (!g_mgr && g_nFilt > 0) paste_entry(hwnd, g_filt[g_first]);
            return 0;
        case VK_PRIOR: g_first -= g_rows * g_cols; clamp_first(); panel_repaint(); return 0;
        case VK_NEXT:  g_first += g_rows * g_cols; clamp_first(); panel_repaint(); return 0;
        case VK_UP:    g_first -= g_cols; clamp_first(); panel_repaint(); return 0;
        case VK_DOWN:  g_first += g_cols; clamp_first(); panel_repaint(); return 0;
        case VK_LEFT:  if (g_caret > 0) { g_caret--; g_caretOn = TRUE; panel_repaint(); } return 0;
        case VK_RIGHT: if (g_caret < g_slen) { g_caret++; g_caretOn = TRUE; panel_repaint(); } return 0;
        case VK_HOME:  g_caret = 0; g_caretOn = TRUE; panel_repaint(); return 0;
        case VK_END:   g_caret = g_slen; g_caretOn = TRUE; panel_repaint(); return 0;
        case VK_BACK:
            if (g_caret > 0) {
                memmove(g_search + g_caret - 1, g_search + g_caret, (g_slen - g_caret) * 2);
                g_caret--; g_slen--; g_search[g_slen] = 0;
                search_changed();
            }
            return 0;
        case VK_DELETE:
            if (g_caret < g_slen) {
                memmove(g_search + g_caret, g_search + g_caret + 1, (g_slen - g_caret - 1) * 2);
                g_slen--; g_search[g_slen] = 0;
                search_changed();
            }
            return 0;
        default:
            break;   // 数字键不再直发（用户决定）：回归纯文本输入，最近行仅点击发送
        }
        break;

    case WM_CHAR: {
        wchar_t c = (wchar_t)wParam;
        // 组合期间忽略（结果串走 WM_IME_COMPOSITION，避免重复）
        if (g_complen) return 0;
        if (c >= 32 && c != 127) {
            search_insert(&c, 1);
            g_caretOn = TRUE;
        }
        return 0;
    }

    // ---- IME ----
    case WM_IME_SETCONTEXT:
        lParam &= ~ISC_SHOWUICOMPOSITIONWINDOW;   // 组合串自绘
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    case WM_IME_STARTCOMPOSITION:
        ime_update_position(hwnd);
        return 0;

    case WM_IME_COMPOSITION: {
        HIMC im = ImmGetContext(hwnd);
        if (!im) return 0;
        wchar_t buf[128];
        if (lParam & GCS_RESULTSTR) {
            LONG n = ImmGetCompositionStringW(im, GCS_RESULTSTR, NULL, 0);
            if (n > 0 && n <= 250) {
                ImmGetCompositionStringW(im, GCS_RESULTSTR, buf, n);
                buf[n / 2] = 0;
                search_insert(buf, n / 2);
            }
            g_complen = 0; g_comp[0] = 0;
        }
        if (lParam & GCS_COMPSTR) {
            LONG n = ImmGetCompositionStringW(im, GCS_COMPSTR, NULL, 0);
            if (n > 0 && n <= 250) {
                ImmGetCompositionStringW(im, GCS_COMPSTR, buf, n);
                n /= 2;
                if (n > 120) n = 120;
                memcpy(g_comp, buf, n * 2);
                g_comp[n] = 0; g_complen = n;
            } else {
                g_comp[0] = 0; g_complen = 0;
            }
            ime_update_position(hwnd);
            panel_repaint();
        }
        ImmReleaseContext(hwnd, im);
        return 0;
    }

    case WM_IME_ENDCOMPOSITION:
        if (g_complen) { g_comp[0] = 0; g_complen = 0; panel_repaint(); }
        return 0;

    // ---- 鼠标 ----
    case WM_MOUSEMOVE: {
        g_mouse.x = (short)LOWORD(lParam);
        g_mouse.y = (short)HIWORD(lParam);
        int hv = cell_from_point(g_mouse);
        // 最近行的 hover 也驱动动画请求（ei 相同则无开销）
        int hvEi = -1;
        if (hv >= 0) hvEi = g_filt[hv];
        else for (int i = 0; i < 7; i++)
            if (PtInRect(&g_recent[i], g_mouse) && i < g_nRecent) { hvEi = g_recentIdx[i]; break; }
        if (hvEi != g_anim.want) anim_request(hwnd, hvEi);
        if (hv != g_hover) {
            g_hover = hv;
            g_previewIdx = -1;
            KillTimer(hwnd, PREVIEW_TMR);
            if (hv >= 0) SetTimer(hwnd, PREVIEW_TMR, 280, NULL);
            panel_repaint();
        }
        BOOL hand = hv >= 0 || PtInRect(&g_closeRc, g_mouse);
        if (!hand) for (int i = 0; i < 7; i++)
            if (PtInRect(&g_recent[i], g_mouse) && g_recentIdx[i] >= 0) { hand = TRUE; break; }
        if (!hand) for (int i = 0; i < g_nChips; i++)
            if (PtInRect(&g_chipRc[i], g_mouse)) { hand = TRUE; break; }
        SetCursor(LoadCursorW(NULL, hand ? IDC_HAND : IDC_ARROW));
        if (!g_tracking) {
            TRACKMOUSEEVENT tm;
            tm.cbSize = sizeof tm; tm.dwFlags = TME_LEAVE; tm.hwndTrack = hwnd;
            tm.dwHoverTime = 0;
            if (TrackMouseEvent(&tm)) g_tracking = TRUE;
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        g_hover = -1;
        g_previewIdx = -1;
        g_tracking = FALSE;
        g_mouse.x = g_mouse.y = -30000;
        anim_request(hwnd, -1);
        panel_repaint();
        return 0;

    case WM_MOUSEWHEEL: {
        int step = g_rows > 2 ? 2 : 1;
        if (GET_WHEEL_DELTA_WPARAM(wParam) > 0) g_first -= step;
        else g_first += step;
        clamp_first();
        panel_repaint();
        return 0;
    }

    case WM_LBUTTONUP: {
        POINT pt;
        pt.x = (short)LOWORD(lParam);
        pt.y = (short)HIWORD(lParam);
        if (g_renameEi >= 0) return 0;              // 重命名编辑中不响应点击
        if (PtInRect(&g_closeRc, pt)) {
            if (g_mgr) mgr_exit(hwnd);
            else panel_hide(hwnd);
            return 0;
        }
        if (g_mgr) {
            // 操作按钮
            for (int i = 0; i < 4; i++)
                if (PtInRect(&g_btnRc[i], pt)) {
                    if (i == BTN_RENAME && g_nSel == 1) mgr_rename_begin();
                    else if (i == BTN_DELETE) mgr_delete(hwnd);
                    else if (i == BTN_DEDUP) mgr_dedup(hwnd);
                    else if (i == BTN_FIXEXT) mgr_fixext(hwnd);
                    return 0;
                }
            // 胶囊：选中>0 = 批量打标（以首个选中项的方向为准）；否则 = 过滤
            for (int i = 0; i < g_nChips; i++)
                if (PtInRect(&g_chipRc[i], pt)) {
                    int id = g_chipId[i];
                    if (g_nSel > 0 && id >= 0) {
                        u64 bit = (u64)1 << id;
                        BOOL firstHas = FALSE;
                        for (int k = 0; k < g_store.count; k++)
                            if (sel_has(k)) { firstHas = (g_store.entries[k].tagmask & bit) != 0; break; }
                        for (int k = 0; k < g_store.count; k++) {
                            if (!sel_has(k)) continue;
                            if (firstHas) g_store.entries[k].tagmask &= ~bit;
                            else g_store.entries[k].tagmask |= bit;
                        }
                        store_save(&g_store);
                        wnsprintfW(g_status, 96, L"已%s标签「%s」× %d",
                                   firstHas ? L"移除" : L"添加", g_store.tagNames[id], g_nSel);
                        panel_repaint();
                        return 0;
                    }
                    if (id == -1) g_tagFilter = 0;
                    else g_tagFilter = (g_tagFilter == ((u64)1 << id)) ? 0 : ((u64)1 << id);
                    refilter();
                    panel_repaint();
                    return 0;
                }
            // 网格：选择（Ctrl=多选 toggle；普通=单选重置）
            int idx = cell_from_point(pt);
            if (idx >= 0) {
                int ei = g_filt[idx];
                g_delArm = FALSE;
                if (wParam & MK_CONTROL) sel_set(ei, !sel_has(ei));
                else { sel_clear(); sel_set(ei, TRUE); }
                panel_repaint();
            }
            return 0;
        }
        for (int i = 0; i < 7; i++)
            if (PtInRect(&g_recent[i], pt) && g_recentIdx[i] >= 0) {
                paste_entry(hwnd, g_recentIdx[i]);
                return 0;
            }
        for (int i = 0; i < g_nChips; i++)
            if (PtInRect(&g_chipRc[i], pt)) {
                int id = g_chipId[i];
                if (id == -1) g_tagFilter = 0;
                else g_tagFilter = (g_tagFilter == ((u64)1 << id)) ? 0 : ((u64)1 << id);
                refilter();
                panel_repaint();
                return 0;
            }
        int idx = cell_from_point(pt);
        if (idx >= 0) paste_entry(hwnd, g_filt[idx]);
        return 0;
    }

    case WM_RBUTTONUP: {
        POINT pt;
        pt.x = (short)LOWORD(lParam);
        pt.y = (short)HIWORD(lParam);
        int idx = cell_from_point(pt);
        if (idx < 0) return 0;
        Entry* e = &g_store.entries[g_filt[idx]];
        HMENU m = CreatePopupMenu();
        for (int i = 0; i < MP_MAX_TAGS; i++) {
            if (!g_store.tagNames[i]) continue;
            AppendMenuW(m, MF_STRING | ((e->tagmask & ((u64)1 << i)) ? MF_CHECKED : 0),
                        IDM_TAG_BASE + i, g_store.tagNames[i]);
        }
        if (!GetMenuItemCount(m))
            AppendMenuW(m, MF_STRING | MF_GRAYED, 0, L"（拖入一个文件夹即可创建标签）");
        AppendMenuW(m, MF_SEPARATOR, 0, NULL);
        AppendMenuW(m, MF_STRING, IDM_MGR, L"管理…");
        POINT sp = pt;
        ClientToScreen(hwnd, &sp);
        SetForegroundWindow(hwnd);
        int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                 sp.x, sp.y, 0, NULL, NULL);
        DestroyMenu(m);
        if (cmd >= IDM_TAG_BASE) {
            e->tagmask ^= (u64)1 << (cmd - IDM_TAG_BASE);
            store_save(&g_store);
            panel_repaint();
        }
        else if (cmd == IDM_MGR) mgr_enter(hwnd);
        return 0;
    }

    case WM_DROPFILES:
        import_drop(hwnd, (HDROP)wParam);
        return 0;

    case WM_ACTIVATEAPP:
        if (!wParam) panel_hide(hwnd);
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
            else if (cmd == IDM_MGR) { panel_show(hwnd); mgr_enter(hwnd); }
            else if (cmd == IDM_RESCAN) {
                store_scan(&g_store);
                refilter();
                recalc_recents();
                if (IsWindowVisible(hwnd)) panel_repaint();
                lstrcpynW(g_nid.szInfoTitle, APP_NAME, 64);
                wchar_t info[96];
                wnsprintfW(info, 96, L"扫描完成，库共 %d 张", g_store.count);
                lstrcpynW(g_nid.szInfo, info, 256);
                g_nid.uFlags = NIF_INFO;
                Shell_NotifyIconW(NIM_MODIFY, &g_nid);
                g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
            }
            else if (cmd == IDM_EXIT) DestroyWindow(hwnd);
        }
        return 0;

    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        UnregisterHotKey(hwnd, HOTKEY_ID);
        store_save(&g_store);
        tc_clear();
        anim_free();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================
// 图标（终端风：近黑绿磷屏 + 绿方框 + > 提示符 + 块状 caret；GDI 画在不透明 icon DIB）
// ============================================================
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
    void* colorBits = NULL, * maskBits = NULL;
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
        // 近黑绿磷屏底（对角微渐变）
        u32* px = (u32*)colorBits;
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 64; x++) {
                float t = (x + y) / 126.0f;
                u8 r = (u8)(6 + 4 * t), g = (u8)(12 + 5 * t), b = (u8)(8 + 3 * t);
                px[y * 64 + x] = 0xFF000000u | (r << 16) | (g << 8) | b;   // alpha 修补：icon 走 ARGB 路径也正确
            }
        HDC mdc = CreateCompatibleDC(sdc);
        HGDIOBJ old = SelectObject(mdc, color);
        // 双线绿框（方角）
        HBRUSH ph = CreateSolidBrush(RGB(42, 255, 136));
        RECT f1 = { 5, 5, 59, 7 }, f2 = { 5, 57, 59, 59 }, f3 = { 5, 5, 7, 59 }, f4 = { 57, 5, 59, 59 };
        FillRect(mdc, &f1, ph); FillRect(mdc, &f2, ph);
        FillRect(mdc, &f3, ph); FillRect(mdc, &f4, ph);
        // 扫描线
        HPEN lp = CreatePen(PS_SOLID, 1, RGB(3, 7, 5));
        HGDIOBJ op = SelectObject(mdc, lp);
        for (int y = 10; y < 56; y += 3) {
            MoveToEx(mdc, 9, y, NULL); LineTo(mdc, 55, y);
        }
        SelectObject(mdc, op);
        // > 提示符 + 块状 caret
        HPEN wp = CreatePen(PS_SOLID, 5, RGB(120, 255, 175));
        SelectObject(mdc, wp);
        SelectObject(mdc, GetStockObject(NULL_BRUSH));
        MoveToEx(mdc, 18, 22, NULL); LineTo(mdc, 31, 32); LineTo(mdc, 18, 42);
        RECT cb = { 40, 25, 48, 39 };
        FillRect(mdc, &cb, ph);
        SelectObject(mdc, old);
        DeleteDC(mdc);
        DeleteObject(ph); DeleteObject(lp); DeleteObject(wp);
        // alpha 通道 GDI 会写 0：整体修补回 255（mask 兜底为全不透明）
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
// 无头渲染（-shot）：视觉验证/设计迭代专用——不显示窗口、不注册热键/托盘、
// 不发送任何合成输入；与真实显示共用同一条合成管线（panel_layout -> panel_repaint），
// 仅把 DIB 合成到确定性背景上后经 WIC 存成 PNG。用法：
//   MemePanel.exe -shot <file.png> [-mgr] [-hover N] [-tag N] [-text 词]
//     -hover N  模拟悬停第 N 个可见格（含 hover 态 + 名字条 + 168px 大图；管理模式下同时选中）
//     -tag N    激活第 N 个标签胶囊
//     -mgr      管理模式（60%×66% 大窗、96px 大格）
// ============================================================
static int watoi_(const wchar_t* s)
{
    while (*s == L' ') s++;
    int v = 0;
    while (*s >= L'0' && *s <= L'9') { v = v * 10 + (*s - L'0'); s++; }
    return v;
}

// 返回命令行里 "-flag " 之后的指针（无则 NULL）
static const wchar_t* wfind_ci_(const wchar_t* hay, const wchar_t* needle)
{
    if (!hay || !needle || !*needle) return NULL;
    for (const wchar_t* p = hay; *p; p++) {
        const wchar_t* a = p, * b = needle;
        while (*a && *b) {
            wchar_t ca = *a, cb = *b;
            if (ca >= L'A' && ca <= L'Z') ca += 32;
            if (cb >= L'A' && cb <= L'Z') cb += 32;
            if (ca != cb) break;
            a++; b++;
        }
        if (!*b) return p;
    }
    return NULL;
}

static const wchar_t* cl_arg(const wchar_t* flag)
{
    const wchar_t* p = wfind_ci_(GetCommandLineW(), flag);
    if (!p) return NULL;
    p += wlen(flag);
    while (*p == L' ') p++;
    return *p ? p : NULL;
}

static void cl_token(const wchar_t* p, wchar_t* out, int cap)
{
    int n = 0;
    while (p && *p && *p != L' ' && n < cap - 1) out[n++] = *p++;
    out[n] = 0;
}

// 确定性"桌面"背景：暗蓝灰垂直渐变 + 暖/冷两个柔光块 + 轻暗角。
// 所有变体/状态共用同一公式——玻璃半透明才有可比性。
static void shot_bg_px(int x, int y, int W, int H, int* oR, int* oG, int* oB)
{
    float u = (x + 0.5f) / W, v = (y + 0.5f) / H;
    float ar = (float)H / W;
    float r = 56 + (34 - 56) * v, g = 64 + (38 - 64) * v, b = 84 + (50 - 84) * v;
    float d1x = u - 0.22f, d1y = (v - 0.30f) * ar;
    float e1 = d1x * d1x + d1y * d1y;
    if (e1 < 1.0f) { float k = 0.13f * (1 - e1) * (1 - e1); r += (196 - r) * k; g += (148 - g) * k; b += (96 - b) * k; }
    float d2x = u - 0.80f, d2y = (v - 0.72f) * ar;
    float e2 = d2x * d2x + d2y * d2y;
    if (e2 < 1.0f) { float k = 0.15f * (1 - e2) * (1 - e2); r += (86 - r) * k; g += (140 - g) * k; b += (200 - b) * k; }
    float dx = u - 0.5f, dy = (v - 0.5f) * ar;
    float q = dx * dx + dy * dy;
    if (q > 0.02f) { float s = 1.0f - 0.22f * fmin_(q * 2.2f, 1.0f); r *= s; g *= s; b *= s; }
    *oR = (int)r; *oG = (int)g; *oB = (int)b;
}

static void shot_run(HWND hwnd)
{
    wchar_t path[MAX_PATH];
    cl_token(cl_arg(L"-shot"), path, MAX_PATH);
    if (!path[0]) lstrcpynW(path, L"shot.png", MAX_PATH);

    // 演示数据无使用记录：无头导出时伪造最近使用时间，让「最近行」进入构图
    for (int i = 0; i < 7 && g_store.count; i++) {
        int ei = (int)((u64)i * (u64)g_store.count / 7);
        if (ei >= g_store.count) ei = g_store.count - 1;
        g_store.entries[ei].used = GetTickCount64() - (u64)(7 - i) * 3600000ull;
    }
    recalc_recents();

    if (wcontains_ci(GetCommandLineW(), L"-mgr")) {
        g_mgr = TRUE;
        sel_reserve(g_store.count);
        lstrcpynW(g_status, L"已添加标签「猫猫」× 3", 96);   // 状态行样例文案
    }
    if (cl_arg(L"-tag")) {
        int id = watoi_(cl_arg(L"-tag"));
        if (id >= 0 && id < MP_MAX_TAGS && g_store.tagNames[id]) {
            g_tagFilter = (u64)1 << id;
            refilter();
        }
    }
    if (cl_arg(L"-text")) {
        wchar_t tok[64];
        cl_token(cl_arg(L"-text"), tok, 64);
        lstrcpynW(g_search, tok, 127);
        g_slen = (int)wlen(g_search);
        g_caret = g_slen;
        refilter();
    }

    // 与 panel_show/mgr_enter 同一套尺寸（隐藏窗口，无任何屏上影响）
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    int pw, ph;
    if (g_mgr) { pw = sw * 60 / 100; ph = sh * 66 / 100; }
    else {
        pw = sw * 46 / 100; ph = sh * 54 / 100;
        if (pw < S(700)) pw = S(700);
        if (ph < S(460)) ph = S(460);
    }
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, pw + g_bleed * 2, ph + g_bleed * 2,
                 SWP_NOACTIVATE | SWP_NOMOVE);
    panel_layout(hwnd);
    g_fade = 255;
    g_caretOn = TRUE;

    if (cl_arg(L"-hover") && g_nFilt > 0) {
        int vis = g_cols * g_rows;
        int k = watoi_(cl_arg(L"-hover"));
        if (k < 0) k = 0;
        if (k >= vis) k = vis - 1;
        int idx = g_first + k;
        if (idx >= g_nFilt) idx = g_nFilt - 1;
        g_hover = idx;
        g_previewIdx = idx;                       // 大图浮层直接展开
        int col = (idx - g_first) % g_cols, row = (idx - g_first) / g_cols;
        g_mouse.x = g_grid.left + col * (g_cell + g_gap) + g_cell / 2;
        g_mouse.y = g_grid.top + row * (g_cell + g_gap) + g_cell / 2;
        if (g_mgr) sel_set(g_filt[idx], TRUE);
    }
    panel_repaint();

    // DIB（预乘）合成到背景 -> straight BGRA -> PNG
    int W = g_winW, H = g_winH;
    u8* out = (u8*)HeapAlloc(GetProcessHeap(), 0, (u64)W * H * 4);
    if (out && g_bits) {
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                u32 px = g_bits[(u64)y * W + x];
                u32 a = px >> 24;
                u8* o = out + ((u64)y * W + x) * 4;
                if (a == 0) {
                    int br, bg_, bb;
                    shot_bg_px(x, y, W, H, &br, &bg_, &bb);
                    o[0] = (u8)bb; o[1] = (u8)bg_; o[2] = (u8)br; o[3] = 255;
                } else if (a == 255) {
                    o[0] = (u8)(px & 255); o[1] = (u8)((px >> 8) & 255);
                    o[2] = (u8)((px >> 16) & 255); o[3] = 255;
                } else {
                    int br, bg_, bb;
                    shot_bg_px(x, y, W, H, &br, &bg_, &bb);
                    o[0] = (u8)((px & 255) + bb * (255 - a) / 255);
                    o[1] = (u8)(((px >> 8) & 255) + bg_ * (255 - a) / 255);
                    o[2] = (u8)(((px >> 16) & 255) + br * (255 - a) / 255);
                    o[3] = 255;
                }
            }
        }
        wic_save_png(out, W, H, W * 4, path);
        HeapFree(GetProcessHeap(), 0, out);
    }
}

// ============================================================
// 入口
// ============================================================
static GpFont* make_font(HDC dc, int px, int weight, const wchar_t* face)
{
    LOGFONTW lf;
    memset(&lf, 0, sizeof lf);
    lf.lfHeight = -px;
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    lstrcpynW(lf.lfFaceName, face, 32);
    GpFont* f = NULL;
    GdipCreateFontFromLogfontW(dc, &lf, &f);
    return f;
}

static GpStringFormat* make_sf(int h, int v)
{
    GpStringFormat* sf = NULL;
    if (GdipCreateStringFormat(0, 0, &sf)) return NULL;
    GdipSetStringFormatAlign(sf, h);
    GdipSetStringFormatLineAlign(sf, v);
    GdipSetStringFormatTrimming(sf, GP_TRIM_ELLIPSIS);
    return sf;
}

void entry(void)
{
    g_hInst = GetModuleHandleW(NULL);
    g_headless = wcontains_ci(GetCommandLineW(), L"-shot");

    BOOL (WINAPI *pSDPA)(void) =
        (BOOL (WINAPI*)(void))GetProcAddress(GetModuleHandleW(L"user32"), "SetProcessDPIAware");
    if (pSDPA) pSDPA();
    HDC zdc = GetDC(NULL);
    g_dpi = GetDeviceCaps(zdc, LOGPIXELSX);
    ReleaseDC(NULL, zdc);
    g_bleed = S(8);    // 角加强十字向外伸 S(5)：出血留足余量（无投影阴影）

    if (!g_headless) {   // 无头模式：可并行多次运行，跳过单实例互斥
        HANDLE mx = CreateMutexW(NULL, TRUE, L"MemePanel_SingleInstance");
        if (mx && GetLastError() == ERROR_ALREADY_EXISTS) {
            MessageBoxW(NULL, L"MemePanel 已在运行（见系统托盘）。", APP_NAME, MB_ICONINFORMATION);
            ExitProcess(0);
        }
    }

    if (!gp_init()) {
        if (g_headless) ExitProcess(1);
        MessageBoxW(NULL, L"GDI+ 初始化失败（gdiplus.dll）。", APP_NAME, MB_ICONERROR);
        ExitProcess(1);
    }
    wic_init();
    store_init(&g_store);
    g_anim.want = -1;
    g_anim.pending = -1;

    // 字体 / 格式 / 质感件：全站 Consolas（中文 fallback 雅黑），标题 Impact
    HDC fdc = GetDC(NULL);
    g_fText  = make_font(fdc, S(14), FW_NORMAL,   L"Consolas");
    g_fUi    = make_font(fdc, S(13), FW_NORMAL,   L"Consolas");
    g_fSmall = make_font(fdc, S(11), FW_NORMAL,   L"Consolas");
    g_fTiny  = make_font(fdc, S(9),  FW_NORMAL,   L"Consolas");
    g_fMono  = make_font(fdc, S(11), FW_NORMAL,   L"Consolas");
    g_fTitle = make_font(fdc, S(17), FW_NORMAL,   L"Impact");
    ReleaseDC(NULL, fdc);
    g_sfL  = make_sf(GP_ALIGN_NEAR,   GP_ALIGN_CENTER);
    g_sfC  = make_sf(GP_ALIGN_CENTER, GP_ALIGN_CENTER);
    g_sfR  = make_sf(GP_ALIGN_FAR,    GP_ALIGN_CENTER);
    g_sfCF = make_sf(GP_ALIGN_CENTER, GP_ALIGN_FAR);
    // 全部格式禁用换行：长文件名/标签行用省略号截断（默认会折行被矩形裁掉=破相）
    GdipSetStringFormatFlags(g_sfL,  GP_NOWRAP);
    GdipSetStringFormatFlags(g_sfC,  GP_NOWRAP);
    GdipSetStringFormatFlags(g_sfR,  GP_NOWRAP);
    GdipSetStringFormatFlags(g_sfCF, GP_NOWRAP);
    make_glow(&g_glowSoft, &g_glowSoftBuf, 0.25f);    // 激活标签/hover：peak 0.25
    make_glow(&g_glowStrong, &g_glowStrongBuf, 0.40f);
    g_memDC = CreateCompatibleDC(NULL);
    g_dcStockBmp = (HBITMAP)GetCurrentObject(g_memDC, OBJ_BITMAP);

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

    // WS_EX_LAYERED + UpdateLayeredWindow（不用 SetLayeredWindowAttributes —— 会切换出逐像素模式）
    g_hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
                              L"MemePanelWnd", APP_NAME,
                              WS_POPUP,
                              0, 0, S(760) + g_bleed * 2, S(480) + g_bleed * 2,
                              NULL, NULL, g_hInst, NULL);
    DragAcceptFiles(g_hwnd, TRUE);

    if (!g_headless) {
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
    }

    store_scan(&g_store);
    refilter();
    recalc_recents();
    g_mouse.x = g_mouse.y = -30000;

    if (g_headless) {
        shot_run(g_hwnd);
        ExitProcess(0);
    }

    if (wcontains_ci(GetCommandLineW(), L"-mgr")) {
        panel_show(g_hwnd);
        mgr_enter(g_hwnd);
    }
    else     if (wcontains_ci(GetCommandLineW(), L"-show"))
        panel_show(g_hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    ExitProcess(0);
}
