// main.c — MemePanel v0.18.2「SWISS」：格间发丝线移除（留白分格）、最近行墨线分节、
//       主面板标签行横向滚动（整枚可见 + 滚轮吸附）、管理窗复开表面重建、评审修复
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
#define IDM_LANG        2222              // 语言切换（中/英，prefs bit1 持久化）
#define IDM_TAGS        2223              // 标签管理窗口（v0.10）
#define IDM_IMPORT      2224              // 导入窗口（v0.10）
#define IDM_AUTOSTART   2225              // 开机自启（checkable，HKCU Run 键）
#define IDM_RECENT_BASE 2230              // 2230..2236 最近行格数 1..7（checkable）
#define IDM_OPACITY_BASE 2240             // 2240..2245 不透明度 100..50%（checkable，prefs v3）
#define IDM_FOLLOWCUR   2246              // 呼出定位模式（checkable：勾 = 始终光标锚定）
#define IDM_SETTINGS    2247              // 设置窗口（v0.16：托盘瘦身为入口集后的设置中心）
#define THEME_MAGIC  0x31545054u           // 'TPT1'
#define ANIM_TIMER   2
#define CARET_TIMER  3
#define PREVIEW_TMR  4
#define GIF_TIMER    5
#define ARM_TMR      6                   // 删除二次确认武装 3 秒复位
#define OPACITY_TMR  7                   // v0.11 透明度 HUD 读数 900ms 自灭

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
static HWND      g_hwndTags, g_hwndImp;   // v0.10 标签管理 / 导入 两个独立顶层窗
static HWND      g_hwndSet;               // v0.16 设置窗口
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
static u32*    g_fadeSnap;      // 淡入/淡出动画的全亮度帧快照（动画期间免全量合成）
static int     g_fade = 255;
static BOOL    g_fadeOut;       // v0.9 关闭淡出中：panel_hide 只启动动画，末帧 hide_finish 真隐藏

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
#define MP_RECENT_MAX 16   // 最近行槽位上限（v0.16.2：槽位=网格列数随宽度变，超宽屏封顶；数组尺寸红线）
static RECT g_recent[MP_RECENT_MAX];
static int  g_recentIdx[MP_RECENT_MAX], g_nRecent;
static RECT g_chipRc[MP_MAX_TAGS + 1];
static int  g_chipId[MP_MAX_TAGS + 1], g_nChips;
static int  g_chipScr = 0;           // v0.17.1 标签行横向滚动偏移（px；0 = 未滚）
static int  g_chipY;                 // 标签行 y（panel_layout 唯一来源；绘制/滚轮共读——v0.18.2 收敛三处复制）
static int  g_chipMax = 0;           // 滚满量 = 全队列宽 - 可见宽（0 = 不溢出，滚轮不接管）
static int  g_chipVisL, g_chipVisR;  // 可见带（已扣掉 ‹› 提示位的实际落墨/注册区间）
static RECT g_grid, g_preview;
static int  g_cell, g_gap, g_cols, g_rows;
static int  g_gridR;            // 网格区右界（抽屉开 = drawerLeft；关 = panel.right）

// 数据 / 交互状态
static u64  g_tagFilter;
static int* g_filt; static int g_nFilt, g_capFilt;
static int  g_first, g_hover = -1, g_previewIdx = -1;
static int  g_recentHover = -1;     // v0.16.3：最近行鼠标 hover 槽位（行内滑动也要触发重绘——见 WM_MOUSEMOVE）
static POINT g_mouse; static BOOL g_tracking;

// ---- 输入焦点路由（v3 三缓冲 + v0.10 第四缓冲）：搜索 / 抽屉索引 / 抽屉新标签 /
//      标签管理窗口（改名|新建共用 g_ebT，t_ren 区分语义）----
typedef struct { wchar_t b[128]; int len, caret; } EBuf;
enum { EDIT_SEARCH = 0, EDIT_INDEX, EDIT_NEWTAG, EDIT_TAG, EDIT_NAME };
static int  g_editFocus = EDIT_SEARCH;

// ---- v0.12 纯键盘操作：两级「非输入」键盘焦点（绘制并入 hover 高亮，零新视觉）----
// g_tagFocus：主标签行焦点（g_chipRc 下标；-1 无）——←→/Enter/Esc/↓；
// g_chipFocus：抽屉标签 chips 焦点（g_dChipRc 下标；-1 无）——同款。Tab 在
// 搜索 → 标签行（抽屉关）/ 搜索 → 索引框 → chips → 新标签（抽屉开）间循环。
// g_recentFocus：最近行焦点（格子下标；-1 无）——↑ 从网格首行贯通进入（列对齐），
// ←→ 移动 / Enter 即发 / ↑ 继续到标签行 / ↓ 回网格；Tab 不进（空间导航专属）。
static int g_tagFocus = -1, g_chipFocus = -1, g_recentFocus = -1;
static EBuf g_ebS, g_ebI, g_ebN, g_ebT, g_ebR;   // 搜索（改即 refilter）/ 索引（改即写 entry）/ 新标签（Enter 提交）/ 标签管理 / 抽屉改名（改即写 origName）
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
static int  g_drawerLeft;          // 2px 黑竖分隔线 x（抽屉内容左界 = 其右 + padX）
static int  g_dContentL, g_dContentR;
static RECT g_dPrevRc;                                   // 96px 预览（批量态 = 计数块）
static RECT g_dNameRc, g_dSizeRc, g_dOpenRc, g_dClearRc; // 元数据列 / 批量 ✕ 清除
static RECT g_dIndexBox, g_dNewTagBox, g_dNewChipRc, g_dDelRc, g_dFavRc;
static RECT g_dChipRc[MP_MAX_TAGS + 1];                  // 抽屉标签 chips（含 + 新标签）
static int  g_dChipId[MP_MAX_TAGS + 1]; static int g_nDChips;
static int  g_dSecY, g_dIdxY;                            // 「标签」/「索引文字」节标题 y（drawer_layout 输出）

// ---- 选择集（Ctrl+点击 toggle；抽屉批量目标 = 选择集）----
static u8*  g_selBits;             // entry 索引选中位图
static int  g_nSel;

// ---- prefs.cfg：搜索「包含文件名」（bit0 默认开）、界面语言（bit1）、
//      最近行格数 recentN（v2 'MPP2' 追加 u32；1..7，默认 7）；
//      v3 'MPP3' 再追加 u32 posX/posY（拖动记忆位，-1 = 无）+ u32 opacity（255 = 全不透明）----
#define PF_NAMESEARCH 1
#define PF_ENGLISH    2
#define PF_FOLLOWCURSOR 4                 // 呼出定位模式：1 = 始终光标锚定（忽略拖动记忆位）
#define PREFS_MAGIC   0x3150504Du   // 'MPP1'：magic + u32 flags（8B）
#define PREFS_MAGIC2  0x3250504Du   // 'MPP2'：magic + u32 flags + u32 recentN（12B）
#define PREFS_MAGIC3  0x3350504Du   // 'MPP3'：… + u32 posX + u32 posY + u32 opacity（24B）
#define POS_NONE      0xFFFFFFFFu   // posX/posY 的「无记忆」哨兵（-1 序列化态）
static u32 g_prefs = PF_NAMESEARCH;
static BOOL g_recentOn = TRUE;      // v0.16.2：最近行只剩开/关；槽位数固定=网格列数（满了轮换，新的顶掉旧的）
static int  g_recentN = 7;          // 当前槽位数（panel_layout 派生：开=列数上限 16，关=0；勿手写）

// ---- v0.11 体验三件套 ----
// 拖动记忆位：拖拽/-pos 落点（窗口左上，含 bleed）；-1 = 无。panel_show 仅在
// 「记忆中心与光标同屏」时沿用（换屏 = 跟随光标重新智能定位），沿用即夹回工作区。
static int g_posX = -1, g_posY = -1;
static BOOL g_cliPos;   // -pos 指定过：优先于「跟随光标」开关（IME 实验/探针确定性）
// 窗口不透明度（三窗共用）：经 ULW SourceConstantAlpha 生效——预乘位图整体再乘
// op/255，数学上与逐像素乘等价且零遍历成本；无头 -shot 走 PNG 合成不经过 ULW，
// 天然不受影响（探针确定性保住）。默认 255；夹取 [OPACITY_MIN, 255]。
#define OPACITY_MIN  76             // ≈30%：v0.16.1 下探（原 110≈43%——用户反馈范围太窄）
#define OPACITY_STEP 13             // Ctrl+滚轮每档 ≈5%
// 不透明度档位（v0.11 托盘 6 档 → v0.16 设置窗 → v0.16.1 扩到 100..30% 八档）
static const int OP_BUCKETS[8] = { 255, 230, 204, 179, 153, 127, 102, 76 };
static u32  g_opacity = 255;
static BOOL g_opacityHud;           // 调节时左下角瞬时读数（900ms 自灭）

// ============================================================
// 本地化（v0.9）：zh 默认 / en 备选；托盘「语言」项切换，prefs.cfg bit1 持久化。
// 两表严格同序（编译期枚举计数兜底）；带 %d 的条目是 wnsprintfW 格式串。
// LS_TRAY_LANG 存的是「切换目标」的语言名：中文态显示 English，英文态显示 中文。
// ============================================================
enum {
    LS_ALL, LS_SEARCH, LS_EMPTY,
    LS_TAGS, LS_NEWTAG, LS_NEWTAG_PLUS,
    LS_INDEX_SEC, LS_INDEX_WM, LS_INDEX_HINT, LS_INDEX_RO,
    LS_OPENLOC, LS_DELETE, LS_CONFIRM, LS_SEL_FMT, LS_DUP_NOTE, LS_N_FMT,
    LS_TRAY_SHOW, LS_TRAY_THEME, LS_THEME0, LS_THEME1, LS_THEME2, LS_THEME3,
    LS_THEME4, LS_THEME_CUSTOM, LS_TRAY_LANG, LS_TRAY_MAINT, LS_TRAY_DEDUP,
    LS_TRAY_NAMESEARCH, LS_TRAY_RESCAN, LS_TRAY_EXIT, LS_TIP,
    LS_BAL_IMPORT, LS_BAL_TOOSMALL, LS_BAL_NODUP, LS_BAL_SCAN, LS_BAL_HOTKEY,
    LS_ALREADY, LS_GDIPFAIL,
    LS_TRAY_TAGS, LS_TRAY_IMPORT, LS_TRAY_AUTOSTART, LS_TRAY_RECENT,
    LS_MGR_TAGS, LS_MGR_NEW, LS_MGR_RENAME, LS_MGR_REMOVE, LS_MGR_EMPTY,
    LS_MGR_HINT, LS_MGR_REN_WM,
    LS_IMP_TITLE, LS_IMP_SUM, LS_IMP_FILES, LS_IMP_FOLDER, LS_IMP_OPEN,
    LS_IMP_DROP, LS_IMP_READY, LS_IMP_PASTE, LS_IMP_NOPASTE,
    LS_FAV, LS_FAVED,
    LS_TRAY_OPACITY, LS_OPACITY, LS_TRAY_FOLLOWCUR, LS_NAME_HINT,
    LS_SET_TITLE, LS_SEC_LOOK, LS_SEC_BEHAV, LS_SEC_LIB, LS_SET_DEDUP_OK,
    LS_COUNT
};
static const wchar_t* const L_ZH[LS_COUNT] = {
    L"全部", L"搜索", L"库空——点击此处导入",
    L"标签", L"新标签", L"+ 新标签",
    L"索引文字", L"索引…", L"空格分隔 · 用于搜索", L"多选时不可编辑索引",
    L"打开位置", L"删除", L"确认删除", L"已选 %d 张%s", L" · 疑似重复", L"%d 张",
    L"打开面板", L"主题色", L"正红（默认）", L"钴蓝", L"常青", L"琥珀", L"墨黑", L"自定义…",
    L"English", L"维护", L"去重扫描…", L"搜索：包含文件名", L"重新扫描", L"退出",
    L"MemePanel — Ctrl+Shift+. 呼出",
    L"新增 %d · 重复 %d · 非图片 %d · 库共 %d", L"库太小，无需去重", L"未发现重复",
    L"扫描完成，库共 %d 张", L"快捷键注册失败（可能被占用），可点托盘图标打开。",
    L"MemePanel 已在运行（见系统托盘）。", L"GDI+ 初始化失败（gdiplus.dll）。",
    L"标签管理…", L"导入…", L"开机自启", L"最近行",
    L"标签管理", L"新建", L"重命名", L"删除",
    L"暂无标签", L"在面板右键图片打标签，或把文件夹拖进「导入」",
    L"重命名为…",
    L"导入", L"库 %d 张 · %d 个标签 · %u MB", L"添加文件…", L"添加文件夹…",
    L"打开库文件夹", L"把图片或文件夹拖到本窗口", L"就绪", L"粘贴导入", L"剪贴板里没有图片",
    L"★ 收藏", L"★ 已收藏",
    L"不透明度", L"不透明度 %d%%", L"呼出跟随光标", L"名称…",
    L"设置", L"外观", L"行为", L"库", L"已选 %d 张疑似重复——在面板抽屉里删除",
};
static const wchar_t* const L_EN[LS_COUNT] = {
    L"ALL", L"SEARCH", L"EMPTY — CLICK TO IMPORT",
    L"TAGS", L"New tag", L"+ NEW TAG",
    L"INDEX TEXT", L"Index…", L"Space-separated · searchable",
    L"Index not editable for multi-selection",
    L"Show in Explorer", L"Delete", L"Confirm delete",
    L"%d selected%s", L" · possible duplicates", L"%d items",
    L"Open panel", L"Accent color", L"Red (default)", L"Cobalt", L"Evergreen",
    L"Amber", L"Ink black", L"Custom…",
    L"中文", L"Maintenance", L"Dedup scan…", L"Search: include file names",
    L"Rescan", L"Exit",
    L"MemePanel — Ctrl+Shift+. to summon",
    L"new %d · dup %d · non-image %d · library %d", L"Library too small for dedup",
    L"No duplicates found", L"Scan done — %d in library",
    L"Hotkey registration failed (possibly taken). Click the tray icon to open.",
    L"MemePanel is already running (see system tray).",
    L"GDI+ init failed (gdiplus.dll).",
    L"Tag manager…", L"Import…", L"Start with Windows", L"Recent row",
    L"TAG MANAGER", L"New", L"Rename", L"Remove",
    L"No tags yet", L"Right-click a meme on the panel, or drop a folder into Import",
    L"Rename to…",
    L"IMPORT", L"Library %d memes · %d tags · %u MB", L"Add files…", L"Add folder…",
    L"Open library folder", L"Drop images or folders onto this window", L"Ready",
    L"Paste import", L"No image on the clipboard",
    L"★ Favorite", L"★ Favorited",
    L"Opacity", L"Opacity %d%%", L"Summon at cursor", L"name…",
    L"SETTINGS", L"Appearance", L"Behavior", L"Library",
    L"%d marked as possible duplicates — delete from the panel drawer",
};
static int g_lang = 0;   // 0 = 中文（默认），1 = English
static const wchar_t* LS(int id) { return (g_lang ? L_EN : L_ZH)[id]; }

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
    // 底 = 96% 玻璃（预乘：250*245/255=240，249*245/255=239）。
    // v0.11.1：窗口不透明度满档时底补到全实心——「100% 还能透 4%」的根因就是
    // 这个玻璃常数而非 ULW alpha；255 档预乘 = 原色。跨档时 opacity_set 重烤本缓存
    u32 paper = (g_opacity >= 255)
        ? (0xFF000000u | (250u << 16) | (250u << 8) | 249u)
        : ((245u << 24) | (240u << 16) | (240u << 8) | 239u);
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
    // v0.11：SourceConstantAlpha = 用户不透明度（预乘位图整体再乘 op/255，与逐像素
    // 乘等价、零遍历成本；淡入淡出的逐像素 alpha 与之复合 = fade × opacity）
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)g_opacity, AC_SRC_ALPHA };
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

static OneShot g_recentImg[MP_RECENT_MAX];
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
static int  wendswith_ci(const wchar_t* s, const wchar_t* suf);   // paste_entry 三格式用（定义在后）
static void panel_repaint(void);
static void maint_fixext(void);              // 定义在后（import_run / 扫描路径调用）
static void drawer_show_batch(HWND hwnd);    // 定义在后（maint_dedup 调用）
static void tags_repaint(void);              // v0.10 标签管理窗口（定义在后）
static void tags_layout(void);               // 同上（editor 路由后需同步几何）
static void tags_reset_input(void);          // 收起内联输入/清缓冲/焦点归搜索
static void import_run(HWND hwnd, const wchar_t* const* paths, int n);  // 导入核心（定义在后）
static void imp_open(HWND hwnd);             // 打开导入窗口
static void tags_open(HWND hwnd);            // 打开标签管理窗口
static void tags_size(HWND hwnd);            // v0.17.1 复开重建表面（open 先于 size 定义）
static void imp_size(HWND hwnd);
static void set_size(HWND hwnd);

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
        if ((g_prefs & PF_NAMESEARCH) && wcontains_ci(entry_disp_name(e), q)) return TRUE;
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
    // v0.10 收藏稳定前置：★ 项排在最前，组内保持库序（临时缓冲两遍搬运）
    if (g_nFilt > 1) {
        int* tmp = (int*)HeapAlloc(GetProcessHeap(), 0, (u64)g_nFilt * 4);
        if (tmp) {
            int m = 0;
            for (int k = 0; k < g_nFilt; k++)
                if (g_store.entries[g_filt[k]].flags & MP_FLAG_FAV) tmp[m++] = g_filt[k];
            for (int k = 0; k < g_nFilt; k++)
                if (!(g_store.entries[g_filt[k]].flags & MP_FLAG_FAV)) tmp[m++] = g_filt[k];
            memcpy(g_filt, tmp, (u64)g_nFilt * 4);
            HeapFree(GetProcessHeap(), 0, tmp);
        }
    }
    tc_clear();
}

static void recalc_recents(void)
{
    g_nRecent = 0;
    for (int i = 0; i < g_store.count && g_nRecent < g_recentN; i++) {
        if (!g_store.entries[i].used) continue;
        int k = g_nRecent++;
        g_recentIdx[k] = i;
        while (k > 0 && g_store.entries[g_recentIdx[k - 1]].used < g_store.entries[g_recentIdx[k]].used) {
            int t = g_recentIdx[k - 1]; g_recentIdx[k - 1] = g_recentIdx[k]; g_recentIdx[k] = t;
            k--;
        }
    }
    // v0.16.2：空槽消毒——未填到的槽位统一 -1，杜绝陈旧索引被点击/悬停命中（空白格闪退的另一半根因）
    for (int k = g_nRecent; k < MP_RECENT_MAX; k++) g_recentIdx[k] = -1;
    if (g_nRecent == 0) g_recentFocus = g_recentHover = -1;   // v0.18.2：最近集清空 = 行焦点/悬停一并失效（防旧下标被键盘命中）
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
    g_chipY = chipY;                    // v0.18.2：单一来源（panel_repaint / WM_MOUSEWHEEL 共读）
    g_hairY = chipY + S(26) + S(8);
    int wallT = g_hairY + 1 + S(10);
    int avail = g_grid.right - g_grid.left;
    int minCell = S(52);
    g_cols = (avail + g_gap) / (minCell + g_gap); if (g_cols < 1) g_cols = 1;
    g_cell = (avail - (g_cols - 1) * g_gap) / g_cols;
    if (g_cell < S(44)) g_cell = S(44);
    // v0.16.2：槽位 = 开 ? 网格列数(封顶 16) : 0——「固定第一行」；新使用顶掉最旧（recalc 按时间排序天然轮换）
    g_recentN = g_recentOn ? (g_cols > MP_RECENT_MAX ? MP_RECENT_MAX : g_cols) : 0;
    for (int i = 0; i < g_recentN; i++) {
        g_recent[i].left = g_grid.left + i * (g_cell + g_gap);
        g_recent[i].right = g_recent[i].left + g_cell;
        g_recent[i].top = wallT;
        g_recent[i].bottom = wallT + g_cell;
    }
    // v0.16.1：recentN=0 = 关最近行——整带收起（不留空档），图片墙从发丝线起直通到底
    g_grid.top = wallT + (g_recentN ? g_cell + S(8) : 0);   // v0.18.1：最近行→网格间距 3→8（墨线分节要呼吸感）
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
    // v0.16.2：槽位随列数派生后必须就地重算——否则拖宽面板/抽屉开合改变列数时
    // g_nRecent 停在旧槽位数，新槽位空置（recalc 便宜，O(count)）
    recalc_recents();
}

// ============================================================
// 显示 / 隐藏（v0.9：隐藏 = 淡出对称于淡入——panel_hide 只启动递减动画，
// 末帧 alpha=0 后 hide_finish 真隐藏；粘贴/失焦路径即时返回不阻塞）
// ============================================================
static void panel_repaint(void);
static void hide_finish(HWND hwnd);   // 定义在 panel_hide 后

// 光标所在显示器的工作区（v0.10：热键面板/管理窗口都在鼠标屏幕弹出）
static void work_area_at_cursor(RECT* rc)
{
    POINT p;
    GetCursorPos(&p);
    HMONITOR mon = MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST);
    if (mon) {
        MONITORINFO mi;
        mi.cbSize = sizeof mi;
        if (GetMonitorInfoW(mon, &mi)) { *rc = mi.rcWork; return; }
    }
    rc->left = 0; rc->top = 0;
    rc->right = GetSystemMetrics(SM_CXSCREEN);
    rc->bottom = GetSystemMetrics(SM_CYSCREEN);
}

// v0.11 主面板拖动死区：发丝线以上整带（搜索框 / ✕ / 标签 chips 除外）——
// 顶衬带、搜索框↔✕ 竖缝、横梁带、chips 间缝，全是无命中像素；
// 网格/最近行/抽屉列（x ≥ g_gridR）一律不可拖，避免与点击发送/抽屉控件冲突
static BOOL panel_drag_zone(POINT pt)
{
    if (pt.y < g_panel.top || pt.y >= g_hairY) return FALSE;
    if (pt.x < g_panel.left || pt.x >= g_gridR) return FALSE;
    if (PtInRect(&g_searchBox, pt) || PtInRect(&g_closeRc, pt)) return FALSE;
    for (int i = 0; i < g_nChips; i++)
        if (PtInRect(&g_chipRc[i], pt)) return FALSE;
    return TRUE;
}

static void panel_show(HWND hwnd)
{
    if (g_fadeOut) hide_finish(hwnd);   // 淡出未完就唤出：先同步收尾，全新会话
    HWND fg = GetForegroundWindow();
    if (fg != hwnd) g_lastTarget = fg;
    RECT wa;
    work_area_at_cursor(&wa);   // 光标所在屏工作区（多屏）；回退主屏
    int sw = wa.right - wa.left, sh = wa.bottom - wa.top;
    int pw = sw * 46 / 100, ph = sh * 54 / 100;
    if (pw < S(700)) pw = S(700);
    if (ph < S(460)) ph = S(460);
    int w = pw + g_bleed * 2, h = ph + g_bleed * 2;
    g_fade = 0;
    g_caretOn = TRUE;
    // 关键顺序：隐藏状态下先定尺寸/重建表面、合成 alpha=0 的首帧并 ULW 提交，
    // 之后才显示窗口——否则 SWP_SHOWWINDOW 会先亮出上一会话残留的不透明旧帧（闪烁）
    // v0.11 智能定位（热键/托盘呼出共用）：
    //   ① 拖动记忆位（拖拽落点，或 -pos X,Y 实验/探针覆盖）：记忆窗口中心与光标
    //      同屏才沿用——换屏即放弃跟随光标；沿用时夹回工作区（分辨率/DPI 可能已变）。
    //      托盘「呼出跟随光标」开关（prefs bit2）可整体关掉本通道；-pos 优先于开关。
    //   ② 无记忆/跟随模式：光标锚定——水平以光标居中、光标落在面板上 40% 线（视线上偏），
    //      整面夹回工作区（面板永远完整可见且追着注意力走）
    int px = -1, py = -1;
    if (g_posX >= 0 && g_posY >= 0 &&
        (g_cliPos || !(g_prefs & PF_FOLLOWCURSOR))) {
        POINT cur, c;
        GetCursorPos(&cur);
        c.x = g_posX + w / 2; c.y = g_posY + h / 2;
        if (MonitorFromPoint(cur, MONITOR_DEFAULTTONEAREST) ==
            MonitorFromPoint(c, MONITOR_DEFAULTTONULL)) {
            px = g_posX; py = g_posY;
        }
    }
    if (px < 0) {
        POINT cur;
        GetCursorPos(&cur);
        px = cur.x - w / 2;
        py = cur.y - h * 2 / 5;
    }
    if (px < wa.left) px = wa.left;
    if (px > wa.right - w) px = wa.right - w;
    if (py < wa.top) py = wa.top;
    if (py > wa.bottom - h) py = wa.bottom - h;
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
    g_ebR.b[0] = 0; g_ebR.len = g_ebR.caret = 0;
    g_delArm = FALSE;
    if (hwnd) KillTimer(hwnd, ARM_TMR);
    g_editFocus = EDIT_SEARCH;
    g_tagFocus = g_chipFocus = g_recentFocus = -1;   // v0.12：会话结束，键盘焦点一并复位
    drawer_layout();   // v3.5：会话结束，抽屉内容 rect 全量清零（防 IME 读到跨会话残留）
}

