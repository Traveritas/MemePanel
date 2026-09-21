# MemePanel — 操作手册

极简高性能表情包面板。常驻托盘，`Ctrl+Shift+.` 呼出，点击即发送到当前聊天窗口（保留 GIF 动画）。

**约 110 KB 单文件 exe，零依赖，无运行时**（纯 C + Win32，无 CRT，WIC 系统解码）。视觉：**SWISS 黑白**——纸白方角面 + 2px 墨框 + 单一强调色（默认正红，可改），零圆角零阴影零渐变，网格只靠留白分格。

## 使用

| 操作 | 效果 |
|---|---|
| `Ctrl+Shift+.` | 呼出/隐藏面板（默认记忆位置；托盘/设置可切换「呼出跟随光标」） |
| 输入文字（含中文 IME） | 按标签/索引文字/文件名（可选）即时过滤（自绘输入框 + 组合串下划线） |
| 点击缩略图 | 三格式同贴到之前焦点窗口（自定义 GIF/PNG + CF_HDROP + CF_DIB，QQ/微信/Telegram 保留 GIF 动画） |
| `Ctrl`+点击 / `Ctrl`+`空格` | 多选（四角角标；批量打标签/批量删除）——鼠标/键盘同义 |
| Enter | 发送选中/首个可见；空库时打开导入窗 |
| Esc / 点击别处 / × | 分层收起：chips 焦点 → 索引框 → 搜索框 → 关抽屉 → 隐藏面板 |
| `↑↓←→` / `PgUp` `PgDn` / 滚轮 | 网格导航与翻页；首行 `↑` 贯通进最近行、再 `↑` 到标签行（首格 `←` 直达标签行） |
| 最近行 | 固定第一行（槽位=网格列数，满员轮换最旧的）；`←→` 移动、`Enter` 即发、`↓` 回网格；行下墨线与图片墙分节 |
| 标签行滚轮 | 横向滚动标签（吸附标签边界，任何位置不切半个字；`‹` `›` 提示还有隐藏标签） |
| 标签行/抽屉 chips 焦点 | 红色粗下缘线高亮，`←→` 移动，`Enter`/`空格` 切换筛选或打/去标签，`↓`/`Esc` 离开 |
| `Tab` | 展开/收起检视抽屉；单目标打开即聚焦索引文字框，`↑` 进标签 chips、`↓` 回索引框 |
| `Ctrl`+`I` / 菜单键 / 右键 | 打开/重指向选中项的检视抽屉 |
| `F2`（抽屉内名称框） | 重命名选中项（原名写回索引；导入判重后显示名即原文件名） |
| `Ctrl`+`D` | 收藏/取消收藏当前选中项（= 抽屉 ★；前置排序 + 右上角标） |
| `Ctrl`+`Delete` | 武装删除当前选中项（两段式：先开抽屉看清 → 武装 3 秒 → 再按确认） |
| `Ctrl`+滚轮 | 调节窗口不透明度（30%–100% 八档，三窗同步，prefs 持久化） |
| 拖动顶带（搜索框上方/周围空白带） | 移动面板（十字光标区域），落点记忆 |
| 悬停格子 ≥280ms | 右下角 168px 大图浮层 |
| 托盘右键 | 打开面板 / 导入… / 设置… / 呼出跟随光标 / 退出 |

数据全部放在 exe 同目录（绿色便携）：`memes/` 图片库（**内容寻址：FNV-1a 64 哈希命名，同图全局只存一份**）、`thumbs/` 首帧 JPEG 缩略图缓存、`index.bin` 索引（v4：hash + 原始文件名字段；v1–v3 自动迁移并留 `.bak`；内存快照式序列化，tmp + 原子替换）、`prefs.cfg` 偏好、`theme.cfg` 主题色。

标签为 64 位掩码：每图 8 字节、最多 64 个标签，筛选是一次内存位与扫描（万张微秒级）。

## 构建

需要 VS2022 C++ 生成工具（x64）。任一终端：

```
build.bat
```

产物 `out\MemePanel.exe`（约 110 KB）。重新链接前若实例在跑，先 `taskkill /IM MemePanel.exe /F`（运行中的 exe 会锁文件）。

## 架构（v0.18 SWISS 渲染层）

- **窗口**：`WS_EX_LAYERED` + `UpdateLayeredWindow` 提交预乘 32bpp DIB；四周留出血边（2px 墨框 + alpha=0 穿透带）。**绝不调 `SetLayeredWindowAttributes`**。淡入/淡出 = 整帧预乘 alpha 双路位乘。
- **静态底图**（resize/hide→show 时生成一次）：纸白实心面 + 2px 墨框，缓存于 `g_base`，每帧一次 memcpy；不透明度非满档时自动切半透明底。
- **动态件**：全部 GDI+（flat C，`gp.c` 动态绑定 GetProcAddress，零导入表）——GDI 直写 32bpp DIB 会破坏 alpha 通道，这是全部走 GDI+ 的根因。文字 `GdipDrawString` 灰度 AA。
- **管理窗框架**（标签/导入/设置）：三个独立分层顶层窗，`mgr_swap` 临时换绑全局绘制句柄复用面板绘制函数；关闭释放表面、打开按需重建（勿依赖 WM_SIZE）。
- **输入框自绘**：无 EDIT 子窗口。WM_CHAR 直收 + `WM_IME_COMPOSITION`（组合串下划线自绘），`ImmSetCompositionWindow` 跟随光标；多缓冲 `EBuf` 焦点路由（搜索/索引/新标签/名称框）。
- **热路径（常驻）**：托盘 + 隐藏窗口 + 索引（RAM）+ 缩略图环形缓存；最近行/大图预览各带单项解码缓存；视口内 GIF 帧动画（解码与渲染分离）。

关键领域知识（踩坑记录全量见 HANDOFF.md）：
- GDI+ 对 32bpp DIB 的正确姿势：`GdipCreateBitmapFromScan0(stride=w*4, PixelFormat32bppPARGB, bmBits)` 包 DIB 像素 + `GdipGetImageGraphicsContext`。
- SDK flat API 字符串对齐函数名是 `GdipSetStringFormatAlign`（没有 "ment"）。
- 粘贴必须 CF_HDROP；粘贴前 20–100ms 焦点落定延迟是承重墙。
- 量宽必须与绘制同源（凡显示文本经过变换（如大写化），量宽喂变换后的文本）。
- PowerShell 临时脚本一律纯 ASCII（无 BOM 中文注释会破坏 PS5.1 解析）。

## 源码

```
src/
  main.c     面板/托盘/热键/渲染/输入框+IME/粘贴/导入/三管理窗（~5900 行）
  gp.c/.h    GDI+ flat C API 动态绑定（GetProcAddress 宏表）
  store.c    索引序列化（v4）+ 目录扫描 + 标签 + 内容哈希
  wicthumb.c WIC 缩略图生成/加载（COM C 风格）
  util.c     CRT-free 基础函数（memcpy/memset/宽字符）
```

版本沿革与决策史见 `../HANDOFF.md`；待办与已否决项见 `../BACKLOG.md`。
