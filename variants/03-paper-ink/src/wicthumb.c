// wicthumb.c — WIC 编解码（COM C 风格）
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <initguid.h>   // 必须在最前：让 WIC 各 GUID 在本编译单元落地
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <wincodec.h>
#include "wicthumb.h"

static IWICImagingFactory* g_factory;

void wic_init(void)
{
    if (g_factory) return;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                     &IID_IWICImagingFactory, (void**)&g_factory);
}

void wic_shutdown(void)
{
    if (g_factory) { IWICImagingFactory_Release(g_factory); g_factory = NULL; }
    CoUninitialize();
}

BOOL wic_make_thumb(const wchar_t* src, const wchar_t* dst, int maxEdge)
{
    if (!g_factory) return FALSE;
    IWICBitmapDecoder* dec = NULL;    IWICBitmapFrameDecode* frame = NULL;
    IWICFormatConverter* conv = NULL; IWICBitmapScaler* scaler = NULL;
    IWICStream* stream = NULL;        IWICBitmapEncoder* enc = NULL;
    IWICBitmapFrameEncode* fe = NULL; IPropertyBag2* bag = NULL;
    BOOL ok = FALSE;

    if (FAILED(IWICImagingFactory_CreateDecoderFromFilename(
            g_factory, src, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) || !dec)
        return FALSE;

    if (SUCCEEDED(IWICBitmapDecoder_GetFrame(dec, 0, &frame)) && frame) {
        UINT w = 0, h = 0;
        if (SUCCEEDED(IWICBitmapFrameDecode_GetSize(frame, &w, &h)) && w && h) {
            double sc = (double)maxEdge / (double)(w > h ? w : h);
            if (sc > 1.0) sc = 1.0;
            UINT tw = w > h ? (UINT)maxEdge : (UINT)(w * sc); if (!tw) tw = 1;
            UINT th = h >= w ? (UINT)maxEdge : (UINT)(h * sc); if (!th) th = 1;
            // 管线直接输出 24bppBGR（JPEG 原生格式），编码器无需再转换；
            // 帧序列必须是 Initialize -> SetPixelFormat -> SetSize -> WriteSource
            if (SUCCEEDED(IWICImagingFactory_CreateFormatConverter(g_factory, &conv)) && conv &&
                SUCCEEDED(IWICFormatConverter_Initialize(
                    conv, (IWICBitmapSource*)frame, &GUID_WICPixelFormat24bppBGR,
                    WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom)) &&
                SUCCEEDED(IWICImagingFactory_CreateBitmapScaler(g_factory, &scaler)) && scaler &&
                SUCCEEDED(IWICBitmapScaler_Initialize(
                    scaler, (IWICBitmapSource*)conv, tw, th, WICBitmapInterpolationModeLinear)) &&
                SUCCEEDED(IWICImagingFactory_CreateStream(g_factory, &stream)) && stream &&
                SUCCEEDED(IWICStream_InitializeFromFilename(stream, dst, GENERIC_WRITE)) &&
                SUCCEEDED(IWICImagingFactory_CreateEncoder(
                    g_factory, &GUID_ContainerFormatJpeg, NULL, &enc)) && enc &&
                SUCCEEDED(IWICBitmapEncoder_Initialize(
                    enc, (IStream*)stream, WICBitmapEncoderNoCache)) &&
                SUCCEEDED(IWICBitmapEncoder_CreateNewFrame(enc, &fe, &bag)) && fe)
            {
                if (bag) {
                    PROPBAG2 pb; VARIANT v;
                    memset(&pb, 0, sizeof pb);
                    memset(&v, 0, sizeof v);
                    pb.pstrName = (LPOLESTR)L"ImageQuality";
                    V_VT(&v) = VT_R4; V_UNION(&v, fltVal) = 0.82f;
                    IPropertyBag2_Write(bag, 1, &pb, &v);
                }
                WICPixelFormatGUID fmt = GUID_WICPixelFormat24bppBGR;
                ok = SUCCEEDED(IWICBitmapFrameEncode_Initialize(fe, bag)) &&
                     SUCCEEDED(IWICBitmapFrameEncode_SetPixelFormat(fe, &fmt)) &&
                     SUCCEEDED(IWICBitmapFrameEncode_SetSize(fe, tw, th)) &&
                     SUCCEEDED(IWICBitmapFrameEncode_WriteSource(fe, (IWICBitmapSource*)scaler, NULL)) &&
                     SUCCEEDED(IWICBitmapFrameEncode_Commit(fe)) &&
                     SUCCEEDED(IWICBitmapEncoder_Commit(enc));
            }
        }
    }

    if (bag)    IPropertyBag2_Release(bag);
    if (fe)     IWICBitmapFrameEncode_Release(fe);
    if (enc)    IWICBitmapEncoder_Release(enc);
    if (stream) IWICStream_Release(stream);
    if (scaler) IWICBitmapScaler_Release(scaler);
    if (conv)   IWICFormatConverter_Release(conv);
    if (frame)  IWICBitmapFrameDecode_Release(frame);
    if (dec)    IWICBitmapDecoder_Release(dec);
    return ok;
}

// straight BGRA（顶朝下）-> PNG；无头验证用（-shot 导出合成帧）
BOOL wic_save_png(const u8* bgra, int w, int h, int stride, const wchar_t* path)
{
    if (!g_factory || !bgra) return FALSE;
    IWICStream* stream = NULL;
    IWICBitmapEncoder* enc = NULL;
    IWICBitmapFrameEncode* fe = NULL;
    BOOL ok = SUCCEEDED(IWICImagingFactory_CreateStream(g_factory, &stream)) && stream &&
              SUCCEEDED(IWICStream_InitializeFromFilename(stream, path, GENERIC_WRITE)) &&
              SUCCEEDED(IWICImagingFactory_CreateEncoder(
                  g_factory, &GUID_ContainerFormatPng, NULL, &enc)) && enc &&
              SUCCEEDED(IWICBitmapEncoder_Initialize(
                  enc, (IStream*)stream, WICBitmapEncoderNoCache)) &&
              SUCCEEDED(IWICBitmapEncoder_CreateNewFrame(enc, &fe, NULL)) && fe;
    if (ok) {
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
        ok = SUCCEEDED(IWICBitmapFrameEncode_Initialize(fe, NULL)) &&
             SUCCEEDED(IWICBitmapFrameEncode_SetSize(fe, (UINT)w, (UINT)h)) &&
             SUCCEEDED(IWICBitmapFrameEncode_SetPixelFormat(fe, &fmt)) &&
             SUCCEEDED(IWICBitmapFrameEncode_WritePixels(
                 fe, (UINT)h, (UINT)stride, (UINT)stride * (UINT)h, (BYTE*)bgra)) &&
             SUCCEEDED(IWICBitmapFrameEncode_Commit(fe)) &&
             SUCCEEDED(IWICBitmapEncoder_Commit(enc));
    }
    if (fe)     IWICBitmapFrameEncode_Release(fe);
    if (enc)    IWICBitmapEncoder_Release(enc);
    if (stream) IWICStream_Release(stream);
    return ok;
}

HBITMAP wic_load_bitmap(const wchar_t* path)
{
    if (!g_factory) return NULL;
    IWICBitmapDecoder* dec = NULL;
    IWICBitmapFrameDecode* frame = NULL;
    IWICFormatConverter* conv = NULL;
    HBITMAP hb = NULL;

    if (FAILED(IWICImagingFactory_CreateDecoderFromFilename(
            g_factory, path, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) || !dec)
        return NULL;

    if (SUCCEEDED(IWICBitmapDecoder_GetFrame(dec, 0, &frame)) && frame) {
        UINT w = 0, h = 0;
        if (SUCCEEDED(IWICBitmapFrameDecode_GetSize(frame, &w, &h)) && w && h &&
            SUCCEEDED(IWICImagingFactory_CreateFormatConverter(g_factory, &conv)) && conv &&
            SUCCEEDED(IWICFormatConverter_Initialize(
                conv, (IWICBitmapSource*)frame, &GUID_WICPixelFormat32bppBGR,
                WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom)))
        {
            BITMAPINFO bi;
            memset(&bi, 0, sizeof bi);
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = (LONG)w;
            bi.bmiHeader.biHeight = -(LONG)h;   // 顶朝下
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            void* bits = NULL;
            HDC dc = GetDC(NULL);
            hb = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
            ReleaseDC(NULL, dc);
            if (hb && bits) {
                UINT stride = w * 4, cb = stride * h;
                if (FAILED(IWICFormatConverter_CopyPixels(conv, NULL, stride, cb, (BYTE*)bits))) {
                    DeleteObject(hb); hb = NULL;
                }
            } else if (hb) {
                DeleteObject(hb); hb = NULL;
            }
        }
    }

    if (conv)  IWICFormatConverter_Release(conv);
    if (frame) IWICBitmapFrameDecode_Release(frame);
    if (dec)   IWICBitmapDecoder_Release(dec);
    return hb;
}

// ============================================================
// v0.5：GIF 逐帧解码（在调用线程自建的 WIC 工厂上跑，后台线程安全）
// ============================================================

// 数值型元数据读取（VT_UI2/UI4/I2/UI1；数值 PROPVARIANT 无内嵌资源，不需 Clear）
static BOOL qr_num(IWICMetadataQueryReader* qr, const WCHAR* name, int* v)
{
    PROPVARIANT pv;
    memset(&pv, 0, sizeof pv);
    if (FAILED(IWICMetadataQueryReader_GetMetadataByName(qr, name, &pv)))
        return FALSE;
    BOOL ok = FALSE;
    switch (pv.vt) {
    case VT_UI1: *v = pv.bVal; ok = TRUE; break;
    case VT_UI2: *v = pv.uiVal; ok = TRUE; break;
    case VT_I2:  *v = pv.iVal; ok = TRUE; break;
    case VT_UI4: *v = (int)pv.ulVal; ok = TRUE; break;
    }
    return ok;
}

void wic_free_gif(GifFrames* g)
{
    if (!g) return;
    if (g->frames) {
        for (int i = 0; i < g->nFrames; i++)
            if (g->frames[i]) HeapFree(GetProcessHeap(), 0, g->frames[i]);
        HeapFree(GetProcessHeap(), 0, g->frames);
    }
    if (g->delaysMs) HeapFree(GetProcessHeap(), 0, g->delaysMs);
    memset(g, 0, sizeof *g);
}

BOOL wic_decode_gif(void* factoryIn, const wchar_t* path, GifFrames* out)
{
    IWICImagingFactory* fac = (IWICImagingFactory*)factoryIn;
    memset(out, 0, sizeof *out);
    if (!fac) return FALSE;

    IWICBitmapDecoder* dec = NULL;
    if (FAILED(IWICImagingFactory_CreateDecoderFromFilename(
            fac, path, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) || !dec)
        return FALSE;

    UINT w = 0, h = 0, nf = 0;
    // GIF 逻辑屏幕尺寸 = 首帧尺寸（IWICBitmapDecoder 没有 GetSize）
    IWICBitmapFrameDecode* f0 = NULL;
    if (SUCCEEDED(IWICBitmapDecoder_GetFrame(dec, 0, &f0)) && f0) {
        IWICBitmapFrameDecode_GetSize(f0, &w, &h);
        IWICBitmapFrameDecode_Release(f0);
    }
    IWICBitmapDecoder_GetFrameCount(dec, &nf);
    BOOL ok = FALSE;
    // 约束：画布 ≤512px、帧数 ≤64、总内存 ≤16MB（悬停动画是短命对象，不值得更大）
    if (!w || !h || !nf || w > 512 || h > 512 || nf > 64 ||
        (u64)w * h * 4 * nf > 16ull * 1024 * 1024)
        goto done;

    u32 cb = w * h * 4;
    u8* canvas = (u8*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, cb);
    u8* prevSnap = (u8*)HeapAlloc(GetProcessHeap(), 0, cb);
    u8* tmp = (u8*)HeapAlloc(GetProcessHeap(), 0, cb);
    out->frames = (u8**)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (u64)nf * sizeof(u8*));
    out->delaysMs = (int*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (u64)nf * sizeof(int));
    if (!canvas || !prevSnap || !tmp || !out->frames || !out->delaysMs) {
        HeapFree(GetProcessHeap(), 0, canvas); HeapFree(GetProcessHeap(), 0, prevSnap);
        HeapFree(GetProcessHeap(), 0, tmp);
        goto done;
    }

    ok = TRUE;
    out->w = (int)w;
    out->h = (int)h;
    int prevDisposal = 0;
    RECT prevRc = { 0, 0, (LONG)w, (LONG)h };
    BOOL haveSnap = FALSE;

    for (UINT i = 0; i < nf; i++) {
        IWICBitmapFrameDecode* frame = NULL;
        if (FAILED(IWICBitmapDecoder_GetFrame(dec, i, &frame)) || !frame) { ok = FALSE; break; }

        // 帧元数据：目标矩形 / 延迟 / 处置方法
        UINT dl = 0, dt = 0, dw2 = 0, dh2 = 0, delay = 10, disposal = 1;
        IWICMetadataQueryReader* qr = NULL;
        if (SUCCEEDED(IWICBitmapFrameDecode_GetMetadataQueryReader(frame, &qr)) && qr) {
            int v;
            if (qr_num(qr, L"/imgdesc/Left", &v))  dl = (UINT)v;
            if (qr_num(qr, L"/imgdesc/Top", &v))   dt = (UINT)v;
            if (qr_num(qr, L"/imgdesc/Width", &v)) dw2 = (UINT)v;
            if (qr_num(qr, L"/imgdesc/Height", &v)) dh2 = (UINT)v;
            if (qr_num(qr, L"/grctlext/Delay", &v)) delay = (UINT)v;
            if (qr_num(qr, L"/grctlext/Disposal", &v)) disposal = v;
            IWICMetadataQueryReader_Release(qr);
        }
        if (!dw2 || !dh2) { dw2 = w; dh2 = h; }            // 无元数据按整幅
        RECT rc = { (LONG)dl, (LONG)dt, (LONG)(dl + dw2), (LONG)(dt + dh2) };
        if (rc.left < 0) rc.left = 0;  if (rc.top < 0) rc.top = 0;
        if (rc.right > (LONG)w) rc.right = w;
        if (rc.bottom > (LONG)h) rc.bottom = h;
        if (rc.right <= rc.left || rc.bottom <= rc.top) {
            IWICBitmapFrameDecode_Release(frame);
            continue;
        }

        // ① 处置上一帧（disposal 2=清区域为透明；3=恢复快照）
        if (i > 0) {
            if (prevDisposal == 2) {
                for (LONG y = prevRc.top; y < prevRc.bottom; y++)
                    memset(canvas + ((u64)y * w + prevRc.left) * 4, 0,
                           (u64)(prevRc.right - prevRc.left) * 4);
            } else if (prevDisposal == 3 && haveSnap) {
                memcpy(canvas, prevSnap, cb);
            }
        }
        // ② 本帧 disposal==3：绘制前保存快照
        if (disposal == 3) { memcpy(prevSnap, canvas, cb); haveSnap = TRUE; }

        // ③ 解码帧像素到 tmp（32bppBGRA）
        IWICFormatConverter* conv = NULL;
        if (SUCCEEDED(IWICImagingFactory_CreateFormatConverter(fac, &conv)) && conv &&
            SUCCEEDED(IWICFormatConverter_Initialize(
                conv, (IWICBitmapSource*)frame, &GUID_WICPixelFormat32bppBGRA,
                WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom))) {
            UINT fw = rc.right - rc.left, fh = rc.bottom - rc.top;
            if (FAILED(IWICFormatConverter_CopyPixels(conv, NULL, fw * 4, fw * fh * 4, tmp))) {
                IWICFormatConverter_Release(conv);
                IWICBitmapFrameDecode_Release(frame);
                ok = FALSE; break;
            }
            IWICFormatConverter_Release(conv);
            // ④ 二值 alpha 合成到 canvas（GIF 透明是 0/255）
            for (UINT y = 0; y < fh; y++) {
                u8* src = tmp + (u64)y * fw * 4;
                u8* dst = canvas + (((u64)(rc.top + y) * w) + rc.left) * 4;
                for (UINT x = 0; x < fw; x++) {
                    if (src[3]) { dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = 255; }
                    src += 4; dst += 4;
                }
            }
        }
        IWICBitmapFrameDecode_Release(frame);

        // ⑤ 存整幅快照（播放时零合成成本）+ 延迟 clamp（<20ms 的耍赖 GIF 按 100ms）
        out->frames[i] = (u8*)HeapAlloc(GetProcessHeap(), 0, cb);
        if (!out->frames[i]) { ok = FALSE; break; }
        memcpy(out->frames[i], canvas, cb);
        out->delaysMs[i] = delay < 2 ? 100 : (int)delay * 10;
        out->nFrames = (int)i + 1;
        prevDisposal = disposal;
        prevRc = rc;
    }

    if (!ok || !out->nFrames) { wic_free_gif(out); ok = FALSE; }
    HeapFree(GetProcessHeap(), 0, canvas);
    HeapFree(GetProcessHeap(), 0, prevSnap);
    HeapFree(GetProcessHeap(), 0, tmp);

done:
    IWICBitmapDecoder_Release(dec);
    return ok;
}

// 后台线程入口：自建 MTA 工厂 + 解码，调用方线程无需预初始化 COM
BOOL wic_decode_gif_bg(const wchar_t* path, GifFrames** out)
{
    *out = NULL;
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    BOOL needUninit = SUCCEEDED(hr);
    IWICImagingFactory* fac = NULL;
    CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                     &IID_IWICImagingFactory, (void**)&fac);
    GifFrames* g = (GifFrames*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(GifFrames));
    BOOL ok = FALSE;
    if (fac && g) ok = wic_decode_gif(fac, path, g);
    if (fac) IWICImagingFactory_Release(fac);
    if (needUninit) CoUninitialize();
    if (ok) { *out = g; return TRUE; }
    if (g) { wic_free_gif(g); HeapFree(GetProcessHeap(), 0, g); }
    return FALSE;
}