static void panel_hide(HWND hwnd)
{
    if (g_fadeOut) return;   // 已在淡出中（失焦/热键等重复触发）：让动画走完，不闪断
    // 淡出：快照当前全亮度帧（淡入中快照已在则直接复用），alpha 递减到 0 再真隐藏。
    // 无头/异常路径（无表面）= 走原同步隐藏。
    if (!g_headless && IsWindowVisible(hwnd) && g_bits && g_fade > 0) {
        KillTimer(hwnd, CARET_TIMER);
        KillTimer(hwnd, PREVIEW_TMR);
        if (!g_fadeSnap) {
            g_fadeSnap = (u32*)HeapAlloc(GetProcessHeap(), 0, (u64)g_winW * g_winH * 4);
            if (g_fadeSnap) memcpy(g_fadeSnap, g_bits, (u64)g_winW * g_winH * 4);
        }
        if (g_fadeSnap) {
            g_fadeOut = TRUE;
            SetTimer(hwnd, ANIM_TIMER, 10, NULL);   // 复用淡入定时器（WM_TIMER 按 g_fadeOut 分流）
            return;
        }
    }
    hide_finish(hwnd);
}

// 真隐藏（原 panel_hide 全部收尾：状态复位 + 释放表面）
static void hide_finish(HWND hwnd)
{
    KillTimer(hwnd, ANIM_TIMER);
    KillTimer(hwnd, CARET_TIMER);
    KillTimer(hwnd, PREVIEW_TMR);
    KillTimer(hwnd, GIF_TIMER);
    KillTimer(hwnd, OPACITY_TMR);
    g_fade = 255;
    g_fadeOut = FALSE;
    g_hover = -1;
    g_previewIdx = -1;
    g_opacityHud = FALSE;
    drawer_reset_state(hwnd);   // 抽屉随之关（面板隐藏 = 会话结束）
    anim_free();
    ShowWindow(hwnd, SW_HIDE);
    destroy_surface();   // 释放 ~9MB 合成表面（下次 show 重建，gen_base 代价 ~5ms）
}

// ============================================================
// 粘贴链路（v0.10 三格式同贴；调研 OhMyMeme 源码级结论）：
//   ① 自定义 "GIF"/"PNG"（RegisterClipboardFormat，payload = 完整文件字节）
//   ② CF_HDROP（文件路径——微信/QQ 动图粘贴的关键，库内文件常驻所以路径始终有效）
//   ③ CF_DIB（24bpp bottom-up 首帧，静态兜底；无解码器时省略）
// EmptyClipboard 后一次写全；写入顺序无讲究，讲究的是 HDROP 指向的文件不能消失。
// ============================================================

// 整文件读进 GMEM_MOVEABLE HGLOBAL（SetClipboardData 专用）
static HGLOBAL hglobal_from_file(const wchar_t* path)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;
    LARGE_INTEGER li;
    if (!GetFileSizeEx(f, &li) || li.QuadPart <= 0 || li.QuadPart > 0x2000000) {   // 32MB 上限
        CloseHandle(f);
        return NULL;
    }
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)li.QuadPart);
    if (!hg) { CloseHandle(f); return NULL; }
    void* p = GlobalLock(hg);
    BOOL ok = p != NULL;
    DWORD total = 0, got = 0;
    while (ok && total < (DWORD)li.QuadPart) {
        ok = ReadFile(f, (u8*)p + total, (DWORD)li.QuadPart - total, &got, NULL) && got;
        total += got;
    }
    if (p) GlobalUnlock(hg);
    CloseHandle(f);
    if (!ok || total != (DWORD)li.QuadPart) { GlobalFree(hg); return NULL; }
    return hg;
}

// straight BGRA（顶朝下）→ CF_DIB payload：BITMAPINFOHEADER + 24bpp BGR bottom-up（4 字节行对齐）
static HGLOBAL dib24_from_bgra(const u8* bgra, int w, int h)
{
    int stride = (w * 3 + 3) & ~3;
    SIZE_T cb = 40 + (SIZE_T)stride * h;
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, cb);
    if (!hg) return NULL;
    u8* p = (u8*)GlobalLock(hg);
    if (!p) { GlobalFree(hg); return NULL; }
    BITMAPINFOHEADER* bh = (BITMAPINFOHEADER*)p;
    memset(bh, 0, 40);
    bh->biSize = 40;
    bh->biWidth = w;
    bh->biHeight = h;        // 正值 = bottom-up（OhMyMeme/PIL 同款约定）
    bh->biPlanes = 1;
    bh->biBitCount = 24;
    bh->biCompression = BI_RGB;
    for (int y = 0; y < h; y++) {
        const u8* srow = bgra + (u64)y * w * 4;
        u8* drow = p + 40 + (u64)(h - 1 - y) * stride;
        for (int x = 0; x < w; x++) {
            drow[x * 3]     = srow[x * 4];
            drow[x * 3 + 1] = srow[x * 4 + 1];
            drow[x * 3 + 2] = srow[x * 4 + 2];
        }
    }
    GlobalUnlock(hg);
    return hg;
}

static void paste_entry(HWND hwnd, int ei)
{
    Entry* e = &g_store.entries[ei];
    e->used = GetTickCount64();
    store_save(&g_store);
    recalc_recents();

    wchar_t path[MAX_PATH];
    store_build_path(&g_store, e, path, MAX_PATH);
    panel_hide(hwnd);

    // ② CF_HDROP payload（单文件路径 + 双 NUL 收尾）
    HGLOBAL hDrop = NULL;
    {
        u32 bytes = (u32)(sizeof(MPDROPFILES) + ((u64)wlen(path) + 2) * 2);
        hDrop = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (hDrop) {
            MPDROPFILES* df = (MPDROPFILES*)GlobalLock(hDrop);
            if (df) {
                df->pFiles = sizeof(MPDROPFILES);
                df->fNC = FALSE; df->fWide = TRUE;
                memcpy(df + 1, path, ((u64)wlen(path) + 1) * 2);
                ((wchar_t*)(df + 1))[wlen(path) + 1] = 0;
                GlobalUnlock(hDrop);
            } else {
                GlobalFree(hDrop); hDrop = NULL;
            }
        }
    }
    // ① 自定义格式：GIF/PNG 附完整文件字节（认格式的程序直接拿动图/透明数据）
    UINT fmtCustom = 0;
    HGLOBAL hCustom = NULL;
    BOOL isGif = is_gif_name(e->name);
    if (isGif || wendswith_ci(e->name, L".png")) {
        fmtCustom = RegisterClipboardFormatW(isGif ? L"GIF" : L"PNG");
        if (fmtCustom) hCustom = hglobal_from_file(path);
    }
    // ③ CF_DIB：首帧 24bpp 兜底（静态图全走这条）
    HGLOBAL hDib = NULL;
    {
        u8* bgra = NULL;
        int iw = 0, ih = 0;
        if (wic_load_bgra(path, &iw, &ih, &bgra)) {
            hDib = dib24_from_bgra(bgra, iw, ih);
            HeapFree(GetProcessHeap(), 0, bgra);
        }
    }

    BOOL ok = FALSE;
    for (int t = 0; t < 4 && !ok; t++) {
        if (!OpenClipboard(hwnd)) { Sleep(15); continue; }
        if (EmptyClipboard()) {
            if (hCustom && fmtCustom && SetClipboardData(fmtCustom, hCustom)) { hCustom = NULL; ok = TRUE; }
            if (hDrop && SetClipboardData(CF_HDROP, hDrop))                  { hDrop = NULL;  ok = TRUE; }
            if (hDib && SetClipboardData(CF_DIB, hDib))                      { hDib = NULL;   ok = TRUE; }
        }
        CloseClipboard();
        if (!ok) Sleep(15);
    }
    if (hCustom) GlobalFree(hCustom);
    if (hDrop) GlobalFree(hDrop);
    if (hDib) GlobalFree(hDib);

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
// 导入（v0.10：导入只在导入窗口发生；文件夹自动建同名标签并批量打标。
//        v0.14：内容寻址——目标名 = FNV-1a 64 + 真实扩展名，同内容全局一份）
// ============================================================
typedef struct { wchar_t dst[MAX_PATH]; wchar_t orig[MAX_PATH]; int tag; int dup; } Imported;

// 12 字节魔数 → 真实图片扩展名；不认 NULL（导入过滤与 fixext 共用同一套 magic）。
// 不含 BMP：其魔数仅 "BM" 2 字节，普通文本也可能命中（BMW….txt），只走扩展名白名单。
static const wchar_t* sniff_img_ext(const u8* m, DWORD got)
{
    if (got >= 4 && m[0]=='G' && m[1]=='I' && m[2]=='F' && m[3]=='8') return L".gif";
    if (got >= 4 && m[0]==0x89 && m[1]=='P' && m[2]=='N' && m[3]=='G') return L".png";
    if (got >= 3 && m[0]==0xFF && m[1]==0xD8 && m[2]==0xFF) return L".jpg";
    if (got >= 12 && m[0]=='R' && m[1]=='I' && m[2]=='F' && m[3]=='F' &&
        m[8]=='W' && m[9]=='E' && m[10]=='B' && m[11]=='P') return L".webp";
    return NULL;
}

// 哈希名：16 位小写 hex + 扩展名（wnsprintf 的 64 位格式符各家不一，手写最稳）
static void hash_name(wchar_t* out, int cap, u64 h, const wchar_t* ext)
{
    static const wchar_t HEX[] = L"0123456789abcdef";
    int p = 0;
    for (int k = 60; k >= 0 && p < cap - 1; k -= 4) out[p++] = HEX[(u32)((h >> k) & 15)];
    lstrcpynW(out + p, ext, cap - p);
}

// 入库唯一门：嗅探真实格式 → 算内容哈希 → 哈希命名复制。
// 返回 0=新入库 1=重复（库内已有同内容）2=非图片 3=失败；dstNameOut = 库内目标名（0/1 时有效）
static int copy_into(Store* s, const wchar_t* src, wchar_t* dstNameOut, int dstCap)
{
    u8 m[12]; DWORD got = 0;
    HANDLE f = CreateFileW(src, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 3;
    ReadFile(f, m, 12, &got, NULL);
    CloseHandle(f);
    const wchar_t* ext = sniff_img_ext(m, got);
    if (!ext) {
        // 魔数不认：白名单扩展名兜底（QQNT 错后缀已被上面的魔数纠正；.jpeg 归一为 .jpg）
        if      (wendswith_ci(src, L".png"))  ext = L".png";
        else if (wendswith_ci(src, L".jpg") || wendswith_ci(src, L".jpeg")) ext = L".jpg";
        else if (wendswith_ci(src, L".gif"))  ext = L".gif";
        else if (wendswith_ci(src, L".webp")) ext = L".webp";
        else if (wendswith_ci(src, L".bmp"))  ext = L".bmp";
        else return 2;   // 白名单+魔数都不认：非图片，源头拒收
    }

    u64 h = fnv_file(src);
    if (!h) return 3;
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExW(src, GetFileExInfoStandard, &fa)) return 3;

    // 判重①（索引）：同 hash 且同 size——手放进库的原名文件也能命中（size 双保险挡哈希碰撞）
    u64 sz = ((u64)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
    for (int i = 0; i < s->count; i++)
        if (s->entries[i].hash == h && s->entries[i].size == sz) {
            lstrcpynW(dstNameOut, s->entries[i].name, dstCap);
            return 1;
        }
    hash_name(dstNameOut, dstCap, h, ext);
    // 判重②（磁盘）：同哈希名已存在——索引丢失后的孤儿 / 同批同内容；磁盘即真相，天然幂等
    wchar_t dst[MAX_PATH];
    wnsprintfW(dst, MAX_PATH, L"%s\\%s", s->memesDir, dstNameOut);
    if (GetFileAttributesW(dst) != INVALID_FILE_ATTRIBUTES) return 1;
    return CopyFileW(src, dst, FALSE) ? 0 : 3;
}

// 拖放入口（导入窗口）：枚举 HDROP → import_run 统一处理
static void import_drop(HWND hwnd, HDROP hd)
{
    UINT n = DragQueryFileW(hd, 0xFFFFFFFF, NULL, 0);
    wchar_t** paths = (wchar_t**)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                           (u64)(n ? n : 1) * sizeof(wchar_t*));
    int nReal = 0;
    if (paths) {
        for (UINT i = 0; i < n; i++) {
            paths[nReal] = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, MAX_PATH * 2);
            if (paths[nReal] && DragQueryFileW(hd, i, paths[nReal], MAX_PATH)) nReal++;
            else if (paths[nReal]) { HeapFree(GetProcessHeap(), 0, paths[nReal]); paths[nReal] = NULL; }
        }
        import_run(hwnd, (const wchar_t* const*)paths, nReal);
        for (int i = 0; i < nReal; i++) HeapFree(GetProcessHeap(), 0, paths[i]);
        HeapFree(GetProcessHeap(), 0, paths);
    }
    DragFinish(hd);
}

// ============================================================
// v0.10 管理窗口（标签管理 / 导入）：两个独立顶层窗（WS_EX_LAYERED + 自绘，
// 与面板同一套 SWISS 视觉）。绘制复用面板助手：mgr_swap 把本窗 DIB/GDI+
// 上下文临时装进全局 g_bits/g_gfx/g_gpbmp（fill_rect/gtext/draw_input_box
// 即刻绑到本窗），画完换回——单线程 UI 下安全，助手零改动。
// 主界面不再承担导入（面板 DragAcceptFiles 已移除）：导入只在导入窗口发生。
// ============================================================

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
// 去重扫描（托盘「维护 ▸ 去重扫描…」）：按 size 分桶 -> 同 size 组内哈希 ->
// 每组保留一个，其余预选中；结果 = 选中重复项 + 打开抽屉批量态（顶部「疑似重复」）
// v0.14：fnv_file 已移入 store.c（导入/扫描/迁移共用）
// 去重扫描（设置窗「库 ▸ 去重扫描…」；v0.15 零 IO；v0.16.1 返回预选数供设置窗状态行反馈）：
// 按 size 分桶 -> 同 size 且同 hash -> 保留一项、其余预选中 + 打开抽屉批量态（顶部「疑似重复」）
static int maint_dedup(HWND hwnd)
{
    // v0.15：内容哈希已常驻索引（导入内容寻址），判重纯内存比较——零文件 IO，
    // 万张库从「全库读盘算哈希」降到毫秒级位比较。size 分桶 + 同 size 才比 hash（双保险防碰撞）。
    int n = g_store.count;
    sel_clear();
    if (n < 2) {
        lstrcpynW(g_nid.szInfoTitle, APP_NAME, 64);
        lstrcpynW(g_nid.szInfo, LS(LS_BAL_TOOSMALL), 256);
        g_nid.uFlags = NIF_INFO;
        Shell_NotifyIconW(NIM_MODIFY, &g_nid);
        g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        return 0;
    }
    HANDLE hp = GetProcessHeap();
    int* head = (int*)HeapAlloc(hp, 0, 4096 * 4);
    if (head) memset(head, 0xFF, 4096 * 4);   // 链尾哨兵必须是 -1；零初始化会指向索引 0 形成死环（曾致卡死）
    int* next = (int*)HeapAlloc(hp, HEAP_ZERO_MEMORY, (u64)n * 4);
    u8* done  = (u8*)HeapAlloc(hp, HEAP_ZERO_MEMORY, n);
    int marked = 0;
    if (head && next && done) {
        for (int i = 0; i < n; i++) {
            int b = (int)(g_store.entries[i].size & 4095);
            next[i] = head[b]; head[b] = i;
        }
        for (int i = 0; i < n; i++) {
            if (done[i] || !g_store.entries[i].hash) continue;   // 保序留首项；hash 未知（0）无法判定
            done[i] = TRUE;
            for (int j = head[g_store.entries[i].size & 4095]; j >= 0; j = next[j]) {
                if (j <= i || done[j]) continue;
                if (g_store.entries[j].size == g_store.entries[i].size &&
                    g_store.entries[j].hash == g_store.entries[i].hash) {
                    sel_set(j, TRUE); marked++; done[j] = TRUE;
                }
            }
        }
    }
    if (head) HeapFree(hp, 0, head);
    if (next) HeapFree(hp, 0, next);
    if (done) HeapFree(hp, 0, done);

    if (marked && IsWindowVisible(hwnd)) panel_repaint();
    if (marked) {
        if (!IsWindowVisible(hwnd)) panel_show(hwnd);
        g_drawerNote = TRUE;
        drawer_show_batch(hwnd);   // 顶部「已选 N 张 · 疑似重复」，删除按钮在抽屉底部
    } else {
        lstrcpynW(g_nid.szInfoTitle, APP_NAME, 64);
        lstrcpynW(g_nid.szInfo, LS(LS_BAL_NODUP), 256);
        g_nid.uFlags = NIF_INFO;
        Shell_NotifyIconW(NIM_MODIFY, &g_nid);
        g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    }
    return marked;
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
        g_ebR.b[0] = 0; g_ebR.len = g_ebR.caret = 0;
        g_delArm = FALSE;
        KillTimer(hwnd, ARM_TMR);
        g_editFocus = EDIT_SEARCH;
        g_chipFocus = -1;   // v0.12：chips 焦点随抽屉消失（rect 已清零）
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
    lstrcpynW(g_ebR.b, entry_disp_name(e), 127);   // v0.15 名称框装载现值（原名缺失 = 哈希名）
    g_ebR.len = (int)wlen(g_ebR.b);
    g_ebR.caret = g_ebR.len;
    if (!g_drawer) drawer_set_open(hwnd, TRUE);
    else panel_repaint();
}

