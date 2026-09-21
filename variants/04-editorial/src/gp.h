// gp.h — GDI+ flat C API 动态绑定（零 SDK 头依赖、零导入库）
// 声明与常量逐条取自 Windows SDK 26100 的 gdiplusflat.h / gdiplusenums.h / gdipluspixelformats.h。
// gdiplus.dll 是系统组件（Win7+ 必在），LoadLibrary + GetProcAddress 即可，产物不增导入表。
#pragma once
#include "util.h"

// ---- 不透明句柄 ----
typedef struct GpGraphics GpGraphics;
typedef struct GpImage    GpImage;
typedef struct GpBitmap   GpBitmap;
typedef struct GpBrush    GpBrush;
typedef struct GpSolidFill GpSolidFill;
typedef struct GpLineGradient GpLineGradient;
typedef struct GpPath     GpPath;
typedef struct GpPen      GpPen;
typedef struct GpFont     GpFont;
typedef struct GpStringFormat GpStringFormat;

typedef struct { float x, y; } GpPointF;
typedef struct { float x, y, width, height; } GpRectF;
typedef struct { int   x, y; } GpPoint;

typedef struct {
    u32   version;                 // 必须为 1
    void* debugCallback;
    BOOL  suppressBackgroundThread;
    BOOL  suppressExternalCodecs;
} GpStartupInput;

// ---- 枚举值（SDK gdiplusenums.h）----
enum {
    GP_OK = 0,
    GP_FILL_WINDING   = 1,     // FillModeWinding
    GP_UNIT_PIXEL     = 2,     // UnitPixel
    GP_WRAP_CLAMP     = 4,     // WrapModeClamp
    GP_SMOOTH_AA      = 4,     // SmoothingModeAntiAlias
    GP_INTERP_BICUBIC = 7,     // InterpolationModeHighQualityBicubic
    GP_TEXT_AA_GRIDFIT = 3,    // TextRenderingHintAntiAliasGridFit
    GP_ALIGN_NEAR     = 0,     // StringAlignmentNear
    GP_ALIGN_CENTER   = 1,
    GP_ALIGN_FAR      = 2,
    GP_NOWRAP         = 0x1000,// StringFormatFlagsNoWrap
    GP_TRIM_ELLIPSIS  = 3,     // StringTrimmingEllipsisCharacter
};

// PixelFormat 公式（SDK gdipluspixelformats.h）：序号 | (bpp << 8) | 标志位
#define GP_PF_32RGB   (9  | (32 << 8) | 0x00020000)                       // 不透明，BGRX
#define GP_PF_32ARGB  (10 | (32 << 8) | 0x00040000 | 0x00020000)          // straight alpha
#define GP_PF_32PARGB (11 | (32 << 8) | 0x00040000 | 0x00080000 | 0x00020000) // 预乘 ARGB

// ---- 绑定表：gp.h 声明 / gp.c 定义并填充 ----
#define GP_LIST(GPA) \
    GPA(int,  GdiplusStartup,            (u64*, const GpStartupInput*, void*)) \
    GPA(void, GdiplusShutdown,           (u64)) \
    GPA(int,  GdipCreateBitmapFromScan0, (int w, int h, int stride, int format, u8* scan0, GpBitmap** bitmap)) \
    GPA(int,  GdipDisposeImage,          (GpImage*)) \
    GPA(int,  GdipGetImageGraphicsContext, (GpImage*, GpGraphics**)) \
    GPA(int,  GdipDeleteGraphics,        (GpGraphics*)) \
    GPA(int,  GdipSetSmoothingMode,      (GpGraphics*, int)) \
    GPA(int,  GdipSetInterpolationMode,  (GpGraphics*, int)) \
    GPA(int,  GdipSetTextRenderingHint,  (GpGraphics*, int)) \
    GPA(int,  GdipCreatePath,            (int fillMode, GpPath**)) \
    GPA(int,  GdipDeletePath,            (GpPath*)) \
    GPA(int,  GdipAddPathArcI,           (GpPath*, int x, int y, int w, int h, float startAngle, float sweepAngle)) \
    GPA(int,  GdipAddPathLineI,          (GpPath*, int x1, int y1, int x2, int y2)) \
    GPA(int,  GdipClosePathFigure,       (GpPath*)) \
    GPA(int,  GdipCreateSolidFill,       (u32 argb, GpSolidFill**)) \
    GPA(int,  GdipCreateLineBrushI,      (const GpPoint*, const GpPoint*, u32 c1, u32 c2, int wrap, GpLineGradient**)) \
    GPA(int,  GdipDeleteBrush,           (GpBrush*)) \
    GPA(int,  GdipFillPath,              (GpGraphics*, GpBrush*, GpPath*)) \
    GPA(int,  GdipFillRectangleI,        (GpGraphics*, GpBrush*, int x, int y, int w, int h)) \
    GPA(int,  GdipCreatePen1,            (u32 argb, float width, int unit, GpPen**)) \
    GPA(int,  GdipDeletePen,             (GpPen*)) \
    GPA(int,  GdipDrawPath,              (GpGraphics*, GpPen*, GpPath*)) \
    GPA(int,  GdipDrawLineI,             (GpGraphics*, GpPen*, int x1, int y1, int x2, int y2)) \
    GPA(int,  GdipDrawEllipseI,          (GpGraphics*, GpPen*, int x, int y, int w, int h)) \
    GPA(int,  GdipCreateFontFromLogfontW,(HDC, const LOGFONTW*, GpFont**)) \
    GPA(int,  GdipDeleteFont,            (GpFont*)) \
    GPA(int,  GdipCreateStringFormat,    (int attrs, u16 langid, GpStringFormat**)) \
    GPA(int,  GdipDeleteStringFormat,    (GpStringFormat*)) \
    GPA(int,  GdipSetStringFormatAlign,  (GpStringFormat*, int align)) \
    GPA(int,  GdipSetStringFormatLineAlign, (GpStringFormat*, int align)) \
    GPA(int,  GdipSetStringFormatTrimming,  (GpStringFormat*, int trimming)) \
    GPA(int,  GdipSetStringFormatFlags,     (GpStringFormat*, int flags)) \
    GPA(int,  GdipDrawString,            (GpGraphics*, const wchar_t*, int len, const GpFont*, const GpRectF*, const GpStringFormat*, const GpBrush*)) \
    GPA(int,  GdipMeasureString,         (GpGraphics*, const wchar_t*, int len, const GpFont*, const GpRectF*, const GpStringFormat*, GpRectF* bbox, int* fitted, int* lines)) \
    GPA(int,  GdipDrawImageRectI,        (GpGraphics*, GpImage*, int x, int y, int w, int h)) \
    GPA(int,  GdipSetClipRectI,           (GpGraphics*, int x, int y, int w, int h, int combineMode)) \
    GPA(int,  GdipSetClipPath,           (GpGraphics*, GpPath*, int combineMode)) \
    GPA(int,  GdipResetClip,             (GpGraphics*)) \

int  gp_init(void);      // 成功返回 1（GdiplusStartup 完成）
void gp_shutdown(void);
extern int g_gpOk;

#define GPA(ret, name, args) extern ret (WINAPI *name) args;
GP_LIST(GPA)
#undef GPA
