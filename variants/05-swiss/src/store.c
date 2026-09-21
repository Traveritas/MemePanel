// store.c — 索引存取与目录扫描
#include "store.h"
#include "wicthumb.h"
#include <shlwapi.h>

#define MP_MAGIC   0x3158504Du   // "MPX1"
#define MP_VERSION 2u            // v2 = entry 尾部追加 indexText（u32 字符数 + UTF-16；0=空不跟数据）

void store_build_path(const Store* s, const Entry* e, wchar_t* out, int cap)
{
    wnsprintfW(out, cap, L"%s\\%s", s->memesDir, e->name);
}

void store_build_thumb(const Store* s, const Entry* e, wchar_t* out, int cap)
{
    wnsprintfW(out, cap, L"%s\\%s.jpg", s->thumbsDir, e->name);
}

int store_find(const Store* s, const wchar_t* name)
{
    for (int i = 0; i < s->count; i++)
        if (weq(s->entries[i].name, name)) return i;
    return -1;
}

int store_add(Store* s, const wchar_t* name, u64 size, u64 mtime, u64 tagmask)
{
    if (s->count == s->cap) {
        int nc = s->cap ? s->cap * 2 : 512;
        Entry* ne = s->entries
            ? (Entry*)HeapReAlloc(GetProcessHeap(), 0, s->entries, (u64)nc * sizeof(Entry))
            : (Entry*)HeapAlloc(GetProcessHeap(), 0, (u64)nc * sizeof(Entry));
        if (!ne) return -1;
        s->entries = ne; s->cap = nc;
    }
    wchar_t* d = wdup(name);
    if (!d) return -1;
    Entry* e = &s->entries[s->count];
    e->name = d; e->size = size; e->mtime = mtime;
    e->tagmask = tagmask; e->used = 0;
    e->indexText = NULL;
    return s->count++;
}

void store_delete_at(Store* s, int i)
{
    if (i < 0 || i >= s->count) return;
    HeapFree(GetProcessHeap(), 0, s->entries[i].name);
    if (s->entries[i].indexText) HeapFree(GetProcessHeap(), 0, s->entries[i].indexText);
    s->entries[i] = s->entries[s->count - 1];
    s->count--;
}

int store_tag_id(Store* s, const wchar_t* name)
{
    for (int i = 0; i < MP_MAX_TAGS; i++)
        if (s->tagNames[i] && weq(s->tagNames[i], name)) return i;
    return -1;
}

int store_tag_add(Store* s, const wchar_t* name)
{
    int i = store_tag_id(s, name);
    if (i >= 0) return i;
    for (i = 0; i < MP_MAX_TAGS; i++) {
        if (!s->tagNames[i]) {
            s->tagNames[i] = wdup(name);
            return s->tagNames[i] ? i : -1;
        }
    }
    return -1;   // 64 个标签已满
}

