// store.h — 数据层：内存索引 + 原子序列化（Everything 式"索引即内存快照"）
#pragma once
#include "util.h"

#define MP_MAX_TAGS 64   // 标签用 u64 位掩码，64 上限；每图标签集合 = 8 字节

typedef struct {
    wchar_t* name;       // memes 目录内的文件名
    u64 tagmask;
    u64 size;
    u64 mtime;           // 变化则重新生成缩略图
    u64 used;            // 最近一次粘贴的 GetTickCount64
    wchar_t* indexText;  // v2 索引文字（空格分词，供搜索命中；NULL = 空）
} Entry;

typedef struct {
    wchar_t* tagNames[MP_MAX_TAGS];   // NULL = 空槽
    Entry*   entries;
    int      count, cap;
    wchar_t  baseDir[MAX_PATH];
    wchar_t  memesDir[MAX_PATH];
    wchar_t  thumbsDir[MAX_PATH];
    wchar_t  indexPath[MAX_PATH];
} Store;

void store_init(Store* s);                       // 目录定位 + 加载索引
int  store_scan(Store* s);                       // 磁盘 <-> 索引同步，返回新增数
void store_save(Store* s);                       // tmp + 原子替换
int  store_find(const Store* s, const wchar_t* name);
int  store_add(Store* s, const wchar_t* name, u64 size, u64 mtime, u64 tagmask);
void store_delete_at(Store* s, int i);
int  store_tag_id(Store* s, const wchar_t* name);     // 已有 -> id，否则 -1
int  store_tag_add(Store* s, const wchar_t* name);    // 已有返回已有 id；满返回 -1

void store_build_path(const Store* s, const Entry* e, wchar_t* out, int cap);
void store_build_thumb(const Store* s, const Entry* e, wchar_t* out, int cap);