// 批量目标 = 当前选择集
static void drawer_show_batch(HWND hwnd)
{
    g_drawerEi = -1;
    g_delArm = FALSE;
    g_ebI.b[0] = 0; g_ebI.len = g_ebI.caret = 0;
    g_ebR.b[0] = 0; g_ebR.len = g_ebR.caret = 0;
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

// 名称（origName）即时写回——与 index_commit 同款三分支；空 = 清除改名（显示回退磁盘哈希名）
static void name_commit(void)
{
    Entry* e = drawer_target();
    if (!e) return;
    if (!g_ebR.len) {
        if (e->origName) { HeapFree(GetProcessHeap(), 0, e->origName); e->origName = NULL; }
    } else if (e->origName) {
        wchar_t* nb = (wchar_t*)HeapReAlloc(GetProcessHeap(), 0, e->origName,
                                            ((u64)g_ebR.len + 1) * 2);
        if (nb) { memcpy(nb, g_ebR.b, (u64)g_ebR.len * 2); nb[g_ebR.len] = 0; e->origName = nb; }
    } else {
        e->origName = wdup(g_ebR.b);
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
        const wchar_t* real = sniff_img_ext(m, got);   // 与导入过滤同一套 magic
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
// v0.14 一次性迁移：库内容寻址化（旧名文件 → 哈希名 + 同内容合并）
// 触发条件：任一条目哈希未知（v3 及更早索引），或已知哈希但文件名还不是哈希名
// （手放进库的文件 / 上次迁移改到一半）。幂等，收敛后存 v4 不再跑。
// ============================================================

// 文件名是否已是该条目的哈希名（16 hex + 可选 .ext）
static BOOL is_hash_name(const Entry* e)
{
    wchar_t nn[24];
    hash_name(nn, 24, e->hash, L"");
    return wlen(e->name) >= 16 &&
           !memcmp_(e->name, nn, 16 * 2) &&
           (e->name[16] == L'.' || e->name[16] == 0);
}

static void hash_migrate(void)
{
    Store* s = &g_store;
    wchar_t p[MAX_PATH], nn[MAX_PATH];
    BOOL need = FALSE;
    for (int i = 0; i < s->count && !need; i++)
        need = (!s->entries[i].hash || !is_hash_name(&s->entries[i]));
    if (!need) return;

    // 迁移前备份一版旧索引（只此一次，出问题可手工还原）
    wchar_t bak[MAX_PATH];
    wnsprintfW(bak, MAX_PATH, L"%s.bak", s->indexPath);
    CopyFileW(s->indexPath, bak, FALSE);

    // 1) 补哈希。文件缺失的条目：已知哈希的先试哈希名（上次迁移改名改到一半的自愈），
    //    两处都没有才是真死条目，清掉。
    for (int i = s->count - 1; i >= 0; i--) {
        Entry* e = &s->entries[i];
        store_build_path(s, e, p, MAX_PATH);
        if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) {
            if (!e->hash) e->hash = fnv_file(p);
            continue;
        }
        if (e->hash) {
            const wchar_t* dot = e->name + wlen(e->name);
            while (dot > e->name && dot[-1] != L'.') dot--;
            hash_name(nn, MAX_PATH, e->hash, dot > e->name ? dot - 1 : L"");
            wnsprintfW(p, MAX_PATH, L"%s\\%s", s->memesDir, nn);
            if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) {
                if (!e->origName) e->origName = wdup(e->name);   // 原名留档
                wchar_t oldt[MAX_PATH], newt[MAX_PATH];          // 缩略图随迁（file_rename_entry 此时文件已在位，走不了）
                wnsprintfW(oldt, MAX_PATH, L"%s\\%s.jpg", s->thumbsDir, e->name);
                wnsprintfW(newt, MAX_PATH, L"%s\\%s.jpg", s->thumbsDir, nn);
                if (!MoveFileW(oldt, newt)) DeleteFileW(oldt);
                wchar_t* d = wdup(nn);
                if (d) { HeapFree(GetProcessHeap(), 0, e->name); e->name = d; }
                continue;
            }
        }
        store_delete_at(s, i);
    }

    // 2) 同哈希合并：保序留首项；tagmask/flags 取或、used 取大、indexText/origName 取首个非空；
    //    多余文件与缩略图删除（这一步就是全库去重）
    for (int i = 0; i < s->count; i++) {
        u64 h = s->entries[i].hash;
        if (!h) continue;
        for (int j = s->count - 1; j > i; j--) {
            if (s->entries[j].hash != h) continue;
            s->entries[i].tagmask |= s->entries[j].tagmask;
            if (s->entries[j].used > s->entries[i].used) s->entries[i].used = s->entries[j].used;
            s->entries[i].flags |= s->entries[j].flags;
            if (!s->entries[i].indexText && s->entries[j].indexText) {
                s->entries[i].indexText = s->entries[j].indexText; s->entries[j].indexText = NULL;
            }
            if (!s->entries[i].origName && s->entries[j].origName) {
                s->entries[i].origName = s->entries[j].origName; s->entries[j].origName = NULL;
            }
            store_build_path(s, &s->entries[j], p, MAX_PATH);
            DeleteFileW(p);
            store_build_thumb(s, &s->entries[j], nn, MAX_PATH);
            DeleteFileW(nn);
            store_delete_at(s, j);
        }
    }

    // 3) 改哈希名（ext 沿用现名；QQNT 错后缀由随后的 fixext 静默纠正——只动扩展名段，身份不变）
    for (int i = 0; i < s->count; i++) {
        Entry* e = &s->entries[i];
        if (!e->hash) continue;   // 哈希没算出来（文件被锁等）：保持现状，下轮启动重试
        const wchar_t* dot = e->name + wlen(e->name);
        while (dot > e->name && dot[-1] != L'.') dot--;
        hash_name(nn, MAX_PATH, e->hash, dot > e->name ? dot - 1 : L"");
        if (weq(nn, e->name)) continue;
        wchar_t* on = wdup(e->name);
        if (file_rename_entry(i, nn) && on) {
            if (e->origName) HeapFree(GetProcessHeap(), 0, e->origName);
            e->origName = on;
        } else if (on) {
            HeapFree(GetProcessHeap(), 0, on);   // 改名失败（罕见）：保持原名，身份在 hash 字段
        }
    }
    store_save(s);
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
    draw_input_box(g_searchBox, &g_ebS, g_editFocus == EDIT_SEARCH, LS(LS_SEARCH));

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

// v0.12.2 行焦点互斥：键盘焦点在标签行/最近行时，网格选中框与大图预览停画
//（g_hover 内存保留——↓ 回网格原位恢复），同一时刻屏上只有一个红框
static BOOL kbd_row_focus(void)
{
    return g_recentFocus >= 0 || g_tagFocus >= 0;
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

// 收藏角标（v0.10）：右上 6×6 accent 小方块——与 hover 全框/选择角标/GIF 右下标不冲突
static void draw_fav_mark(RECT rc)
{
    fill_rect(rc.right - S(8), rc.top + 2, rc.right - 2, rc.top + 2 + S(6), C_RED(255));
}

static void draw_recents(void)
{
    // 标签行与图片墙之间的 1px 发丝线（网格线是主角；抽屉开时止于竖分隔线）
    hair_h(g_hairY, g_panel.left + g_padX, g_gridR - g_padX);
    // v0.18.1：最近行强调改款——洗底带不好看（用户反馈），改为带下一整条 1px 墨线：
    // 上有灰 hairY、下有墨线，最近行自成一段（编辑排版式分节）；仅在有最近记录时画
    if (g_recentN > 0 && g_nRecent > 0)
        fill_rect(g_grid.left, g_recent[0].bottom + S(4), g_grid.right, g_recent[0].bottom + S(5), C_INK(255));
    // 最近快捷行：与网格完全同款密排格子；hover/键盘焦点 = 红框 + 编号 + 名字条
    for (int i = 0; i < g_recentN; i++) {
        int ei = (i < g_nRecent && g_recentIdx[i] < g_store.count) ? g_recentIdx[i] : -1;
        if (ei < 0) continue;
        RECT rc = g_recent[i];
        BOOL hov = PtInRect(&rc, g_mouse) || i == g_recentFocus;   // v0.12 键盘焦点同视觉
        Entry* e = &g_store.entries[ei];
        if (hov && g_anim.ei == ei && g_anim.pb) {
            draw_image_contain(g_anim.pb[g_anim.cur], g_anim.w, g_anim.h, rc);
        } else {
            int iw, ih;
            GpBitmap* pb = oneshot_get(&g_recentImg[i], ei, &iw, &ih);
            if (pb) draw_image_contain(pb, iw, ih, rc);
        }
        if (is_gif_name(e->name)) draw_gif_badge(rc);
        if (e->flags & MP_FLAG_FAV) draw_fav_mark(rc);   // 收藏角标（右上 accent 小方块）
        if (hov) {
            draw_red_frame(rc);
            draw_badge(rc.left, rc.top, i + 1);
            draw_name_strip(rc, e->indexText && e->indexText[0] ? e->indexText : entry_disp_name(e));
        }
    }
}

// 标签显示名：「全部」= 本地化词条；用户标签 = 原名原样（ASCII 转大写保留刊头感，中文不受影响）
static const wchar_t* tag_label(const wchar_t* name, int id)
{
    if (id == -1) return LS(LS_ALL);
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
    g_dIndexBox = g_dNewTagBox = g_dNewChipRc = g_dDelRc = g_dFavRc = (RECT){ 0, 0, 0, 0 };
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
        g_dNameRc = (RECT){ mL, T0, mR, T0 + S(28) };   // v0.15 名称输入框（F2 / 点击进入）
        g_dSizeRc = (RECT){ mL, T0 + S(32), mR, T0 + S(48) };
        int ow = (int)gtext_w(LS(LS_OPENLOC), -1, g_fSmall);
        g_dOpenRc = (RECT){ mL, T0 + S(58), mL + ow + S(4), T0 + S(76) };
        // 收藏切换（v0.10）：打开位置下方一行；宽度取两种词条的较大者（切换不改 rect）
        int fw = (int)gtext_w(LS(LS_FAV), -1, g_fSmall);
        int fw2 = (int)gtext_w(LS(LS_FAVED), -1, g_fSmall);
        if (fw2 > fw) fw = fw2;
        g_dFavRc = (RECT){ mL, T0 + S(80), mL + fw + S(4), T0 + S(98) };
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
        int w = (int)gtext_w(LS(LS_NEWTAG_PLUS), -1, g_fUi) + S(16), h = S(24);
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
    int dw = (int)gtext_w(LS(LS_CONFIRM), -1, g_fUi) + S(24);
    int dh = S(26);
    g_dDelRc = (RECT){ L, g_panel.bottom - S(16) - dh, L + dw, g_panel.bottom - S(16) };
}

// 标签：大写左对齐文字；激活 = 黑色实底白字方块 + 前置红色 6×6 方块；hover = 黑色下划线
// v0.12 kbd：键盘焦点（g_tagFocus）并入 hover 高亮——同一视觉语言，鼠标/键盘无二义
// v0.17.1：标签行横向滚动——量宽（chip_need）与落墨（draw_chip）分离：
// 前者按全队列走一遍（滚满量），后者只对与可见带相交的 chip 落墨/注册
// （完全滚出左缘的不注册：不在边框拖动死区留下不可见命中）。
static int chip_need(const wchar_t* label, int id)
{
    float tw = gtext_w(label, -1, g_fUi);
    int active = (id == -1) ? (g_tagFilter == 0) : (g_tagFilter == ((u64)1 << id));
    return active ? (S(6) + S(6) + (int)tw + S(20)) : ((int)tw + S(4));
}

static void draw_chip(int* px, int y, const wchar_t* txt, int id, BOOL kbd)
{
    const wchar_t* label = tag_label(txt, id);
    int h = S(26);
    int need = chip_need(label, id);
    // v0.17.4：整枚可见才落墨/注册——缘上绝不竖切半个字（左缘滚出、右缘放不下的整枚略过；
    // 行尾留空是原生语义：v0.17.0 之前放不下即整枚不画）。三轮真机“右面不完整”的根因即此。
    if (*px >= g_chipVisL && *px + need <= g_chipVisR) {
        int active = (id == -1) ? (g_tagFilter == 0) : (g_tagFilter == ((u64)1 << id));
        float tw = gtext_w(label, -1, g_fUi);
        RECT hit;
        if (active) {
            int sq = S(6);
            int bl = *px + sq + S(6);
            int bw = (int)tw + S(20);
            fill_rect(*px, y + (h - sq) / 2, *px + sq, y + (h + sq) / 2, C_RED(255));
            fill_rect(bl, y, bl + bw, y + h, C_INK(255));
            gtext(label, -1, g_fUi, (RECT){ bl, y, bl + bw, y + h }, g_sfC, C_PAPER(255));
            // v0.12.5 焦点线只画黑块段（bl..bl+bw）——前置红方块在黑块外，其下方不画，
            // 线绝不越出黑框（v0.12.4 误用整 hit 宽导致左端露到白纸上）
            if (kbd) fill_rect(bl, y + h - 3, bl + bw, y + h, C_RED(255));
            hit.left = *px; hit.right = bl + bw; hit.top = y; hit.bottom = y + h;
        } else {
            hit.left = *px; hit.right = *px + (int)tw + S(4); hit.top = y; hit.bottom = y + h;
            BOOL hov = PtInRect(&hit, g_mouse);
            gtext(label, -1, g_fUi, hit, g_sfL, C_INK(hov ? 255 : 150));
            if (hov) fill_rect(hit.left, y + h - 2, hit.right, y + h, C_INK(255));
            if (kbd) fill_rect(hit.left, y + h - 3, hit.right, y + h, C_RED(255));
        }
        if (g_nChips < MP_MAX_TAGS + 1) {
            g_chipRc[g_nChips] = hit;
            g_chipId[g_nChips] = id;
            g_nChips++;
        }
    }
    *px += need + S(28);   // v0.17.1：画不画都前进——滚满量按全队列宽计（旧版放不下即停笔）
}

// v0.17.4：滚轮吸附 chip 边界——每次滚动落在“某枚右缘恰齐裁剪缘”的位置（含 0 与滚到底），
// 配合整枚可见门，任何停留帧都不出现竖切半字。候选 = 各 chip 右缘对齐 clipR 的滚动量。
// v0.17.6：自包含——滚满量在函数内按同一 walk 现算（不再读上次绘制留下的 g_chipMax，
// 消除首滚/隐藏期滚轮吃到陈旧值的隐患）；空标签槽必须先判空再喂 tag_label
// （它用静态 buf，永不返回 NULL——喂 NULL 名直接解引用崩溃 = v0.17.5 真机滚轮闪退根因）。
static int chip_snap_scroll(int cur, int dir)
{
    int visL0 = g_panel.left + g_padX, visR0 = g_gridR - g_padX;
    int clipR = visR0 - S(18);
    int x = visL0, lastR = visL0;
    int found = (dir > 0) ? 0x7FFFFFFF : -1;   // 向下：>cur 的最小候选；向上：<cur 的最大候选
    for (int i = 0; i <= MP_MAX_TAGS; i++) {   // 队列与 panel_repaint 同序：全部 + 各标签
        const wchar_t* label;
        if (i == 0) label = tag_label(NULL, -1);
        else {
            if (!g_store.tagNames[i - 1]) continue;   // 空槽：先判空（见上）
            label = tag_label(g_store.tagNames[i - 1], i - 1);
        }
        x += chip_need(label, i - 1);
        lastR = x;
        int cand = x - clipR;
        if (dir > 0) { if (cand > cur && cand < found) found = cand; }
        else         { if (cand < cur && cand > found) found = cand; }
        x += S(28);
    }
    int maxScr = lastR - visR0;                 // 末枚右缘滚到 visR0 = 滚到底（与 panel_repaint 同式同源）
    if (maxScr < 0) maxScr = 0;
    int best = found;
    if (dir > 0) { if (best == 0x7FFFFFFF) best = maxScr; }   // 无候选 = 到底
    else         { if (best < 0) best = 0; }                  // 无候选 = 回开头
    if (best < 0) best = 0;
    if (best > maxScr) best = maxScr;
    return best;
}

static void draw_empty(void)
{
    int cy = (g_grid.top + g_grid.bottom) / 2;
    gtext(LS(LS_EMPTY), -1, g_fUi,
          (RECT){ g_grid.left, cy - S(12), g_grid.right, cy + S(12) }, g_sfC, C_INK(130));
}

static void draw_grid(void)
{
    if (g_nFilt == 0) { draw_empty(); return; }
    int vis = g_cols * g_rows;

    // v0.18：格间发丝线全部移除（用户定版——网格只靠留白分格；层级分隔由最近行带下墨线承担，见 draw_recents）
    for (int k = 0; k < vis; k++) {
        int idx = g_first + k;
        if (idx >= g_nFilt) break;
        int col = k % g_cols, row = k / g_cols;
        RECT rc = { g_grid.left + col * (g_cell + g_gap),
                    g_grid.top + row * (g_cell + g_gap),
                    0, 0 };
        rc.right = rc.left + g_cell; rc.bottom = rc.top + g_cell;
        BOOL hov = (idx == g_hover) && !kbd_row_focus();   // 行焦点时停画（单一焦点原则）
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
        if (e->flags & MP_FLAG_FAV) draw_fav_mark(rc);   // 收藏角标
        // 选择 = 四角 accent 角标记；悬停/键盘选中 = accent 全框 + 编号 + 名字条
        if (sel) draw_sel_marks(rc);
        if (hov) {
            draw_red_frame(rc);
            draw_badge(rc.left, rc.top, idx + 1);
        }
        // 名字条：索引文字优先（使用概念），文件名兜底（存储概念）
        if (hov) draw_name_strip(rc, e->indexText && e->indexText[0] ? e->indexText : entry_disp_name(e));
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
    if (kbd_row_focus()) return;   // v0.12.2：行焦点时大图同停（配合网格框互斥）
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
// 抽屉 chip：实底 = 已打标；hover = 2px 黑下划线；键盘焦点 = 底边内侧 3px accent 线
static void draw_dchip(RECT rc, const wchar_t* txt, BOOL on, BOOL kbd)
{
    if (on) {
        fill_rect(rc.left, rc.top, rc.right, rc.bottom, C_INK(255));
        gtext(txt, -1, g_fUi, rc, g_sfC, C_PAPER(255));
    } else {
        BOOL hov = PtInRect(&rc, g_mouse);
        gtext(txt, -1, g_fUi, rc, g_sfL, C_INK(hov ? 255 : 150));
        if (hov) fill_rect(rc.left, rc.bottom - 2, rc.right, rc.bottom, C_INK(255));
    }
    if (kbd) fill_rect(rc.left, rc.bottom - 3, rc.right, rc.bottom, C_RED(255));
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
        wnsprintfW(hdr, 96, LS(LS_SEL_FMT), g_nSel, g_drawerNote ? LS(LS_DUP_NOTE) : L"");
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
        wnsprintfW(nb, 24, LS(LS_N_FMT), g_nSel);
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
        // 元数据列（预览右侧 ~160px）：v0.15 名称 = 可编辑输入框（F2 / 点击，即时写回 origName）
        draw_input_box(g_dNameRc, &g_ebR, g_editFocus == EDIT_NAME, LS(LS_NAME_HINT));
        // 大小 · 格式（KB/MB 自算，GIF 标注）
        wchar_t sz[48], ext[8], line[64];
        fmt_size(tgt->size, sz, 48);
        fmt_ext(tgt->name, ext, 8);
        wnsprintfW(line, 64, L"%s · %s", sz, ext);
        gtext(line, -1, g_fSmall, g_dSizeRc, g_sfL, C_INK(140));
        // 「打开位置」下划线文字按钮（v0.9：词条随语言，rect 在布局期同参度量）
        RECT ob = g_dOpenRc;
        BOOL ohov = PtInRect(&ob, g_mouse);
        gtext(LS(LS_OPENLOC), -1, g_fSmall, ob, g_sfL, C_INK(ohov ? 255 : 140));
        fill_rect(ob.left, ob.bottom - 1, ob.right, ob.bottom, C_INK(ohov ? 255 : 140));
        // 收藏切换（v0.10）：已收藏 = accent 文字；点击 toggle（rect 布局期定宽防跳）
        if (g_dFavRc.right > g_dFavRc.left) {
            BOOL fav = (tgt->flags & MP_FLAG_FAV) != 0;
            BOOL fh = PtInRect(&g_dFavRc, g_mouse);
            gtext(LS(fav ? LS_FAVED : LS_FAV), -1, g_fSmall, g_dFavRc, g_sfL,
                  fav ? C_RED(fh ? 255 : 190) : C_INK(fh ? 255 : 140));
            fill_rect(g_dFavRc.left, g_dFavRc.bottom - 1, g_dFavRc.right, g_dFavRc.bottom,
                      fav ? C_RED(fh ? 255 : 190) : C_INK(fh ? 255 : 140));
        }
    }

    if (!tgt && !batch) return;   // 目标失效（扫描后越界）：只画分隔线

    // ---- 「标签」节：11px 加重节标题 + chips（rect 已在 drawer_layout 排好）----
    gtext(LS(LS_TAGS), -1, g_fSect, (RECT){ L, g_dSecY, R, g_dSecY + S(16) }, g_sfL, C_INK(255));
    int firstSel = -1;                            // 批量方向 = 首个目标
    if (batch)
        for (int i = 0; i < g_store.count; i++) if (sel_has(i)) { firstSel = i; break; }
    for (int i = 0; i < g_nDChips && i < MP_MAX_TAGS + 1; i++) {
        int id = g_dChipId[i];
        if (id < 0) continue;                     // -2 = 「+ 新标签」chip，下方分支绘制
        BOOL on = batch
            ? (firstSel >= 0 && (g_store.entries[firstSel].tagmask & ((u64)1 << id)) != 0)
            : ((tgt->tagmask & ((u64)1 << id)) != 0);
        draw_dchip(g_dChipRc[i], tag_label(g_store.tagNames[id], id), on, i == g_chipFocus);
    }
    if (g_newtagOpen) {
        // + 新标签 → 原地变内联输入框（白底 + 黑下线），Enter 连续建多个
        draw_input_box(g_dNewTagBox, &g_ebN, g_editFocus == EDIT_NEWTAG, LS(LS_NEWTAG));
    } else {
        RECT nc = g_dNewChipRc;
        BOOL nh = PtInRect(&nc, g_mouse) ||
                  (g_chipFocus >= 0 && g_dChipId[g_chipFocus] == -2);   // v0.12 键盘焦点并入
        gtext(LS(LS_NEWTAG_PLUS), -1, g_fUi, nc, g_sfL, C_INK(nh ? 255 : 150));
        if (nh) fill_rect(nc.left, nc.bottom - 2, nc.right, nc.bottom, C_INK(255));
    }

    // ---- 「索引文字」节：仅单目标可编辑 ----
    gtext(LS(LS_INDEX_SEC), -1, g_fSect, (RECT){ L, g_dIdxY, R, g_dIdxY + S(16) }, g_sfL, C_INK(255));
    if (batch) {
        gtext(LS(LS_INDEX_RO), -1, g_fSmall,
              (RECT){ L, g_dIdxY + S(20), R, g_dIdxY + S(38) }, g_sfL, C_INK(140));
    } else if (tgt) {
        RECT ib = g_dIndexBox;
        draw_input_box(ib, &g_ebI, g_editFocus == EDIT_INDEX, LS(LS_INDEX_WM));
        gtext(LS(LS_INDEX_HINT), -1, g_fNano,
              (RECT){ L, ib.bottom + S(6), R, ib.bottom + S(20) },
              g_sfL, ARGB(255, 0x55, 0x55, 0x55));
    }

    // ---- 底部：删除（accent 描边直角；武装 3 秒 = accent 实底「确认删除」）----
    RECT db = g_dDelRc;
    BOOL dhov = PtInRect(&db, g_mouse);
    if (g_delArm)   fill_rect(db.left, db.top, db.right, db.bottom, C_RED(255));
    else if (dhov)  fill_rect(db.left, db.top, db.right, db.bottom, C_INK(255));
    else            frame_rect(db, 2, C_RED(255));
    gtext(g_delArm ? LS(LS_CONFIRM) : LS(LS_DELETE), -1, g_fUi, db, g_sfC,
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
    // v0.17.1 标签行横向滚动：量全队列宽 → 滚满量/钳制 → 预留 ‹› 提示位 → 裁剪落墨。
    // 钳制必须在落墨前（标签删减/抽屉开合使可见宽回缩时，旧偏移当帧即收敛）
    int visL0 = g_panel.left + g_padX, visR0 = g_gridR - g_padX;
    {
        int walk = visL0 + chip_need(tag_label(NULL, -1), -1) + S(28);
        for (int i = 0; i < MP_MAX_TAGS; i++)
            if (g_store.tagNames[i])
                walk += chip_need(tag_label(g_store.tagNames[i], i), i) + S(28);   // v0.17.5：量宽必须用 tag_label（大写化显示文本）——量原名比实际窄，末枚被整枚可见门挡掉
        int over0 = walk - S(28) - visR0;          // 裸溢出（不留提示位）
        // v0.17.3：滚满量 = 裸溢出。右预留位改为「没滚到底才存在」（见下方 clipR）——
        // 滚到底时裁剪放开到 visR0，末标签右缘与网格右缘平齐；v0.17.2 的「+S(18) 补偿」
        // 只把文字救完整，末行仍常驻一段空带 = 真机反馈“最右面还是差一小截”
        g_chipMax = over0 > 0 ? over0 : 0;
        if (g_chipScr > g_chipMax) g_chipScr = g_chipMax;
        if (g_chipScr < 0) g_chipScr = 0;
    }
    g_chipVisL = visL0 + (g_chipScr > 0 ? S(18) : 0);            // 左提示位（滚过才出现）
    g_chipVisR = visR0 - (g_chipScr < g_chipMax ? S(18) : 0);    // 右提示位（没到底才留；到底放开=与网格右缘平齐）
    int cx = visL0 - g_chipScr;
    int chipY = g_chipY;
    GdipSetClipRectI(g_gfx, g_chipVisL, chipY - S(2), g_chipVisR - g_chipVisL, S(30), 0);
    draw_chip(&cx, chipY, NULL, -1, g_tagFocus == 0);   // txt 不用于 id==-1（tag_label 返回本地化「全部」）
    for (int i = 0; i < MP_MAX_TAGS; i++)
        if (g_store.tagNames[i])
            draw_chip(&cx, chipY, g_store.tagNames[i], i, g_tagFocus == g_nChips);   // g_nChips = 即将注册的下标
    GdipResetClip(g_gfx);
    if (g_chipScr > 0)
        gtext(L"‹", -1, g_fUi, (RECT){ visL0, chipY, g_chipVisL, chipY + S(26) }, g_sfC, C_INK(150));
    if (g_chipScr < g_chipMax)
        gtext(L"›", -1, g_fUi, (RECT){ g_chipVisR, chipY, visR0, chipY + S(26) }, g_sfC, C_INK(150));
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
    gtext(L"MemePanel v0.18.2 b" __TIME__, -1, g_fNano,
          (RECT){ g_panel.right - S(190), g_panel.bottom - S(16), g_panel.right - S(6), g_panel.bottom - S(2) },
          g_sfL, ARGB(95, 17, 17, 17));
    if (g_opacityHud) {   // v0.11 透明度调节读数（左下角，900ms 自灭——与版本角标同款 8.5px）
        wchar_t ob[48];
        wnsprintfW(ob, 48, LS(LS_OPACITY), (int)(g_opacity * 100 / 255));
        gtext(ob, -1, g_fNano,
              (RECT){ g_panel.left + S(6), g_panel.bottom - S(16), g_panel.left + S(150), g_panel.bottom - S(2) },
              g_sfL, ARGB(150, 17, 17, 17));
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
// 自绘输入框编辑（v3 三缓冲路由）/ IME
// WM_CHAR / IME 结果串 / 退格删除全部作用于 g_editFocus 指向的缓冲：
//   搜索（改即 refilter）/ 索引（改即写回 entry->indexText）/ 新标签（Enter 提交）
// ============================================================
static EBuf* eb_active(void)
{
    return g_editFocus == EDIT_INDEX ? &g_ebI :
           g_editFocus == EDIT_NEWTAG ? &g_ebN :
           g_editFocus == EDIT_TAG ? &g_ebT :
           g_editFocus == EDIT_NAME ? &g_ebR : &g_ebS;
}

static void edit_notify(void)
{
    if (g_editFocus == EDIT_SEARCH) refilter();
    else if (g_editFocus == EDIT_INDEX) index_commit();   // 即时写回（含 store_save）
    else if (g_editFocus == EDIT_NAME) name_commit();     // v0.15 改名即时写回 origName
    else if (g_editFocus == EDIT_TAG) {                   // v0.10 标签管理窗口内联输入
        if (IsWindowVisible(g_hwndTags) || g_headless) tags_repaint();
        return;
    }
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

//（合并主线清理：曾试验过的「默认 IME 窗搬迁」代码已删——实测无效；IME 定位根治 = WM_IME_REQUEST/IMR_QUERYCHARPOSITION 应答，见 WndProc）
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
    case EDIT_NAME:
        rc = (g_dNameRc.right > g_dNameRc.left) ? g_dNameRc : g_searchBox;
        e = &g_ebR; break;
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
// v0.11 prefs.cfg：exe 同目录，'MPP3' = magic + u32 flags + u32 recentN +
// u32 posX + u32 posY + u32 opacity（v1 'MPP1'/v2 'MPP2' 兼容加载后自然升版）
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
    u8 buf[24];
    DWORD got = 0;
    BOOL ok = ReadFile(f, buf, 24, &got, NULL) && (got == 8 || got == 12 || got == 24);
    CloseHandle(f);
    if (!ok) return;
    u32 magic, flags, recentN = 7, posX = POS_NONE, posY = POS_NONE, opacity = 255;
    memcpy(&magic, buf, 4);
    memcpy(&flags, buf + 4, 4);
    if (got >= 12) memcpy(&recentN, buf + 8, 4);
    if (got >= 24) {
        memcpy(&posX, buf + 12, 4);
        memcpy(&posY, buf + 16, 4);
        memcpy(&opacity, buf + 20, 4);
    }
    if (magic == PREFS_MAGIC3 && got == 24) {
        // v3：flags + 最近行格数 + 拖动记忆位 + 不透明度
    } else if (magic == PREFS_MAGIC2 && got == 12) {
        // v2：flags + 最近行格数
    } else if (magic == PREFS_MAGIC && got == 8) {
        // v1：仅 flags（recentN 默认 7，无记忆位/不透明度）
    } else {
        return;   // 坏文件 = 全默认（文件名搜索开 / 最近行 7 / 无记忆位 / 不透明）
    }
    g_prefs = flags;
    g_recentOn = (recentN != 0);        // v0.16.2：数量档退役——非 0 = 开（旧文件 1..15 兼容），0 = 关
    g_recentN = g_recentOn ? 7 : 0;     // 首次布局前的安全值；真实槽位由 panel_layout 派生
    g_posX = (posX == POS_NONE) ? -1 : (int)posX;
    g_posY = (posY == POS_NONE) ? -1 : (int)posY;
    if (g_posX < -32000 || g_posY < -32000) g_posX = g_posY = -1;   // 疯值作废
    if (opacity < OPACITY_MIN) opacity = OPACITY_MIN;
    if (opacity > 255) opacity = 255;
    g_opacity = opacity;
}

static void prefs_save(void)
{
    wchar_t p[MAX_PATH], tmp[MAX_PATH];
    prefs_path(p, MAX_PATH);
    wnsprintfW(tmp, MAX_PATH, L"%s.tmp", p);
    u8 buf[24];
    u32 magic = PREFS_MAGIC3;
    u32 posX = (g_posX >= 0) ? (u32)g_posX : POS_NONE;
    u32 posY = (g_posY >= 0) ? (u32)g_posY : POS_NONE;
    memcpy(buf, &magic, 4);
    memcpy(buf + 4, &g_prefs, 4);
    u32 rn = g_recentOn ? 7u : 0u;      // v0.16.2：盘面只存开/关（7=开 向后兼容，0=关）
    memcpy(buf + 8, &rn, 4);
    memcpy(buf + 12, &posX, 4);
    memcpy(buf + 16, &posY, 4);
    memcpy(buf + 20, &g_opacity, 4);
    HANDLE f = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD got = 0;
        if (WriteFile(f, buf, 24, &got, NULL) && got == 24) {
            CloseHandle(f);
            MoveFileExW(tmp, p, MOVEFILE_REPLACE_EXISTING);
        } else {
            CloseHandle(f);
            DeleteFileW(tmp);
        }
    }
}

// ============================================================
// v0.11 不透明度统一入口：夹取 [OPACITY_MIN,255]、prefs v3 落盘、三窗即时生效。
// 主面板可见时左下角亮 900ms 读数（OPACITY_TMR 自灭）；
// 无头 -shot 不经此处（探针走 PNG 合成，透明度默认 255 时与旧输出逐像素一致）。
// ============================================================
static void imp_repaint(void);
static void opacity_set(int v)
{
    BOOL wasFull = (g_opacity >= 255);
    if (v < OPACITY_MIN) v = OPACITY_MIN;
    if (v > 255) v = 255;
    g_opacity = (u32)v;
    prefs_save();
    // 满档 ↔ 半透跨界 = 底色玻璃常数切换：面板可见时重烤底图缓存
    // （管理窗每帧重画底自动生效；面板隐藏时 g_bits 已释放，下次 show 自然新值）
    if ((wasFull != (g_opacity >= 255)) && g_dib && g_base) gen_base();
    if (IsWindowVisible(g_hwnd)) {
        g_opacityHud = TRUE;
        SetTimer(g_hwnd, OPACITY_TMR, 900, NULL);
        panel_repaint();
    }
    if (g_hwndTags && IsWindowVisible(g_hwndTags)) tags_repaint();
    if (g_hwndImp && IsWindowVisible(g_hwndImp)) imp_repaint();
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
// v0.12 纯键盘：动作键的「当前对象」与焦点辅助
// ============================================================
// 动作键（Ctrl+D 收藏 / Ctrl+Delete 删除）面向的对象：抽屉目标优先，网格键盘选中次之
static Entry* kbd_target(void)
{
    Entry* t = drawer_target();
    if (t) return t;
    if (g_hover >= 0 && g_hover < g_nFilt) return &g_store.entries[g_filt[g_hover]];
    return NULL;
}

// 键盘选中格（开抽屉用）：选中项优先，否则首个可见（与 Enter 发送同规则）
static int kbd_ei(void)
{
    if (g_nFilt <= 0) return -1;
    int idx = (g_hover >= 0 && g_hover < g_nFilt) ? g_hover : g_first;
    if (idx < 0) idx = 0;
    if (idx >= g_nFilt) idx = g_nFilt - 1;
    return g_filt[idx];
}

// 标签行键盘焦点落点：当前激活 chip（无激活 = 0「全部」），进入即对齐现状
static int tag_focus_entry(void)
{
    if (g_nChips <= 0) return -1;
    int f = 0;
    for (int i = 0; i < g_nChips; i++) {
        int id = g_chipId[i];
        if (id == -1 ? (g_tagFilter == 0) : (g_tagFilter == ((u64)1 << id))) { f = i; break; }
    }
    return f;
}

// ============================================================
// v3 键盘导航：方向键在网格内移动 g_hover（= 键盘选中框，视觉与 hover 同源：
// accent 框 + 编号块），越出可视区时 g_first 按行滚动跟随；鼠标 hover 两态同源
// ============================================================
static void hover_move(HWND hwnd, int delta)
{
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
    SetTimer(hwnd, PREVIEW_TMR, 280, NULL);   // v0.12：键盘选中同样 280ms 亮大图（对齐鼠标 hover）
    // v0.13：键盘选中同样驱动 GIF 动画（对齐鼠标 hover——anim want 切到当前格）
    anim_request(hwnd, (g_hover >= 0 && g_hover < g_nFilt) ? g_filt[g_hover] : -1);
    panel_repaint();
}

// ============================================================
// v0.10 管理窗口公共件：表面 + swap 绘制 + ULW 提交（见模块头注释）
// ============================================================
typedef struct {
    HBITMAP dib;
    u32*    bits;
    GpBitmap*   gpbmp;
    GpGraphics* gfx;
    int w, h;
    RECT panel;      // 内容面（四周 g_bleed 出血 + 2px 墨框）
    RECT closeRc;    // 右上 ✕
} Mgr;

typedef struct { u32* bits; GpBitmap* gpbmp; GpGraphics* gfx; } MgrSwap;

static Mgr g_mgT, g_mgI, g_mgS;   // 标签 / 导入 / 设置 三个管理窗的表面（v0.10/v0.16）
static POINT g_tMouse = { -30000, -30000 };   // 标签管理窗口 hover 位（-30000 = 屏外）

static void mgr_surface(Mgr* m)
{
    if (m->gfx)  { GdipDeleteGraphics(m->gfx); m->gfx = NULL; }
    if (m->gpbmp){ GdipDisposeImage((GpImage*)m->gpbmp); m->gpbmp = NULL; }
    if (m->dib)  { DeleteObject(m->dib); m->dib = NULL; }
    m->bits = NULL;
    if (m->w <= 0 || m->h <= 0) return;
    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = m->w;
    bi.bmiHeader.biHeight = -m->h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    m->dib = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, (void**)&m->bits, NULL, 0);
    if (!m->dib) return;
    GdipCreateBitmapFromScan0(m->w, m->h, m->w * 4, GP_PF_32PARGB, (u8*)m->bits, &m->gpbmp);
    GdipGetImageGraphicsContext((GpImage*)m->gpbmp, &m->gfx);
    if (m->gfx) {
        GdipSetSmoothingMode(m->gfx, 0);
        GdipSetInterpolationMode(m->gfx, GP_INTERP_BICUBIC);
        GdipSetTextRenderingHint(m->gfx, GP_TEXT_AA_GRIDFIT);
    }
}

static void mgr_release(Mgr* m)
{
    if (m->gfx)  { GdipDeleteGraphics(m->gfx); m->gfx = NULL; }
    if (m->gpbmp){ GdipDisposeImage((GpImage*)m->gpbmp); m->gpbmp = NULL; }
    if (m->dib)  { DeleteObject(m->dib); m->dib = NULL; }
    m->bits = NULL;
}

static void mgr_swap_begin(Mgr* m, MgrSwap* s)
{
    s->bits = g_bits; s->gpbmp = g_gpbmp; s->gfx = g_gfx;
    g_bits = m->bits; g_gpbmp = m->gpbmp; g_gfx = m->gfx;
}

static void mgr_swap_end(MgrSwap* s)
{
    g_bits = s->bits; g_gpbmp = s->gpbmp; g_gfx = s->gfx;
}

// 底图：纸白方角面 + 2px 墨框（swap 内调用；helper 已绑到本窗表面）
static void mgr_base(Mgr* m)
{
    memset(m->bits, 0, (u64)m->w * m->h * 4);
    // v0.11.1：满档全实心（同 gen_base；本函数每帧重画，改常数即时生效）
    u32 paper = (g_opacity >= 255)
        ? (0xFF000000u | (250u << 16) | (250u << 8) | 249u)
        : ((245u << 24) | (240u << 16) | (240u << 8) | 239u);
    for (int y = m->panel.top; y < m->panel.bottom; y++) {
        u32* row = m->bits + (u64)y * m->w;
        for (int x = m->panel.left; x < m->panel.right; x++) row[x] = paper;
    }
    frame_rect(m->panel, 2, C_INK(255));
}

// ULW 提交（借 g_memDC 暂选本窗 DIB，提交完换回面板的——单线程安全）
static void mgr_ulw(HWND hwnd, Mgr* m)
{
    if (!m->dib || !hwnd || g_headless) return;
    HGDIOBJ old = SelectObject(g_memDC, m->dib);
    RECT wr;
    GetWindowRect(hwnd, &wr);
    POINT src = { 0, 0 }, dst = { wr.left, wr.top };
    SIZE sz = { m->w, m->h };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)g_opacity, AC_SRC_ALPHA };   // v0.11 三窗同透明度
    HDC scr = GetDC(NULL);
    UpdateLayeredWindow(hwnd, scr, &dst, &sz, g_memDC, &src, 0, &bf, ULW_ALPHA);
    ReleaseDC(NULL, scr);
    if (old && old != (HGDIOBJ)m->dib) SelectObject(g_memDC, old);
}

// v0.11 刊头带拖动死区：标题文字带（panel 顶到横梁前），✕ 除外——
// 与主面板同款 WM_NCLBUTTONDOWN(HTCAPTION) 手感；管理窗不记位置（每次居中开）
static BOOL mgr_drag_zone(Mgr* m, POINT pt)
{
    return pt.y >= m->panel.top && pt.y < m->panel.top + S(38) &&
           !PtInRect(&m->closeRc, pt);
}

// 刊头（标题 + 2px 横梁 + ✕）与命中（swap 内绘制）
static void mgr_chrome(Mgr* m, const wchar_t* title)
{
    int padX = S(20);
    gtext(title, -1, g_fSect,
          (RECT){ m->panel.left + padX, m->panel.top + S(12), m->panel.right - padX - S(32), m->panel.top + S(30) },
          g_sfL, C_INK(255));
    int beamY = m->panel.top + S(38);
    fill_rect(m->panel.left, beamY, m->panel.right, beamY + S(2), C_INK(255));
    // ✕：hover 黑底白叉（同面板关闭钮）
    RECT cb = m->closeRc;
    BOOL ch = PtInRect(&cb, g_tMouse);
    if (ch) fill_rect(cb.left, cb.top, cb.right, cb.bottom, C_INK(255));
    GpPen* xp = NULL;
    if (GdipCreatePen1(ch ? ARGB(255, 255, 255, 255) : C_INK(255), 1.8f, GP_UNIT_PIXEL, &xp) == 0) {
        int mg = S(8);
        GdipSetSmoothingMode(g_gfx, GP_SMOOTH_AA);
        GdipDrawLineI(g_gfx, xp, cb.left + mg, cb.top + mg, cb.right - mg, cb.bottom - mg);
        GdipDrawLineI(g_gfx, xp, cb.right - mg, cb.top + mg, cb.left + mg, cb.bottom - mg);
        GdipSetSmoothingMode(g_gfx, 0);
        GdipDeletePen(xp);
    }
}

// 文字按钮（描边矩形：mode 0 = 墨框；1 = accent 框；armed = accent 实底）
static void draw_mbtn(RECT rc, const wchar_t* txt, BOOL hov, BOOL enabled, BOOL armed, int accent)
{
    if (!enabled) {
        gtext(txt, -1, g_fUi, rc, g_sfC, C_INK(90));
        return;
    }
    if (armed)  fill_rect(rc.left, rc.top, rc.right, rc.bottom, C_RED(255));
    else if (hov) fill_rect(rc.left, rc.top, rc.right, rc.bottom, C_INK(255));
    else frame_rect(rc, 2, accent ? C_RED(255) : C_INK(255));
    gtext(txt, -1, g_fUi, rc, g_sfC,
          (armed || hov) ? ARGB(255, 250, 250, 249) : C_INK(255));
}

static int n_tags(void)
{
    int n = 0;
    for (int i = 0; i < MP_MAX_TAGS; i++) if (g_store.tagNames[i]) n++;
    return n;
}

// ============================================================
// 标签管理窗口（v0.10）：列表选择 + 内联改名（IME）+ 新建 + 武装删除 + 滚动
// ============================================================
static int  t_sel = -1;        // 选中标签 id（-1 无）
static int  t_ren;             // 0 无；1 = 改名；2 = 新建（列表末尾输入行）
static BOOL t_arm;             // 删除二次确认武装（3s 复位）
static int  t_top;             // 首可见行（滚动）
static RECT t_rowRc[MP_MAX_TAGS + 1];
static int  t_rowId[MP_MAX_TAGS + 1];   // 标签 id；新建输入行 = -3
static int  t_nRows;
static RECT t_inRc;            // 内联输入框（t_ren 时有效）
static RECT t_btnNew, t_btnRen, t_btnDel;
static int  t_listT, t_listB, t_rowH;

static void tags_layout(void)
{
    Mgr* m = &g_mgT;
    int padX = S(20);
    t_nRows = 0;
    t_inRc = (RECT){ 0, 0, 0, 0 };
    t_rowH = S(30);
    t_listT = m->panel.top + S(38) + S(2) + S(12);
    int btnY = m->panel.bottom - S(16) - S(30);
    t_listB = btnY - S(12);

    // 底部按钮：新建 / 重命名 / 删除（宽度 = 文字 + S(24)）
    int x = m->panel.left + padX;
    int w1 = (int)gtext_w(LS(LS_MGR_NEW), -1, g_fUi) + S(24);
    int w2 = (int)gtext_w(LS(LS_MGR_RENAME), -1, g_fUi) + S(24);
    int w3 = (int)gtext_w(LS(LS_CONFIRM), -1, g_fUi) + S(24);   // 删除钮取武装文案宽（防跳）
    t_btnNew = (RECT){ x, btnY, x + w1, btnY + S(30) }; x += w1 + S(12);
    t_btnRen = (RECT){ x, btnY, x + w2, btnY + S(30) }; x += w2 + S(12);
    t_btnDel = (RECT){ x, btnY, x + w3, btnY + S(30) };

    int vis = (t_listB - t_listT) / t_rowH;
    if (vis < 1) vis = 1;
    int total = n_tags() + (t_ren == 2 ? 1 : 0);
    if (t_top > total - vis) t_top = total - vis;
    if (t_top < 0) t_top = 0;

    int idx = 0;
    for (int id = 0, row = 0; id < MP_MAX_TAGS && idx < vis; id++) {
        if (!g_store.tagNames[id]) continue;
        if (row++ < t_top) continue;   // 滚动：跳过首可见行之前的标签
        t_rowRc[idx] = (RECT){ m->panel.left, t_listT + idx * t_rowH, m->panel.right, t_listT + (idx + 1) * t_rowH };
        t_rowId[idx] = id;
        idx++;
    }
    if (t_ren == 2 && idx < vis) {
        t_rowRc[idx] = (RECT){ m->panel.left, t_listT + idx * t_rowH, m->panel.right, t_listT + (idx + 1) * t_rowH };
        t_rowId[idx] = -3;
        idx++;
    }
    t_nRows = idx;
    // 内联输入框：改名 = 选中行内嵌；新建 = 末行整行
    if (t_ren) {
        for (int i = 0; i < t_nRows; i++) {
            int isInput = (t_ren == 1) ? (t_rowId[i] == t_sel) : (t_rowId[i] == -3);
            if (isInput) {
                RECT r = t_rowRc[i];
                t_inRc = (RECT){ r.left + padX - S(4), r.top + S(3), r.right - padX + S(4), r.bottom - S(3) };
                break;
            }
        }
    }
}

static void tags_paint(void)
{
    Mgr* m = &g_mgT;
    int padX = S(20);
    mgr_base(m);
    mgr_chrome(m, LS(LS_MGR_TAGS));

    // 行
    for (int i = 0; i < t_nRows; i++) {
        RECT r = t_rowRc[i];
        int id = t_rowId[i];
        if ((t_ren == 1 && id == t_sel) || (t_ren == 2 && id == -3)) {
            draw_input_box(t_inRc, &g_ebT, TRUE,
                           t_ren == 1 ? LS(LS_MGR_REN_WM) : LS(LS_NEWTAG));
            continue;
        }
        const wchar_t* label = tag_label(g_store.tagNames[id], id);
        wchar_t cnt[24];
        wnsprintfW(cnt, 24, LS(LS_N_FMT), store_tag_count(&g_store, id));
        float cw = gtext_w(cnt, -1, g_fNano);
        if (id == t_sel) {
            fill_rect(r.left, r.top, r.right, r.bottom, C_INK(255));
            int sq = S(6);
            fill_rect(r.left + padX, r.top + (t_rowH - sq) / 2, r.left + padX + sq, r.top + (t_rowH + sq) / 2, C_RED(255));
            gtext(label, -1, g_fUi, (RECT){ r.left + padX + sq + S(8), r.top, r.right - padX - S(64), r.bottom },
                  g_sfL, C_PAPER(255));
            gtext(cnt, -1, g_fNano, (RECT){ r.right - padX - (int)cw - S(2), r.top, r.right - padX, r.bottom },
                  g_sfL, ARGB(190, 250, 250, 249));
        } else {
            BOOL hov = PtInRect(&r, g_tMouse);
            gtext(label, -1, g_fUi, (RECT){ r.left + padX, r.top, r.right - padX - S(64), r.bottom },
                  g_sfL, C_INK(hov ? 255 : 150));
            gtext(cnt, -1, g_fNano, (RECT){ r.right - padX - (int)cw - S(2), r.top, r.right - padX, r.bottom },
                  g_sfL, ARGB(255, 0x77, 0x77, 0x77));
            if (hov) fill_rect(r.left + padX, r.bottom - 2, r.left + padX + (int)gtext_w(label, -1, g_fUi) + S(4), r.bottom, C_INK(255));
        }
        if (i + 1 < t_nRows) hair_h(r.bottom, m->panel.left + padX, m->panel.right - padX);
    }
    // v0.17 滚动指示：右缘 2px 发丝轨道 + 墨色滑块（总数 ≤ 可见行数时不画）
    {
        int total = n_tags() + (t_ren == 2 ? 1 : 0);
        int vis = (t_listB - t_listT) / t_rowH;
        if (vis < 1) vis = 1;
        if (total > vis) {
            int trackH = t_listB - t_listT;
            int thumbH = vis * trackH / total;
            if (thumbH < S(24)) thumbH = S(24);
            int maxY = trackH - thumbH;
            int ty = t_listT + (maxY > 0 ? t_top * maxY / (total - vis) : 0);
            int sx = m->panel.right - S(8);
            fill_rect(sx, t_listT, sx + 2, t_listB, C_HAIR);
            fill_rect(sx, ty, sx + 2, ty + thumbH, C_INK(255));
        }
    }
    // 空态
    if (n_tags() == 0 && !t_ren) {
        int cy = (t_listT + t_listB) / 2;
        gtext(LS(LS_MGR_EMPTY), -1, g_fUi,
              (RECT){ m->panel.left, cy - S(14), m->panel.right, cy }, g_sfC, C_INK(130));
        gtext(LS(LS_MGR_HINT), -1, g_fNano,
              (RECT){ m->panel.left, cy + S(4), m->panel.right, cy + S(20) }, g_sfC, ARGB(255, 0x77, 0x77, 0x77));
    }
    // 底部按钮
    draw_mbtn(t_btnNew, LS(LS_MGR_NEW), PtInRect(&t_btnNew, g_tMouse), TRUE, FALSE, 0);
    draw_mbtn(t_btnRen, LS(LS_MGR_RENAME), PtInRect(&t_btnRen, g_tMouse), t_sel >= 0, FALSE, 0);
    draw_mbtn(t_btnDel, t_arm ? LS(LS_CONFIRM) : LS(LS_MGR_REMOVE), PtInRect(&t_btnDel, g_tMouse),
              t_sel >= 0, t_arm, 1);
}

static void tags_repaint(void)
{
    Mgr* m = &g_mgT;
    if (!m->bits || !m->gfx) return;
    MgrSwap s;
    mgr_swap_begin(m, &s);
    tags_layout();
    tags_paint();
    mgr_swap_end(&s);
    mgr_ulw(g_hwndTags, m);
}

// 标签变更后的统一收尾：落盘 + 面板刷新（chips/筛选/最近行）
static void tags_changed(void)
{
    store_save(&g_store);
    // 过滤位指向已删标签 → 清过滤
    for (int i = 0; i < MP_MAX_TAGS; i++)
        if (g_tagFilter == ((u64)1 << i) && !g_store.tagNames[i]) g_tagFilter = 0;
    refilter();
    recalc_recents();
    if (IsWindowVisible(g_hwnd) || g_headless) panel_repaint();
}

static void tags_reset_input(void)
{
    if (t_ren) {
        t_ren = 0;
        g_ebT.b[0] = 0; g_ebT.len = g_ebT.caret = 0;
    }
    if (g_editFocus == EDIT_TAG) g_editFocus = EDIT_SEARCH;
    if (g_hwndTags) KillTimer(g_hwndTags, CARET_TIMER);
}

static void tags_start_rename(HWND hwnd)
{
    if (t_sel < 0 || !g_store.tagNames[t_sel]) return;
    tags_reset_input();
    t_ren = 1;
    lstrcpynW(g_ebT.b, g_store.tagNames[t_sel], 127);
    g_ebT.len = (int)wlen(g_ebT.b);
    g_ebT.caret = g_ebT.len;
    g_editFocus = EDIT_TAG;
    g_caretOn = TRUE;
    SetTimer(hwnd, CARET_TIMER, 530, NULL);
    tags_repaint();
}

static void tags_start_new(HWND hwnd)
{
    tags_reset_input();
    t_ren = 2;
    g_ebT.b[0] = 0; g_ebT.len = g_ebT.caret = 0;
    g_editFocus = EDIT_TAG;
    g_caretOn = TRUE;
    SetTimer(hwnd, CARET_TIMER, 530, NULL);
    // 新建行滚进视野
    int vis = (t_listB - t_listT) / t_rowH;
    int total = n_tags() + 1;
    if (total > vis) t_top = total - vis;
    tags_repaint();
}

static void tags_commit(HWND hwnd)
{
    int a = 0, b = g_ebT.len;
    while (a < b && g_ebT.b[a] == L' ') a++;
    while (b > a && g_ebT.b[b - 1] == L' ') b--;
    g_ebT.b[b] = 0;
    const wchar_t* name = g_ebT.b + a;
    if (t_ren == 1 && t_sel >= 0 && *name)
        store_tag_rename(&g_store, t_sel, name);   // 重名(-2)：保持输入态，用户自行改
    else if (t_ren == 2 && *name) {
        int id = store_tag_add(&g_store, name);
        if (id >= 0) {
            tags_reset_input();
            t_sel = id;
            tags_changed();
            tags_start_new(hwnd);   // 连续新建（同抽屉新标签交互）
            return;
        }
    }
    if (t_ren == 1) {
        tags_reset_input();
        tags_changed();
    } else if (t_ren == 2) {
        tags_reset_input();
        tags_changed();
    }
    tags_repaint();
}

static void tags_delete_sel(HWND hwnd)
{
    if (t_sel < 0 || !g_store.tagNames[t_sel]) return;
    if (!t_arm) {
        t_arm = TRUE;
        SetTimer(hwnd, ARM_TMR, 3000, NULL);
        tags_repaint();
        return;
    }
    t_arm = FALSE;
    KillTimer(hwnd, ARM_TMR);
    store_tag_delete(&g_store, t_sel);
    t_sel = -1;
    tags_reset_input();
    tags_changed();
    tags_repaint();
}

static void tags_open(HWND hwnd)
{
    (void)hwnd;
    if (!g_hwndTags) return;
    Mgr* m = &g_mgT;
    RECT wa;
    work_area_at_cursor(&wa);
    int w = S(420) + g_bleed * 2, h = S(580) + g_bleed * 2;
    SetWindowPos(g_hwndTags, HWND_TOP,
                 wa.left + (wa.right - wa.left - w) / 2,
                 wa.top + (wa.bottom - wa.top - h) / 3,
                 w, h, SWP_SHOWWINDOW);
    SetForegroundWindow(g_hwndTags);
    SetFocus(g_hwndTags);
    if (!g_mgT.bits) tags_size(g_hwndTags);   // v0.17.1 复开冻结修复：关闭时表面已释放，同尺寸 SWP 不触发 WM_SIZE，须就地重建
    tags_repaint();
}

static void tags_close(HWND hwnd)
{
    tags_reset_input();
    t_arm = FALSE;
    KillTimer(hwnd, ARM_TMR);
    ShowWindow(hwnd, SW_HIDE);
    mgr_release(&g_mgT);
}

static void tags_size(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    g_mgT.w = rc.right; g_mgT.h = rc.bottom;
    if (g_mgT.w <= 0 || g_mgT.h <= 0) return;
    g_mgT.panel.left = g_bleed; g_mgT.panel.top = g_bleed;
    g_mgT.panel.right = g_mgT.w - g_bleed; g_mgT.panel.bottom = g_mgT.h - g_bleed;
    g_mgT.closeRc.right = g_mgT.panel.right - S(20);
    g_mgT.closeRc.left = g_mgT.closeRc.right - S(26);
    g_mgT.closeRc.top = g_mgT.panel.top + S(10);
    g_mgT.closeRc.bottom = g_mgT.closeRc.top + S(26);
    mgr_surface(&g_mgT);
    if (g_mgT.bits) tags_repaint();
}

static LRESULT CALLBACK TagsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE:
        tags_size(hwnd);
        return 0;
    case WM_TIMER:
        if (wParam == CARET_TIMER) {
            g_caretOn = !g_caretOn;
            tags_repaint();
            return 0;
        }
        if (wParam == ARM_TMR) {
            KillTimer(hwnd, ARM_TMR);
            if (t_arm) { t_arm = FALSE; tags_repaint(); }
            return 0;
        }
        return 0;

    case WM_SETFOCUS:
        g_caretOn = TRUE;
        if (t_ren) SetTimer(hwnd, CARET_TIMER, 530, NULL);
        if (!g_sysCaret) { CreateCaret(hwnd, NULL, 1, 1); g_sysCaret = TRUE; }
        return 0;
    case WM_KILLFOCUS:
        KillTimer(hwnd, CARET_TIMER);
        if (g_sysCaret) { DestroyCaret(); g_sysCaret = FALSE; }
        return 0;

    case WM_MOUSEMOVE:
        g_tMouse.x = (short)LOWORD(lParam);
        g_tMouse.y = (short)HIWORD(lParam);
        {
            BOOL hand = PtInRect(&g_mgT.closeRc, g_tMouse) ||
                        PtInRect(&t_btnNew, g_tMouse) ||
                        (t_sel >= 0 && (PtInRect(&t_btnRen, g_tMouse) || PtInRect(&t_btnDel, g_tMouse)));
            for (int i = 0; !hand && i < t_nRows; i++) hand = PtInRect(&t_rowRc[i], g_tMouse);
            BOOL grab = mgr_drag_zone(&g_mgT, g_tMouse);   // v0.11 刊头可拖
            SetCursor(LoadCursorW(NULL, grab ? IDC_SIZEALL : (hand ? IDC_HAND : IDC_ARROW)));
        }
        tags_repaint();
        return 0;
    case WM_MOUSELEAVE:
        g_tMouse.x = g_tMouse.y = -30000;
        tags_repaint();
        return 0;

    case WM_MOUSEWHEEL: {
        if (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) {   // v0.11 Ctrl+滚轮 = 不透明度
            opacity_set((int)g_opacity + (GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? OPACITY_STEP : -OPACITY_STEP));
            return 0;
        }
        // v0.17：行滚升级一格三行（原一格一行手感死；且无指示条 = 用户不知道能滚，见 tags_paint）
        t_top += (GET_WHEEL_DELTA_WPARAM(wParam) > 0) ? -3 : 3;
        int vis = (t_listB - t_listT) / t_rowH;
        if (vis < 1) vis = 1;
        int total = n_tags() + (t_ren == 2 ? 1 : 0);
        if (t_top > total - vis) t_top = total - vis;
        if (t_top < 0) t_top = 0;
        tags_repaint();
        return 0;
    }
    case WM_LBUTTONDOWN: {   // v0.11 刊头带拖动（不记位置）
        POINT pt;
        pt.x = (short)LOWORD(lParam);
        pt.y = (short)HIWORD(lParam);
        if (mgr_drag_zone(&g_mgT, pt)) {
            SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            return 0;
        }
        break;
    }

    case WM_LBUTTONUP: {
        POINT pt;
        pt.x = (short)LOWORD(lParam);
        pt.y = (short)HIWORD(lParam);
        if (PtInRect(&g_mgT.closeRc, pt)) { tags_close(hwnd); return 0; }
        if (PtInRect(&t_btnNew, pt)) { tags_start_new(hwnd); return 0; }
        if (PtInRect(&t_btnRen, pt)) { tags_start_rename(hwnd); return 0; }
        if (PtInRect(&t_btnDel, pt)) { tags_delete_sel(hwnd); return 0; }
        for (int i = 0; i < t_nRows; i++) {
            if (!PtInRect(&t_rowRc[i], pt)) continue;
            int id = t_rowId[i];
            if (id == -3) continue;   // 新建输入行：点击即聚焦（已在输入态）
            if (id != t_sel) {
                t_sel = id;
                if (t_ren == 1) { tags_reset_input(); }   // 改名中途换行 = 放弃
                t_arm = FALSE;
                tags_repaint();
            }
            return 0;
        }
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        POINT pt;
        pt.x = (short)LOWORD(lParam);
        pt.y = (short)HIWORD(lParam);
        for (int i = 0; i < t_nRows; i++)
            if (PtInRect(&t_rowRc[i], pt) && t_rowId[i] >= 0) {
                t_sel = t_rowId[i];
                tags_start_rename(hwnd);
                return 0;
            }
        return 0;
    }

    case WM_KEYDOWN:
        if (g_complen) break;   // IME 组合中交给输入法
        switch (wParam) {
        case VK_ESCAPE:
            if (t_ren) { tags_reset_input(); tags_repaint(); }
            else tags_close(hwnd);
            return 0;
        case VK_RETURN:
            if (t_ren) tags_commit(hwnd);
            else if (t_sel >= 0) tags_start_rename(hwnd);
            return 0;
        case VK_DELETE:
            tags_delete_sel(hwnd);
            return 0;
        case VK_UP:
        case VK_DOWN: {
            if (t_ren) return 0;
            int dir = (wParam == VK_UP) ? -1 : 1;
            int ids[MP_MAX_TAGS], n = 0;
            for (int i = 0; i < MP_MAX_TAGS; i++) if (g_store.tagNames[i]) ids[n++] = i;
            if (!n) return 0;
            int cur = -1;
            for (int i = 0; i < n; i++) if (ids[i] == t_sel) { cur = i; break; }
            cur += dir;
            if (cur < 0) cur = 0;
            if (cur >= n) cur = n - 1;
            t_sel = ids[cur];
            // 滚动跟随：行号 cur 需在 [t_top, t_top+vis)
            int vis = (t_listB - t_listT) / t_rowH;
            if (cur < t_top) t_top = cur;
            if (cur >= t_top + vis) t_top = cur - vis + 1;
            t_arm = FALSE;
            tags_repaint();
            return 0;
        }
        case VK_LEFT:  if (t_ren) edit_caret_move(-1); return 0;
        case VK_RIGHT: if (t_ren) edit_caret_move(1);  return 0;
        case VK_HOME:  if (t_ren && g_ebT.caret) { g_ebT.caret = 0; g_caretOn = TRUE; tags_repaint(); } return 0;
        case VK_END:   if (t_ren && g_ebT.caret != g_ebT.len) { g_ebT.caret = g_ebT.len; g_caretOn = TRUE; tags_repaint(); } return 0;
        case VK_BACK:  if (t_ren) { edit_backspace(); g_caretOn = TRUE; } return 0;
        default:
            break;
        }
        break;

    case WM_CHAR:
        if (t_ren && !g_complen) {
            wchar_t c = (wchar_t)wParam;
            if (c >= 32 && c != 127) { edit_insert(&c, 1); g_caretOn = TRUE; }
            return 0;
        }
        return 0;

    // ---- IME（t_ren 输入态；根治手法同面板：应答 IMR_QUERYCHARPOSITION）----
    case WM_IME_REQUEST:
        if (wParam == IMR_QUERYCHARPOSITION && lParam && t_ren && t_inRc.right > t_inRc.left) {
            IMECHARPOSITION* icp = (IMECHARPOSITION*)lParam;
            int tx = t_inRc.left + S(2);
            float preW = g_ebT.caret ? gtext_w(g_ebT.b, g_ebT.caret, g_fText) : 0.0f;
            int n = (int)icp->dwCharPos;
            if (n < 0) n = 0;
            if (n > g_complen) n = g_complen;
            float cw = n ? gtext_w(g_comp, n, g_fText) : 0.0f;
            icp->pt.x = tx + (int)preW + (int)cw;
            icp->pt.y = (t_inRc.top + t_inRc.bottom) / 2;
            ClientToScreen(hwnd, &icp->pt);
            icp->cLineHeight = (UINT)(t_inRc.bottom - t_inRc.top);
            icp->rcDocument = t_inRc;
            POINT p1 = { t_inRc.left, t_inRc.top }, p2 = { t_inRc.right, t_inRc.bottom };
            ClientToScreen(hwnd, &p1); ClientToScreen(hwnd, &p2);
            icp->rcDocument.left = p1.x; icp->rcDocument.top = p1.y;
            icp->rcDocument.right = p2.x; icp->rcDocument.bottom = p2.y;
            return TRUE;
        }
        break;

    case WM_IME_SETCONTEXT:
        lParam &= ~ISC_SHOWUICOMPOSITIONWINDOW;   // 组合串自绘
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    case WM_IME_STARTCOMPOSITION:
        if (t_ren) {
            if (!g_sysCaret) { CreateCaret(hwnd, NULL, 1, 1); g_sysCaret = TRUE; }
            float preW = g_ebT.caret ? gtext_w(g_ebT.b, g_ebT.caret, g_fText) : 0.0f;
            SetCaretPos(t_inRc.left + S(2) + (int)preW, (t_inRc.top + t_inRc.bottom) / 2 - S(9));
        }
        return 0;

    case WM_IME_COMPOSITION: {
        if (!t_ren) return 0;
        HIMC im = ImmGetContext(hwnd);
        if (!im) return 0;
        wchar_t buf[128];
        if (lParam & GCS_RESULTSTR) {
            LONG n = ImmGetCompositionStringW(im, GCS_RESULTSTR, NULL, 0);
            if (n > 0 && n <= 250) {
                ImmGetCompositionStringW(im, GCS_RESULTSTR, buf, n);
                buf[n / 2] = 0;
                edit_insert(buf, n / 2);
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
            tags_repaint();
        }
        ImmReleaseContext(hwnd, im);
        return 0;
    }

    case WM_IME_ENDCOMPOSITION:
        if (g_complen) { g_comp[0] = 0; g_complen = 0; tags_repaint(); }
        return 0;

    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================
// 导入窗口（v0.10）：主界面去导入化后唯一的入库通道——
// 添加文件（多选）/ 添加文件夹（自动建同名标签）/ 粘贴导入（HDROP 或位图→PNG）/
// 拖放到本窗 / 库统计 + 打开库文件夹 / 最近导入状态行。
// ============================================================
static POINT i_mouse = { -30000, -30000 };
static wchar_t i_status[128];
static RECT i_btnFiles, i_btnFolder, i_btnPaste, i_openRc, i_dropRc;

static void imp_layout(void)
{
    Mgr* m = &g_mgI;
    int padX = S(20);
    // v0.17：按钮行下移（原 S(14) 起点与库统计文字 50..66 重叠）——统计行 48..64，按钮 74 起
    int y = m->panel.top + S(38) + S(2) + S(34);
    // 三按钮并排（等宽 = 文字最宽者 + S(28)，视觉对齐）
    int wb = (int)gtext_w(LS(LS_IMP_FILES), -1, g_fUi);
    int w2 = (int)gtext_w(LS(LS_IMP_FOLDER), -1, g_fUi);
    int w3 = (int)gtext_w(LS(LS_IMP_PASTE), -1, g_fUi);
    if (w2 > wb) wb = w2;
    if (w3 > wb) wb = w3;
    wb += S(28);
    i_btnFiles  = (RECT){ m->panel.left + padX, y, m->panel.left + padX + wb, y + S(34) };
    i_btnFolder = (RECT){ i_btnFiles.right + S(12), y, i_btnFiles.right + S(12) + wb, y + S(34) };
    i_btnPaste  = (RECT){ i_btnFolder.right + S(12), y, i_btnFolder.right + S(12) + wb, y + S(34) };
    // 打开库文件夹（下划线文字）
    int ow = (int)gtext_w(LS(LS_IMP_OPEN), -1, g_fSmall);
    i_openRc = (RECT){ m->panel.left + padX, y + S(44), m->panel.left + padX + ow + S(4), y + S(62) };
    // 拖放区：剩余全部（1px 发丝框 + 中央提示）；状态行压底
    i_dropRc.left = m->panel.left + padX;
    i_dropRc.top = i_openRc.bottom + S(12);
    i_dropRc.right = m->panel.right - padX;
    i_dropRc.bottom = m->panel.bottom - S(14) - S(18) - S(10);
}

static void imp_paint(void)
{
    Mgr* m = &g_mgI;
    int padX = S(20);
    mgr_base(m);
    mgr_chrome(m, LS(LS_IMP_TITLE));
    // 库统计：N 张 · M 标签 · X MB
    u64 total = 0;
    for (int i = 0; i < g_store.count; i++) total += g_store.entries[i].size;
    wchar_t sum[96];
    wnsprintfW(sum, 96, LS(LS_IMP_SUM), g_store.count, n_tags(), (u32)((total + 524288) / 1048576));
    gtext(sum, -1, g_fSmall,
          (RECT){ m->panel.left + padX, m->panel.top + S(50), m->panel.right - padX, m->panel.top + S(66) },
          g_sfL, C_INK(140));
    // 三按钮 + 打开库文件夹
    draw_mbtn(i_btnFiles, LS(LS_IMP_FILES), PtInRect(&i_btnFiles, i_mouse), TRUE, FALSE, 0);
    draw_mbtn(i_btnFolder, LS(LS_IMP_FOLDER), PtInRect(&i_btnFolder, i_mouse), TRUE, FALSE, 0);
    draw_mbtn(i_btnPaste, LS(LS_IMP_PASTE), PtInRect(&i_btnPaste, i_mouse), TRUE, FALSE, 0);
    BOOL oh = PtInRect(&i_openRc, i_mouse);
    gtext(LS(LS_IMP_OPEN), -1, g_fSmall, i_openRc, g_sfL, C_INK(oh ? 255 : 140));
    fill_rect(i_openRc.left, i_openRc.bottom - 1, i_openRc.right, i_openRc.bottom, C_INK(oh ? 255 : 140));
    // 拖放区：发丝框 + 中央提示（SWISS：无虚线，用留白表达）
    frame_rect(i_dropRc, 1, C_HAIR);
    int cy = (i_dropRc.top + i_dropRc.bottom) / 2;
    gtext(LS(LS_IMP_DROP), -1, g_fUi,
          (RECT){ i_dropRc.left, cy - S(16), i_dropRc.right, cy }, g_sfC, C_INK(130));
    // 状态行
    gtext(i_status[0] ? i_status : LS(LS_IMP_READY), -1, g_fNano,
          (RECT){ m->panel.left + padX, m->panel.bottom - S(24), m->panel.right - padX, m->panel.bottom - S(8) },
          g_sfL, ARGB(255, 0x77, 0x77, 0x77));
}

static void imp_repaint(void)
{
    Mgr* m = &g_mgI;
    if (!m->bits || !m->gfx) return;
    MgrSwap s;
    mgr_swap_begin(m, &s);
    imp_layout();
    imp_paint();
    mgr_swap_end(&s);
    mgr_ulw(g_hwndImp, m);
}

// 导入核心（v0.10 自 import_drop 抽出；v0.14 内容寻址重写）：
// 文件夹→首个可入库文件时才建同名标签（空文件夹不留空标签）并批量打标；
// 文件→哈希命名复制（同内容自动跳过）；新增/重复/非图片三计数；
// 原名写回 entry->origName → 扫描+修后缀+落盘+刷新+气泡/状态行
static void import_run(HWND hwnd, const wchar_t* const* paths, int n)
{
    (void)hwnd;
    int capGot = n > 16 ? n : 16;   // 文件夹导入时件数远超路径数，got 动态扩容
    Imported* got = (Imported*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (u64)capGot * sizeof(Imported));
    int nGot = 0, nNew = 0, nDup = 0, nSkip = 0, tagId = -1;
    wchar_t pat[MAX_PATH];

    for (int i = 0; i < n && got; i++) {
        const wchar_t* src = paths[i];
        if (!src || !src[0]) continue;
        DWORD at = GetFileAttributesW(src);
        if (at == INVALID_FILE_ATTRIBUTES) continue;
        if (at & FILE_ATTRIBUTE_DIRECTORY) {
            const wchar_t* base = src + wlen(src);
            while (base > src && base[-1] != L'\\') base--;
            tagId = -1;   // 惰性：文件夹里确实有东西入库才建标签
            wnsprintfW(pat, MAX_PATH, L"%s\\*", src);
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW(pat, &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                    wchar_t full[MAX_PATH];
                    wnsprintfW(full, MAX_PATH, L"%s\\%s", src, fd.cFileName);
                    if (nGot == capGot) {
                        Imported* ng = (Imported*)HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                                               got, (u64)capGot * 2 * sizeof(Imported));
                        if (!ng) break;
                        got = ng; capGot *= 2;
                    }
                    int st = copy_into(&g_store, full, got[nGot].dst, MAX_PATH);
                    if (st >= 2) { nSkip++; continue; }   // 2=非图片 3=失败
                    if (tagId < 0) tagId = store_tag_add(&g_store, base);
                    lstrcpynW(got[nGot].orig, fd.cFileName, MAX_PATH);
                    got[nGot].tag = tagId;
                    got[nGot].dup = (st == 1);
                    if (st == 1) nDup++; else nNew++;
                    nGot++;
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
        } else {
            const wchar_t* base = src + wlen(src);
            while (base > src && base[-1] != L'\\') base--;
            if (nGot == capGot) {
                Imported* ng = (Imported*)HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                                       got, (u64)capGot * 2 * sizeof(Imported));
                if (ng) { got = ng; capGot *= 2; }
            }
            if (nGot < capGot) {
                int st = copy_into(&g_store, src, got[nGot].dst, MAX_PATH);
                if (st >= 2) nSkip++;
                else {
                    lstrcpynW(got[nGot].orig, base, MAX_PATH);
                    got[nGot].tag = -1;
                    got[nGot].dup = (st == 1);
                    if (st == 1) nDup++; else nNew++;
                    nGot++;
                }
            }
        }
    }

    store_scan(&g_store);
    maint_fixext();   // 扫描后静默修后缀（QQNT 错乱后缀——哈希名只动扩展名段，身份不变）
    for (int k = 0; k < nGot; k++) {
        int ei = store_find(&g_store, got[k].dst);
        if (ei < 0) continue;
        if (!got[k].dup) {   // 新件写原名；重复件保留既有原名（先到先得）
            wchar_t* d = wdup(got[k].orig);
            if (d) {
                if (g_store.entries[ei].origName) HeapFree(GetProcessHeap(), 0, g_store.entries[ei].origName);
                g_store.entries[ei].origName = d;
            }
        }
        if (got[k].tag >= 0) g_store.entries[ei].tagmask |= (u64)1 << got[k].tag;
    }
    store_save(&g_store);
    if (got) HeapFree(GetProcessHeap(), 0, got);

    refilter();
    recalc_recents();
    if (IsWindowVisible(g_hwnd) || g_headless) panel_repaint();

    wnsprintfW(i_status, 128, LS(LS_BAL_IMPORT), nNew, nDup, nSkip, g_store.count);
    if (IsWindowVisible(g_hwndImp) || g_headless) imp_repaint();
    // 托盘气泡（导入发生时窗口可能已被关掉，气泡兜底告知）
    lstrcpynW(g_nid.szInfoTitle, APP_NAME, 64);
    lstrcpynW(g_nid.szInfo, i_status, 256);
    g_nid.uFlags = NIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
}

// ---- 添加文件（comdlg32 动态加载，零新增导入表；与 ChooseColor 同法）----
static void imp_pick_files(HWND hwnd)
{
    HMODULE cd = LoadLibraryW(L"comdlg32.dll");
    if (!cd) return;
    BOOL (WINAPI *pGOFNW)(OPENFILENAMEW*) =
        (BOOL (WINAPI*)(OPENFILENAMEW*))GetProcAddress(cd, "GetOpenFileNameW");
    if (!pGOFNW) return;
    wchar_t* buf = (wchar_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 65536 * 2);
    if (!buf) return;
    static const wchar_t FLT[] =
        L"图片 (*.png;*.jpg;*.gif;*.webp;*.bmp)\0*.png;*.jpg;*.jpeg;*.gif;*.webp;*.bmp\0"
        L"所有文件 (*.*)\0*.*\0";
    OPENFILENAMEW of;
    memset(&of, 0, sizeof of);
    of.lStructSize = sizeof of;
    of.hwndOwner = hwnd;
    of.lpstrFilter = FLT;
    of.lpstrFile = buf;
    of.nMaxFile = 65536;
    of.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    g_modal = TRUE;   // 模态期间面板失焦不自动收起
    BOOL ok = pGOFNW(&of);
    g_modal = FALSE;
    if (ok) {
        wchar_t** list = (wchar_t**)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 512 * sizeof(wchar_t*));
        if (list) {
            int n = 0;
            wchar_t* p = buf;
            if (!*(p + wlen(p) + 1)) {
                list[n++] = p;                       // 单选：完整路径
            } else {
                wchar_t dir[MAX_PATH];
                lstrcpynW(dir, p, MAX_PATH);
                int dl = wlen(dir);
                if (dl && dir[dl - 1] != L'\\') { dir[dl] = L'\\'; dir[dl + 1] = 0; }
                p += wlen(p) + 1;
                while (*p && n < 512) {
                    wchar_t* full = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, MAX_PATH * 2);
                    if (full) {
                        wnsprintfW(full, MAX_PATH, L"%s%s", dir, p);
                        list[n++] = full;
                    }
                    p += wlen(p) + 1;
                }
            }
            import_run(hwnd, (const wchar_t* const*)list, n);
            for (int i = 0; i < n; i++) if (list[i] != buf) HeapFree(GetProcessHeap(), 0, list[i]);
            HeapFree(GetProcessHeap(), 0, list);
        }
    }
    HeapFree(GetProcessHeap(), 0, buf);
}

// ---- 添加文件夹（shell32 SHBrowseForFolder；BIF_NEWDIALOGSTYLE 需 STA COM）----
typedef struct {
    HWND hwndOwner;
    void* pidlRoot;
    wchar_t* pszDisplayName;
    const wchar_t* lpszTitle;
    UINT ulFlags;
    void* lpfn;
    LPARAM lParam;
    int iImage;
} MPBIW;   // BROWSEINFOW 的 ABI 等价定义（不引入 shlobj.h）
extern void* WINAPI SHBrowseForFolderW(MPBIW* bi);
extern BOOL  WINAPI SHGetPathFromIDListW(void* pidl, wchar_t* path);
// OleInitialize/OleUninitialize：系统头（ole2.h，经 objbase.h 链）已声明，不再自声明

static void imp_pick_folder(HWND hwnd)
{
    wchar_t disp[MAX_PATH], path[MAX_PATH];
    MPBIW bi;
    memset(&bi, 0, sizeof bi);
    bi.hwndOwner = hwnd;
    bi.pszDisplayName = disp;
    bi.lpszTitle = LS(LS_IMP_FOLDER);
    bi.ulFlags = 0x0040 /* BIF_NEWDIALOGSTYLE */ | 0x0001 /* BIF_RETURNONLYFSDIRS */;
    OleInitialize(NULL);
    g_modal = TRUE;
    void* pidl = SHBrowseForFolderW(&bi);
    g_modal = FALSE;
    if (pidl) {
        if (SHGetPathFromIDListW(pidl, path) && path[0]) {
            const wchar_t* one = path;
            import_run(hwnd, &one, 1);
        }
        CoTaskMemFree(pidl);
    }
    OleUninitialize();
}

// ---- 粘贴导入：剪贴板 CF_HDROP（路径列表）或 CF_DIB（位图→PNG 落库）----
static void imp_paste(HWND hwnd)
{
    if (!OpenClipboard(hwnd)) return;
    BOOL did = FALSE;
    HANDLE h = GetClipboardData(CF_HDROP);
    if (h) {
        HDROP hd = (HDROP)h;
        UINT n = DragQueryFileW(hd, 0xFFFFFFFF, NULL, 0);
        wchar_t** list = (wchar_t**)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (u64)(n ? n : 1) * sizeof(wchar_t*));
        if (list) {
            int nReal = 0;
            for (UINT i = 0; i < n; i++) {
                list[nReal] = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, MAX_PATH * 2);
                if (list[nReal] && DragQueryFileW(hd, i, list[nReal], MAX_PATH)) nReal++;
                else if (list[nReal]) { HeapFree(GetProcessHeap(), 0, list[nReal]); list[nReal] = NULL; }
            }
            import_run(hwnd, (const wchar_t* const*)list, nReal);
            for (int i = 0; i < nReal; i++) HeapFree(GetProcessHeap(), 0, list[i]);
            HeapFree(GetProcessHeap(), 0, list);
            did = nReal > 0;
        }
    }
    if (!did) {
        HANDLE hdib = GetClipboardData(CF_DIB);
        if (hdib) {
            u8* dib = (u8*)GlobalLock(hdib);
            if (dib) {
                BITMAPINFOHEADER* bh = (BITMAPINFOHEADER*)dib;
                if ((bh->biBitCount == 24 || bh->biBitCount == 32) &&
                    bh->biCompression == BI_RGB && bh->biWidth > 0 && bh->biHeight != 0) {
                    int w = bh->biWidth;
                    int hh = bh->biHeight > 0 ? bh->biHeight : -bh->biHeight;
                    BOOL bottomUp = bh->biHeight > 0;
                    int stride = ((w * (bh->biBitCount / 8)) + 3) & ~3;
                    u8* px = dib + bh->biSize;   // BI_RGB 24/32bpp：头后即像素，无调色板
                    // → 顶朝下 BGRA（wic_save_png 的输入约定）
                    u8* bgra = (u8*)HeapAlloc(GetProcessHeap(), 0, (u64)w * hh * 4);
                    if (bgra) {
                        for (int y = 0; y < hh; y++) {
                            u8* srow = px + (u64)(bottomUp ? (hh - 1 - y) : y) * stride;
                            u8* drow = bgra + (u64)y * w * 4;
                            for (int x = 0; x < w; x++) {
                                if (bh->biBitCount == 32) {
                                    drow[x * 4] = srow[x * 4];
                                    drow[x * 4 + 1] = srow[x * 4 + 1];
                                    drow[x * 4 + 2] = srow[x * 4 + 2];
                                    drow[x * 4 + 3] = 255;   // 忽略源 alpha（聊天位图多为不透明）
                                } else {
                                    drow[x * 4] = srow[x * 3];
                                    drow[x * 4 + 1] = srow[x * 3 + 1];
                                    drow[x * 4 + 2] = srow[x * 3 + 2];
                                    drow[x * 4 + 3] = 255;
                                }
                            }
                        }
                        // v0.14：先落临时名再哈希收编——粘贴与拖放同一条内容寻址纪律
                        // （同一张截图贴两次只存一份）；st 语义与 copy_into 对齐
                        u32 tick = GetTickCount();
                        wchar_t tmp[MAX_PATH], hn[MAX_PATH], dst[MAX_PATH], pn[48];
                        wnsprintfW(tmp, MAX_PATH, L"%s\\~paste_%u.png", g_store.memesDir, tick);
                        wnsprintfW(pn, 48, L"paste_%u.png", tick);
                        if (wic_save_png(bgra, w, hh, w * 4, tmp)) {
                            u64 h = fnv_file(tmp);
                            WIN32_FILE_ATTRIBUTE_DATA fa;
                            int st = 3;                     // 0=新入库 1=重复 3=失败
                            if (h && GetFileAttributesExW(tmp, GetFileExInfoStandard, &fa)) {
                                u64 sz = ((u64)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
                                st = 0;
                                for (int i = 0; i < g_store.count; i++)
                                    if (g_store.entries[i].hash == h && g_store.entries[i].size == sz) { st = 1; break; }
                                hash_name(hn, MAX_PATH, h, L".png");
                                wnsprintfW(dst, MAX_PATH, L"%s\\%s", g_store.memesDir, hn);
                                if (st == 0 && GetFileAttributesW(dst) != INVALID_FILE_ATTRIBUTES) st = 1;
                                if (st == 0 && !MoveFileW(tmp, dst)) st = 3;
                            }
                            if (st != 0) DeleteFileW(tmp);
                            if (st != 3) {
                                store_scan(&g_store);
                                maint_fixext();
                                if (st == 0) {
                                    int ei = store_find(&g_store, hn);
                                    if (ei >= 0 && !g_store.entries[ei].origName) {
                                        wchar_t* d = wdup(pn);
                                        if (d) g_store.entries[ei].origName = d;
                                    }
                                }
                                store_save(&g_store);
                                refilter();
                                recalc_recents();
                                if (IsWindowVisible(g_hwnd) || g_headless) panel_repaint();
                                wnsprintfW(i_status, 128, LS(LS_BAL_IMPORT),
                                           st == 0 ? 1 : 0, st == 1 ? 1 : 0, 0, g_store.count);
                                imp_repaint();
                                did = TRUE;
                            }
                        }
                        HeapFree(GetProcessHeap(), 0, bgra);
                    }
                }
                GlobalUnlock(hdib);
            }
        }
    }
    CloseClipboard();
    if (!did) {
        lstrcpynW(i_status, LS(LS_IMP_NOPASTE), 128);
        imp_repaint();
    }
}

static void imp_open(HWND hwnd)
{
    (void)hwnd;
    if (!g_hwndImp) return;
    RECT wa;
    work_area_at_cursor(&wa);
    int w = S(460) + g_bleed * 2, h = S(460) + g_bleed * 2;
    SetWindowPos(g_hwndImp, HWND_TOP,
                 wa.left + (wa.right - wa.left - w) / 2,
                 wa.top + (wa.bottom - wa.top - h) / 3,
                 w, h, SWP_SHOWWINDOW);
    SetForegroundWindow(g_hwndImp);
    SetFocus(g_hwndImp);
    if (!g_mgI.bits) imp_size(g_hwndImp);   // v0.17.1 复开冻结修复（同 tags_open）
    imp_repaint();
}

static void imp_close(HWND hwnd)
{
    ShowWindow(hwnd, SW_HIDE);
    mgr_release(&g_mgI);
}

static void imp_size(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    g_mgI.w = rc.right; g_mgI.h = rc.bottom;
    if (g_mgI.w <= 0 || g_mgI.h <= 0) return;
    g_mgI.panel.left = g_bleed; g_mgI.panel.top = g_bleed;
    g_mgI.panel.right = g_mgI.w - g_bleed; g_mgI.panel.bottom = g_mgI.h - g_bleed;
    g_mgI.closeRc.right = g_mgI.panel.right - S(20);
    g_mgI.closeRc.left = g_mgI.closeRc.right - S(26);
    g_mgI.closeRc.top = g_mgI.panel.top + S(10);
    g_mgI.closeRc.bottom = g_mgI.closeRc.top + S(26);
    mgr_surface(&g_mgI);
    if (g_mgI.bits) imp_repaint();
}

static LRESULT CALLBACK ImportProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE:
        imp_size(hwnd);
        return 0;

    case WM_MOUSEMOVE:
        i_mouse.x = (short)LOWORD(lParam);
        i_mouse.y = (short)HIWORD(lParam);
        g_tMouse = i_mouse;   // mgr_chrome 的 ✕ 悬停读 g_tMouse——不回写则导入窗 ✕ 悬停死（v0.16 修）
        {
            BOOL hand = PtInRect(&g_mgI.closeRc, i_mouse) ||
                        PtInRect(&i_btnFiles, i_mouse) || PtInRect(&i_btnFolder, i_mouse) ||
                        PtInRect(&i_btnPaste, i_mouse) || PtInRect(&i_openRc, i_mouse);
            BOOL grab = mgr_drag_zone(&g_mgI, i_mouse);   // v0.11 刊头可拖
            SetCursor(LoadCursorW(NULL, grab ? IDC_SIZEALL : (hand ? IDC_HAND : IDC_ARROW)));
        }
        imp_repaint();
        return 0;
    case WM_MOUSELEAVE:
        i_mouse.x = i_mouse.y = -30000;
        imp_repaint();
        return 0;

    case WM_LBUTTONDOWN: {   // v0.11 刊头带拖动（不记位置）
        POINT pt;
        pt.x = (short)LOWORD(lParam);
        pt.y = (short)HIWORD(lParam);
        if (mgr_drag_zone(&g_mgI, pt)) {
            SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            return 0;
        }
        break;
    }

    case WM_MOUSEWHEEL:   // v0.11 Ctrl+滚轮 = 不透明度（导入窗无列表滚动）
        if (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) {
            opacity_set((int)g_opacity + (GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? OPACITY_STEP : -OPACITY_STEP));
            return 0;
        }
        return 0;

    case WM_LBUTTONUP: {
        POINT pt;
        pt.x = (short)LOWORD(lParam);
        pt.y = (short)HIWORD(lParam);
        if (PtInRect(&g_mgI.closeRc, pt)) { imp_close(hwnd); return 0; }
        if (PtInRect(&i_btnFiles, pt))  { imp_pick_files(hwnd); return 0; }
        if (PtInRect(&i_btnFolder, pt)) { imp_pick_folder(hwnd); return 0; }
        if (PtInRect(&i_btnPaste, pt))  { imp_paste(hwnd); return 0; }
        if (PtInRect(&i_openRc, pt)) {
            g_modal = TRUE;
            ShellExecuteW(hwnd, L"open", L"explorer.exe", g_store.memesDir, NULL, SW_SHOWNORMAL);
            g_modal = FALSE;
            return 0;
        }
        return 0;
    }

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) { imp_close(hwnd); return 0; }
        if (wParam == 'V' && GetKeyState(VK_CONTROL) < 0) { imp_paste(hwnd); return 0; }
        return 0;

    case WM_DROPFILES:
        import_drop(hwnd, (HDROP)wParam);
        return 0;

    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================
