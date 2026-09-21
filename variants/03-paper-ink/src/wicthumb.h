// wicthumb.h — WIC 缩略图：全部用系统自带解码器，零自带编解码代码
#pragma once
#include "util.h"

void   wic_init(void);      // UI 线程调用一次（CoInitialize + 工厂）
void   wic_shutdown(void);

// 任意 WIC 支持格式(GIF/JPG/PNG/BMP/TIFF) -> 首帧 JPEG 缩略图
BOOL   wic_make_thumb(const wchar_t* src, const wchar_t* dstJpg, int maxEdge);

// 无头验证：straight BGRA（顶朝下，首像素=左上）-> PNG 文件
BOOL   wic_save_png(const u8* bgra, int w, int h, int stride, const wchar_t* path);

// JPEG 缩略图 -> 32bpp 顶朝下 DIB（可直接 BitBlt）
HBITMAP wic_load_bitmap(const wchar_t* jpgPath);

// ---- v0.5：GIF 逐帧解码（线程安全：factory 由调用线程自建）----
typedef struct {
    int   w, h;          // 画布尺寸
    int   nFrames;
    int*  delaysMs;      // 每帧毫秒（已 clamp：<20ms -> 100ms）
    u8**  frames;        // nFrames 张完整画布快照，BGRA（straight alpha）
} GifFrames;

BOOL   wic_decode_gif(void* factory, const wchar_t* path, GifFrames* out);
void   wic_free_gif(GifFrames* g);

// 后台线程入口：自建 MTA 工厂并解码（成功时 *out 为新分配，调用方负责 wic_free_gif + 释放）
BOOL   wic_decode_gif_bg(const wchar_t* path, GifFrames** out);
