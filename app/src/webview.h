// webview.h — WebView2 桥（C 接口；实现在 webview.cpp，C++ 无 CRT）
// UI 层从 GDI+ 自绘迁移为 HTML：HTML 嵌 exe 资源（NavigateToString），
// 图片经虚拟主机映射本地 memes/thumbs 目录，JS↔C 走 WebMessage JSON。
#pragma once
#include "util.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef void (*WEB_MSG_CB)(const wchar_t* json);   // JS→C 消息（UI 线程）

// 创建环境+控制器+加载 HTML（异步；完成后 wv_ready() 为真并回调 webready）
BOOL wv_init(HWND parent, const wchar_t* userDataDir, WEB_MSG_CB cb);
void wv_navigate(const char* html, int len);       // NavigateToString（UTF-8）
void wv_exec(const wchar_t* js);                   // C→JS：ExecuteScript
void wv_exec_a(const char* jsUtf8);                // 同上（UTF-8 简化）
void wv_resize(int cx, int cy);                    // 控制器 Bounds（客户区像素）
void wv_set_visible(BOOL on);                      // 控制器 IsVisible
void wv_notify_moved();                            // 窗口移动后通知（合成器跟随）
BOOL wv_ready(void);
#ifdef __cplusplus
}
#endif