// 开机自启（v0.10）：HKCU\...\Run 值 "MemePanel" = "exe路径"（无 -show = 静默驻留托盘）。
// advapi32 全动态加载（零新增导入表，与 comdlg32 同纪律）。
// winreg.h 不随 WIN32_LEAN_AND_MEAN 引入：常量自带。
// ============================================================
#define MP_HKCU         ((HKEY)(uintptr_t)0x80000001)
#define MP_KEY_QUERY_   0x0001
#define MP_KEY_SETVALUE 0x0002
#define MP_REG_SZ_      1

typedef LSTATUS (WINAPI *MPRegOpen)(HKEY, LPCWSTR, DWORD, DWORD, PHKEY);
typedef LSTATUS (WINAPI *MPRegQuery)(HKEY, LPCWSTR, DWORD*, DWORD*, BYTE*, DWORD*);
typedef LSTATUS (WINAPI *MPRegSet)(HKEY, LPCWSTR, DWORD, DWORD, const BYTE*, DWORD);
typedef LSTATUS (WINAPI *MPRegDel)(HKEY, LPCWSTR);
typedef LSTATUS (WINAPI *MPRegClose)(HKEY);

static BOOL autostart_get(void)
{
    HMODULE adv = LoadLibraryW(L"advapi32.dll");
    if (!adv) return FALSE;
    MPRegOpen  op = (MPRegOpen)GetProcAddress(adv, "RegOpenKeyExW");
    MPRegQuery qr = (MPRegQuery)GetProcAddress(adv, "RegQueryValueExW");
    MPRegClose cl = (MPRegClose)GetProcAddress(adv, "RegCloseKey");
    if (!op || !qr || !cl) return FALSE;
    HKEY k = NULL;
    BOOL on = FALSE;
    if (op(MP_HKCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, MP_KEY_QUERY_, &k) == 0) {
        on = (qr(k, L"MemePanel", NULL, NULL, NULL, NULL) == 0);
        cl(k);
    }
    return on;
}

