// gp.c — GDI+ flat API 绑定：LoadLibrary 一次，宏表填充全部函数指针
#include "gp.h"

int g_gpOk;

#define GPA(ret, name, args) ret (WINAPI *name) args;
GP_LIST(GPA)
#undef GPA

static u64 g_token;

int gp_init(void)
{
    if (g_gpOk) return 1;
    HMODULE h = LoadLibraryW(L"gdiplus.dll");
    if (!h) return 0;

#define GPA(ret, name, args) \
    if (!(name = (ret (WINAPI*)args)(void*)GetProcAddress(h, #name))) return 0;
    GP_LIST(GPA)
#undef GPA

    GpStartupInput in;
    in.version = 1;
    in.debugCallback = NULL;
    in.suppressBackgroundThread = FALSE;
    in.suppressExternalCodecs = FALSE;
    if (GdiplusStartup(&g_token, &in, NULL) != GP_OK) return 0;
    g_gpOk = 1;
    return 1;
}

void gp_shutdown(void)
{
    if (!g_gpOk) return;
    g_gpOk = 0;
    GdiplusShutdown(g_token);
}
