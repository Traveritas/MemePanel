// util.c — CRT-free：memcpy/memset/memmove 自带实现（/NODEFAULTLIB）
#include <string.h>
#include <stddef.h>
#include "util.h"

// no-CRT 下使用浮点时链接器需要该标记（x64 上仅为占位符）
int _fltused = 1;

void* memcpy(void* dst, const void* src, size_t n)
{
    u8* d = (u8*)dst; const u8* s = (const u8*)src;
    while (n--) *d++ = *s++;
    return dst;
}

void* memset(void* dst, int c, size_t n)
{
    u8* d = (u8*)dst;
    while (n--) *d++ = (u8)c;
    return dst;
}

void* memmove(void* dst, const void* src, size_t n)
{
    u8* d = (u8*)dst; const u8* s = (const u8*)src;
    if (d < s) { while (n--) *d++ = *s++; }
    else { d += n; s += n; while (n--) *--d = *--s; }
    return dst;
}

u32 wlen(const wchar_t* s)
{
    u32 n = 0;
    while (*s++) n++;
    return n;
}

wchar_t* wdup(const wchar_t* s)
{
    u32 n = wlen(s);
    wchar_t* p = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, ((u64)n + 1) * 2);
    if (!p) return NULL;
    memcpy(p, s, ((u64)n + 1) * 2);
    return p;
}

int weq(const wchar_t* a, const wchar_t* b)
{
    return CompareStringOrdinal(a, -1, b, -1, TRUE) == CSTR_EQUAL;
}

// 库规模 ~1 万，朴素匹配足够；255 字符截断防栈溢出
int wcontains_ci(const wchar_t* hay, const wchar_t* needle)
{
    u32 hl = wlen(hay), nl = wlen(needle);
    if (nl == 0) return 1;
    if (nl > 255) return 0;
    if (hl > 255) hl = 255;
    if (nl > hl)  return 0;
    wchar_t h[256], nd[256];
    lstrcpynW(h, hay, (int)hl + 1);   CharLowerBuffW(h, hl);
    lstrcpynW(nd, needle, (int)nl + 1); CharLowerBuffW(nd, nl);
    for (u32 i = 0; i + nl <= hl; i++) {
        u32 j = 0;
        while (j < nl && h[i + j] == nd[j]) j++;
        if (j == nl) return 1;
    }
    return 0;
}

void buf_put(Buf* b, const void* data, u32 n)
{
    if (b->len + n > b->cap) {
        u32 nc = b->cap ? b->cap * 2 : 4096;
        while (nc < b->len + n) nc *= 2;
        // HeapReAlloc 不接受 NULL（与 realloc 不同），首次用 HeapAlloc
        u8* np = b->p ? (u8*)HeapReAlloc(GetProcessHeap(), 0, b->p, nc)
                      : (u8*)HeapAlloc(GetProcessHeap(), 0, nc);
        if (!np) return;
        b->p = np;
        b->cap = nc;
    }
    memcpy(b->p + b->len, data, n);
    b->len += n;
}

void buf_u32(Buf* b, u32 v) { buf_put(b, &v, 4); }
void buf_u64(Buf* b, u64 v) { buf_put(b, &v, 8); }

int cur_bytes(Cur* c, void* out, u32 n)
{
    if (c->pos + n > c->len) return 0;
    memcpy(out, c->p + c->pos, n);
    c->pos += n;
    return 1;
}

int cur_u32(Cur* c, u32* v) { return cur_bytes(c, v, 4); }
int cur_u64(Cur* c, u64* v) { return cur_bytes(c, v, 8); }

int memcmp_(const void* a, const void* b, u32 n)
{
    const u8* x = (const u8*)a; const u8* y = (const u8*)b;
    for (u32 i = 0; i < n; i++) { if (x[i] != y[i]) return x[i] - y[i]; }
    return 0;
}