static void autostart_set(BOOL on)
{
    HMODULE adv = LoadLibraryW(L"advapi32.dll");
    if (!adv) return;
    MPRegOpen  op = (MPRegOpen)GetProcAddress(adv, "RegOpenKeyExW");
    MPRegSet   st = (MPRegSet)GetProcAddress(adv, "RegSetValueExW");
    MPRegDel   dl = (MPRegDel)GetProcAddress(adv, "RegDeleteValueW");
    MPRegClose cl = (MPRegClose)GetProcAddress(adv, "RegCloseKey");
    if (!op || !st || !dl || !cl) return;
    HKEY k = NULL;
    if (op(MP_HKCU, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, MP_KEY_SETVALUE, &k) != 0)
        return;
    if (on) {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(NULL, exe, MAX_PATH);
        st(k, L"MemePanel", 0, MP_REG_SZ_, (const BYTE*)exe, (DWORD)((wlen(exe) + 1) * 2));
    } else {
        dl(k, L"MemePanel");
    }
    cl(k);
}

// ============================================================
// v0.16 设置窗口：偏好项集中地（托盘瘦身为入口集后的设置中心）。
// 外观（主题色 swatch + 自定义 / 不透明度六档 / 最近行格数）·
// 行为（语言 / 开机自启 / 呼出跟随光标 / 搜索包含文件名）·
// 库（重新扫描 / 去重扫描 / 标签管理入口）。
// 复用 Mgr 管理窗框架与全部既有动作函数；无文本输入 → 无 IME。
// ============================================================
static POINT s_mouse = { -30000, -30000 };
static RECT s_swRc[5], s_customRc, s_opRc[8];
static RECT s_tglRc[5], s_chkRc[5];               // [0]语言 [1]自启 [2]跟随光标 [3]文件名搜索 [4]最近行(开关)
static RECT s_btnRescan, s_btnDedup, s_btnTags;
static int  s_yLook, s_yBehav, s_yLib, s_yTheme, s_yOpac;
static wchar_t s_status[128];                      // v0.16.1 库操作反馈（去重/重扫结果就地可见——不再只靠托盘气泡）