void store_init(Store* s)
{
    memset(s, 0, sizeof *s);
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t* slash = exe + wlen(exe);
    while (slash > exe && slash[-1] != L'\\') slash--;
    *slash = 0;
    lstrcpynW(s->baseDir, exe, MAX_PATH);
    wnsprintfW(s->memesDir,  MAX_PATH, L"%smemes",  exe);
    wnsprintfW(s->thumbsDir, MAX_PATH, L"%sthumbs", exe);
    wnsprintfW(s->indexPath, MAX_PATH, L"%sindex.bin", exe);

    // ---- 加载索引（任何字段异常则整体作废，扫描会重建）----
    HANDLE f = CreateFileW(s->indexPath, GENERIC_READ, FILE_SHARE_READ,
                           NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER li;
    if (!GetFileSizeEx(f, &li) || li.QuadPart <= 0 || li.QuadPart > 0x4000000) {
        CloseHandle(f); return;
    }
    u32 len = (u32)li.QuadPart;
    u8* buf = (u8*)HeapAlloc(GetProcessHeap(), 0, len);
    DWORD got = 0;
    BOOL rd = buf && ReadFile(f, buf, len, &got, NULL) && got == len;
    CloseHandle(f);
    if (!rd) { if (buf) HeapFree(GetProcessHeap(), 0, buf); return; }

    Cur c = { buf, len, 0 };
    u32 magic = 0, ver = 0, nTags = 0, nEnt = 0, chars = 0;
    wchar_t* tags[MP_MAX_TAGS];
    Entry* ents = NULL;
    int n = 0, ok = 1;

    memset(tags, 0, sizeof tags);
    // v1 兼容迁移：ver==1 按旧布局读完、indexText=NULL，内存态直接可用
    // （下次 store_save 自然写成 v2）——绝不能走「版本不符→整体丢弃重扫」（会丢标签）；
    // 只有 ver 既非 1 也非 2（真正的未知未来格式）才丢弃。
    if (!cur_u32(&c, &magic) || magic != MP_MAGIC ||
        !cur_u32(&c, &ver) || (ver != 1 && ver != MP_VERSION) ||
        !cur_u32(&c, &nTags) || nTags > MP_MAX_TAGS) ok = 0;

    for (u32 t = 0; ok && t < nTags; t++) {
        wchar_t tmp[512];
        if (!cur_u32(&c, &chars) || chars >= 512 ||
            !cur_bytes(&c, tmp, chars * 2)) { ok = 0; break; }
        tmp[chars] = 0;
        tags[t] = wdup(tmp);
        if (!tags[t]) ok = 0;
    }
    if (ok && (!cur_u32(&c, &nEnt) || nEnt > 1000000)) ok = 0;
    if (ok && nEnt) ents = (Entry*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (u64)nEnt * sizeof(Entry));
    if (ok && nEnt && !ents) ok = 0;

    for (u32 k = 0; ok && k < nEnt; k++) {
        wchar_t tmp[512];
        Entry* e = &ents[n];
        if (!cur_u32(&c, &chars) || chars >= 512 ||
            !cur_bytes(&c, tmp, chars * 2) ||
            !cur_u64(&c, &e->tagmask) || !cur_u64(&c, &e->size) ||
            !cur_u64(&c, &e->mtime) || !cur_u64(&c, &e->used)) { ok = 0; break; }
        tmp[chars] = 0;
        e->name = wdup(tmp);
        if (!e->name) { ok = 0; break; }
        e->indexText = NULL;                    // HEAP_ZERO_MEMORY 已置 NULL，显式再确认
        if (ver >= 2) {                          // v2：used 之后跟 indexText
            if (!cur_u32(&c, &chars) || chars >= 512) { ok = 0; break; }
            if (chars) {
                if (!cur_bytes(&c, tmp, chars * 2)) { ok = 0; break; }
                tmp[chars] = 0;
                e->indexText = wdup(tmp);
                if (!e->indexText) { ok = 0; break; }
            }
        }
        n++;
    }

    if (ok) {
        memcpy(s->tagNames, tags, sizeof tags);
        s->entries = ents; s->count = n; s->cap = (int)nEnt;
    } else {
        for (int i = 0; i < MP_MAX_TAGS; i++) if (tags[i]) HeapFree(GetProcessHeap(), 0, tags[i]);
        for (int i = 0; i < n; i++) {
            if (ents[i].name) HeapFree(GetProcessHeap(), 0, ents[i].name);
            if (ents[i].indexText) HeapFree(GetProcessHeap(), 0, ents[i].indexText);
        }
        if (ents) HeapFree(GetProcessHeap(), 0, ents);
    }
    HeapFree(GetProcessHeap(), 0, buf);
}

void store_save(Store* s)
{
    Buf b = { 0, 0, 0 };
    buf_u32(&b, MP_MAGIC);
    buf_u32(&b, MP_VERSION);

    u32 nTags = 0;
    for (int i = 0; i < MP_MAX_TAGS; i++) if (s->tagNames[i]) nTags++;
    buf_u32(&b, nTags);
    for (int i = 0; i < MP_MAX_TAGS; i++) {
        if (!s->tagNames[i]) continue;
        u32 l = wlen(s->tagNames[i]);
        buf_u32(&b, l);
        buf_put(&b, s->tagNames[i], l * 2);
    }

    buf_u32(&b, (u32)s->count);
    for (int i = 0; i < s->count; i++) {
        Entry* e = &s->entries[i];
        u32 l = wlen(e->name);
        buf_u32(&b, l);
        buf_put(&b, e->name, l * 2);
        buf_u64(&b, e->tagmask);
        buf_u64(&b, e->size);
        buf_u64(&b, e->mtime);
        buf_u64(&b, e->used);
        u32 il = e->indexText ? wlen(e->indexText) : 0;   // v2：索引文字（0 = 空，不跟数据）
        buf_u32(&b, il);
        if (il) buf_put(&b, e->indexText, il * 2);
    }

    wchar_t tmp[MAX_PATH];
    wnsprintfW(tmp, MAX_PATH, L"%s.tmp", s->indexPath);
    HANDLE f = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD got = 0;
        if (WriteFile(f, b.p, b.len, &got, NULL) && got == b.len) {
            CloseHandle(f);
            MoveFileExW(tmp, s->indexPath, MOVEFILE_REPLACE_EXISTING);
        } else {
            CloseHandle(f);
            DeleteFileW(tmp);
        }
    }
    if (b.p) HeapFree(GetProcessHeap(), 0, b.p);
}

static void thumb_ensure(Store* s, Entry* e, BOOL force)
{
    wchar_t tp[MAX_PATH], src[MAX_PATH];
    store_build_thumb(s, e, tp, MAX_PATH);
    // 0 字节残留（曾编码失败）视为缺失
    WIN32_FILE_ATTRIBUTE_DATA fa;
    BOOL have = GetFileAttributesExW(tp, GetFileExInfoStandard, &fa);
    if (!force && have &&
        (fa.nFileSizeLow != 0 || fa.nFileSizeHigh != 0)) return;
    store_build_path(s, e, src, MAX_PATH);
    DeleteFileW(tp);
    if (!wic_make_thumb(src, tp, 256))
        DeleteFileW(tp);   // 失败不留空壳，下次扫描重试
}

int store_scan(Store* s)
{
    CreateDirectoryW(s->memesDir, NULL);
    CreateDirectoryW(s->thumbsDir, NULL);
    int added = 0;

    wchar_t pat[MAX_PATH];
    wnsprintfW(pat, MAX_PATH, L"%s\\*", s->memesDir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchNameMatch,
                                NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            u64 size  = ((u64)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            u64 mtime = ((u64)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
            int i = store_find(s, fd.cFileName);
            if (i >= 0) {
                if (s->entries[i].size != size || s->entries[i].mtime != mtime) {
                    s->entries[i].size = size;
                    s->entries[i].mtime = mtime;
                    thumb_ensure(s, &s->entries[i], TRUE);
                }
            } else {
                int ni = store_add(s, fd.cFileName, size, mtime, 0);
                if (ni >= 0) {
                    added++;
                    thumb_ensure(s, &s->entries[ni], FALSE);
                }
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    // 移除磁盘上已消失的条目
    for (int i = s->count - 1; i >= 0; i--) {
        wchar_t p[MAX_PATH];
        store_build_path(s, &s->entries[i], p, MAX_PATH);
        if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES)
            store_delete_at(s, i);
    }

    store_save(s);
    return added;
}
