// main.c — MemePanel v0.5「瑞士网格 SWISS」v3：检视抽屉 + 横切全套
// 架构：UpdateLayeredWindow 整窗提交 32bpp 预乘 DIB；GDI+（flat C，见 gp.h）负责全部
//       矢量/文字/位图合成（GDI 直写 DIB 会破坏 alpha 通道，故一律 GDI+）。
//       静态底图（纯白玻璃方角面 + 2px 墨黑外框）resize 时预渲染一次；
//       每帧 = memcpy 底图 + 动态件绘制 + 一次 ULW 提交。
//       v3：管理模式彻底删除（产品词典中不存在「模式」）——右键就地编辑。
//       右缘检视抽屉（同窗加宽 S(300)，2px 黑竖线分隔）：96px 预览 + 元数据列 +
//       标签 chips 自动换行（含 + 新标签内联输入）+ 索引文字输入 + 武装删除；
//       批量态 = Ctrl+点击选择集（四角 accent 角标记）。数据模型 v2（indexText，
//       兼容加载 v1 并迁移）；搜索 = 标签名 ∪ 索引文字 ∪（文件名 ⊕ prefs 开关）；
//       修后缀改扫描后静默执行；去重移入托盘「维护」；方向键网格导航 + Enter 发送。
//       设计 tokens：黑白 + 主题强调色 g_accent（默认正红 #FF2B1E，theme.cfg / 托盘
//       菜单 / -accent 可改；仅 hover 框/编号、激活标签前块、caret、选择角标、武装删除）；
//       零圆角零阴影零渐变；全站唯一字体 Segoe UI；节标题大写 11px 加重；
//       网格线是主角（1px 发丝线只画在格子之间）；一切左对齐（x = padX 铁律）。
//       无 EDIT 子窗口（layered 主窗口下不可见）：输入框自绘，中文走 WM_IME_COMPOSITION，
//       三缓冲（搜索/索引/新标签）按 g_editFocus 路由。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <objbase.h>
#include <imm.h>
#include <commdlg.h>   // 仅取 CHOOSECOLORW 结构/常量定义；ChooseColorW 运行时动态加载（零新增导入表）
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
// 命令 ID 全量重排（v3）：修掉 v1 起 IDM_MGR=2100 与 IDM_TAG_BASE 同值的历史冲突
#define IDM_SHOW     2001
#define IDM_RESCAN   2002
#define IDM_EXIT     2003
#define IDM_TAG_BASE 2100                 // 2100..2163 右键标签 toggle
//（v3.2：右键菜单整体移除——右键直接展开抽屉；IDM_EDIT 随之废弃）
#define IDM_THEME_BASE 2210               // 2210..2214 预设，2215 自定义…
#define IDM_MAINT_DEDUP 2220              // 维护 ▸ 去重扫描…
#define IDM_SEARCHFLAG  2221              // 搜索：包含文件名（checkable）
#define THEME_MAGIC  0x31545054u           // 'TPT1'
#define PREFS_MAGIC  0x3150504Du          // 'MPP1'
#define ANIM_TIMER   2
#define CARET_TIMER  3
#define PREVIEW_TMR  4
#define GIF_TIMER    5
#define ARM_TMR      6                   // 删除二次确认武装 3 秒复位

// ---- 「瑞士网格 SWISS」tokens ----
// 墨黑 #111111 文字/横梁/外框；主题强调色 g_accent（默认正红 #FF2B1E）只干四件事：
// hover 框+编号、激活标签前置 6×6 方块、搜索 caret 块、管理武装删除底色；
// 面板底 rgba(250,250,249,0.96)；内部网格线 1px rgba(17,17,17,0.15)。
static u32 ARGB(int a, int r, int g, int b)
{
    return ((u32)(a & 255) << 24) | ((u32)(r & 255) << 16) | ((u32)(g & 255) << 8) | (u32)(b & 255);
}
// 主题强调色：COLORREF -> GDI+ ARGB（alpha 由调用点给）
// 注意 COLORREF 布局是 0x00BBGGRR：低字节 R、中 G、高 B
static COLORREF g_accent = RGB(255, 43, 30);
static u32 acc_argb(int a)
{
    return ARGB(a, g_accent & 255, (g_accent >> 8) & 255, (g_accent >> 16) & 255);
}
#define C_INK(a)   ARGB(a, 17, 17, 17)     // 墨黑 #111
#define C_RED(a)   acc_argb(a)             // 强调色（v2：随 g_accent 变）
#define C_PAPER(a) ARGB(a, 250, 250, 249)  // 纸白
#define C_HAIR     ARGB(38, 17, 17, 17)    // 发丝网格线 0.15

// 托盘「主题色」预设（顺序 = 菜单顺序；[0] 为默认正红）
static const COLORREF THEME_PRESETS[5] = {
    RGB(255, 43, 30),    // 正红（默认）
    RGB(0x1F, 0x4A, 0xFF),  // 钴蓝
    RGB(0x00, 0x8A, 0x3E),  // 常青
    RGB(0xFF, 0x7A, 0x00),  // 琥珀
    RGB(0x11, 0x11, 0x11),  // 墨黑
};

// ---- 全局状态 ----
static Store     g_store;
static HINSTANCE g_hInst;
static HWND      g_hwnd, g_lastTarget;
static BOOL      g_headless;   // -shot 无头渲染：不显示窗口、无托盘/热键，直接导出合成帧 PNG
static HICON     g_icon;
static NOTIFYICONDATAW g_nid;

// 合成表面（窗口 = 面板 + 四周 S(BLEED) 出血）
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

// 字体 / 字符串格式（全站唯一家族 Segoe UI，靠字重/字号分层）
static GpFont* g_fText, * g_fUi, * g_fSmall, * g_fTiny, * g_fNano, * g_fSect;  // Nano=8.5px 淡化字；Sect=11px 节标题加重
static GpStringFormat *g_sfL, * g_sfC, * g_sfCF;

// 布局（窗口坐标）
static int  g_dpi = 96;
static int  g_bleed;
static int  g_padX;             // 左对齐铁律：标签/搜索框左缘 = 面板左 + padX
static RECT g_panel;
static int  g_beamY;            // 搜索行下全宽 3px 黑横梁（v2：唯一重线条 = 新刊头）
static int  g_hairY;            // 标签行下的 1px 发丝线
static RECT g_searchBox, g_closeRc;
static RECT g_recent[7];
static int  g_recentIdx[7], g_nRecent;
static RECT g_chipRc[MP_MAX_TAGS + 1];
static int  g_chipId[MP_MAX_TAGS + 1], g_nChips;
static RECT g_grid, g_preview;
static int  g_cell, g_gap, g_cols, g_rows;
static int  g_gridR;            // 网格区右界（抽屉开 = drawerLeft；关 = panel.right）

// 数据 / 交互状态
static u64  g_tagFilter;
static int* g_filt; static int g_nFilt, g_capFilt;
static int  g_first, g_hover = -1, g_previewIdx = -1;
static POINT g_mouse; static BOOL g_tracking;

// ---- 输入焦点路由（v3 三缓冲）：搜索 / 抽屉索引文字 / 抽屉新标签 ----
// WM_CHAR / WM_IME_* / 编辑键全部对「当前焦点缓冲」操作；组合串 g_comp 同一时刻只属一个缓冲
typedef struct { wchar_t b[128]; int len, caret; } EBuf;
enum { EDIT_SEARCH = 0, EDIT_INDEX, EDIT_NEWTAG };
static int  g_editFocus = EDIT_SEARCH;
static EBuf g_ebS, g_ebI, g_ebN;             // 搜索（改即 refilter）/ 索引（改即写 entry）/ 新标签（Enter 提交）
static wchar_t g_comp[128];   static int g_complen;   // IME 组合串（路由到焦点缓冲）
static BOOL g_caretOn;

// ---- 检视抽屉（v3：右键就地编辑，替代已删除的管理模式）----
static BOOL g_drawer;              // 抽屉打开（面板右缘加宽 S(300)）
static int  g_drawerEi = -2;       // -2 无目标；-1 批量（= 当前选择集）；>=0 单目标 entry
static BOOL g_drawerNote;          // 批量头部附注「· 疑似重复」（去重扫描结果）
static BOOL g_delArm;              // 删除二次确认武装（3 秒复位）
static BOOL g_newtagOpen;          // 「+ 新标签」chip 展开为内联输入框
static BOOL g_modal;               // 主题色/资源管理器等模态期间：失焦不自动收起面板
static BOOL g_menuUp;              // 右键菜单弹出期间：同理（ownerless 菜单曾致面板被误收起）
static BOOL g_imeDbg;              // -imedebug：面板上叠加 IME 定位调试（十字锚点 + 坐标数字）
static POINT g_imeSp;  static RECT g_imeBr;  static BOOL g_imeHave;
static RECT g_imeRc;   static int g_imePreW; static int g_imeCompW;
static BOOL g_sysCaret;            // 隐形系统 caret（CreateCaret 后从不 Show，仅供输入法查询）
static int g_posX = -1, g_posY = -1;   // -pos X,Y 窗口定位覆盖（IME 偏移实验用）
static int  g_drawerLeft;          // 2px 黑竖分隔线 x（抽屉内容左界 = 其右 + padX）
static int  g_dContentL, g_dContentR;
static RECT g_dPrevRc;                                   // 96px 预览（批量态 = 计数块）
static RECT g_dNameRc, g_dSizeRc, g_dOpenRc, g_dClearRc; // 元数据列 / 批量 ✕ 清除
static RECT g_dIndexBox, g_dNewTagBox, g_dNewChipRc, g_dDelRc;
static RECT g_dChipRc[MP_MAX_TAGS + 1];                  // 抽屉标签 chips（含 + 新标签）
static int  g_dChipId[MP_MAX_TAGS + 1]; static int g_nDChips;
static int  g_dSecY, g_dIdxY;                            // 「标签」/「索引文字」节标题 y（drawer_layout 输出）

// ---- 选择集（Ctrl+点击 toggle；抽屉批量目标 = 选择集）----
static u8*  g_selBits;             // entry 索引选中位图
static int  g_nSel;

// ---- prefs.cfg：搜索「包含文件名」开关等（bit0，默认开）----
#define PF_NAMESEARCH 1
static u32 g_prefs = PF_NAMESEARCH;

// 选择位图随库增长自适应（HeapSize 实测容量；HeapReAlloc 不接受 NULL）
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
    if (ei < 0 || ei >= g_store.count) return;
    SIZE_T need = ((SIZE_T)g_store.count >> 3) + 1;
    if (!g_selBits || HeapSize(GetProcessHeap(), 0, g_selBits) < need)
        sel_reserve(g_store.count);
    if (!g_selBits) return;
    u8 m = (u8)(1 << (ei & 7));
    if (on && !(g_selBits[ei >> 3] & m)) { g_selBits[ei >> 3] |= m; g_nSel++; }
    if (!on && (g_selBits[ei >> 3] & m)) { g_selBits[ei >> 3] &= (u8)~m; g_nSel--; }
}
static void sel_clear(void)
{
    if (g_selBits) {
        SIZE_T cb = HeapSize(GetProcessHeap(), 0, g_selBits);
        if (cb != (SIZE_T)-1) memset(g_selBits, 0, cb);
    }
    g_nSel = 0;
}

static int S(int v) { return MulDiv(v, g_dpi, 96); }

// ============================================================
// 数学：无 CRT 的 min（shot 背景暗角用）
// ============================================================
static float fmin_(float a, float b) { return a < b ? a : b; }

// ============================================================
// GDI+ 绘制助手（瑞士风：全直角，一律矩形填充，无路径无圆角）
// ============================================================
static void fill_rect(int l, int t, int r, int b, u32 argb)
{
    if (r <= l || b <= t) return;
    GpSolidFill* br = NULL;
    if (GdipCreateSolidFill(argb, &br) == 0) {
        GdipFillRectangleI(g_gfx, (GpBrush*)br, l, t, r - l, b - t);
        GdipDeleteBrush((GpBrush*)br);
    }
}

// 直角描框（w 物理像素，四边硬边）
static void frame_rect(RECT rc, int w, u32 argb)
{
    fill_rect(rc.left, rc.top, rc.right, rc.top + w, argb);
    fill_rect(rc.left, rc.bottom - w, rc.right, rc.bottom, argb);
    fill_rect(rc.left, rc.top + w, rc.left + w, rc.bottom - w, argb);
    fill_rect(rc.right - w, rc.top + w, rc.right, rc.bottom - w, argb);
}

// 1px 发丝网格线 rgba(17,17,17,0.15) —— 只画在格子之间的缝隙，不穿透图片
static void hair_h(int y, int l, int r)  { fill_rect(l, y, r, y + 1, C_HAIR); }
static void hair_v(int x, int t, int b)  { fill_rect(x, t, x + 1, b, C_HAIR); }

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