static void set_layout(void)
{
    Mgr* m = &g_mgS;
    s_swRc[0] = s_customRc = s_opRc[0] = s_tglRc[0] = s_chkRc[0] =
    s_btnRescan = s_btnDedup = s_btnTags = (RECT){ 0, 0, 0, 0 };
    s_yLook = s_yBehav = s_yLib = s_yTheme = s_yOpac = 0;
    if (!m->bits || !m->gfx) return;
    int padX = S(20);
    int L = m->panel.left + padX, R = m->panel.right - padX;
    int y = m->panel.top + S(38) + S(2) + S(14);

    // ---- 外观 ----
    s_yLook = y; y += S(22);
    s_yTheme = y; y += S(18);                       // 「主题色」小标
    int sw = S(26);
    for (int i = 0; i < 5; i++)
        s_swRc[i] = (RECT){ L + i * (sw + S(10)), y, L + i * (sw + S(10)) + sw, y + sw };
    int cw = (int)gtext_w(LS(LS_THEME_CUSTOM), -1, g_fUi) + S(28);
    s_customRc = (RECT){ s_swRc[4].right + S(14), y, s_swRc[4].right + S(14) + cw, y + sw };
    y += sw + S(18);
    s_yOpac = y; y += S(18);                        // 「不透明度」小标
    int ox = L;
    for (int i = 0; i < 8; i++) {                   // v0.16.1 八档（100..30%），合计 ~350px 一行放下
        wchar_t lb[8];
        wnsprintfW(lb, 8, L"%d%%", (OP_BUCKETS[i] * 100 + 127) / 255);   /* v0.16.1 四舍五入：127/255 显示 50% 而非 49% */
        int w = (int)gtext_w(lb, -1, g_fUi) + S(12);
        s_opRc[i] = (RECT){ ox, y, ox + w, y + S(24) };
        ox += w + S(5);
    }
    y += S(24) + S(16);

    // ---- 行为：五开关行（v0.16.2：最近行数量档退役，只剩开关；槽位=网格列数固定一行，满了轮换）----
    s_yBehav = y; y += S(22);
    for (int i = 0; i < 5; i++) {
        s_tglRc[i] = (RECT){ L, y, R, y + S(30) };
        s_chkRc[i] = (RECT){ R - S(18), y + S(7), R, y + S(23) };
        y += S(30);
    }
    y += S(16);

    // ---- 库：三按钮 ----
    s_yLib = y; y += S(22);
    int bx = L;
    const wchar_t* bt[3] = { LS(LS_TRAY_RESCAN), LS(LS_TRAY_DEDUP), LS(LS_TRAY_TAGS) };
    RECT* br[3] = { &s_btnRescan, &s_btnDedup, &s_btnTags };
    for (int i = 0; i < 3; i++) {
        int w = (int)gtext_w(bt[i], -1, g_fUi) + S(28);
        *br[i] = (RECT){ bx, y, bx + w, y + S(30) };
        bx += w + S(12);
    }
}

// 开关行绘制：左标签 + 右 16px 方块（accent 实心 = 开；细框 = 关；行 hover = 标签下划线）
static void draw_toggle(int i, const wchar_t* label, BOOL on)
{
    BOOL hov = PtInRect(&s_tglRc[i], s_mouse);
    gtext(label, -1, g_fUi, s_tglRc[i], g_sfL, C_INK(255));
    if (hov) fill_rect(s_tglRc[i].left, s_tglRc[i].bottom - 2, s_tglRc[i].left + (int)gtext_w(label, -1, g_fUi) + S(2), s_tglRc[i].bottom, C_INK(255));
    if (on) fill_rect(s_chkRc[i].left, s_chkRc[i].top, s_chkRc[i].right, s_chkRc[i].bottom, C_RED(255));
    else    frame_rect(s_chkRc[i], hov ? 2 : 1, C_INK(hov ? 255 : 150));
}

// 小 chip（数字/百分比）：激活 = accent 实底白字（主题色标记"选中态"——对齐面板激活胶囊语言）；
// 否则细框；hover 加粗框
static void draw_chipbox(RECT rc, const wchar_t* txt, BOOL on, BOOL hov)
{
    if (on) fill_rect(rc.left, rc.top, rc.right, rc.bottom, C_RED(255));
    else    frame_rect(rc, hov ? 2 : 1, C_INK(hov ? 255 : 150));
    gtext(txt, -1, g_fUi, rc, g_sfC, on ? ARGB(255, 250, 250, 249) : C_INK(255));
}

static void set_paint(void)
{
    Mgr* m = &g_mgS;
    mgr_base(m);
    mgr_chrome(m, LS(LS_SET_TITLE));
    int padX = S(20);
    int L = m->panel.left + padX, R = m->panel.right - padX;

    // ---- 外观 ----
    gtext(LS(LS_SEC_LOOK), -1, g_fSect, (RECT){ L, s_yLook, R, s_yLook + S(16) }, g_sfL, C_INK(255));
    gtext(LS(LS_TRAY_THEME),   -1, g_fSmall, (RECT){ L, s_yTheme, R, s_yTheme + S(14) }, g_sfL, C_INK(140));
    gtext(LS(LS_TRAY_OPACITY), -1, g_fSmall, (RECT){ L, s_yOpac,  R, s_yOpac + S(14) },  g_sfL, C_INK(140));
    for (int i = 0; i < 5; i++) {                   // swatch：预设色块；当前主题 = accent 框（主题色标选中）
        RECT rc = s_swRc[i];
        fill_rect(rc.left, rc.top, rc.right, rc.bottom, ARGB(255, GetRValue(THEME_PRESETS[i]), GetGValue(THEME_PRESETS[i]), GetBValue(THEME_PRESETS[i])));
        BOOL cur = (g_accent == THEME_PRESETS[i]);
        BOOL hov = PtInRect(&rc, s_mouse);
        frame_rect((RECT){ rc.left - 2, rc.top - 2, rc.right + 2, rc.bottom + 2 }, cur ? 2 : (hov ? 2 : 1),
                   cur ? C_RED(255) : C_INK(hov ? 255 : 120));
    }
    draw_mbtn(s_customRc, LS(LS_THEME_CUSTOM), PtInRect(&s_customRc, s_mouse), TRUE, FALSE, 0);
    int best = 0, bd = 255;                         // 当前值离哪档最近（Ctrl+滚轮微调后仍标得对）
    for (int j = 0; j < 8; j++) {
        int d = (int)g_opacity - OP_BUCKETS[j];
        if (d < 0) d = -d;
        if (d < bd) { bd = d; best = j; }
    }
    for (int i = 0; i < 8; i++) {
        wchar_t lb[8];
        wnsprintfW(lb, 8, L"%d%%", (OP_BUCKETS[i] * 100 + 127) / 255);   /* v0.16.1 四舍五入：127/255 显示 50% 而非 49% */
        draw_chipbox(s_opRc[i], lb, best == i, PtInRect(&s_opRc[i], s_mouse));
    }

    // ---- 行为 ----
    gtext(LS(LS_SEC_BEHAV), -1, g_fSect, (RECT){ L, s_yBehav, R, s_yBehav + S(16) }, g_sfL, C_INK(255));
    draw_toggle(0, LS(LS_TRAY_LANG),        g_lang != 0);            // 词条显示当前语言名
    draw_toggle(1, LS(LS_TRAY_AUTOSTART),   autostart_get());
    draw_toggle(2, LS(LS_TRAY_FOLLOWCUR),   (g_prefs & PF_FOLLOWCURSOR) != 0);
    draw_toggle(3, LS(LS_TRAY_NAMESEARCH),  (g_prefs & PF_NAMESEARCH) != 0);
    draw_toggle(4, LS(LS_TRAY_RECENT), g_recentOn);   // v0.16.2：开关（槽位=网格列数，满了轮换）

    // ---- 库 ----
    gtext(LS(LS_SEC_LIB), -1, g_fSect, (RECT){ L, s_yLib, R, s_yLib + S(16) }, g_sfL, C_INK(255));
    draw_mbtn(s_btnRescan, LS(LS_TRAY_RESCAN), PtInRect(&s_btnRescan, s_mouse), TRUE, FALSE, 0);
    draw_mbtn(s_btnDedup,  LS(LS_TRAY_DEDUP),  PtInRect(&s_btnDedup,  s_mouse), TRUE, FALSE, 0);
    draw_mbtn(s_btnTags,   LS(LS_TRAY_TAGS),   PtInRect(&s_btnTags,   s_mouse), TRUE, FALSE, 0);
    // 状态行（导入窗同款）：去重/重扫结果就地可见——托盘气泡会被系统通知设置吞掉
    if (s_status[0])
        gtext(s_status, -1, g_fNano,
              (RECT){ L, m->panel.bottom - S(24), R, m->panel.bottom - S(8) },
              g_sfL, ARGB(255, 0x77, 0x77, 0x77));
}

