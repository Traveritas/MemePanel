# MemePanel

极简高性能表情包面板。常驻托盘，`Ctrl+Shift+.` 呼出，点击即发送到当前聊天窗口（保留 GIF 动画）。

**44 KB 单文件 exe，零依赖，无运行时**（纯 C + Win32，无 CRT，WIC 系统解码）。

视觉：**「琉璃 HALO」**深色玻璃浮层——`UpdateLayeredWindow` 整窗 32bpp 逐像素 alpha、双层距离场软阴影、164° 线性渐变 + 顶部径向高光、激活胶囊蓝紫渐变（唯一渐变对）、GDI+ 全矢量抗锯齿。规格见 `../design/halo-spec.md`。

## 使用

| 操作 | 效果 |
|---|---|
| `Ctrl+Shift+.` | 呼出/隐藏面板（默认拖过记住位置、同屏沿用；托盘「呼出跟随光标」可切换为每次都在光标附近弹出） |
| 输入文字（含中文 IME） | 按标签/索引文字/文件名即时过滤（自绘输入框 + 组合串下划线） |
| 点击缩略图 | 三格式同贴到之前焦点窗口（自定义 GIF/PNG + CF_HDROP + CF_DIB，QQ/微信/Telegram 保留 GIF 动画） |
| `Ctrl`+点击 / `Ctrl`+`空格` | 多选（四角角标；批量打标签/批量删除）——鼠标/键盘同义 |
| Enter | 发送选中/首个可见；空库时打开导入窗 |
| Esc / 点击别处 / × | 分层收起：chips 焦点 → 索引框 → 搜索框 → 关抽屉 → 隐藏面板 |
| `↑↓←→` / `PgUp` `PgDn` / 滚轮 | 网格导航与翻页；首行 `↑` 贯通进最近行、再 `↑` 到标签行（首格 `←` 直达标签行）；键盘选中 280ms 亮大图、GIF 悬停同款播放 |
| 最近行焦点 | 红框+编号高亮，`←→` 移动、`Enter` 即发（快发最近用过的表情）、`↓` 回网格 |
| `Tab` | 展开/收起检视抽屉；单目标打开即聚焦索引文字框（直接编辑），`↑` 进标签 chips、`↓` 回索引框 |
| 标签行/抽屉 chips 焦点 | 红色粗下缘线高亮（激活项同样显眼），`←→` 移动，`Enter`/`空格` 切换筛选或打/去标签，`↓`/`Esc` 离开 |
| `Ctrl`+`I` / 菜单键 | 打开/重指向选中项的检视抽屉（= 右键） |
| `Ctrl`+`D` | 收藏/取消收藏当前选中项（= 抽屉 ★） |
| `Ctrl`+`Delete` | 武装删除当前选中项（两段式：先开抽屉看清 → 武装 3 秒 → 再按确认） |
| `Ctrl`+滚轮 | 调节窗口不透明度（43%–100%，三窗同步，prefs 持久化；100% 为全实心，托盘子菜单有六档） |
| 拖动顶带（搜索框上方/周围空白带） | 移动面板（十字光标区域），落点记忆 |
| 悬停格子 ≥280ms | 右下角 168px 大图浮层 |
| 右键缩略图 | 展开右缘检视抽屉（标签/索引文字/收藏/武装删除） |
| 点击标签胶囊 | 按标签筛选（再点取消） |
| 托盘右键 | 打开面板 / 标签管理 / 导入 / 主题色 / 不透明度 / 呼出跟随光标 / 维护 / 退出 |

数据全部放在 exe 同目录（绿色便携）：`memes/` 图片库、`thumbs/` 首帧 JPEG 缩略图缓存、`index.bin` 索引（内存快照式序列化，tmp + 原子替换，损坏自动重建）、`prefs.cfg` 偏好（搜索开关/语言/最近行格数/面板记忆位/不透明度）、`theme.cfg` 主题色。

标签为 64 位掩码：每图 8 字节、最多 64 个标签，筛选是一次内存位与扫描（万张微秒级）。

## 构建

需要 VS2022（C++ 工作负载，x64）。任一终端：

```
build.bat
```

产物 `out\MemePanel.exe`（约 44 KB）。

## 设计规格（可验证的数字）

