// util.h — CRT-free 基础设施
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef unsigned long long u64;

#ifdef __cplusplus
extern "C" {
#endif

u32     wlen(const wchar_t* s);
wchar_t* wdup(const wchar_t* s);              // HeapAlloc 复制
int     weq(const wchar_t* a, const wchar_t* b);   // 序号比较、忽略大小写
int     memcmp_(const void* a, const void* b, u32 n);  // GUID 等比较用
int     wcontains_ci(const wchar_t* hay, const wchar_t* needle);

// 可增长字节缓冲（索引序列化用）
typedef struct { u8* p; u32 len; u32 cap; } Buf;
void buf_put(Buf* b, const void* data, u32 n);
void buf_u32(Buf* b, u32 v);
void buf_u64(Buf* b, u64 v);

typedef struct { const u8* p; u32 len; u32 pos; } Cur;
int cur_bytes(Cur* c, void* out, u32 n);
int cur_u32(Cur* c, u32* v);
int cur_u64(Cur* c, u64* v);
#ifdef __cplusplus
}
#endif