static void set_repaint(void)
{
    Mgr* m = &g_mgS;
    if (!m->bits || !m->gfx) return;
    MgrSwap s;
    mgr_swap_begin(m, &s);
    set_layout();
    set_paint();
    mgr_swap_end(&s);
    mgr_ulw(g_hwndSet, m);
}

// 语言切换（设置窗入口；词条即时生效 + 托盘 tip 同步 + 可见窗全刷）
static void set_lang_switch(void)
{
    g_lang ^= 1;
    if (g_lang) g_prefs |= PF_ENGLISH; else g_prefs &= ~PF_ENGLISH;
    prefs_save();
    lstrcpynW(g_nid.szTip, LS(LS_TIP), 128);
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    if (IsWindowVisible(g_hwnd)) panel_repaint();
    if (IsWindowVisible(g_hwndTags)) tags_repaint();
    if (IsWindowVisible(g_hwndImp)) imp_repaint();
    set_repaint();
}

// 重扫（设置窗入口；与旧托盘路径同源：scan+fixext+refilter+气泡；结果同时写设置窗状态行）
static void set_rescan(void)
{
    store_scan(&g_store);
    maint_fixext();
    refilter();
    recalc_recents();
    if (IsWindowVisible(g_hwnd)) panel_repaint();
    wnsprintfW(s_status, 128, LS(LS_BAL_SCAN), g_store.count);
    lstrcpynW(g_nid.szInfoTitle, APP_NAME, 64);
    wchar_t info[96];
    wnsprintfW(info, 96, LS(LS_BAL_SCAN), g_store.count);
    lstrcpynW(g_nid.szInfo, info, 256);
    g_nid.uFlags = NIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
}

static void set_open(HWND hwnd)
{
    (void)hwnd;
    if (!g_hwndSet) return;
    RECT wa;
    work_area_at_cursor(&wa);
    int w = S(420) + g_bleed * 2, h = S(560) + g_bleed * 2;
    SetWindowPos(g_hwndSet, HWND_TOP,
                 wa.left + (wa.right - wa.left - w) / 2,
                 wa.top + (wa.bottom - wa.top - h) / 3,
                 w, h, SWP_SHOWWINDOW);
    SetForegroundWindow(g_hwndSet);
    SetFocus(g_hwndSet);
    if (!g_mgS.bits) set_size(g_hwndSet);   // v0.17.1 复开冻结修复（同 tags_open）
    set_repaint();
}

static void set_close(HWND hwnd)
{
    ShowWindow(hwnd, SW_HIDE);
    mgr_release(&g_mgS);
}

static void set_size(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    g_mgS.w = rc.right; g_mgS.h = rc.bottom;
    if (g_mgS.w <= 0 || g_mgS.h <= 0) return;
    g_mgS.panel.left = g_bleed; g_mgS.panel.top = g_bleed;
    g_mgS.panel.right = g_mgS.w - g_bleed; g_mgS.panel.bottom = g_mgS.h - g_bleed;
    g_mgS.closeRc.right = g_mgS.panel.right - S(20);
    g_mgS.closeRc.left = g_mgS.closeRc.right - S(26);
    g_mgS.closeRc.top = g_mgS.panel.top + S(10);
    g_mgS.closeRc.bottom = g_mgS.closeRc.top + S(26);
    mgr_surface(&g_mgS);
    if (g_mgS.bits) set_repaint();
}