// 底图：纯白玻璃方角面 rgba(250,250,249,0.96) + 2px 墨黑外框。
// 零阴影零渐变零圆角：出血只需覆盖框线，方角硬边（AA 由框线自带的矩形硬边取代）。
static void gen_base(void)
{
    int W = g_winW, H = g_winH;
    int L = g_panel.left, T = g_panel.top, R = g_panel.right, B = g_panel.bottom;
    memset(g_bits, 0, (u64)W * H * 4);
    // 预乘：250*245/255=240，249*245/255=239
    u32 paper = (245u << 24) | (240u << 16) | (240u << 8) | 239u;
    for (int y = T; y < B; y++) {
        u32* row = g_bits + (u64)y * W;
        for (int x = L; x < R; x++) row[x] = paper;
    }
    // 外框 1.5px #111（120dpi 下 2 物理像素；矩形填充 = 方角硬边）
    frame_rect((RECT){ L, T, R, B }, 2, C_INK(255));
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
        // 瑞士风：矩形填充一律硬边（0 = SmoothingModeDefault，不做几何 AA；
        // 发丝线/外框/红框需要精确 1-2px 的实心像素，AA 会把它们糊成 3px 半调）
        GdipSetSmoothingMode(g_gfx, 0);
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
static OneShot g_drawerImg;    // v3 抽屉 96px 预览/批量首图（独立缓存，不与悬停大图互踩）

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
static void maint_fixext(void);              // 定义在后（import_drop / 扫描路径调用）
static void drawer_show_batch(HWND hwnd);    // 定义在后（maint_dedup 调用）

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

// contain 绘制（保持比例、居中、硬裁剪到方角格子）
static void draw_image_contain(GpBitmap* img, int iw, int ih, RECT rc)
{
    if (!img) return;
    GdipSetClipRectI(g_gfx, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, 0);
    int aw = rc.right - rc.left, ah = rc.bottom - rc.top;
    double sc = (iw && ih) ? (double)aw / iw : 1.0;
    if ((double)ah / ih < sc) sc = (double)ah / ih;
    int dw = (int)(iw * sc), dh = (int)(ih * sc);
    if (dw < 1) dw = 1; if (dh < 1) dh = 1;
    GdipDrawImageRectI(g_gfx, (GpImage*)img,
                       rc.left + (aw - dw) / 2, rc.top + (ah - dh) / 2, dw, dh);
    GdipResetClip(g_gfx);
}

// ============================================================
// 筛选 / 最近使用
// v3 搜索语义：命中 = 标签名 ∪ 索引文字 ∪（文件名 ⊕ prefs 开关，默认开）
// ============================================================
static BOOL entry_matches(const Entry* e)
{
    if (g_tagFilter && !(e->tagmask & g_tagFilter)) return FALSE;
    if (g_ebS.b[0]) {
        const wchar_t* q = g_ebS.b;
        for (int i = 0; i < MP_MAX_TAGS; i++)
            if (g_store.tagNames[i] && (e->tagmask & ((u64)1 << i)) &&
                wcontains_ci(g_store.tagNames[i], q)) return TRUE;
        if (e->indexText && wcontains_ci(e->indexText, q)) return TRUE;
        if ((g_prefs & PF_NAMESEARCH) && wcontains_ci(e->name, q)) return TRUE;
        return FALSE;
    }
    return TRUE;
}

static void refilter(void)
{
    g_nFilt = 0; g_first = 0; g_hover = -1; g_previewIdx = -1;
    for (int i = 0; i < g_store.count; i++) {
        if (!entry_matches(&g_store.entries[i])) continue;
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
// 布局（v3 瑞士结构：检索行 → 全宽横梁（刊头）→ 标签行 → 发丝线 →
//       密排图片墙吃满全部剩余高度；抽屉打开时右缘加宽 S(300)、
//       2px 黑竖线分隔——网格区几何与常态完全一致（drawerLeft = 常态面板右缘），
//       悬停大图/滚动条锚到网格区右界，不与抽屉重叠）
// 网格数学：cell = (可用宽 - (cols-1)*gap) / cols —— 列精确吃满可用宽，边缘无残条
// v3.5：抽屉内容 rect 全部在布局期由 drawer_layout 生成（单一几何源，
//       绘制函数只读——IME 定位不再依赖「先画一帧」）
// ============================================================
static void drawer_layout(void);   // 抽屉内容几何（定义在 tag_label 之后）

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

    int padX = S(20), padT = S(14), padB = S(10);
    g_padX = padX;

    // 抽屉右缘区（打开时窗口已加宽 S(300)）：竖分隔线 + 内容左右界
    if (g_drawer) {
        g_drawerLeft = g_panel.right - S(300);
        g_dContentL  = g_drawerLeft + 2 + padX;
        g_dContentR  = g_panel.right - padX;
    } else {
        g_drawerLeft = g_panel.right;   // 关闭态无分隔线
        g_dContentL = g_dContentR = 0;
    }
    g_gridR = g_drawer ? g_drawerLeft : g_panel.right;   // 网格/搜索行/横梁的右界

    // 检索行 = 顶边框下第一条内容行（抽屉开时止于竖分隔线）
    int sh = S(32);
    int closeS = S(26);
    g_searchBox.left = g_panel.left + padX;
    g_searchBox.top = g_panel.top + padT;
    g_searchBox.bottom = g_searchBox.top + sh;
    g_searchBox.right = g_gridR - padX - closeS - S(12);
    g_closeRc.right = g_gridR - padX;
    g_closeRc.left = g_closeRc.right - closeS;
    g_closeRc.top = g_searchBox.top + (sh - closeS) / 2;
    g_closeRc.bottom = g_closeRc.top + closeS;

    // 横梁：搜索行下 S(2)=3px@120dpi 黑条（抽屉开时止于竖分隔线，与之间权重）
    g_beamY = g_searchBox.bottom + S(8);

    g_gap = S(3);
    g_grid.left = g_panel.left + padX;
    g_grid.right = g_gridR - padX;

    // 标签行 → 发丝线 → 图片墙（首行=最近快捷格，与网格同格连续成墙）
    int chipY = g_beamY + S(2) + S(10);
    g_hairY = chipY + S(26) + S(8);
    int wallT = g_hairY + 1 + S(10);
    int avail = g_grid.right - g_grid.left;
    int minCell = S(52);
    g_cols = (avail + g_gap) / (minCell + g_gap); if (g_cols < 1) g_cols = 1;
    g_cell = (avail - (g_cols - 1) * g_gap) / g_cols;
    if (g_cell < S(44)) g_cell = S(44);
    for (int i = 0; i < 7; i++) {
        g_recent[i].left = g_grid.left + i * (g_cell + g_gap);
        g_recent[i].right = g_recent[i].left + g_cell;
        g_recent[i].top = wallT;
        g_recent[i].bottom = wallT + g_cell;
    }
    g_grid.top = wallT + g_cell + g_gap;   // 与最近行只隔一条格缝：整面墙
    g_grid.bottom = g_panel.bottom - padB;
    int gh = g_grid.bottom - g_grid.top; if (gh < g_cell) gh = g_cell;
    g_rows = (gh + g_gap) / (g_cell + g_gap); if (g_rows < 1) g_rows = 1;

    // 悬停大图（方角 + 2px 黑框；锚定网格区右下——抽屉开时不与抽屉重叠）
    g_preview.right = g_gridR - S(14);
    g_preview.bottom = g_panel.bottom - S(12);
    g_preview.left = g_preview.right - S(168);
    g_preview.top = g_preview.bottom - S(168);
    clamp_first();
    drawer_layout();   // v3.5：抽屉内容 rect（关闭态 = 全量清零，绝不留过期值）
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
    int pw = sw * 46 / 100, ph = sh * 54 / 100;
    if (pw < S(700)) pw = S(700);
    if (ph < S(460)) ph = S(460);
    int w = pw + g_bleed * 2, h = ph + g_bleed * 2;
    g_fade = 0;
    g_caretOn = TRUE;
    // 关键顺序：隐藏状态下先定尺寸/重建表面、合成 alpha=0 的首帧并 ULW 提交，
    // 之后才显示窗口——否则 SWP_SHOWWINDOW 会先亮出上一会话残留的不透明旧帧（闪烁）
    // -pos X,Y 覆盖定位（IME 偏移实验：验证偏移是否随窗口位置变化）
    int px = (g_posX >= 0) ? g_posX : (sw - w) / 2;
    int py = (g_posY >= 0) ? g_posY : (sh - h) / 5;
    SetWindowPos(hwnd, HWND_TOPMOST, px, py, w, h, SWP_NOACTIVATE);
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

// 抽屉状态复位（不碰几何——隐藏时表面已释放，下次 show 全新定尺寸）
static void drawer_reset_state(HWND hwnd)
{
    g_drawer = FALSE;
    g_drawerEi = -2;
    g_drawerNote = FALSE;
    g_newtagOpen = FALSE;
    g_ebN.b[0] = 0; g_ebN.len = g_ebN.caret = 0;
    g_ebI.b[0] = 0; g_ebI.len = g_ebI.caret = 0;
    g_delArm = FALSE;
    if (hwnd) KillTimer(hwnd, ARM_TMR);
    g_editFocus = EDIT_SEARCH;
    drawer_layout();   // v3.5：会话结束，抽屉内容 rect 全量清零（防 IME 读到跨会话残留）
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
    drawer_reset_state(hwnd);   // 抽屉随之关（面板隐藏 = 会话结束）
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
    maint_fixext();   // v3：扫描后静默修后缀
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
// v3 维护 + 检视抽屉（「管理模式」已从产品词典删除：右键就地编辑）
// ============================================================

static void panel_repaint(void);

// ---- 输入焦点路由：三缓冲切换（离开新标签 = 收起内联输入并清空）----
static void edit_set_focus(int f)
{
    if (f == g_editFocus) return;
    if (g_editFocus == EDIT_NEWTAG && f != EDIT_NEWTAG) {
        g_newtagOpen = FALSE;
        g_ebN.b[0] = 0; g_ebN.len = g_ebN.caret = 0;
        drawer_layout();   // v3.5：收起内联输入 → chips 行数/索引框 y 变化，同步几何
    }
    g_editFocus = f;
    g_caretOn = TRUE;
}

// 改名 + 缩略图迁移 + 索引同步；返回 TRUE 成功（修后缀共用）
static BOOL file_rename_entry(int ei, const wchar_t* newName)
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

// 去重扫描（托盘「维护 ▸ 去重扫描…」）：按 size 分桶 -> 同 size 组内哈希 ->
// 每组保留一个，其余预选中；结果 = 选中重复项 + 打开抽屉批量态（顶部「疑似重复」）
static void maint_dedup(HWND hwnd)
{
    int n = g_store.count;
    sel_clear();
    if (n < 2) {
        lstrcpynW(g_nid.szInfoTitle, APP_NAME, 64);
        lstrcpynW(g_nid.szInfo, L"库太小，无需去重", 256);
        g_nid.uFlags = NIF_INFO;
        Shell_NotifyIconW(NIM_MODIFY, &g_nid);
        g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        return;
    }
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

    if (marked && IsWindowVisible(hwnd)) panel_repaint();
    if (marked) {
        if (!IsWindowVisible(hwnd)) panel_show(hwnd);
        g_drawerNote = TRUE;
        drawer_show_batch(hwnd);   // 顶部「已选 N 张 · 疑似重复」，删除按钮在抽屉底部
    } else {
        lstrcpynW(g_nid.szInfoTitle, APP_NAME, 64);
        lstrcpynW(g_nid.szInfo, L"未发现重复", 256);
        g_nid.uFlags = NIF_INFO;
        Shell_NotifyIconW(NIM_MODIFY, &g_nid);
        g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    }
}

// ============================================================
// 检视抽屉：几何 / 目标规则 / 就地编辑操作
// ============================================================
static void drawer_set_open(HWND hwnd, BOOL open)
{
    if (open == g_drawer) { if (open) panel_repaint(); return; }
    g_drawer = open;
    if (!open) {
        g_drawerEi = -2;
        g_drawerNote = FALSE;
        g_newtagOpen = FALSE;
        g_ebN.b[0] = 0; g_ebN.len = g_ebN.caret = 0;
        g_ebI.b[0] = 0; g_ebI.len = g_ebI.caret = 0;
        g_delArm = FALSE;
        KillTimer(hwnd, ARM_TMR);
        g_editFocus = EDIT_SEARCH;
        drawer_layout();   // v3.5：关抽屉立即清零内容 rect（不再绘制，防 IME 读过期残留）
    }
    // 窗口几何：左缘不动；右缘出屏则整体左移补偿（关抽屉缩回同理防出屏）
    RECT wr; GetWindowRect(hwnd, &wr);
    int w = (wr.right - wr.left) + (open ? S(300) : -S(300));
    if (w < S(400)) w = S(400);
    int left = wr.left;
    int sw = GetSystemMetrics(SM_CXSCREEN);
    if (left + w > sw) left = sw - w;
    if (left < 0) left = 0;
    SetWindowPos(hwnd, HWND_TOPMOST, left, wr.top, w, wr.bottom - wr.top, SWP_NOACTIVATE);
    panel_layout(hwnd);   // WM_SIZE 也会触发；显式调一次覆盖无头/未产生 WM_SIZE 的路径
    panel_repaint();
}

// 单目标：索引缓冲装载该 entry 现值（就地编辑即写回）
static void drawer_show_single(HWND hwnd, int ei)
{
    if (ei < 0 || ei >= g_store.count) return;
    g_drawerEi = ei;
    g_drawerNote = FALSE;
    g_delArm = FALSE;
    Entry* e = &g_store.entries[ei];
    lstrcpynW(g_ebI.b, e->indexText ? e->indexText : L"", 127);
    g_ebI.len = (int)wlen(g_ebI.b);
    g_ebI.caret = g_ebI.len;
    if (!g_drawer) drawer_set_open(hwnd, TRUE);
    else panel_repaint();
}

// 批量目标 = 当前选择集
static void drawer_show_batch(HWND hwnd)
{
    g_drawerEi = -1;
    g_delArm = FALSE;
    g_ebI.b[0] = 0; g_ebI.len = g_ebI.caret = 0;
    if (!g_drawer) drawer_set_open(hwnd, TRUE);
    else panel_repaint();
}

// 右键格子 →「编辑…」的目标规则：该格在当前选择集内 → 整个选择集（批量）；
// 否则目标 = 该单项（选择集不动）。选择集只含 1 项时与单项等价，走单目标可编辑索引。
static void drawer_open_for(HWND hwnd, int ei)
{
    if (sel_has(ei) && g_nSel >= 2) drawer_show_batch(hwnd);
    else                            drawer_show_single(hwnd, ei);
}

// 目标 entry 是否仍有效（扫描/删除可能使其越界）
static Entry* drawer_target(void)
{
    if (!g_drawer) return NULL;
    if (g_drawerEi < 0 || g_drawerEi >= g_store.count) return NULL;
    return &g_store.entries[g_drawerEi];
}

// 索引文字即时写回（HeapReAlloc 不接受 NULL：空 = free，已有 = ReAlloc，无 = alloc）
static void index_commit(void)
{
    Entry* e = drawer_target();
    if (!e) return;
    if (!g_ebI.len) {
        if (e->indexText) { HeapFree(GetProcessHeap(), 0, e->indexText); e->indexText = NULL; }
    } else if (e->indexText) {
        wchar_t* nb = (wchar_t*)HeapReAlloc(GetProcessHeap(), 0, e->indexText,
                                            ((u64)g_ebI.len + 1) * 2);
        if (nb) { memcpy(nb, g_ebI.b, (u64)g_ebI.len * 2); nb[g_ebI.len] = 0; e->indexText = nb; }
    } else {
        e->indexText = wdup(g_ebI.b);
    }
    store_save(&g_store);
}

// 抽屉标签 toggle：单目标 = 该项；批量 = 沿用旧管理胶囊逻辑（方向按首个目标）
static void drawer_toggle_tag(int id)
{
    if (id < 0 || id >= MP_MAX_TAGS || !g_store.tagNames[id]) return;
    u64 bit = (u64)1 << id;
    if (g_drawerEi >= 0 && g_drawerEi < g_store.count) {
        g_store.entries[g_drawerEi].tagmask ^= bit;
    } else if (g_drawer) {
        BOOL firstHas = FALSE;
        for (int k = 0; k < g_store.count; k++)
            if (sel_has(k)) { firstHas = (g_store.entries[k].tagmask & bit) != 0; break; }
        for (int k = 0; k < g_store.count; k++) {
            if (!sel_has(k)) continue;
            if (firstHas) g_store.entries[k].tagmask &= ~bit;
            else          g_store.entries[k].tagmask |= bit;
        }
    }
    store_save(&g_store);
    panel_repaint();
}

// 「+ 新标签」提交：store_tag_add + 应用于抽屉目标，输入框保持展开（连续建多个）
static void newtag_commit(HWND hwnd)
{
    (void)hwnd;
    // 去首尾空格
    int a = 0, b = g_ebN.len;
    while (a < b && g_ebN.b[a] == L' ') a++;
    while (b > a && g_ebN.b[b - 1] == L' ') b--;
    g_ebN.b[b] = 0;
    const wchar_t* name = g_ebN.b + a;
    if (*name) {
        int id = store_tag_add(&g_store, name);
        if (id >= 0) {
            u64 bit = (u64)1 << id;
            if (g_drawerEi >= 0 && g_drawerEi < g_store.count)
                g_store.entries[g_drawerEi].tagmask |= bit;
            else if (g_drawer) {
                for (int k = 0; k < g_store.count; k++)
                    if (sel_has(k)) g_store.entries[k].tagmask |= bit;
            }
            store_save(&g_store);
        }
    }
    g_ebN.b[0] = 0; g_ebN.len = g_ebN.caret = 0;   // 清空，保持展开
    panel_repaint();
}

// 抽屉删除：武装 3 秒（固定宽度防跳动）→ 二次点击送回收站；删后抽屉关闭
static void drawer_delete(HWND hwnd)
{
    int single = (g_drawerEi >= 0 && g_drawerEi < g_store.count) ? g_drawerEi : -1;
    if (single < 0 && g_nSel == 0) return;
    if (!g_delArm) {
        g_delArm = TRUE;
        SetTimer(hwnd, ARM_TMR, 3000, NULL);
        panel_repaint();
        return;
    }
    g_delArm = FALSE;
    KillTimer(hwnd, ARM_TMR);
    int n = single >= 0 ? 1 : g_nSel;
    // SHFileOperation 的 pFrom 需要 "p1\0p2\0\0"
    wchar_t* buf = (wchar_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                       ((u64)n * MAX_PATH + 2) * 2);
    if (buf) {
        wchar_t* w = buf;
        if (single >= 0) {
            store_build_path(&g_store, &g_store.entries[single], w, MAX_PATH);
            w += wlen(w) + 1;
        } else {
            for (int i = 0; i < g_store.count && n > 0; i++) {
                if (!sel_has(i)) continue;
                store_build_path(&g_store, &g_store.entries[i], w, (int)(MAX_PATH - (w - buf)));
                w += wlen(w) + 1;
                n--;
            }
        }
        SHFILEOPSTRUCTW fo;
        memset(&fo, 0, sizeof fo);
        fo.hwnd = hwnd;
        fo.wFunc = FO_DELETE;
        fo.pFrom = buf;
        fo.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
        SHFileOperationW(&fo);
        HeapFree(GetProcessHeap(), 0, buf);
    }
    store_scan(&g_store);
    maint_fixext();
    recalc_recents();
    sel_clear();
    drawer_set_open(hwnd, FALSE);   // 目标已消失
    refilter();
    panel_repaint();
}

// 「打开位置」：explorer /select 定位到源文件（资源管理器激活瞬间用 g_modal 抑制自动收起）
static void drawer_open_location(HWND hwnd)
{
    Entry* e = drawer_target();
    if (!e) return;
    wchar_t p[MAX_PATH], arg[MAX_PATH + 16];
    store_build_path(&g_store, e, p, MAX_PATH);
    wnsprintfW(arg, MAX_PATH + 16, L"/select,\"%s\"", p);
    g_modal = TRUE;
    ShellExecuteW(hwnd, L"open", L"explorer.exe", arg, NULL, SW_SHOWNORMAL);
    g_modal = FALSE;
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

// QQNT 下载表情常见后缀错乱：按文件头 magic 修正扩展名。
// v3：每次 store_scan 后静默自动执行（启动/重新扫描/导入后），不产生任何 UI——
// 无损修正，用户永远不需要知道它存在。
static void maint_fixext(void)
{
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
        if (file_rename_entry(i, nn)) fixed++;
    }
    if (fixed) store_save(&g_store);
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

// v2 刊头：搜索行下 S(2)=3px@120dpi 黑横梁（v3：抽屉开时止于竖分隔线，与之间权重）
static void draw_beam(void)
{
    fill_rect(g_panel.left, g_beamY, g_gridR, g_beamY + S(2), C_INK(255));
}

// 自绘输入框（三缓冲共用视觉）：直角白底 + 1.5px 黑下边线 + accent 块状 caret。
// focused=FALSE 时只显文本不显 caret；组合串只出现在焦点框（同一时刻只属一个缓冲）。
static void draw_input_box(RECT rc, const EBuf* e, BOOL focused, const wchar_t* hint)
{
    fill_rect(rc.left, rc.top, rc.right, rc.bottom, ARGB(255, 255, 255, 255));
    fill_rect(rc.left, rc.bottom - 2, rc.right, rc.bottom, C_INK(255));

    // 文本区（前缀 + 组合串 + 剩余 + caret）；clip 左界放宽 2px，避免 caret 在起点时左半被裁
    int tx = rc.left + S(2);
    int twidth = rc.right - tx - S(8);
    GdipSetClipRectI(g_gfx, tx - 2, rc.top, twidth + 4, rc.bottom - rc.top - 2, 0);

    int cy2 = (rc.top + rc.bottom) / 2;
    BOOL compHere = focused && g_complen;
    if (e->b[0] || compHere) {
        if (e->caret > 0)
            gtext(e->b, e->caret, g_fText, (RECT){ tx, rc.top, tx + twidth, rc.bottom }, g_sfL, C_INK(255));
        float preW = e->caret ? gtext_w(e->b, e->caret, g_fText) : 0.0f;
        int compX = tx + (int)preW;
        if (compHere) {
            gtext(g_comp, g_complen, g_fText, (RECT){ compX, rc.top, tx + twidth, rc.bottom }, g_sfL, C_INK(255));
            float cw = gtext_w(g_comp, g_complen, g_fText);
            fill_rect(compX, cy2 + S(9), compX + (int)cw + 1, cy2 + S(10), C_INK(170));
            compX += (int)cw;
        }
        if (e->caret < e->len) {
            gtext(e->b + e->caret, e->len - e->caret, g_fText, (RECT){ compX, rc.top, tx + twidth, rc.bottom }, g_sfL, C_INK(255));
        }
    } else if (hint) {
        // 空输入引导（避开 caret 块）
        gtext(hint, -1, g_fUi, (RECT){ tx + S(9), rc.top, tx + twidth, rc.bottom }, g_sfL, C_INK(105));
    }
    // caret：accent 实心方块（焦点框空输入也显示，作为输入位置的引导）
    if (focused && g_caretOn && !g_complen) {
        float preW = e->caret ? gtext_w(e->b, e->caret, g_fText) : 0.0f;
        int cxx = tx + (int)preW;
        fill_rect(cxx, cy2 - S(8), cxx + S(7), cy2 + S(8) - 1, C_RED(255));
    }
    GdipResetClip(g_gfx);
}

// 搜索框（焦点路由三框之一）
static void draw_search_box(void)
{
    draw_input_box(g_searchBox, &g_ebS, g_editFocus == EDIT_SEARCH, L"SUCHE");

    // 关闭按钮：黑十字；hover = 黑底方块 + 白十字（直角）
    RECT cb = g_closeRc;
    BOOL chov = PtInRect(&cb, g_mouse);
    if (chov) fill_rect(cb.left, cb.top, cb.right, cb.bottom, C_INK(255));
    GpPen* xp = NULL;
    if (GdipCreatePen1(chov ? ARGB(255, 255, 255, 255) : C_INK(255), 1.8f, GP_UNIT_PIXEL, &xp) == 0) {
        int m = S(8);
        GdipSetSmoothingMode(g_gfx, GP_SMOOTH_AA);   // 斜线十字保留 AA（其余全站硬边）
        GdipDrawLineI(g_gfx, xp, cb.left + m, cb.top + m, cb.right - m, cb.bottom - m);
        GdipDrawLineI(g_gfx, xp, cb.right - m, cb.top + m, cb.left + m, cb.bottom - m);
        GdipSetSmoothingMode(g_gfx, 0);
        GdipDeletePen(xp);
    }
}

// 悬停/键盘选中红框：2px accent，画在格子外侧的格缝里（不遮挡图片）
static void draw_red_frame(RECT rc)
{
    fill_rect(rc.left, rc.top - 2, rc.right, rc.top, C_RED(255));
    fill_rect(rc.left, rc.bottom, rc.right, rc.bottom + 2, C_RED(255));
    fill_rect(rc.left - 2, rc.top - 2, rc.left, rc.bottom + 2, C_RED(255));
    fill_rect(rc.right, rc.top - 2, rc.right + 2, rc.bottom + 2, C_RED(255));
}

// 选择集角标记（v3）：四角 2px accent 短划——区别于 hover 的 accent 全框
static void draw_sel_marks(RECT rc)
{
    int m = S(10), w = 2;
    fill_rect(rc.left - 2, rc.top - 2, rc.left - 2 + m, rc.top - 2 + w, C_RED(255));          // 左上 -
    fill_rect(rc.left - 2, rc.top - 2, rc.left - 2 + w, rc.top - 2 + m, C_RED(255));          // 左上 |
    fill_rect(rc.right + 2 - m, rc.top - 2, rc.right + 2, rc.top - 2 + w, C_RED(255));        // 右上 -
    fill_rect(rc.right + 2 - w, rc.top - 2, rc.right + 2, rc.top - 2 + m, C_RED(255));        // 右上 |
    fill_rect(rc.left - 2, rc.bottom + 2 - w, rc.left - 2 + m, rc.bottom + 2, C_RED(255));    // 左下 -
    fill_rect(rc.left - 2, rc.bottom + 2 - m, rc.left - 2 + w, rc.bottom + 2, C_RED(255));    // 左下 |
    fill_rect(rc.right + 2 - m, rc.bottom + 2 - w, rc.right + 2, rc.bottom + 2, C_RED(255));  // 右下 -
    fill_rect(rc.right + 2 - w, rc.bottom + 2 - m, rc.right + 2, rc.bottom + 2, C_RED(255));  // 右下 |
}

// 左上角红色编号方块（红底白字，Helvetica Moment）
static void draw_badge(int l, int t, int num)
{
    int s = S(16);
    fill_rect(l, t, l + s, t + s, C_RED(255));
    wchar_t b[8];
    wnsprintfW(b, 8, L"%d", num);
    gtext(b, -1, g_fTiny, (RECT){ l + 1, t, l + s - 1, t + s }, g_sfC, ARGB(255, 255, 255, 255));
}

// 悬停名字条（v2 淡化一档）：底部 14 逻辑 px 白条 α90 + 文件名 8.5px #555555
static void draw_name_strip(RECT rc, const wchar_t* name)
{
    int h = S(14);
    fill_rect(rc.left, rc.bottom - h, rc.right, rc.bottom, ARGB(230, 255, 255, 255));
    gtext(name, -1, g_fNano,
          (RECT){ rc.left + S(4), rc.bottom - h, rc.right - S(2), rc.bottom },
          g_sfL, ARGB(255, 0x55, 0x55, 0x55));
}

// GIF 角标：右下黑色直角小块
static void draw_gif_badge(RECT rc)
{
    int w = S(26), h = S(14);
    int l = rc.right - w - S(3), t = rc.bottom - h - S(3);
    fill_rect(l, t, l + w, t + h, C_INK(205));
    gtext(L"GIF", -1, g_fTiny, (RECT){ l, t - 1, l + w, t + h }, g_sfC, ARGB(255, 255, 255, 255));
}

static void draw_recents(void)
{
    // 标签行与图片墙之间的 1px 发丝线（网格线是主角；抽屉开时止于竖分隔线）
    hair_h(g_hairY, g_panel.left + g_padX, g_gridR - g_padX);
    // 最近行内列间发丝线（只画在格子之间的缝隙）
    for (int i = 1; i < 7 && i < g_nRecent; i++)
        hair_v(g_recent[i].left - g_gap + g_gap / 2, g_recent[0].top, g_recent[0].bottom);
    // 最近快捷行：与网格完全同款密排格子；hover = 红框 + 编号 + 名字条
    for (int i = 0; i < 7; i++) {
        int ei = (i < g_nRecent && g_recentIdx[i] < g_store.count) ? g_recentIdx[i] : -1;
        if (ei < 0) continue;
        RECT rc = g_recent[i];
        BOOL hov = PtInRect(&rc, g_mouse);
        Entry* e = &g_store.entries[ei];
        if (hov && g_anim.ei == ei && g_anim.pb) {
            draw_image_contain(g_anim.pb[g_anim.cur], g_anim.w, g_anim.h, rc);
        } else {
            int iw, ih;
            GpBitmap* pb = oneshot_get(&g_recentImg[i], ei, &iw, &ih);
            if (pb) draw_image_contain(pb, iw, ih, rc);
        }
        if (is_gif_name(e->name)) draw_gif_badge(rc);
        if (hov) {
            draw_red_frame(rc);
            draw_badge(rc.left, rc.top, i + 1);
            draw_name_strip(rc, e->indexText && e->indexText[0] ? e->indexText : e->name);
        }
    }
}

// 德语式标签命名：全部→ALLE 动物→TIERE 猫猫→KATZEN；其余标签转大写
static const wchar_t* tag_label(const wchar_t* name, int id)
{
    if (id == -1) return L"ALLE";
    if (weq(name, L"动物")) return L"TIERE";
    if (weq(name, L"猫猫")) return L"KATZEN";
    static wchar_t buf[64];
    int i = 0;
    for (; name[i] && i < 63; i++) {
        wchar_t c = name[i];
        if (c >= L'a' && c <= L'z') c -= 32;
        buf[i] = c;
    }
    buf[i] = 0;
    return buf;
}

// ============================================================
// 抽屉内容几何（v3.5 单一数据源）：预览 →「标签」chips 换行 →「+ 新标签」→
// 「索引文字」输入框 → 删除钮 的全部 rect 在此计算；draw_drawer 与命中测试只读。
// chips 宽度用与绘制完全相同的 gtext_w 同参度量（同字体/同 +S(16)/同换行阈值），
// 绘制与命中逐像素一致。关闭 / 批量（索引框）/ 目标失效态：先全量清零——
// IME 定位（ime_update_position）在任何时序下都读不到过期残留 rect。
// 触发点：panel_layout（尺寸/开关抽屉）、panel_repaint（状态变化后每帧）、
//         drawer_set_open(FALSE)/drawer_reset_state/edit_set_focus（收起内联输入）。
// ============================================================
static void drawer_layout(void)
{
    g_nDChips = 0;
    g_dPrevRc = g_dNameRc = g_dSizeRc = g_dOpenRc = g_dClearRc =
    g_dIndexBox = g_dNewTagBox = g_dNewChipRc = g_dDelRc = (RECT){ 0, 0, 0, 0 };
    g_dSecY = g_dIdxY = 0;
    if (!g_drawer || !g_gfx) return;

    Entry* tgt = drawer_target();
    BOOL batch = (g_drawerEi == -1);
    int L = g_dContentL, R = g_dContentR;
    int T0 = g_panel.top + S(14);

    int prevB = T0 + S(96);                       // 预览行底（节的起点基准）
    if (batch) {
        RECT xr = { R - S(16), T0, R, T0 + S(16) };           // 批量 ✕ 清除选择
        g_dClearRc = xr;
        RECT cb = { L, T0 + S(24), L + S(96), T0 + S(24) + S(96) };  // N 计数块
        g_dPrevRc = cb;
        prevB = cb.bottom;
    } else if (tgt) {
        RECT pv = { L, T0, L + S(96), T0 + S(96) };           // 96px 预览衬纸
        g_dPrevRc = pv;
        int mL = pv.right + S(12);                             // 元数据列（右侧 ~160px）
        int mR = mL + S(160); if (mR > R) mR = R;
        g_dNameRc = (RECT){ mL, T0, mR, T0 + S(14) };
        g_dSizeRc = (RECT){ mL, T0 + S(18), mR, T0 + S(34) };
        int ow = (int)gtext_w(L"打开位置", -1, g_fSmall);
        g_dOpenRc = (RECT){ mL, T0 + S(44), mL + ow + S(4), T0 + S(62) };
    } else {
        return;   // 目标失效（扫描后越界）：无内容 rect（绘制只画分隔线）
    }

    // ---- 「标签」节：chips 自动换行（换行行数决定下方所有节的 y）----
    int secY = prevB + S(16);
    g_dSecY = secY;
    int cx = L, cy = secY + S(20);
    for (int i = 0; i < MP_MAX_TAGS; i++) {
        if (!g_store.tagNames[i]) continue;
        int h = S(24);
        int w = (int)gtext_w(tag_label(g_store.tagNames[i], i), -1, g_fUi) + S(16);
        if (cx + w > R) { cx = L; cy += h + S(6); }           // 放不下 → 换行
        RECT rc = { cx, cy, cx + w, cy + h };
        if (g_nDChips < MP_MAX_TAGS + 1) {
            g_dChipRc[g_nDChips] = rc;
            g_dChipId[g_nDChips] = i;
            g_nDChips++;
        }
        cx = rc.right + S(8);
    }
    int chipsB;
    if (g_newtagOpen) {
        // + 新标签 → 原地变内联输入框（Enter 连续建多个）
        int w = S(140), h = S(24);
        if (cx + w > R) { cx = L; cy += S(24) + S(6); }
        g_dNewTagBox = (RECT){ cx, cy, cx + w, cy + h };
        chipsB = cy + h;
    } else {
        const wchar_t* nt = L"+ 新标签";
        int w = (int)gtext_w(nt, -1, g_fUi) + S(16), h = S(24);
        if (cx + w > R) { cx = L; cy += S(24) + S(6); }
        RECT nc = { cx, cy, cx + w, cy + h };
        g_dNewChipRc = nc;
        if (g_nDChips < MP_MAX_TAGS + 1) {                     // 与标签 chips 并表命中
            g_dChipRc[g_nDChips] = nc;
            g_dChipId[g_nDChips] = -2;
            g_nDChips++;
        }
        chipsB = cy + h;
    }

    // ---- 「索引文字」节：仅单目标可编辑（批量 = g_dIndexBox 保持清零）----
    int idxY = chipsB + S(14);
    g_dIdxY = idxY;
    if (!batch && tgt)
        g_dIndexBox = (RECT){ L, idxY + S(18), R, idxY + S(18) + S(30) };

    // ---- 删除钮：固定按武装文案宽度（武装态不改 rect，二次确认不会点空）----
    int dw = (int)gtext_w(L"确认删除", -1, g_fUi) + S(24);
    int dh = S(26);
    g_dDelRc = (RECT){ L, g_panel.bottom - S(16) - dh, L + dw, g_panel.bottom - S(16) };
}

// 标签：大写左对齐文字；激活 = 黑色实底白字方块 + 前置红色 6×6 方块；hover = 黑色下划线
static void draw_chip(int* px, int y, const wchar_t* txt, int id)
{
    const wchar_t* label = tag_label(txt, id);
    int h = S(26);
    float tw = gtext_w(label, -1, g_fUi);
    int active = (id == -1) ? (g_tagFilter == 0) : (g_tagFilter == ((u64)1 << id));
    // 放不下（越过网格区右缘）的不画
    int need = active ? (S(6) + S(6) + (int)tw + S(20)) : ((int)tw + S(4));
    if (*px + need > g_gridR - g_padX) return;
    RECT hit;
    if (active) {
        int sq = S(6);
        int bl = *px + sq + S(6);
        int bw = (int)tw + S(20);
        fill_rect(*px, y + (h - sq) / 2, *px + sq, y + (h + sq) / 2, C_RED(255));
        fill_rect(bl, y, bl + bw, y + h, C_INK(255));
        gtext(label, -1, g_fUi, (RECT){ bl, y, bl + bw, y + h }, g_sfC, C_PAPER(255));
        hit.left = *px; hit.right = bl + bw; hit.top = y; hit.bottom = y + h;
        *px = bl + bw + S(28);
    } else {
        hit.left = *px; hit.right = *px + (int)tw + S(4); hit.top = y; hit.bottom = y + h;
        BOOL hov = PtInRect(&hit, g_mouse);
        gtext(label, -1, g_fUi, hit, g_sfL, C_INK(hov ? 255 : 150));
        if (hov) fill_rect(hit.left, y + h - 2, hit.right, y + h, C_INK(255));
        *px = hit.right + S(28);
    }
    if (g_nChips < MP_MAX_TAGS + 1) {
        g_chipRc[g_nChips] = hit;
        g_chipId[g_nChips] = id;
        g_nChips++;
    }
}

static void draw_empty(void)
{
    int cy = (g_grid.top + g_grid.bottom) / 2;
    gtext(L"LEER — 把表情图拖到这里", -1, g_fUi,
          (RECT){ g_grid.left, cy - S(12), g_grid.right, cy + S(12) }, g_sfC, C_INK(130));
}

static void draw_grid(void)
{
    if (g_nFilt == 0) { draw_empty(); return; }
    int vis = g_cols * g_rows;

    // 结构发丝线：列间 + 行间，只画在格缝里、只画到有内容的最后一行（不穿透图片）
    int shown = g_nFilt - g_first; if (shown > vis) shown = vis;
    int lastRow = (shown - 1) / g_cols;
    int wallB = g_grid.top + (lastRow + 1) * (g_cell + g_gap) - g_gap;
    for (int c = 1; c < g_cols; c++)
        hair_v(g_grid.left + c * (g_cell + g_gap) - g_gap + g_gap / 2, g_grid.top, wallB);
    for (int r = 1; r <= lastRow; r++)
        hair_h(g_grid.top + r * (g_cell + g_gap) - g_gap + g_gap / 2, g_grid.left, g_grid.right);

    for (int k = 0; k < vis; k++) {
        int idx = g_first + k;
        if (idx >= g_nFilt) break;
        int col = k % g_cols, row = k / g_cols;
        RECT rc = { g_grid.left + col * (g_cell + g_gap),
                    g_grid.top + row * (g_cell + g_gap),
                    0, 0 };
        rc.right = rc.left + g_cell; rc.bottom = rc.top + g_cell;
        BOOL hov = (idx == g_hover);
        BOOL sel = sel_has(g_filt[idx]);   // Ctrl+点击选择集（四角 accent 角标记）

        Entry* e = &g_store.entries[g_filt[idx]];
        int iw, ih;
        if (hov && g_anim.ei == g_filt[idx] && g_anim.pb) {
            // 悬停中的 GIF：播放动画帧（解码未就绪时走静态 fallback）
            draw_image_contain(g_anim.pb[g_anim.cur], g_anim.w, g_anim.h, rc);
        } else {
            GpBitmap* pb = tc_get(idx, &iw, &ih);
            if (pb) draw_image_contain(pb, iw, ih, rc);
        }
        if (is_gif_name(e->name)) draw_gif_badge(rc);
        // 选择 = 四角 accent 角标记；悬停/键盘选中 = accent 全框 + 编号 + 名字条
        if (sel) draw_sel_marks(rc);
        if (hov) {
            draw_red_frame(rc);
            draw_badge(rc.left, rc.top, idx + 1);
        }
        // 名字条：索引文字优先（使用概念），文件名兜底（存储概念）
        if (hov) draw_name_strip(rc, e->indexText && e->indexText[0] ? e->indexText : e->name);
    }
    // 滚动条：网格区右缘 4px 黑色实心条（thumb 纯黑，track rgba(17,17,17,0.08)，直角）
    if (g_nFilt > vis) {
        int tw = S(4);
        int tx = g_gridR - 2 - tw;
        int trackT = g_grid.top, trackH = g_grid.bottom - g_grid.top;
        fill_rect(tx, trackT, tx + tw, trackT + trackH, ARGB(20, 17, 17, 17));
        int thumbH = trackH * vis / g_nFilt;
        if (thumbH < S(28)) thumbH = S(28);
        int maxF = g_nFilt - vis;
        int thumbY = trackT + (int)((trackH - thumbH) * (maxF ? (double)g_first / maxF : 0));
        fill_rect(tx, thumbY, tx + tw, thumbY + thumbH, C_INK(255));
    }
}

static void draw_preview(void)
{
    if (g_previewIdx < 0 || g_previewIdx >= g_nFilt) return;
    RECT rc = g_preview;
    // 图（白底 + 2px 黑框，方角；零阴影，靠黑框与图片墙形成层次）
    fill_rect(rc.left, rc.top, rc.right, rc.bottom, ARGB(255, 255, 255, 255));
    int ei = g_filt[g_previewIdx];
    RECT in = { rc.left + 2, rc.top + 2, rc.right - 2, rc.bottom - 2 };
    if (g_anim.ei == ei && g_anim.pb) {
        draw_image_contain(g_anim.pb[g_anim.cur], g_anim.w, g_anim.h, in);
    } else {
        int iw, ih;
        GpBitmap* pb = oneshot_get(&g_previewImg, ei, &iw, &ih);
        if (pb) draw_image_contain(pb, iw, ih, in);
    }
    frame_rect(rc, 2, C_INK(255));
}

// ============================================================
// 检视抽屉（v3）：右缘 S(300)——预览/元数据 · 标签 · 索引文字 · 武装删除
// 节奏全部 4/8 模数；节标题 11px 加重左对齐；批量态 = 选择集目标
// ============================================================

// 大小自算：KB / MB（四舍五入）
static void fmt_size(u64 bytes, wchar_t* out, int cap)
{
    if (bytes >= 1048576) wnsprintfW(out, cap, L"%u MB", (u32)((bytes + 524288) / 1048576));
    else                  wnsprintfW(out, cap, L"%u KB", (u32)((bytes + 512) / 1024));
}

// 扩展名大写（GIF 标注；无扩展名 = —）
static void fmt_ext(const wchar_t* name, wchar_t* out, int cap)
{
    const wchar_t* dot = name + wlen(name);
    while (dot > name && dot[-1] != L'.') dot--;
    if (dot == name) { lstrcpynW(out, L"—", cap); return; }
    int i = 0;
    for (; dot[i] && i < cap - 1; i++) {
        wchar_t c = dot[i];
        if (c >= L'a' && c <= L'z') c -= 32;
        out[i] = c;
    }
    out[i] = 0;
}

// 抽屉标签 chip：已打标 = 黑底白字实心块；未打标 = 灰字（hover 黑下划线）
// （v3.5：rect 由 drawer_layout 预计算并登记命中，此处纯绘制）
static void draw_dchip(RECT rc, const wchar_t* txt, BOOL on)
{
    if (on) {
        fill_rect(rc.left, rc.top, rc.right, rc.bottom, C_INK(255));
        gtext(txt, -1, g_fUi, rc, g_sfC, C_PAPER(255));
    } else {
        BOOL hov = PtInRect(&rc, g_mouse);
        gtext(txt, -1, g_fUi, rc, g_sfL, C_INK(hov ? 255 : 150));
        if (hov) fill_rect(rc.left, rc.bottom - 2, rc.right, rc.bottom, C_INK(255));
    }
}

// ✕ 小十字（批量「清除选择」；黑 1.6px，AA）
static void draw_x(RECT rc, int argb)
{
    GpPen* xp = NULL;
    if (GdipCreatePen1(argb, 1.6f, GP_UNIT_PIXEL, &xp) == 0) {
        int m = S(3);
        GdipSetSmoothingMode(g_gfx, GP_SMOOTH_AA);
        GdipDrawLineI(g_gfx, xp, rc.left + m, rc.top + m, rc.right - m, rc.bottom - m);
        GdipDrawLineI(g_gfx, xp, rc.right - m, rc.top + m, rc.left + m, rc.bottom - m);
        GdipSetSmoothingMode(g_gfx, 0);
        GdipDeletePen(xp);
    }
}

static void draw_drawer(void)
{
    if (!g_drawer) return;
    // 2px 黑竖分隔线（与横梁同权重，全高）
    fill_rect(g_drawerLeft, g_panel.top, g_drawerLeft + 2, g_panel.bottom, C_INK(255));

    // v3.5：全部内容 rect 来自 drawer_layout（布局期单一数据源），此处只读不写
    Entry* tgt = drawer_target();
    BOOL batch = (g_drawerEi == -1);
    int L = g_dContentL, R = g_dContentR;
    int T0 = g_panel.top + S(14);

    if (batch) {
        // ---- 批量头部：「已选 N 张」12px + ✕ 清除选择 ----
        wchar_t hdr[96];
        wnsprintfW(hdr, 96, L"已选 %d 张%s", g_nSel, g_drawerNote ? L" · 疑似重复" : L"");
        gtext(hdr, -1, g_fUi, (RECT){ L, T0, R - S(20), T0 + S(16) }, g_sfL, C_INK(255));
        RECT xr = g_dClearRc;
        BOOL xh = PtInRect(&xr, g_mouse);
        draw_x(xr, xh ? C_INK(255) : C_INK(150));
        if (xh) fill_rect(xr.left, xr.bottom, xr.right, xr.bottom + 2, C_INK(255));
        // 预览区 → N 计数大字块（12px「N 张」+ 首图小缩略图，白衬纸 + 发丝框）
        RECT cb = g_dPrevRc;
        fill_rect(cb.left, cb.top, cb.right, cb.bottom, ARGB(255, 255, 255, 255));
        int first = -1;
        for (int i = 0; i < g_store.count; i++) if (sel_has(i)) { first = i; break; }
        if (first >= 0) {
            int iw, ih;
            GpBitmap* pb = oneshot_get(&g_drawerImg, first, &iw, &ih);
            RECT in = { cb.left + S(4), cb.top + S(4), cb.left + S(60), cb.bottom - S(4) };
            if (pb) draw_image_contain(pb, iw, ih, in);
        }
        wchar_t nb[24];
        wnsprintfW(nb, 24, L"%d 张", g_nSel);
        gtext(nb, -1, g_fUi, (RECT){ cb.left + S(64), cb.top, cb.right - S(2), cb.bottom },
              g_sfL, C_INK(255));
        frame_rect(cb, 1, C_HAIR);
    } else if (tgt) {
        // ---- 单目标：96px 白衬纸预览（contain + 1px 发丝框）+ 元数据列 ----
        RECT pv = g_dPrevRc;
        fill_rect(pv.left, pv.top, pv.right, pv.bottom, ARGB(255, 255, 255, 255));
        int iw, ih;
        GpBitmap* pb = oneshot_get(&g_drawerImg, g_drawerEi, &iw, &ih);
        if (pb) draw_image_contain(pb, iw, ih, pv);
        frame_rect(pv, 1, C_HAIR);
        // 元数据列（预览右侧 ~160px）：文件名 8.5px #555 单行省略（淡化）
        gtext(tgt->name, -1, g_fNano, g_dNameRc, g_sfL, ARGB(255, 0x55, 0x55, 0x55));
        // 大小 · 格式（KB/MB 自算，GIF 标注）
        wchar_t sz[48], ext[8], line[64];
        fmt_size(tgt->size, sz, 48);
        fmt_ext(tgt->name, ext, 8);
        wnsprintfW(line, 64, L"%s · %s", sz, ext);
        gtext(line, -1, g_fSmall, g_dSizeRc, g_sfL, C_INK(140));
        // 「打开位置」下划线文字按钮
        const wchar_t* openTxt = L"打开位置";
        RECT ob = g_dOpenRc;
        BOOL ohov = PtInRect(&ob, g_mouse);
        gtext(openTxt, -1, g_fSmall, ob, g_sfL, C_INK(ohov ? 255 : 140));
        fill_rect(ob.left, ob.bottom - 1, ob.right, ob.bottom, C_INK(ohov ? 255 : 140));
    }

    if (!tgt && !batch) return;   // 目标失效（扫描后越界）：只画分隔线

    // ---- 「标签」节：11px 加重节标题 + chips（rect 已在 drawer_layout 排好）----
    gtext(L"标签", -1, g_fSect, (RECT){ L, g_dSecY, R, g_dSecY + S(16) }, g_sfL, C_INK(255));
    int firstSel = -1;                            // 批量方向 = 首个目标
    if (batch)
        for (int i = 0; i < g_store.count; i++) if (sel_has(i)) { firstSel = i; break; }
    for (int i = 0; i < g_nDChips && i < MP_MAX_TAGS + 1; i++) {
        int id = g_dChipId[i];
        if (id < 0) continue;                     // -2 = 「+ 新标签」chip，下方分支绘制
        BOOL on = batch
            ? (firstSel >= 0 && (g_store.entries[firstSel].tagmask & ((u64)1 << id)) != 0)
            : ((tgt->tagmask & ((u64)1 << id)) != 0);
        draw_dchip(g_dChipRc[i], tag_label(g_store.tagNames[id], id), on);
    }
    if (g_newtagOpen) {
        // + 新标签 → 原地变内联输入框（白底 + 黑下线），Enter 连续建多个
        draw_input_box(g_dNewTagBox, &g_ebN, g_editFocus == EDIT_NEWTAG, L"新标签");
    } else {
        const wchar_t* nt = L"+ 新标签";
        RECT nc = g_dNewChipRc;
        BOOL nh = PtInRect(&nc, g_mouse);
        gtext(nt, -1, g_fUi, nc, g_sfL, C_INK(nh ? 255 : 150));
        if (nh) fill_rect(nc.left, nc.bottom - 2, nc.right, nc.bottom, C_INK(255));
    }

    // ---- 「索引文字」节：仅单目标可编辑 ----
    gtext(L"索引文字", -1, g_fSect, (RECT){ L, g_dIdxY, R, g_dIdxY + S(16) }, g_sfL, C_INK(255));
    if (batch) {
        gtext(L"多选时不可编辑索引", -1, g_fSmall,
              (RECT){ L, g_dIdxY + S(20), R, g_dIdxY + S(38) }, g_sfL, C_INK(140));
    } else if (tgt) {
        RECT ib = g_dIndexBox;
        draw_input_box(ib, &g_ebI, g_editFocus == EDIT_INDEX, L"索引…");
        gtext(L"空格分隔 · 用于搜索", -1, g_fNano,
              (RECT){ L, ib.bottom + S(6), R, ib.bottom + S(20) },
              g_sfL, ARGB(255, 0x55, 0x55, 0x55));
    }

    // ---- 底部：删除（accent 描边直角；武装 3 秒 = accent 实底「确认删除」）----
    RECT db = g_dDelRc;
    BOOL dhov = PtInRect(&db, g_mouse);
    if (g_delArm)   fill_rect(db.left, db.top, db.right, db.bottom, C_RED(255));
    else if (dhov)  fill_rect(db.left, db.top, db.right, db.bottom, C_INK(255));
    else            frame_rect(db, 2, C_RED(255));
    gtext(g_delArm ? L"确认删除" : L"删除", -1, g_fUi, db, g_sfC,
          (g_delArm || dhov) ? ARGB(255, 250, 250, 249) : C_INK(255));
}

// 每帧合成 + 提交。淡入期间合成一次全亮度快照，动画帧只做拷贝+缩放+提交
static void panel_repaint(void)
{
    if (!g_bits || !g_gfx) return;
    drawer_layout();   // v3.5：每帧重算抽屉几何（新标签展开/加标签/切换目标等状态变化后保持同步）
    memcpy(g_bits, g_base, (u64)g_winW * g_winH * 4);
    draw_beam();
    draw_search_box();
    draw_recents();
    g_nChips = 0;
    int cx = g_panel.left + g_padX;
    int chipY = g_beamY + S(2) + S(10);
    draw_chip(&cx, chipY, L"全部", -1);
    for (int i = 0; i < MP_MAX_TAGS; i++)
        if (g_store.tagNames[i]) draw_chip(&cx, chipY, g_store.tagNames[i], i);
    draw_grid();
    if (g_drawer) draw_drawer();
    draw_preview();
    if (g_imeDbg && g_imeHave) {
        // IME 定位调试：我们传给输入法的锚点（红/强调色十字）+ 排除矩形（蓝框）+ 原始数字
        POINT c = g_imeSp;  ScreenToClient(g_hwnd, &c);
        GpPen* cp = NULL;
        if (GdipCreatePen1(C_RED(255), 2.0f, GP_UNIT_PIXEL, &cp) == 0) {
            GdipDrawLineI(g_gfx, cp, c.x - 14, c.y, c.x + 14, c.y);
            GdipDrawLineI(g_gfx, cp, c.x, c.y - 14, c.x, c.y + 14);
            GdipDeletePen(cp);
        }
        RECT br = g_imeBr;
        POINT p1 = { br.left, br.top }, p2 = { br.right, br.bottom };
        ScreenToClient(g_hwnd, &p1); ScreenToClient(g_hwnd, &p2);
        GpPen* bp = NULL;
        if (GdipCreatePen1(ARGB(255, 40, 40, 255), 1.5f, GP_UNIT_PIXEL, &bp) == 0) {
            GdipDrawLineI(g_gfx, bp, p1.x, p1.y, p2.x, p1.y);
            GdipDrawLineI(g_gfx, bp, p2.x, p1.y, p2.x, p2.y);
            GdipDrawLineI(g_gfx, bp, p2.x, p2.y, p1.x, p2.y);
            GdipDrawLineI(g_gfx, bp, p1.x, p2.y, p1.x, p1.y);
            GdipDeletePen(bp);
        }
        RECT wr; GetWindowRect(g_hwnd, &wr);
        wchar_t dbg[208];
        wnsprintfW(dbg, 208,
                   L"B" __TIME__ " f=%d sp=(%d,%d) cli=(%d,%d) wr=(%d,%d) rc=(%d,%d,%d,%d) pre=%d comp=%d dw=%d",
                   (int)g_editFocus, g_imeSp.x, g_imeSp.y, c.x, c.y, wr.left, wr.top,
                   g_imeRc.left, g_imeRc.top, g_imeRc.right, g_imeRc.bottom,
                   g_imePreW, g_imeCompW, (int)g_drawer);
        gtext(dbg, -1, g_fSmall,
              (RECT){ g_panel.left + S(6), g_panel.top + S(3), g_panel.right, g_panel.top + S(18) },
              g_sfL, ARGB(255, 200, 30, 30));
        // 参考十字 A/B/C（绿色，已知客户区坐标）：截图后可像素级测量 IME 浮窗的真实偏移
        static const struct { int x, y; const wchar_t* n; } REFMK[] = {
            { 100, 100, L"A" }, { 600, 300, L"B" }, { 1100, 500, L"C" },
        };
        for (int i = 0; i < 3; i++) {
            GpPen* rp = NULL;
            if (GdipCreatePen1(ARGB(255, 20, 140, 20), 2.0f, GP_UNIT_PIXEL, &rp) == 0) {
                GdipDrawLineI(g_gfx, rp, REFMK[i].x - 10, REFMK[i].y, REFMK[i].x + 10, REFMK[i].y);
                GdipDrawLineI(g_gfx, rp, REFMK[i].x, REFMK[i].y - 10, REFMK[i].x, REFMK[i].y + 10);
                GdipDeletePen(rp);
            }
            gtext(REFMK[i].n, -1, g_fSmall,
                  (RECT){ REFMK[i].x + 8, REFMK[i].y - 22, REFMK[i].x + 44, REFMK[i].y },
                  g_sfL, ARGB(255, 20, 140, 20));
        }
        // 同一份数据落盘到 exe 目录 imedbg.log（零歧义的取证通道，不依赖读图）
        {
            char line[256];
            int ln = wnsprintfA(line, 256,
                "t=%u f=%d sp=%d,%d cli=%d,%d wr=%d,%d rc=%d,%d,%d,%d pre=%d comp=%d dw=%d sw=%d sh=%d dpi=%d win=%d,%d\r\n",
                GetTickCount(), (int)g_editFocus, g_imeSp.x, g_imeSp.y, c.x, c.y,
                wr.left, wr.top, g_imeRc.left, g_imeRc.top, g_imeRc.right, g_imeRc.bottom,
                g_imePreW, g_imeCompW, (int)g_drawer,
                GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), g_dpi,
                g_winW, g_winH);
            wchar_t p[MAX_PATH];
            GetModuleFileNameW(NULL, p, MAX_PATH);
            wchar_t* sl = p + wlen(p);
            while (sl > p && sl[-1] != L'\\') sl--;
            lstrcpynW(sl, L"imedbg.log", MAX_PATH - (int)(sl - p));
            HANDLE f = CreateFileW(p, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                                   OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (f != INVALID_HANDLE_VALUE) {
                DWORD w2; WriteFile(f, line, ln, &w2, NULL); CloseHandle(f);
            }
        }
    }
    if (g_fade < 255) {
        if (!g_fadeSnap)
            g_fadeSnap = (u32*)HeapAlloc(GetProcessHeap(), 0, (u64)g_winW * g_winH * 4);
        if (g_fadeSnap) memcpy(g_fadeSnap, g_bits, (u64)g_winW * g_winH * 4);
    } else if (g_fadeSnap) {
        HeapFree(GetProcessHeap(), 0, g_fadeSnap);
        g_fadeSnap = NULL;
    }
    // 版本角标（常显，右下角 8.5px 浅灰）：真机调试反复出现新旧进程混淆，一眼辨真伪
    gtext(L"swiss v3.9 b" __TIME__, -1, g_fNano,
          (RECT){ g_panel.right - S(190), g_panel.bottom - S(16), g_panel.right - S(6), g_panel.bottom - S(2) },
          g_sfL, ARGB(95, 17, 17, 17));
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
// 自绘输入框编辑（v3 三缓冲路由）/ IME
// WM_CHAR / IME 结果串 / 退格删除全部作用于 g_editFocus 指向的缓冲：
//   搜索（改即 refilter）/ 索引（改即写回 entry->indexText）/ 新标签（Enter 提交）
// ============================================================
static EBuf* eb_active(void)
{
    return g_editFocus == EDIT_INDEX ? &g_ebI :
           g_editFocus == EDIT_NEWTAG ? &g_ebN : &g_ebS;
}

static void edit_notify(void)
{
    if (g_editFocus == EDIT_SEARCH) refilter();
    else if (g_editFocus == EDIT_INDEX) index_commit();   // 即时写回（含 store_save）
    if (IsWindowVisible(g_hwnd) || g_headless) panel_repaint();
}

static void edit_insert(const wchar_t* s, int n)
{
    EBuf* e = eb_active();
    if (e->len + g_complen + n >= 126) return;
    memmove(e->b + e->caret + n, e->b + e->caret, (size_t)(e->len - e->caret) * 2);
    memcpy(e->b + e->caret, s, (size_t)n * 2);
    e->caret += n; e->len += n;
    e->b[e->len] = 0;
    edit_notify();
}

static void edit_backspace(void)
{
    EBuf* e = eb_active();
    if (e->caret > 0) {
        memmove(e->b + e->caret - 1, e->b + e->caret, (size_t)(e->len - e->caret) * 2);
        e->caret--; e->len--; e->b[e->len] = 0;
        edit_notify();
    }
}

static void edit_delete(void)
{
    EBuf* e = eb_active();
    if (e->caret < e->len) {
        memmove(e->b + e->caret, e->b + e->caret + 1, (size_t)(e->len - e->caret - 1) * 2);
        e->len--; e->b[e->len] = 0;
        edit_notify();
    }
}

static void edit_caret_move(int delta)   // -1/+1；钳在缓冲内
{
    EBuf* e = eb_active();
    int nc = e->caret + delta;
    if (nc < 0) nc = 0;
    if (nc > e->len) nc = e->len;
    if (nc != e->caret) { e->caret = nc; g_caretOn = TRUE; panel_repaint(); }
}

// 组合窗/候选窗位置随「焦点框」的 caret（v3.5：三框 rect 均在布局期由
// panel_layout/drawer_layout 生成——单一几何源，任何 IME 消息时序下都不依赖「先画一帧」）。
// 真机二轮修复（候选窗曾飘到网格区第三行并伸出面板右缘——IME 完全无视我们给的位置）：
//   ①ImmSetCompositionFontW 给输入法喂字体（自绘框没有 EDIT 的字体信息，IME 缺字体时
//     用自己的默认字号/锚点估算位置——头号嫌疑）；
//   ②组合窗 CFS_FORCE_POSITION 锚 caret 正下方 + rcArea=窗口矩形；
//   ③候选窗改 CFS_EXCLUDE 排除输入框屏幕矩形（「别盖住框、贴着摆」——自绘控件的标准手法；
//     CFS_CANDIDATEPOS 常被微软拼音无视）；
//   ④焦点框 rect 空态回退搜索框；最终坐标钳制在窗口矩形内。
static void ime_dbg_log(const char* s)   // -imedebug 落盘（exe 目录 imedbg.log）
{
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(NULL, p, MAX_PATH);
    wchar_t* sl = p + wlen(p);
    while (sl > p && sl[-1] != L'\\') sl--;
    lstrcpynW(sl, L"imedbg.log", MAX_PATH - (int)(sl - p));
    HANDLE f = CreateFileW(p, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD w; WriteFile(f, s, lstrlenA(s), &w, NULL); CloseHandle(f);
    }
}

// 找本线程的「默认 IME 窗」（系统创建，类名恰为 "IME"——输入法浮窗的真锚点）
static HWND g_imeDefWnd;
static BOOL CALLBACK ime_find_defwnd(HWND h, LPARAM lp)
{
    wchar_t c[32];
    if (GetClassNameW(h, c, 32) && weq(c, L"IME")) { *(HWND*)lp = h; return FALSE; }
    return TRUE;
}
static void ime_update_position(HWND hwnd)
{
    RECT rc; EBuf* e;
    switch (g_editFocus) {
    case EDIT_INDEX:
        rc = (g_dIndexBox.right > g_dIndexBox.left) ? g_dIndexBox : g_searchBox;
        e = &g_ebI; break;
    case EDIT_NEWTAG:
        rc = (g_dNewTagBox.right > g_dNewTagBox.left) ? g_dNewTagBox : g_searchBox;
        e = &g_ebN; break;
    default:
        rc = g_searchBox; e = &g_ebS; break;
    }
    if (rc.right <= rc.left || rc.bottom <= rc.top) return;
    HIMC im = ImmGetContext(hwnd);
    if (!im) return;

    // ① 组合字体：自绘输入框没有 EDIT 那样的字体信息，IME 缺字体会用自己的
    //    默认字号/位置估算（真机错位的头号嫌疑）。给输入法与 g_fText 同源的 LOGFONTW。
    static LOGFONTW s_imeLf;
    if (!s_imeLf.lfFaceName[0]) {
        memset(&s_imeLf, 0, sizeof s_imeLf);
        s_imeLf.lfHeight = -S(13);
        s_imeLf.lfWeight = FW_NORMAL;
        s_imeLf.lfCharSet = DEFAULT_CHARSET;
        s_imeLf.lfQuality = CLEARTYPE_QUALITY;
        s_imeLf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
        lstrcpynW(s_imeLf.lfFaceName, L"Segoe UI", 32);
    }
    ImmSetCompositionFontW(im, &s_imeLf);

    // ② 组合窗：锚在 caret 正下方（CFS_FORCE_POSITION + 完整 rcArea）
    float preW = e->caret ? gtext_w(e->b, e->caret, g_fText) : 0.0f;
    // 微软拼音把候选窗摆在「组合串末尾」而非串首：组合串被我们自绘抑制后不可见，
    // 视觉上就成了候选窗向右漂一截（真机仪表实测：锚点正确、候选窗右偏≈组合串宽）。
    // 对症：锚点左移当前组合串宽度，让 IME 认知的「串末尾」落回真实 caret。
    // 真实 caret 位置（不带任何补偿——三组 -pos 实验证明：候选窗 = CFS_EXCLUDE 排除矩形的
    // 邻接摆放。之前传「整个输入框」矩形 → 候选窗被摆在框旁（下+边距）；改为「caret 点矩形」）
    int cxx = rc.left + S(2) + (int)preW;
    if (g_imeDbg) { g_imeRc = rc; g_imePreW = (int)preW; g_imeCompW = 0; }
    int cyy = (rc.top + rc.bottom) / 2;
    POINT sp = { cxx, cyy + S(9) };
    ClientToScreen(hwnd, &sp);
    RECT wr; GetWindowRect(hwnd, &wr);
    if (sp.x < wr.left) sp.x = wr.left;
    if (sp.x > wr.right - 8) sp.x = wr.right - 8;
    if (sp.y < wr.top) sp.y = wr.top;
    if (sp.y > wr.bottom - 8) sp.y = wr.bottom - 8;
    COMPOSITIONFORM cf;
    cf.dwStyle = CFS_FORCE_POSITION;
    cf.ptCurrentPos = sp;
    cf.rcArea = wr;
    ImmSetCompositionWindow(im, &cf);
    // 候选窗：CFS_EXCLUDE 排除「caret 点」小矩形（8x20，屏幕坐标）——邻接摆放即贴着光标
    CANDIDATEFORM cad;
    cad.dwIndex = 0;
    cad.dwStyle = CFS_EXCLUDE;
    cad.ptCurrentPos = sp;
    cad.rcArea.left = sp.x;        cad.rcArea.top = sp.y;
    cad.rcArea.right = sp.x + S(8); cad.rcArea.bottom = sp.y + S(20);
    ImmSetCandidateWindow(im, &cad);
    ImmReleaseContext(hwnd, im);

    // 隐形系统光标（调研定案）：CTF/AIMM 输入法（微软拼音）锚定浮窗靠 GetCaretPos 查询，
    // 自绘输入框没有系统 caret → 查询落空 → 浮窗飘到默认位。Chromium ime_input.cc 即用此法。
    // CreateCaret 默认隐藏、SetCaretPos 对隐藏 caret 照样生效 → 零视觉副作用。
    if (!g_sysCaret) { CreateCaret(hwnd, NULL, 1, 1); g_sysCaret = TRUE; }
    int cyyTop = (rc.top + rc.bottom) / 2 - S(9);
    SetCaretPos(cxx, cyyTop);

    // 「默认 IME 窗」（本线程系统窗口，类名 "IME"）是输入法浮窗的真锚点——
    // 三组 -pos 实验实测候选窗 = 窗口原点 + (103,117) ≈ 该窗口的出厂位 (100,100)。
    // 光标晚建导致系统没挪过它。这里直接把它搬到光标屏幕位置（自家进程的窗口，搬得动）。
    if (!g_imeDefWnd) {
        EnumThreadWindows(GetCurrentThreadId(), ime_find_defwnd, (LPARAM)&g_imeDefWnd);
    }
    if (g_imeDefWnd) {
        RECT iw; GetWindowRect(g_imeDefWnd, &iw);
        SetWindowPos(g_imeDefWnd, NULL, sp.x, sp.y, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        if (g_imeDbg) {
            char cl[96];
            wnsprintfA(cl, 96, "imedef was=(%d,%d) -> (%d,%d)\r\n",
                       iw.left, iw.top, sp.x, sp.y);
            ime_dbg_log(cl);
        }
    }

    g_imeSp = sp;
    g_imeBr.left = sp.x;          g_imeBr.top = sp.y;
    g_imeBr.right = sp.x + S(8);  g_imeBr.bottom = sp.y + S(20);
    g_imeHave = TRUE;
    if (g_imeDbg) {
        POINT gc; BOOL gok = GetCaretPos(&gc);
        char cl[64];
        wnsprintfA(cl, 64, "caret set=%d,%d readback=%d (%d,%d)\r\n",
                   cxx, cyyTop, (int)gok, gc.x, gc.y);
        ime_dbg_log(cl);
        panel_repaint();
    }
}

// ============================================================
// v2 主题色持久化：exe 同目录 theme.cfg = u32 magic 'TPT1' + u32 RGB（共 8 字节）
// 读：无文件/短读/坏 magic = 保持默认正红；写：.tmp + MoveFileExW 原子替换（同 store_save）
// ============================================================
static void theme_path(wchar_t* out, int cap)
{
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t* slash = exe + wlen(exe);
    while (slash > exe && slash[-1] != L'\\') slash--;
    *slash = 0;
    wnsprintfW(out, cap, L"%stheme.cfg", exe);
}

static void theme_load(void)
{
    wchar_t p[MAX_PATH];
    theme_path(p, MAX_PATH);
    HANDLE f = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    u8 buf[8];
    DWORD got = 0;
    BOOL ok = ReadFile(f, buf, 8, &got, NULL) && got == 8;
    CloseHandle(f);
    if (!ok) return;
    u32 magic, rgb;
    memcpy(&magic, buf, 4);
    memcpy(&rgb, buf + 4, 4);
    if (magic != THEME_MAGIC) return;   // 坏文件 = 默认正红
    g_accent = RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
}

static void theme_save(void)
{
    wchar_t p[MAX_PATH], tmp[MAX_PATH];
    theme_path(p, MAX_PATH);
    wnsprintfW(tmp, MAX_PATH, L"%s.tmp", p);
    u8 buf[8];
    u32 magic = THEME_MAGIC;
    u32 rgb = ((u32)GetRValue(g_accent) << 16) | ((u32)GetGValue(g_accent) << 8) | GetBValue(g_accent);
    memcpy(buf, &magic, 4);
    memcpy(buf + 4, &rgb, 4);
    HANDLE f = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD got = 0;
        if (WriteFile(f, buf, 8, &got, NULL) && got == 8) {
            CloseHandle(f);
            MoveFileExW(tmp, p, MOVEFILE_REPLACE_EXISTING);
        } else {
            CloseHandle(f);
            DeleteFileW(tmp);
        }
    }
}

// 选色后统一入口：立即生效 + 落盘 + 重绘
static void theme_apply(COLORREF c)
{
    g_accent = c;
    theme_save();
    if (IsWindowVisible(g_hwnd)) panel_repaint();
}

// ============================================================
// v3 prefs.cfg：exe 同目录 u32 magic 'MPP1' + u32 flags（bit0 = 搜索包含文件名，默认开）
// 读写模式与 theme.cfg 完全一致（.tmp + MoveFileExW 原子替换）
// ============================================================
static void prefs_path(wchar_t* out, int cap)
{
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t* slash = exe + wlen(exe);
    while (slash > exe && slash[-1] != L'\\') slash--;
    *slash = 0;
    wnsprintfW(out, cap, L"%sprefs.cfg", exe);
}

static void prefs_load(void)
{
    wchar_t p[MAX_PATH];
    prefs_path(p, MAX_PATH);
    HANDLE f = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    u8 buf[8];
    DWORD got = 0;
    BOOL ok = ReadFile(f, buf, 8, &got, NULL) && got == 8;
    CloseHandle(f);
    if (!ok) return;
    u32 magic, flags;
    memcpy(&magic, buf, 4);
    memcpy(&flags, buf + 4, 4);
    if (magic != PREFS_MAGIC) return;   // 坏文件 = 默认（文件名搜索开）
    g_prefs = flags;
}

static void prefs_save(void)
{
    wchar_t p[MAX_PATH], tmp[MAX_PATH];
    prefs_path(p, MAX_PATH);
    wnsprintfW(tmp, MAX_PATH, L"%s.tmp", p);
    u8 buf[8];
    u32 magic = PREFS_MAGIC;
    memcpy(buf, &magic, 4);
    memcpy(buf + 4, &g_prefs, 4);
    HANDLE f = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD got = 0;
        if (WriteFile(f, buf, 8, &got, NULL) && got == 8) {
            CloseHandle(f);
            MoveFileExW(tmp, p, MOVEFILE_REPLACE_EXISTING);
        } else {
            CloseHandle(f);
            DeleteFileW(tmp);
        }
    }
}

// 「自定义…」系统颜色对话框：动态加载 comdlg32（保持零新增导入表）。
// 打开期间置 g_modal，防止面板因失焦被 WM_ACTIVATEAPP 自动收起。
static BOOL pick_custom_color(HWND owner, COLORREF* out)
{
    HMODULE cd = LoadLibraryW(L"commdlg32.dll");
    if (!cd) return FALSE;
    BOOL (WINAPI *pChooseColorW)(CHOOSECOLORW*) =
        (BOOL (WINAPI*)(CHOOSECOLORW*))GetProcAddress(cd, "ChooseColorW");
    BOOL ok = FALSE;
    if (pChooseColorW) {
        static COLORREF cust[16];      // 自定义色板：首开初始化为预设色
        static BOOL custInit = FALSE;
        if (!custInit) {
            for (int i = 0; i < 5; i++) cust[i] = THEME_PRESETS[i];
            for (int i = 5; i < 16; i++) cust[i] = RGB(255, 255, 255);
            custInit = TRUE;
        }
        CHOOSECOLORW cc;
        memset(&cc, 0, sizeof cc);
        cc.lStructSize = sizeof cc;
        cc.hwndOwner = owner;
        cc.rgbResult = g_accent;
        cc.lpCustColors = cust;
        cc.Flags = CC_FULLOPEN | CC_RGBINIT;
        g_modal = TRUE;
        if (pChooseColorW(&cc)) { *out = cc.rgbResult; ok = TRUE; }
        g_modal = FALSE;
    }
    FreeLibrary(cd);
    return ok;
}

// ============================================================
// v3 键盘导航：方向键在网格内移动 g_hover（= 键盘选中框，视觉与 hover 同源：
// accent 框 + 编号块），越出可视区时 g_first 按行滚动跟随；鼠标 hover 两态同源
// ============================================================
static void hover_move(HWND hwnd, int delta)
{
    (void)hwnd;
    if (g_nFilt == 0) return;
    if (g_hover < 0) g_hover = 0;
    else {
        int nh = g_hover + delta;
        if (nh < 0) nh = 0;
        if (nh >= g_nFilt) nh = g_nFilt - 1;
        g_hover = nh;
    }
    int vis = g_cols * g_rows;
    int rowStart = (g_hover / g_cols) * g_cols;
    if (g_hover < g_first) g_first = rowStart;
    else if (g_hover >= g_first + vis) {
        g_first = rowStart - (g_rows - 1) * g_cols;
        if (g_first < 0) g_first = 0;
    }
    clamp_first();
    g_previewIdx = -1;
    panel_repaint();
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
        // 隐形系统光标提前建好：IME 激活瞬间系统会查 GetCaretPos 定位「默认 IME 窗」，
        // 晚建（打字时才建）会让它留在出厂位 (100,100) —— 即真机实验测得的候选窗幽灵锚点
        if (!g_sysCaret) { CreateCaret(hwnd, NULL, 1, 1); g_sysCaret = TRUE; }
        return 0;

    case WM_KILLFOCUS:
        KillTimer(hwnd, CARET_TIMER);
        if (g_sysCaret) { DestroyCaret(); g_sysCaret = FALSE; }   // 隐形 caret 归属随焦点
        return 0;

    // ---- 键盘：导航 + 自绘输入框编辑（v3 三缓冲路由）----
    case WM_KEYDOWN:
        if (g_complen) break;   // IME 组合中：Enter/Esc/方向键/退格全部交给输入法，不触发面板行为
        switch (wParam) {
        case VK_ESCAPE:
            // 分层 Esc：焦点在抽屉输入框 → 回搜索框（新标签输入收起）；
            // 否则抽屉开 → 关抽屉；否则隐藏面板
            if (g_editFocus != EDIT_SEARCH) {
                edit_set_focus(EDIT_SEARCH);
                panel_repaint();
            }
            else if (g_drawer) drawer_set_open(hwnd, FALSE);
            else panel_hide(hwnd);
            return 0;
        case VK_RETURN:
            if (g_editFocus == EDIT_NEWTAG) { newtag_commit(hwnd); return 0; }
            if (g_editFocus == EDIT_INDEX) { edit_set_focus(EDIT_SEARCH); panel_repaint(); return 0; }
            // 发送：键盘选中项优先，否则首个可见项
            if (g_nFilt > 0) {
                int idx = (g_hover >= 0 && g_hover < g_nFilt) ? g_hover : g_first;
                paste_entry(hwnd, g_filt[idx]);
            }
            return 0;
        case VK_PRIOR: g_first -= g_rows * g_cols; clamp_first(); panel_repaint(); return 0;
        case VK_NEXT:  g_first += g_rows * g_cols; clamp_first(); panel_repaint(); return 0;
        // 方向键：搜索焦点 = 网格导航（键盘选中框）；抽屉输入焦点 = caret 移动
        case VK_UP:
            if (g_editFocus == EDIT_SEARCH) hover_move(hwnd, -g_cols);
            return 0;
        case VK_DOWN:
            if (g_editFocus == EDIT_SEARCH) hover_move(hwnd, g_cols);
            return 0;
        case VK_LEFT:
            if (g_editFocus == EDIT_SEARCH) hover_move(hwnd, -1);
            else edit_caret_move(-1);
            return 0;
        case VK_RIGHT:
            if (g_editFocus == EDIT_SEARCH) hover_move(hwnd, 1);
            else edit_caret_move(1);
            return 0;
        case VK_HOME: {
            EBuf* e = eb_active();
            if (e->caret) { e->caret = 0; g_caretOn = TRUE; panel_repaint(); }
            return 0;
        }
        case VK_END: {
            EBuf* e = eb_active();
            if (e->caret != e->len) { e->caret = e->len; g_caretOn = TRUE; panel_repaint(); }
            return 0;
        }
        case VK_BACK:
            edit_backspace();
            g_caretOn = TRUE;
            return 0;
        case VK_DELETE:
            edit_delete();
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
            edit_insert(&c, 1);
            g_caretOn = TRUE;
        }
        return 0;
    }

    // ---- IME ----
    case WM_IME_REQUEST:
        // IMR_QUERYCHARPOSITION：输入法「询问」组合字符的屏幕位置（自绘文本应用的官方协议）。
        // 之前从未应答（默认 FALSE）→ 输入法只能用默认位 (103,117) —— 即真机实测的幽灵锚点。
        if (wParam == IMR_QUERYCHARPOSITION && lParam) {
            IMECHARPOSITION* icp = (IMECHARPOSITION*)lParam;
            RECT rc; EBuf* e;
            switch (g_editFocus) {
            case EDIT_INDEX:
                rc = (g_dIndexBox.right > g_dIndexBox.left) ? g_dIndexBox : g_searchBox;
                e = &g_ebI; break;
            case EDIT_NEWTAG:
                rc = (g_dNewTagBox.right > g_dNewTagBox.left) ? g_dNewTagBox : g_searchBox;
                e = &g_ebN; break;
            default: rc = g_searchBox; e = &g_ebS; break;
            }
            if (rc.right <= rc.left) return DefWindowProcW(hwnd, msg, wParam, lParam);
            // dwPos = 组合串内字符序号：x = 文本起点 + 已有前缀宽 + 组合串前 dwPos 字符宽
            int tx = rc.left + S(2);
            float preW = e->caret ? gtext_w(e->b, e->caret, g_fText) : 0.0f;
            int n = (int)icp->dwCharPos;
            if (n < 0) n = 0;
            if (n > g_complen) n = g_complen;
            float cw = n ? gtext_w(g_comp, n, g_fText) : 0.0f;
            icp->pt.x = tx + (int)preW + (int)cw;
            icp->pt.y = (rc.top + rc.bottom) / 2;
            ClientToScreen(hwnd, &icp->pt);
            icp->cLineHeight = (UINT)(rc.bottom - rc.top);
            icp->rcDocument = rc;
            POINT p1 = { rc.left, rc.top }, p2 = { rc.right, rc.bottom };
            ClientToScreen(hwnd, &p1); ClientToScreen(hwnd, &p2);
            icp->rcDocument.left = p1.x; icp->rcDocument.top = p1.y;
            icp->rcDocument.right = p2.x; icp->rcDocument.bottom = p2.y;
            if (g_imeDbg) {
                char cl[96];
                wnsprintfA(cl, 96, "IMR_QUERY dwPos=%u -> (%d,%d)\r\n",
                           icp->dwCharPos, icp->pt.x, icp->pt.y);
                ime_dbg_log(cl);
            }
            return TRUE;   // 已应答：输入法用这个坐标摆组合/候选 UI
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    case WM_IME_SETCONTEXT:
        lParam &= ~ISC_SHOWUICOMPOSITIONWINDOW;   // 组合串自绘
        if (wParam) ime_update_position(hwnd);    // IME 激活瞬间就位（部分输入法在 START 前定位）
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
                edit_insert(buf, n / 2);   // 结果串路由到焦点缓冲
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
        if (PtInRect(&g_closeRc, pt)) {
            panel_hide(hwnd);   // ✕ = 隐藏面板（抽屉随之关）
            return 0;
        }
        // ---- 抽屉控件（命中区在竖分隔线右侧）----
        if (g_drawer && pt.x >= g_drawerLeft) {
            if (PtInRect(&g_dDelRc, pt)) { drawer_delete(hwnd); return 0; }
            if (PtInRect(&g_dClearRc, pt)) {          // 批量 ✕：清除选择（批量目标即消失）
                sel_clear();
                drawer_set_open(hwnd, FALSE);
                return 0;
            }
            if (PtInRect(&g_dIndexBox, pt)) {         // 索引输入框：仅单目标可编辑
                if (drawer_target()) {
                    edit_set_focus(EDIT_INDEX);
                    g_caretOn = TRUE;
                    ime_update_position(hwnd);
                    panel_repaint();
                }
                return 0;
            }
            if (PtInRect(&g_dNewTagBox, pt)) {
                edit_set_focus(EDIT_NEWTAG);
                g_caretOn = TRUE;
                ime_update_position(hwnd);
                panel_repaint();
                return 0;
            }
            for (int i = 0; i < g_nDChips; i++) {     // 抽屉标签 chips / + 新标签
                if (!PtInRect(&g_dChipRc[i], pt)) continue;
                int id = g_dChipId[i];
                if (id == -2) {                       // + 新标签 → 原地展开内联输入
                    g_newtagOpen = TRUE;
                    edit_set_focus(EDIT_NEWTAG);
                    g_caretOn = TRUE;
                    panel_repaint();
                }
                else drawer_toggle_tag(id);
                return 0;
            }
            if (PtInRect(&g_dOpenRc, pt)) { drawer_open_location(hwnd); return 0; }
            return 0;   // 抽屉空白：不处理
        }
        // ---- 网格区 ----
        if (PtInRect(&g_searchBox, pt)) {             // 搜索框：切焦点（不动 caret）
            edit_set_focus(EDIT_SEARCH);
            g_caretOn = TRUE;
            panel_repaint();
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
        if (idx >= 0) {
            if (wParam & MK_CONTROL) {                // Ctrl+点击 = toggle 选择（四角角标）
                sel_set(g_filt[idx], !sel_has(g_filt[idx]));
                panel_repaint();
            } else {
                paste_entry(hwnd, g_filt[idx]);       // 普通点击 = 发送（主路径不变）
            }
            return 0;
        }
        if (g_nSel) {                                 // 点击空白处清选择
            sel_clear();
            if (g_drawer && g_drawerEi == -1) drawer_set_open(hwnd, FALSE);
            panel_repaint();
        }
        return 0;
    }

    case WM_RBUTTONUP: {
        // 用户决定（2026-09-19）：右键直接展开检视抽屉——原菜单的标签 toggle 与抽屉
        // chips 完全重复、其余只剩「编辑…」，多一层反而啰嗦。标签快改就走抽屉。
        POINT pt;
        pt.x = (short)LOWORD(lParam);
        pt.y = (short)HIWORD(lParam);
        int idx = cell_from_point(pt);
        if (idx < 0) {
            // 右键网格区空白：收起抽屉（打开时）
            if (g_drawer && pt.x < g_drawerLeft) drawer_set_open(hwnd, FALSE);
            return 0;
        }
        drawer_open_for(hwnd, g_filt[idx]);
        return 0;
    }

    case WM_DROPFILES:
        import_drop(hwnd, (HDROP)wParam);
        return 0;

    case WM_ACTIVATEAPP:
        if (!wParam && !g_modal && !g_menuUp) panel_hide(hwnd);   // 选色对话框/菜单打开时不收起
        return 0;

    case WM_TRAY:
        if (lParam == WM_LBUTTONUP) {
            panel_show(hwnd);
        } else if (lParam == WM_RBUTTONUP) {
            SetForegroundWindow(hwnd);
            POINT pt; GetCursorPos(&pt);
            HMENU m = CreatePopupMenu();
            AppendMenuW(m, MF_STRING, IDM_SHOW,   L"打开面板");
            // 主题色弹出子菜单：预设打勾标记当前项 + 自定义…
            HMENU th = CreatePopupMenu();
            AppendMenuW(th, MF_STRING | (g_accent == THEME_PRESETS[0] ? MF_CHECKED : 0),
                        IDM_THEME_BASE + 0, L"正红（默认）");
            AppendMenuW(th, MF_STRING | (g_accent == THEME_PRESETS[1] ? MF_CHECKED : 0),
                        IDM_THEME_BASE + 1, L"钴蓝");
            AppendMenuW(th, MF_STRING | (g_accent == THEME_PRESETS[2] ? MF_CHECKED : 0),
                        IDM_THEME_BASE + 2, L"常青");
            AppendMenuW(th, MF_STRING | (g_accent == THEME_PRESETS[3] ? MF_CHECKED : 0),
                        IDM_THEME_BASE + 3, L"琥珀");
            AppendMenuW(th, MF_STRING | (g_accent == THEME_PRESETS[4] ? MF_CHECKED : 0),
                        IDM_THEME_BASE + 4, L"墨黑");
            AppendMenuW(th, MF_SEPARATOR, 0, NULL);
            AppendMenuW(th, MF_STRING, IDM_THEME_BASE + 5, L"自定义…");
            AppendMenuW(m, MF_POPUP, (UINT_PTR)th, L"主题色");
            // 维护子菜单（修后缀已自动化为扫描后静默执行，不占菜单）
            HMENU mt = CreatePopupMenu();
            AppendMenuW(mt, MF_STRING, IDM_MAINT_DEDUP, L"去重扫描…");
            AppendMenuW(m, MF_POPUP, (UINT_PTR)mt, L"维护");
            AppendMenuW(m, MF_STRING | ((g_prefs & PF_NAMESEARCH) ? MF_CHECKED : 0),
                        IDM_SEARCHFLAG, L"搜索：包含文件名");
            AppendMenuW(m, MF_STRING, IDM_RESCAN, L"重新扫描");
            AppendMenuW(m, MF_SEPARATOR, 0, NULL);
            AppendMenuW(m, MF_STRING, IDM_EXIT,   L"退出");
            g_menuUp = TRUE;
            int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                     pt.x, pt.y, 0, hwnd, NULL);
            g_menuUp = FALSE;
            PostMessageW(hwnd, WM_NULL, 0, 0);
            DestroyMenu(m);   // 连同子菜单一起销毁
            if (cmd == IDM_SHOW) panel_show(hwnd);
            else if (cmd >= IDM_THEME_BASE && cmd <= IDM_THEME_BASE + 5) {
                COLORREF pick = g_accent;
                BOOL got = FALSE;
                if (cmd < IDM_THEME_BASE + 5) { pick = THEME_PRESETS[cmd - IDM_THEME_BASE]; got = TRUE; }
                else got = pick_custom_color(hwnd, &pick);
                if (got) theme_apply(pick);
            }
            else if (cmd == IDM_MAINT_DEDUP) maint_dedup(hwnd);
            else if (cmd == IDM_SEARCHFLAG) {
                g_prefs ^= PF_NAMESEARCH;
                prefs_save();
                refilter();
                if (IsWindowVisible(hwnd)) panel_repaint();
            }
            else if (cmd == IDM_RESCAN) {
                store_scan(&g_store);
                maint_fixext();
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
// 图标（瑞士风：纸白方角底 + 黑框 + 黑笑脸 + 左上正红小方块）
// ============================================================
static void sq_gdi(HDC dc, LONG l, LONG t, LONG r, LONG b, HBRUSH fill, HPEN pen)
{
    HGDIOBJ ob = fill ? SelectObject(dc, fill) : SelectObject(dc, GetStockObject(NULL_BRUSH));
    HGDIOBJ op = pen ? SelectObject(dc, pen) : SelectObject(dc, GetStockObject(NULL_PEN));
    Rectangle(dc, l, t, r + 1, b + 1);
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
        // 纸白底（250,250,249），手写像素
        u32* px = (u32*)colorBits;
        for (int i = 0; i < 64 * 64; i++) px[i] = 0xFF000000u | (250u << 16) | (250u << 8) | 249u;
        HDC mdc = CreateCompatibleDC(sdc);
        HGDIOBJ old = SelectObject(mdc, color);
        // 黑色粗直角外框
        HPEN edge = CreatePen(PS_SOLID, 4, RGB(17, 17, 17));
        HBRUSH paper = CreateSolidBrush(RGB(250, 250, 249));
        sq_gdi(mdc, 5, 5, 58, 58, paper, edge);
        // 左上强调色小方块（网格编号的呼应；v2 随 g_accent 变）
        HBRUSH red = CreateSolidBrush(g_accent);
        sq_gdi(mdc, 10, 10, 18, 18, red, NULL);
        // 黑色笑脸（右下区域）
        HPEN wp = CreatePen(PS_SOLID, 4, RGB(17, 17, 17));
        HBRUSH wb = CreateSolidBrush(RGB(17, 17, 17));
        HGDIOBJ op = SelectObject(mdc, wp);
        HGDIOBJ ob = SelectObject(mdc, wb);
        int e = 2;
        Ellipse(mdc, 30 - e, 32 - e, 32 + e, 34 + e);
        Ellipse(mdc, 44 - e, 32 - e, 46 + e, 34 + e);
        SelectObject(mdc, GetStockObject(NULL_BRUSH));
        Arc(mdc, 28, 37, 48, 54, 31, 46, 45, 46);
        SelectObject(mdc, ob);
        SelectObject(mdc, op);
        SelectObject(mdc, old);
        DeleteDC(mdc);
        DeleteObject(paper); DeleteObject(edge);
        DeleteObject(red); DeleteObject(wb); DeleteObject(wp);
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
//   MemePanel.exe -shot <file.png> [-hover N] [-tag N] [-text 词] [-accent RRGGBB]
//                 [-drawer N | -drawer batch]
//     -hover N  模拟悬停/键盘选中第 N 个可见格（含 accent 框 + 名字条 + 168px 大图）
//     -tag N    激活第 N 个标签胶囊
//     -drawer N     打开检视抽屉，单目标 = 过滤后第 N 项（默认 5；indexText 为空则注入示例词）
//     -drawer batch 选择 {2,5,9} 三项 + 打开抽屉批量态
//     -accent RRGGBB  覆盖主题色渲染（hex，大小写均可；不写 theme.cfg）
// ============================================================
static int watoi_(const wchar_t* s)
{
    while (*s == L' ') s++;
    int v = 0;
    while (*s >= L'0' && *s <= L'9') { v = v * 10 + (*s - L'0'); s++; }
    return v;
}

// 解析 "RRGGBB" 十六进制（大小写均可）；非 6 位 hex 返回 0xFFFFFFFF
static u32 whex_(const wchar_t* s)
{
    u32 v = 0;
    int n = 0;
    for (; s && *s; n++) {
        wchar_t c = *s++;
        int d;
        if (c >= L'0' && c <= L'9') d = c - L'0';
        else if (c >= L'a' && c <= L'f') d = c - L'a' + 10;
        else if (c >= L'A' && c <= L'F') d = c - L'A' + 10;
        else return 0xFFFFFFFFu;
        v = (v << 4) | (u32)d;
    }
    return (n == 6) ? v : 0xFFFFFFFFu;
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

    // -accent RRGGBB：shot 模式覆盖主题色渲染（只改内存，不写 theme.cfg）
    if (cl_arg(L"-accent")) {
        wchar_t tok[16];
        cl_token(cl_arg(L"-accent"), tok, 16);
        u32 v = whex_(tok);
        if (v != 0xFFFFFFFFu)
            g_accent = RGB((v >> 16) & 255, (v >> 8) & 255, v & 255);
    }

    // 演示数据无使用记录：无头导出时伪造最近使用时间，让「最近行」进入构图
    for (int i = 0; i < 7 && g_store.count; i++) {
        int ei = (int)((u64)i * (u64)g_store.count / 7);
        if (ei >= g_store.count) ei = g_store.count - 1;
        g_store.entries[ei].used = GetTickCount64() - (u64)(7 - i) * 3600000ull;
    }
    recalc_recents();

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
        lstrcpynW(g_ebS.b, tok, 127);
        g_ebS.len = (int)wlen(g_ebS.b);
        g_ebS.caret = g_ebS.len;
        refilter();
    }
    // -drawer N | -drawer batch：打开检视抽屉（单目标=过滤后第 N 项，N 默认 5；
    // 该项 indexText 为空则注入示例词便于验证填充态；batch = 选择 {2,5,9} 批量态）
    if (cl_arg(L"-drawer") && g_nFilt > 0) {
        wchar_t tok[16];
        cl_token(cl_arg(L"-drawer"), tok, 16);
        BOOL isBatch = (tok[0] == L'b' || tok[0] == L'B');
        if (isBatch) {
            int picks[3] = { 2, 5, 9 };
            for (int i = 0; i < 3; i++) {
                int k = picks[i];
                if (k >= g_nFilt) k = g_nFilt - 1;
                if (k >= 0) sel_set(g_filt[k], TRUE);
            }
            g_drawer = TRUE;
            g_drawerEi = -1;
        } else {
            int k = watoi_(tok);
            if (k <= 0) k = 5;
            if (k >= g_nFilt) k = g_nFilt - 1;
            int ei = g_filt[k];
            if (!g_store.entries[ei].indexText || !g_store.entries[ei].indexText[0])
                g_store.entries[ei].indexText = wdup(L"开心 猫 午睡");
            g_drawer = TRUE;
            g_drawerEi = ei;
            lstrcpynW(g_ebI.b, g_store.entries[ei].indexText ? g_store.entries[ei].indexText : L"", 127);
            g_ebI.len = (int)wlen(g_ebI.b);
            g_ebI.caret = g_ebI.len;
        }
    }

    // 与 panel_show 同一套尺寸 + 抽屉加宽（隐藏窗口，无任何屏上影响）
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    int pw = sw * 46 / 100, ph = sh * 54 / 100;
    if (pw < S(700)) pw = S(700);
    if (ph < S(460)) ph = S(460);
    if (g_drawer) pw += S(300);
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
    }
    // 无头复现 IME 定位（-imedebug）：抽屉单目标 + 索引框焦点 + 假组合串"kaixin"，
    // 让 ime_update_position 的全部输入/输出（rc 选择/preW/compW/最终 sp 十字）进入截图。
    // v3.5：抽屉 rect 已由上方 panel_layout 在布局期算好——无需再「先画一帧」。
    if (g_imeDbg) {
        if (g_drawer && g_drawerEi >= 0) {
            g_editFocus = EDIT_INDEX;
            lstrcpynW(g_comp, L"kaixin", 32);
            g_complen = 6;
            ime_update_position(hwnd);         // 内部会再 panel_repaint 带上调试层
        }
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
    g_imeDbg   = wcontains_ci(GetCommandLineW(), L"-imedebug");
    {   // 每次进程启动留痕（不依赖 -imedebug）：事后可查证用户跑的是哪个构建
        char bl[96];
        wnsprintfA(bl, 96, "=== session v3.9 b%s ===\r\n", __TIME__);
        ime_dbg_log(bl);
    }
    if (cl_arg(L"-pos")) {          // -pos X,Y：窗口定位覆盖（IME 偏移随窗口位置变化的实验）
        const wchar_t* pp = cl_arg(L"-pos");
        g_posX = watoi_(pp);
        while (*pp >= L'0' && *pp <= L'9') pp++;
        if (*pp == L',' || *pp == L'x' || *pp == L'X') pp++;
        g_posY = watoi_(pp);
        if (g_posX < 0 || g_posY < 0) g_posX = g_posY = -1;
    }

    BOOL (WINAPI *pSDPA)(void) =
        (BOOL (WINAPI*)(void))GetProcAddress(GetModuleHandleW(L"user32"), "SetProcessDPIAware");
    if (pSDPA) pSDPA();
    HDC zdc = GetDC(NULL);
    g_dpi = GetDeviceCaps(zdc, LOGPIXELSX);
    ReleaseDC(NULL, zdc);
    g_bleed = S(3);    // 无投影阴影：出血只留圆角 AA 的 2-3px 余量，窗口即贴面板

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
    theme_load();          // theme.cfg（无/坏文件 = 默认正红；-shot 的 -accent 稍后覆盖内存值）
    prefs_load();          // prefs.cfg（无/坏文件 = 默认：搜索包含文件名）
    g_anim.want = -1;
    g_anim.pending = -1;

    // 字体 / 格式（瑞士纪律：全站唯一家族 Segoe UI，靠字重/字号分层）
    HDC fdc = GetDC(NULL);
    g_fText  = make_font(fdc, S(13), FW_NORMAL,   L"Segoe UI");
    g_fUi    = make_font(fdc, S(12), FW_SEMIBOLD, L"Segoe UI");
    g_fSmall = make_font(fdc, S(11), FW_NORMAL,   L"Segoe UI");
    g_fSect  = make_font(fdc, S(11), FW_SEMIBOLD, L"Segoe UI");  // v3 抽屉节标题 11px 加重
    g_fTiny  = make_font(fdc, S(9),  FW_NORMAL,   L"Segoe UI");
    g_fNano  = make_font(fdc, (17 * g_dpi + 96) / 192, FW_NORMAL, L"Segoe UI");  // 8.5px 淡化字
    ReleaseDC(NULL, fdc);
    g_sfL  = make_sf(GP_ALIGN_NEAR,   GP_ALIGN_CENTER);
    g_sfC  = make_sf(GP_ALIGN_CENTER, GP_ALIGN_CENTER);
    g_sfCF = make_sf(GP_ALIGN_CENTER, GP_ALIGN_FAR);
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
    maint_fixext();        // v3：扫描后静默修后缀（启动路径）
    refilter();
    recalc_recents();
    g_mouse.x = g_mouse.y = -30000;

    if (g_headless) {
        shot_run(g_hwnd);
        ExitProcess(0);
    }

    if (wcontains_ci(GetCommandLineW(), L"-show"))
        panel_show(g_hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    ExitProcess(0);
}