| 指标 | 目标 | 实测（2048×1280 屏，9 图测试库） |
|---|---|---|
| exe 体积 | ≤ 64 KB | 44,544 B ✅ |
| 外部依赖 | 0（gdiplus/imm32 系统库，前者动态加载） | ✅ |
| 常驻内存（隐藏） | ≤ 20 MB | ~6.4 MB private（收起即释放合成表面）✅ |
| 可见内存 | ≤ 20 MB | ~19.9 MB private ✅ |
| 热键→面板 | ≤ 50 ms | 常驻隐藏窗口 + 面板重建 ~5ms ✅ |
| 筛选/搜索 | ≤ 5 ms | 全库内存扫描 ✅ |

## 架构（v0.4 琉璃渲染层）

- **窗口**：`WS_EX_LAYERED` + `UpdateLayeredWindow` 提交预乘 32bpp DIB；窗口比视觉面板四周大 60px（阴影出血，alpha=0 区域天然点击穿透）。**绝不调 `SetLayeredWindowAttributes`**（会切回统一 alpha 模式）。淡入 = 整帧预乘 alpha 双路位乘。
- **静态底图**（resize/hide→show 时生成一次）：距离场软阴影（近 σ4 + 远 σ26 两层高斯）+ 164° 线性渐变 + 顶部径向高光 + GDI+ AA 亮边，缓存于 `g_base`，每帧一次 memcpy。
- **动态件**：全部 GDI+（flat C，`gp.c` 动态绑定 GetProcAddress，零导入表）——GDI 直写 32bpp DIB 会破坏 alpha 通道，这是全部走 GDI+ 的根因。文字用 `GdipDrawString`（灰度 AA，比 ClearType 更适合透明底）。
- **输入框自绘**：无 EDIT 子窗口（layered 主窗口下不可见）。WM_CHAR 直收 + `WM_IME_COMPOSITION`（GCS_COMPSTR 组合串下划线自绘 / GCS_RESULTSTR 结果串追加），`ImmSetCompositionWindow(CFS_FORCE_POSITION)` 跟随光标，`ISC_SHOWUICOMPOSITIONWINDOW` 关系统绘制。
- **热路径（常驻）**：托盘 + 隐藏窗口 + 索引（RAM）+ 缩略图环形缓存（768 张 GpBitmap 零拷贝视图）；最近格/大图预览各带单项解码缓存。
- **冷路径（按需，规划）**：管理窗口。

关键领域知识（踩坑记录，延续 HANDOFF.md）：
- GDI+ 对 32bpp DIB 的正确姿势：`GdipCreateBitmapFromScan0(stride=w*4, PixelFormat32bppPARGB, bmBits)` 包 DIB 像素 + `GdipGetImageGraphicsContext`，预乘 alpha 全链路正确。
- SDK flat API 里字符串对齐函数名是 **`GdipSetStringFormatAlign`**（没有 "ment"）；PixelFormat 常量公式 = `序号 | (bpp << 8) | 标志位`。
- CreateCompatibleDC 初始选入 1×1 stock 位图，DeleteObject 一个仍选入 DC 的 HBITMAP 会静默失败——换出再删。
- 粘贴必须 CF_HDROP；粘贴前 20–100ms 焦点落定延迟是承重墙。

## 路线图

- **v0.5**：视口内 GIF 动画（refterm 式瓦片缓存：解码与渲染分离，只动可见项）
- **v0.6**：管理窗口（批量打标/重命名/xxHash3 去重/QQNT 后缀修正/大图预览）
- **v0.7**：ReadDirectoryChangesW 增量同步、使用频率排序、性能指标自动化测试
- **v0.8**：SWISS 设计（瑞士网格/检视抽屉/选择集/主题色/IME 候选窗根治）
- **v0.9**：关闭淡出对称于淡入；中/英本地化（德文艺术字退役，托盘「语言」切换，prefs.cfg bit1 持久化，`-shot -lang en` 探针）
- **v0.10**：标签管理/导入双独立窗口（mgr_swap 复用面板绘制；主界面去导入化，空库点击即导入）；GIF/PNG 三格式同贴（自定义格式 + CF_HDROP + CF_DIB 24bpp）；收藏（flags v3，前置排序 + 右上角标 + 抽屉切换）；面板随鼠标显示器弹出；开机自启（HKCU Run，advapi32 动态加载）；最近行 1..7 可配（prefs v2 'MPP2'）；`-shot -win tags|import`、`-fav N` 探针

## 源码

```
src/
  main.c     面板/托盘/热键/琉璃渲染/自绘输入框+IME/粘贴/拖放导入
  gp.c/.h    GDI+ flat C API 动态绑定（GetProcAddress 宏表）
  store.c    索引序列化 + 目录扫描 + 标签
  wicthumb.c WIC 缩略图生成/加载（COM C 风格）
  util.c     CRT-free 基础函数（memcpy/memset/宽字符）
```