static LRESULT CALLBACK SetProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE:
        set_size(hwnd);
        return 0;

    case WM_MOUSEMOVE:
        s_mouse.x = (short)LOWORD(lParam);
        s_mouse.y = (short)HIWORD(lParam);
        g_tMouse = s_mouse;   // mgr_chrome 的 ✕ 悬停读 g_tMouse（见 ImportProc 同修）
        {
            BOOL hand = PtInRect(&g_mgS.closeRc, s_mouse) ||
                        PtInRect(&s_customRc, s_mouse) ||
                        PtInRect(&s_btnRescan, s_mouse) || PtInRect(&s_btnDedup, s_mouse) ||
                        PtInRect(&s_btnTags, s_mouse);
            for (int i = 0; !hand && i < 5; i++) hand = PtInRect(&s_swRc[i], s_mouse);
            for (int i = 0; !hand && i < 8; i++) hand = PtInRect(&s_opRc[i], s_mouse);
            for (int i = 0; !hand && i < 5; i++) hand = PtInRect(&s_tglRc[i], s_mouse);
            BOOL grab = mgr_drag_zone(&g_mgS, s_mouse);   // v0.11 刊头可拖
            SetCursor(LoadCursorW(NULL, grab ? IDC_SIZEALL : (hand ? IDC_HAND : IDC_ARROW)));
        }
        set_repaint();
        return 0;
    case WM_MOUSELEAVE:
        s_mouse.x = s_mouse.y = -30000;
        set_repaint();
        return 0;

    case WM_LBUTTONDOWN: {   // v0.11 刊头带拖动（不记位置）
        POINT pt;
        pt.x = (short)LOWORD(lParam);
        pt.y = (short)HIWORD(lParam);
        if (mgr_drag_zone(&g_mgS, pt)) {
            SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            return 0;
        }
        break;
    }

    case WM_MOUSEWHEEL:   // Ctrl+滚轮 = 不透明度（与其余两管理窗一致）
        if (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) {
            opacity_set((int)g_opacity + (GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? OPACITY_STEP : -OPACITY_STEP));
            set_repaint();
            return 0;
        }
        return 0;

    case WM_LBUTTONUP: {
        POINT pt;
        pt.x = (short)LOWORD(lParam);
        pt.y = (short)HIWORD(lParam);
        if (PtInRect(&g_mgS.closeRc, pt)) { set_close(hwnd); return 0; }
        for (int i = 0; i < 5; i++)
            if (PtInRect(&s_swRc[i], pt)) { theme_apply(THEME_PRESETS[i]); set_repaint(); return 0; }
        if (PtInRect(&s_customRc, pt)) {
            COLORREF pick = g_accent;
            if (pick_custom_color(hwnd, &pick)) theme_apply(pick);
            set_repaint();
            return 0;
        }
        for (int i = 0; i < 8; i++)
            if (PtInRect(&s_opRc[i], pt)) { opacity_set(OP_BUCKETS[i]); set_repaint(); return 0; }
        if (PtInRect(&s_tglRc[0], pt)) { set_lang_switch(); return 0; }
        if (PtInRect(&s_tglRc[1], pt)) { autostart_set(!autostart_get()); set_repaint(); return 0; }
        if (PtInRect(&s_tglRc[2], pt)) { g_prefs ^= PF_FOLLOWCURSOR; prefs_save(); set_repaint(); return 0; }
        if (PtInRect(&s_tglRc[3], pt)) {
            g_prefs ^= PF_NAMESEARCH;
            prefs_save();
            refilter();
            if (IsWindowVisible(g_hwnd)) panel_repaint();
            set_repaint();
            return 0;
        }
        if (PtInRect(&s_tglRc[4], pt)) {   // v0.16.2：最近行开关（槽位由布局派生，满了轮换）
            g_recentOn = !g_recentOn;
            prefs_save();
            panel_layout(g_hwnd);
            recalc_recents();
            if (IsWindowVisible(g_hwnd)) panel_repaint();
            set_repaint();
            return 0;
        }
        if (PtInRect(&s_btnRescan, pt)) { set_rescan(); set_repaint(); return 0; }
        if (PtInRect(&s_btnDedup,  pt)) {
            int marked = maint_dedup(g_hwnd);
            wnsprintfW(s_status, 128, LS(marked ? LS_SET_DEDUP_OK : LS_BAL_NODUP), marked);
            set_repaint();
            return 0;
        }
        if (PtInRect(&s_btnTags,   pt)) { tags_open(hwnd); return 0; }
        return 0;
    }

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) { set_close(hwnd); return 0; }
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
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
            if (g_fadeOut) {                       // v0.9 关闭淡出：递减到 0 才真隐藏
                g_fade -= 52;
                if (g_fade <= 0) {
                    g_fade = 0;
                    KillTimer(hwnd, ANIM_TIMER);
                    fade_frame();                  // 末帧全透明
                    hide_finish(hwnd);
                    return 0;
                }
                fade_frame();
                return 0;
            }
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
        if (wParam == OPACITY_TMR) {   // v0.11 透明度读数 900ms 自灭
            KillTimer(hwnd, OPACITY_TMR);
            g_opacityHud = FALSE;
            if (IsWindowVisible(hwnd)) panel_repaint();
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

    // ---- 键盘：导航 + 自绘输入框编辑（v3 三缓冲路由；v0.12 纯键盘全覆盖）----
    case WM_KEYDOWN:
        if (g_fadeOut) return 0;   // 淡出中：面板将散，不响应
        if (g_complen) break;   // IME 组合中：Enter/Esc/方向键/退格全部交给输入法，不触发面板行为
        // ---- v0.12 Esc 分层（置于所有焦点前置块之前，任何层级都能退出）：
        // chips 焦点 → 回索引框（批量无索引框则回搜索）；标签行焦点 → 回搜索；
        // 抽屉输入框 → 回搜索框（新标签输入收起）；否则抽屉开 → 关抽屉；否则隐藏面板
        if (wParam == VK_ESCAPE) {
            if (g_chipFocus >= 0) {
                g_chipFocus = -1;
                edit_set_focus(g_dIndexBox.right > g_dIndexBox.left ? EDIT_INDEX : EDIT_SEARCH);
                panel_repaint();
            }
            else if (g_tagFocus >= 0) {
                g_tagFocus = -1;
                anim_request(hwnd, (g_hover >= 0 && g_hover < g_nFilt) ? g_filt[g_hover] : -1);   // v0.13 网格框恢复，动画同恢复
                panel_repaint();
            }
            else if (g_recentFocus >= 0) {
                g_recentFocus = -1;
                anim_request(hwnd, (g_hover >= 0 && g_hover < g_nFilt) ? g_filt[g_hover] : -1);
                panel_repaint();
            }
            else if (g_editFocus != EDIT_SEARCH) {
                edit_set_focus(EDIT_SEARCH);
                panel_repaint();
            }
            else if (g_drawer) drawer_set_open(hwnd, FALSE);
            else panel_hide(hwnd);
            return 0;
        }
        // ---- v0.12 动作键层：面向当前键盘选中对象（抽屉目标 > 网格选中框）----
        {
            BOOL ctrl = GetKeyState(VK_CONTROL) < 0;
            if (wParam == VK_APPS || (ctrl && wParam == 'I')) {   // 开/重指向抽屉（= 右键）
                int ei = kbd_ei();
                if (ei >= 0) {
                    drawer_open_for(hwnd, ei);   // 选择集含选中格且≥2 → 批量
                    if (g_drawer && g_drawerEi >= 0 && g_dIndexBox.right > g_dIndexBox.left) {
                        edit_set_focus(EDIT_INDEX);   // v0.13 单目标：键盘打开即索引编辑上下文（同 Tab）
                        g_caretOn = TRUE;
                        ime_update_position(hwnd);
                    }
                }
                return 0;
            }
            if (wParam == VK_F2) {   // v0.15 重命名：单目标抽屉（未开则开键盘选中格）→ 落名称框
                int ei = (g_drawer && g_drawerEi >= 0) ? g_drawerEi
                       : (g_drawer ? -1 : kbd_ei());   // 批量态无单名，忽略
                if (ei >= 0) {
                    if (!g_drawer || g_drawerEi != ei) drawer_show_single(hwnd, ei);
                    edit_set_focus(g_dNameRc.right > g_dNameRc.left ? EDIT_NAME : EDIT_SEARCH);
                    g_caretOn = TRUE;
                    ime_update_position(hwnd);
                    panel_repaint();
                }
                return 0;
            }
            if (ctrl && wParam == 'D') {                         // 收藏 toggle（= 抽屉 ★）
                Entry* t = kbd_target();
                if (t) {
                    int ei = (int)(t - g_store.entries);
                    t->flags ^= MP_FLAG_FAV;
                    store_save(&g_store);
                    refilter();          // 收藏前置排序即时生效（与鼠标路径同源）
                    for (int i = 0; i < g_nFilt; i++)            // 选中框跟随同一张图（重排防误伤）
                        if (g_filt[i] == ei) { g_hover = i; break; }
                    panel_repaint();
                }
                return 0;
            }
            if (ctrl && wParam == VK_SPACE) {                    // 多选 toggle（= Ctrl+点击）
                if (g_hover >= 0 && g_hover < g_nFilt) {
                    sel_set(g_filt[g_hover], !sel_has(g_filt[g_hover]));
                    panel_repaint();
                }
                return 0;
            }
            if (ctrl && wParam == VK_DELETE) {                   // 武装删除两段式（= 抽屉删除钮）
                if (g_drawer) drawer_delete(hwnd);   // 已开：未武装→武装；已武装→真删
                else if (g_hover >= 0 && g_hover < g_nFilt) {    // 未开：先开抽屉看清对象再武装
                    drawer_open_for(hwnd, g_filt[g_hover]);
                    g_delArm = TRUE;
                    SetTimer(hwnd, ARM_TMR, 3000, NULL);
                    panel_repaint();
                }
                else if (g_nSel > 0) {                           // 无选中框但有选择集 → 批量
                    drawer_show_batch(hwnd);
                    g_delArm = TRUE;
                    SetTimer(hwnd, ARM_TMR, 3000, NULL);
                    panel_repaint();
                }
                return 0;
            }
        }
        // ---- v0.13 Tab = 抽屉展开/收起（用户定案；原 Tab 焦点循环退役——标签行有 ↑/←
        //      贯通入口，抽屉内改空间模型：打开落索引框 → ↑ 进 chips → ↓ 回索引框 → Esc 逐层退）。
        //      单目标打开即落索引框 = 编辑上下文（对齐鼠标点击索引框），批量则焦点留搜索 ----
        if (wParam == VK_TAB) {
            g_tagFocus = g_chipFocus = g_recentFocus = -1;   // 行焦点让位
            if (g_drawer) {
                drawer_set_open(hwnd, FALSE);
            }
            else {
                int ei = kbd_ei();
                if (ei >= 0) {
                    drawer_open_for(hwnd, ei);
                    if (g_drawer && g_drawerEi >= 0 && g_dIndexBox.right > g_dIndexBox.left) {
                        edit_set_focus(EDIT_INDEX);
                        g_caretOn = TRUE;
                        ime_update_position(hwnd);
                    }
                }
            }
            if (g_hover >= 0 && g_hover < g_nFilt) anim_request(hwnd, g_filt[g_hover]);   // 行焦点清了，网格动画恢复
            else anim_request(hwnd, -1);
            panel_repaint();
            return 0;
        }
        // ---- v0.12 最近行键盘焦点：←→ 跳空格移动 / Enter 即发 / ↑ 继续到标签行 / ↓ 回网格 ----
        if (g_recentFocus >= 0) {
            if (g_recentFocus >= g_nRecent) g_recentFocus = g_nRecent - 1;   // 防御（recentN 调小后）
            if (g_recentFocus < 0) { panel_repaint(); return 0; }   // v0.18.2：集已空（如键盘流删光最近项）——清焦点早退，杜绝 -1 下标
            switch (wParam) {
            case VK_LEFT:
                for (int i = g_recentFocus - 1; i >= 0; i--)
                    if (g_recentIdx[i] >= 0) { g_recentFocus = i; break; }
                anim_request(hwnd, g_recentIdx[g_recentFocus]);   // v0.13 焦点格动画
                break;
            case VK_RIGHT:
                for (int i = g_recentFocus + 1; i < g_nRecent; i++)
                    if (g_recentIdx[i] >= 0) { g_recentFocus = i; break; }
                anim_request(hwnd, g_recentIdx[g_recentFocus]);
                break;
            case VK_UP:   // 空间贯通：标签行 ← 最近行 ← 网格首行
                g_recentFocus = -1;
                anim_request(hwnd, -1);
                if (g_nChips > 0) g_tagFocus = tag_focus_entry();
                break;
            case VK_DOWN:
                g_recentFocus = -1;   // 回网格（g_hover 保留原选中框）
                if (g_hover >= 0 && g_hover < g_nFilt) anim_request(hwnd, g_filt[g_hover]);
                else anim_request(hwnd, -1);
                break;
            case VK_RETURN:
                if (g_recentIdx[g_recentFocus] >= 0)
                    paste_entry(hwnd, g_recentIdx[g_recentFocus]);   // 发送后面板隐藏
                return 0;
            default:
                break;
            }
            panel_repaint();
            return 0;
        }
        // ---- v0.12 标签行键盘焦点：←→ 移动 / Enter·空格 筛选 / ↓ 回最近行或网格 ----
        if (g_tagFocus >= 0) {
            if (g_tagFocus >= g_nChips) g_tagFocus = g_nChips - 1;   // 防御（标签增删后）
            if (g_tagFocus < 0) { panel_repaint(); return 0; }   // v0.18.2：可见集为空——早退，杜绝 g_chipId[-1]
            switch (wParam) {
            case VK_LEFT:  if (g_tagFocus > 0) g_tagFocus--; break;
            case VK_RIGHT: if (g_tagFocus < g_nChips - 1) g_tagFocus++; break;
            case VK_DOWN:  g_tagFocus = -1;   // 空间连续：↓ 先落最近行（无则回网格）
                if (g_nRecent > 0) {
                    g_recentFocus = 0;
                    anim_request(hwnd, g_recentIdx[0]);
                }
                else if (g_hover >= 0 && g_hover < g_nFilt) anim_request(hwnd, g_filt[g_hover]);
                else anim_request(hwnd, -1);
                break;
            case VK_RETURN:
            case VK_SPACE: {                     // 与鼠标点击 chips 同逻辑
                int id = g_chipId[g_tagFocus];
                if (id == -1) g_tagFilter = 0;
                else g_tagFilter = (g_tagFilter == ((u64)1 << id)) ? 0 : ((u64)1 << id);
                refilter();
                panel_repaint();
                // v0.18.2：repaint 后按 id 锚回焦点（激活 chip 变宽会使可见/注册集变化，
                // 旧下标失义——tag_focus_entry() 读的是上一帧注册集，曾出现焦点线错位一帧）
                g_tagFocus = -1;
                for (int i = 0; i < g_nChips; i++)
                    if (g_chipId[i] == id) { g_tagFocus = i; break; }
                if (g_tagFocus >= 0) panel_repaint();   // 焦点线入画（同步两连提交，无中间帧）
                return 0;
            }
            default: break;
            }
            panel_repaint();
            return 0;
        }
        // ---- v0.12 抽屉 chips 键盘焦点：←→ 移动 / Enter·空格 打去标签 / Esc·Tab 离开 ----
        if (g_chipFocus >= 0) {
            if (g_chipFocus >= g_nDChips) g_chipFocus = g_nDChips - 1;
            if (g_chipFocus < 0) { panel_repaint(); return 0; }   // v0.18.2：同款早退，杜绝 g_dChipId[-1]
            switch (wParam) {
            case VK_LEFT:  if (g_chipFocus > 0) g_chipFocus--; break;
            case VK_RIGHT: if (g_chipFocus < g_nDChips - 1) g_chipFocus++; break;
            case VK_DOWN:  g_chipFocus = -1;   // v0.13 空间模型：↓ 回索引框（批量无索引框回搜索）
                edit_set_focus(g_dIndexBox.right > g_dIndexBox.left ? EDIT_INDEX : EDIT_SEARCH);
                break;
            case VK_RETURN:
            case VK_SPACE: {
                int id = g_dChipId[g_chipFocus];
                if (id == -2) {                  // + 新标签 → 原地展开内联输入
                    g_newtagOpen = TRUE;
                    g_chipFocus = -1;
                    edit_set_focus(EDIT_NEWTAG);
                    g_caretOn = TRUE;
                }
                else drawer_toggle_tag(id);      // 单/批量语义与点击同源
                break;
            }
            default: break;
            }
            panel_repaint();
            return 0;
        }
        switch (wParam) {
        case VK_RETURN:
            if (g_editFocus == EDIT_NEWTAG) { newtag_commit(hwnd); return 0; }
            if (g_editFocus == EDIT_INDEX || g_editFocus == EDIT_NAME) { edit_set_focus(EDIT_SEARCH); panel_repaint(); return 0; }
            // 发送：键盘选中项优先，否则首个可见项；空库 = 开导入窗（对齐鼠标点击网格）
            if (g_nFilt > 0) {
                int idx = (g_hover >= 0 && g_hover < g_nFilt) ? g_hover : g_first;
                paste_entry(hwnd, g_filt[idx]);
            }
            else imp_open(hwnd);
            return 0;
        case VK_PRIOR: g_first -= g_rows * g_cols; clamp_first(); panel_repaint(); return 0;
        case VK_NEXT:  g_first += g_rows * g_cols; clamp_first(); panel_repaint(); return 0;
        // 方向键：搜索焦点 = 网格导航（键盘选中框）；首行 ↑ / 首格 ← 贯通进标签行；
        // 抽屉输入焦点 = caret 移动
        case VK_UP:
            if (g_editFocus == EDIT_SEARCH) {
                if (g_nChips > 0 && g_hover >= 0 && g_hover < g_cols) {
                    // 首行 ↑ 贯通：有最近行 → 列对齐落最近行（clamp 到末格）；否则直接标签行
                    if (g_nRecent > 0) {
                        g_recentFocus = g_hover % g_cols;
                        if (g_recentFocus >= g_nRecent) g_recentFocus = g_nRecent - 1;
                        anim_request(hwnd, g_recentIdx[g_recentFocus]);   // v0.13 焦点格动画
                    }
                    else {
                        g_tagFocus = tag_focus_entry();
                        anim_request(hwnd, -1);   // 网格框停画，动画同停
                    }
                }
                else hover_move(hwnd, -g_cols);
                panel_repaint();
            }
            else if (g_editFocus == EDIT_NAME) {
                edit_set_focus(EDIT_SEARCH);   // v0.15 名称框 ↑ = 回搜索（名称在抽屉空间模型最上层）
                panel_repaint();
            }
            else if (g_editFocus == EDIT_INDEX && g_nDChips > 0) {
                // v0.13 空间模型：索引框 ↑ = 进 chips 焦点（落在已打标项，无则首项）
                g_chipFocus = 0;
                Entry* t = drawer_target();
                if (t)
                    for (int i = 0; i < g_nDChips; i++)
                        if (g_dChipId[i] >= 0 && (t->tagmask & ((u64)1 << g_dChipId[i]))) { g_chipFocus = i; break; }
                panel_repaint();
            }
            return 0;
        case VK_DOWN:
            if (g_editFocus == EDIT_SEARCH) hover_move(hwnd, g_cols);
            else if (g_editFocus == EDIT_NAME && g_dIndexBox.right > g_dIndexBox.left) {
                edit_set_focus(EDIT_INDEX);   // v0.15 名称框 ↓ = 进索引框
                panel_repaint();
            }
            return 0;
        case VK_LEFT:
            if (g_editFocus == EDIT_SEARCH) {
                if (g_nChips > 0 && g_hover == 0) {
                    g_tagFocus = tag_focus_entry();   // 首格再 ← = 标签行
                    anim_request(hwnd, -1);
                }
                else hover_move(hwnd, -1);
                panel_repaint();
            }
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
        if (g_fadeOut || g_complen) return 0;   // 淡出中忽略；组合期间忽略（结果串走 WM_IME_COMPOSITION）
        if (GetKeyState(VK_CONTROL) < 0) return 0;   // v0.12：Ctrl 组合的字符态（Ctrl+Space 的空格等）不进输入框
        if (c >= 32 && c != 127) {
            if (g_tagFocus >= 0 || g_chipFocus >= 0 || g_recentFocus >= 0) {   // 焦点态打字 = 回搜索框继续输入
                g_tagFocus = g_chipFocus = g_recentFocus = -1;
                edit_set_focus(EDIT_SEARCH);
                anim_request(hwnd, (g_hover >= 0 && g_hover < g_nFilt) ? g_filt[g_hover] : -1);   // v0.13 动画随网格框恢复
            }
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
    case WM_LBUTTONDOWN: {
        // v0.11 死区拖动：借系统模态移动循环（HTCAPTION），与原生标题栏手感一致；
        // 松手后落点即记忆位（prefs v3，下次呼出沿用）。分层窗内容随窗口整体
        // 移动，拖动全程零重绘。点击不动 = 无操作（原死区点击本就无命中）。
        if (g_fadeOut) break;   // 淡出中：窗口将散，不进入拖动
        POINT pt;
        pt.x = (short)LOWORD(lParam);
        pt.y = (short)HIWORD(lParam);
        if (!panel_drag_zone(pt)) break;
        SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        RECT wr;
        GetWindowRect(hwnd, &wr);
        if (wr.left >= -30000 && wr.top >= -30000 &&
            (wr.left != g_posX || wr.top != g_posY)) {
            g_posX = wr.left; g_posY = wr.top;
            prefs_save();
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (g_fadeOut) return 0;   // 淡出中：不追 hover / 不驱动 GIF 动画
        g_mouse.x = (short)LOWORD(lParam);
        g_mouse.y = (short)HIWORD(lParam);
        int hv = cell_from_point(g_mouse);
        // 最近行的 hover 也驱动动画请求（ei 相同则无开销）
        int hvEi = -1;
        int rv = -1;   // v0.16.3：hover 的最近行槽位——hv 在行内恒 -1，没有它横向滑动永不重绘（框卡在进入格）
        if (hv >= 0) hvEi = g_filt[hv];
        else for (int i = 0; i < g_recentN && i < g_nRecent; i++)
            if (PtInRect(&g_recent[i], g_mouse)) { hvEi = g_recentIdx[i]; rv = i; break; }
        if (hvEi != g_anim.want) anim_request(hwnd, hvEi);
        if (hv != g_hover || rv != g_recentHover) {
            g_hover = hv;
            g_recentHover = rv;
            g_previewIdx = -1;
            KillTimer(hwnd, PREVIEW_TMR);
            if (hv >= 0) SetTimer(hwnd, PREVIEW_TMR, 280, NULL);
            panel_repaint();
        }
        BOOL hand = hv >= 0 || PtInRect(&g_closeRc, g_mouse);
        if (!hand) for (int i = 0; i < g_recentN; i++)
            if (PtInRect(&g_recent[i], g_mouse) && g_recentIdx[i] >= 0) { hand = TRUE; break; }
        if (!hand && g_nFilt == 0 && PtInRect(&g_grid, g_mouse)) hand = TRUE;   // 空态可点
        if (!hand) for (int i = 0; i < g_nChips; i++)
            if (PtInRect(&g_chipRc[i], g_mouse)) { hand = TRUE; break; }
        BOOL grab = panel_drag_zone(g_mouse);   // v0.11 死区 = 移动光标（可拖暗示）
        SetCursor(LoadCursorW(NULL, grab ? IDC_SIZEALL : (hand ? IDC_HAND : IDC_ARROW)));
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
        g_recentHover = -1;
        g_previewIdx = -1;
        g_tracking = FALSE;
        g_mouse.x = g_mouse.y = -30000;
        anim_request(hwnd, -1);
        panel_repaint();
        return 0;

    case WM_MOUSEWHEEL: {
        if (g_fadeOut) return 0;
        if (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) {   // v0.11 Ctrl+滚轮 = 不透明度
            opacity_set((int)g_opacity + (GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? OPACITY_STEP : -OPACITY_STEP));
            return 0;
        }
        // v0.17.1 光标在标签行上 = 行内横滚（溢出时）；否则滚轮照旧翻网格页
        POINT wpt;
        wpt.x = (short)LOWORD(lParam); wpt.y = (short)HIWORD(lParam);
        ScreenToClient(hwnd, &wpt);
        int chipY = g_chipY;
        if (wpt.y >= chipY && wpt.y < g_hairY && wpt.x < g_gridR && g_chipMax > 0) {   // v0.18.2：x 限网格区（抽屉开时不劫持抽屉上的滚轮）
            // v0.17.2 键盘焦点随滚动保留：先记标签 id，重绘后仍可见就锚回（滚出视野才清）
            int keepId = (g_tagFocus >= 0 && g_tagFocus < g_nChips) ? g_chipId[g_tagFocus] : -2;
            // v0.17.4：吸附滚动（每格 = 下一枚 chip 右缘对齐；上滚反向），杜绝半字停留帧
            g_chipScr = chip_snap_scroll(g_chipScr, (GET_WHEEL_DELTA_WPARAM(wParam) > 0) ? -1 : 1);
            if (g_chipScr < 0) g_chipScr = 0;
            if (g_chipScr > g_chipMax) g_chipScr = g_chipMax;
            g_tagFocus = -1;   // 可见集变了，旧下标失义（下方按 id 重锚）
            panel_repaint();
            if (keepId != -2)
                for (int i = 0; i < g_nChips; i++)
                    if (g_chipId[i] == keepId) { g_tagFocus = i; break; }
            if (g_tagFocus >= 0) panel_repaint();   // 焦点线入画（同步两连提交，无中间帧）
            return 0;
        }
        int step = g_rows > 2 ? 2 : 1;
        if (GET_WHEEL_DELTA_WPARAM(wParam) > 0) g_first -= step;
        else g_first += step;
        clamp_first();
        panel_repaint();
        return 0;
    }

    case WM_LBUTTONUP: {
        if (g_fadeOut) return 0;   // 淡出中：命中区即将消失，点击不落地
        POINT pt;
        pt.x = (short)LOWORD(lParam);
        pt.y = (short)HIWORD(lParam);
        g_tagFocus = g_chipFocus = g_recentFocus = -1;   // v0.12：鼠标点击 = 键盘焦点让位（避免双高亮）
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
            if (PtInRect(&g_dNameRc, pt)) {           // v0.15 名称输入框：仅单目标可编辑
                if (drawer_target()) {
                    edit_set_focus(EDIT_NAME);
                    g_caretOn = TRUE;
                    ime_update_position(hwnd);
                    panel_repaint();
                }
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
            if (PtInRect(&g_dFavRc, pt) && drawer_target()) {   // 收藏 toggle（v0.10）
                drawer_target()->flags ^= MP_FLAG_FAV;
                store_save(&g_store);
                refilter();          // 收藏前置排序即时生效
                panel_repaint();
                return 0;
            }
            return 0;   // 抽屉空白：不处理
        }
        // ---- 网格区 ----
        if (PtInRect(&g_searchBox, pt)) {             // 搜索框：切焦点（不动 caret）
            edit_set_focus(EDIT_SEARCH);
            g_caretOn = TRUE;
            panel_repaint();
            return 0;
        }
        if (g_nFilt == 0 && PtInRect(&g_grid, pt)) {  // 空库：点击网格任意处开导入窗（v0.10）
            imp_open(hwnd);
            return 0;
        }
        for (int i = 0; i < g_recentN; i++)
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
        if (g_fadeOut) return 0;
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

    case WM_ACTIVATEAPP:
        if (!wParam && !g_modal && !g_menuUp) panel_hide(hwnd);   // 选色对话框/菜单打开时不收起
        return 0;

    case WM_TRAY:
        if (lParam == WM_LBUTTONUP) {
            panel_show(hwnd);
        } else if (lParam == WM_RBUTTONUP) {
            SetForegroundWindow(hwnd);
            POINT pt; GetCursorPos(&pt);
            // v0.16 托盘瘦身为入口集：偏好项全部收进设置窗（线性菜单放不下且还会再涨）；
            // 唯一保留的高频 checkable = 呼出跟随光标。
            HMENU m = CreatePopupMenu();
            AppendMenuW(m, MF_STRING, IDM_SHOW,    LS(LS_TRAY_SHOW));
            AppendMenuW(m, MF_STRING, IDM_IMPORT,  LS(LS_TRAY_IMPORT));
            AppendMenuW(m, MF_STRING, IDM_SETTINGS, LS(LS_SET_TITLE));
            AppendMenuW(m, MF_SEPARATOR, 0, NULL);
            AppendMenuW(m, MF_STRING | ((g_prefs & PF_FOLLOWCURSOR) ? MF_CHECKED : 0),
                        IDM_FOLLOWCUR, LS(LS_TRAY_FOLLOWCUR));
            AppendMenuW(m, MF_SEPARATOR, 0, NULL);
            AppendMenuW(m, MF_STRING, IDM_EXIT,    LS(LS_TRAY_EXIT));
            g_menuUp = TRUE;
            int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                     pt.x, pt.y, 0, hwnd, NULL);
            g_menuUp = FALSE;
            PostMessageW(hwnd, WM_NULL, 0, 0);
            DestroyMenu(m);
            if (cmd == IDM_SHOW) panel_show(hwnd);
            else if (cmd == IDM_IMPORT) imp_open(hwnd);      // v0.10 导入窗口
            else if (cmd == IDM_SETTINGS) set_open(hwnd);    // v0.16 设置窗口
            else if (cmd == IDM_FOLLOWCUR) {   // v0.11 呼出定位模式切换（下次呼出生效）
                g_prefs ^= PF_FOLLOWCURSOR;
                prefs_save();
            }
            else if (cmd == IDM_EXIT) DestroyWindow(hwnd);
        }
        return 0;

    case WM_DESTROY:
        if (g_hwndTags) DestroyWindow(g_hwndTags);
        if (g_hwndImp)  DestroyWindow(g_hwndImp);
        if (g_hwndSet)  DestroyWindow(g_hwndSet);
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
//   MemePanel.exe -shot <file.png> [-win tags|import] [-hover N] [-tag N] [-text 词] [-accent RRGGBB]
//                 [-drawer N | -drawer batch] [-lang en|zh] [-import <路径>] [-scroll N]
//     -hover N  模拟悬停/键盘选中第 N 个可见格（含 accent 框 + 名字条 + 168px 大图）
//     -tag N    激活第 N 个标签胶囊
//     -drawer N     打开检视抽屉，单目标 = 过滤后第 N 项（默认 5；indexText 为空则注入示例词）
//     -drawer batch 选择 {2,5,9} 三项 + 打开抽屉批量态
//     -accent RRGGBB  覆盖主题色渲染（hex，大小写均可；不写 theme.cfg）
//     -import <路径>   启动时先执行一次导入（文件或文件夹；含空格整体加引号），再继续
//                      -shot 渲染或常驻——导入行为（判重/过滤/哈希命名/状态行计数）的确定性验证入口
//     -rename 词       配合 -drawer N：经名称框真链路改目标名称并落盘（同时构图聚焦态）
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

static void shot_compose(const u32* bits, int W, int H, const wchar_t* path);   // 定义在本函数后

static void shot_run(HWND hwnd)
{
    wchar_t path[MAX_PATH];
    cl_token(cl_arg(L"-shot"), path, MAX_PATH);
    if (!path[0]) lstrcpynW(path, L"shot.png", MAX_PATH);

    // -accent / -lang 必须先于 -win 解析（v0.16 修：此前 -win 提前 return，管理窗渲染吃不到覆盖）
    if (cl_arg(L"-accent")) {
        wchar_t tok[16];
        cl_token(cl_arg(L"-accent"), tok, 16);
        u32 v = whex_(tok);
        if (v != 0xFFFFFFFFu)
            g_accent = RGB((v >> 16) & 255, (v >> 8) & 255, v & 255);
    }
    if (cl_arg(L"-lang")) {
        wchar_t tok[8];
        cl_token(cl_arg(L"-lang"), tok, 8);
        g_lang = (tok[0] == L'e' || tok[0] == L'E') ? 1 : 0;
    }

    // -win tags|import|settings（v0.10/v0.16）：无头渲染管理窗口（表面 + 布局 + 绘制，不显示）
    if (cl_arg(L"-win")) {
        wchar_t win[16];
        cl_token(cl_arg(L"-win"), win, 16);
        if (win[0] == L't' || win[0] == L'T') {
            SetWindowPos(g_hwndTags, HWND_TOPMOST, 40, 40,
                         S(420) + g_bleed * 2, S(580) + g_bleed * 2, SWP_NOACTIVATE);
            tags_size(g_hwndTags);      // 尺寸未变不触发 WM_SIZE，显式建表面
            tags_repaint();
            if (g_mgT.bits) shot_compose(g_mgT.bits, g_mgT.w, g_mgT.h, path);
        } else if (win[0] == L's' || win[0] == L'S') {
            SetWindowPos(g_hwndSet, HWND_TOPMOST, 40, 40,
                         S(420) + g_bleed * 2, S(560) + g_bleed * 2, SWP_NOACTIVATE);
            set_size(g_hwndSet);
            set_repaint();
            if (g_mgS.bits) shot_compose(g_mgS.bits, g_mgS.w, g_mgS.h, path);
        } else {
            SetWindowPos(g_hwndImp, HWND_TOPMOST, 40, 40,
                         S(460) + g_bleed * 2, S(460) + g_bleed * 2, SWP_NOACTIVATE);
            imp_size(g_hwndImp);
            imp_repaint();
            if (g_mgI.bits) shot_compose(g_mgI.bits, g_mgI.w, g_mgI.h, path);
        }
        return;
    }

    // -kbdtag / -kbdrecent（v0.12）：预置键盘焦点态——无头验收焦点视觉
    //（-kbdtag 落在激活 chip 上：验证黑底上的 accent 红线）
    if (wcontains_ci(GetCommandLineW(), L"-kbdtag"))    g_tagFocus = 0;
    if (wcontains_ci(GetCommandLineW(), L"-kbdrecent")) g_recentFocus = 0;

    // 演示数据无使用记录：无头导出时伪造最近使用时间，让「最近行」进入构图
    //（v0.16.2 槽位=列数：伪满全部槽位；库小于槽位数时 ei 钳尾，重复无害）
    for (int i = 0; i < MP_RECENT_MAX && g_store.count; i++) {
        int ei = (int)((u64)i * (u64)g_store.count / MP_RECENT_MAX);
        if (ei >= g_store.count) ei = g_store.count - 1;
        g_store.entries[ei].used = GetTickCount64() - (u64)(MP_RECENT_MAX - i) * 3600000ull;
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
            lstrcpynW(g_ebR.b, entry_disp_name(&g_store.entries[ei]), 127);   // v0.15 名称框
            g_ebR.len = (int)wlen(g_ebR.b);
            g_ebR.caret = g_ebR.len;
        }
    }

    // -rename 词（v0.15）：经 g_ebR + name_commit 真链路改抽屉目标的名称并落盘（需配合 -drawer N；
    // 同时把焦点置名称框——一张图同时验证填充态 + 聚焦态 + index.bin 落盘）
    if (cl_arg(L"-rename") && g_drawer && g_drawerEi >= 0) {
        wchar_t tok[64];
        cl_token(cl_arg(L"-rename"), tok, 64);
        if (tok[0]) {
            lstrcpynW(g_ebR.b, tok, 127);
            g_ebR.len = (int)wlen(g_ebR.b);
            g_ebR.caret = g_ebR.len;
            g_editFocus = EDIT_NAME;
            name_commit();
        }
    }

    // -fav N：给过滤后第 N 项打收藏（仅内存，不落盘）——验证 ★ 前置排序 + 角标
    if (cl_arg(L"-fav") && g_nFilt > 0) {
        int fi = watoi_(cl_arg(L"-fav"));
        if (fi < 0) fi = 0;
        if (fi >= g_nFilt) fi = g_nFilt - 1;
        g_store.entries[g_filt[fi]].flags |= MP_FLAG_FAV;
        refilter();
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
    // -scroll N（v0.17.1）：标签行横滚构图；超界由 panel_repaint 钳到滚满（传大数 = 滚到底）
    if (cl_arg(L"-scroll")) g_chipScr = watoi_(cl_arg(L"-scroll"));
    // -wheelsnap N（v0.17.6）：真链路预演“下滚 N 格”（chip_snap_scroll 逐格推进）——
    // 覆盖空槽遍历（v0.17.5 滚轮闪退只有这条路径能踩到，-scroll 直赋值测不到）
    if (cl_arg(L"-wheelsnap")) {
        int n = watoi_(cl_arg(L"-wheelsnap"));
        if (n < 0) n = 0;
        if (n > 200) n = 200;
        for (int k = 0; k < n; k++) g_chipScr = chip_snap_scroll(g_chipScr, 1);
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
    shot_compose(g_bits, g_winW, g_winH, path);
}

// DIB（预乘）合成到背景 -> straight BGRA -> PNG（面板/管理窗口共用的无头导出尾段）
static void shot_compose(const u32* bits, int W, int H, const wchar_t* path)
{
    u8* out = (u8*)HeapAlloc(GetProcessHeap(), 0, (u64)W * H * 4);
    if (out && bits) {
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                u32 px = bits[(u64)y * W + x];
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
    prefs_load();   // v0.9：语言位要在最早的弹窗（单实例/GDI+ 失败）之前就位
    g_lang = (g_prefs & PF_ENGLISH) ? 1 : 0;
    {   // 每次进程启动留痕（不依赖 -imedebug）：事后可查证用户跑的是哪个构建
        char bl[96];
        wnsprintfA(bl, 96, "=== session v0.18.2 b%s ===\r\n", __TIME__);
        ime_dbg_log(bl);
    }
    if (cl_arg(L"-pos")) {          // -pos X,Y：窗口定位覆盖（IME 偏移随窗口位置变化的实验）
        const wchar_t* pp = cl_arg(L"-pos");
        g_posX = watoi_(pp);
        while (*pp >= L'0' && *pp <= L'9') pp++;
        if (*pp == L',' || *pp == L'x' || *pp == L'X') pp++;
        g_posY = watoi_(pp);
        if (g_posX < 0 || g_posY < 0) g_posX = g_posY = -1;
        else g_cliPos = TRUE;       // 优先于「呼出跟随光标」开关（实验/探针确定性）
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
            MessageBoxW(NULL, LS(LS_ALREADY), APP_NAME, MB_ICONINFORMATION);
            ExitProcess(0);
        }
    }

    if (!gp_init()) {
        if (g_headless) ExitProcess(1);
        MessageBoxW(NULL, LS(LS_GDIPFAIL), APP_NAME, MB_ICONERROR);
        ExitProcess(1);
    }
    wic_init();
    store_init(&g_store);
    hash_migrate();        // v0.14：旧库一次性迁移——哈希命名 + 同内容合并（幂等，收敛后不再跑）
    theme_load();          // theme.cfg（无/坏文件 = 默认正红；-shot 的 -accent 稍后覆盖内存值）
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

    // v0.10 管理窗口类（CS_DBLCLKS = 标签行双击直接改名）；隐藏创建，用时按需显示
    WNDCLASSW wc2;
    memcpy(&wc2, &wc, sizeof wc2);
    wc2.style = CS_DBLCLKS;
    wc2.lpfnWndProc = TagsProc;
    wc2.lpszClassName = L"MemePanelTags";
    RegisterClassW(&wc2);
    wc2.lpfnWndProc = ImportProc;
    wc2.lpszClassName = L"MemePanelImp";
    RegisterClassW(&wc2);
    wc2.lpfnWndProc = SetProc;
    wc2.lpszClassName = L"MemePanelSet";
    RegisterClassW(&wc2);

    // WS_EX_LAYERED + UpdateLayeredWindow（不用 SetLayeredWindowAttributes —— 会切换出逐像素模式）
    g_hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
                              L"MemePanelWnd", APP_NAME,
                              WS_POPUP,
                              0, 0, S(760) + g_bleed * 2, S(480) + g_bleed * 2,
                              NULL, NULL, g_hInst, NULL);
    // v0.10：主界面不再接收拖放（导入移入导入窗口）；导入窗口接
    g_hwndTags = CreateWindowExW(WS_EX_LAYERED, L"MemePanelTags", L"MemePanel Tags",
                                  WS_POPUP, 0, 0, S(420) + g_bleed * 2, S(580) + g_bleed * 2,
                                  NULL, NULL, g_hInst, NULL);
    g_hwndImp = CreateWindowExW(WS_EX_LAYERED, L"MemePanelImp", L"MemePanel Import",
                                 WS_POPUP, 0, 0, S(460) + g_bleed * 2, S(460) + g_bleed * 2,
                                 NULL, NULL, g_hInst, NULL);
    DragAcceptFiles(g_hwndImp, TRUE);
    g_hwndSet = CreateWindowExW(WS_EX_LAYERED, L"MemePanelSet", L"MemePanel Settings",
                                 WS_POPUP, 0, 0, S(420) + g_bleed * 2, S(560) + g_bleed * 2,
                                 NULL, NULL, g_hInst, NULL);

    if (!g_headless) {
        memset(&g_nid, 0, sizeof g_nid);
        g_nid.cbSize = sizeof g_nid;
        g_nid.hWnd = g_hwnd;
        g_nid.uID = 1;
        g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        g_nid.uCallbackMessage = WM_TRAY;
        g_nid.hIcon = g_icon;
        lstrcpynW(g_nid.szTip, LS(LS_TIP), 128);
        Shell_NotifyIconW(NIM_ADD, &g_nid);

        if (!RegisterHotKey(g_hwnd, HOTKEY_ID,
                            MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, VK_OEM_PERIOD)) {
            lstrcpynW(g_nid.szInfoTitle, APP_NAME, 64);
            lstrcpynW(g_nid.szInfo, LS(LS_BAL_HOTKEY), 256);
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

    // v0.14 -import <路径>：无头/有头皆可（文件或文件夹），导入后继续走 -shot 渲染或常驻。
    // 测试钩：路径含空格需整体加引号。
    if (cl_arg(L"-import")) {
        const wchar_t* q = cl_arg(L"-import");
        wchar_t ip[MAX_PATH];
        if (*q == L'"') {
            int k = 0; q++;
            while (*q && *q != L'"' && k < MAX_PATH - 1) ip[k++] = *q++;
            ip[k] = 0;
        } else {
            cl_token(q, ip, MAX_PATH);
        }
        if (ip[0]) { const wchar_t* one = ip; import_run(g_hwnd, &one, 1); }
    }

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
