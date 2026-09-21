# MemePanel GDI 设计变体 — 子智能体工作指南（必读）

你在实现 MemePanel（Windows 表情包速发面板，纯 C + Win32 + GDI+，无 CRT，单文件 exe）的**一个视觉设计变体**。你的目录是完全自包含的：`src/`（源码）、`build.bat`、`out/`（演示数据：42 张表情图 + 缩略图 + index.bin 索引，含「动物」「猫猫」两个标签）。

用户正在睡觉。**绝对禁止**：任何会干扰桌面的操作——不要运行带窗口的实例（不加 `-shot` 参数的运行）、不要模拟键盘鼠标、不要弹消息框。所有视觉验证只走下述无头管线。

## 工作流（有界循环，最多 3 轮构建-修复）

1. 读你的 `BRIEF.md`（设计方向）+ 本文件 + `src/main.c`（2296 行，通读绘制相关部分）。
2. 实现设计（主要改 main.c：文件顶部的 tokens、`gen_base`、`panel_layout`、`draw_*` 系列函数）。
3. 构建：`cmd //c build.bat`（在变体目录下；MSVC，需 VS2022——机器已装）。
4. 生成三张验证截图（在变体目录下运行，PNG 落在当前目录）：
   - `./out/MemePanel.exe -shot shot_panel.png`（速发模式 + 最近行 + 标签行 + 网格）
   - `./out/MemePanel.exe -shot shot_hover.png -hover 5`（悬停第 6 格：hover 高亮 + 名字条 + 168px 大图 + footer 文件名）
   - `./out/MemePanel.exe -shot shot_mgr_sel.png -mgr -hover 3`（管理模式：96px 大格 + 选中态勾角标 + 操作按钮 + 状态行）
5. 自检（你**看不到图片**，用两种手段）：
   a. **像素探针**：用 PowerShell + System.Drawing 采样 PNG 特定点，断言主题色/文字像素存在。模板：
   ```powershell
   Add-Type -AssemblyName System.Drawing
   $b = New-Object System.Drawing.Bitmap("shot_panel.png")
   # 例：面板中心应是深色底而非背景
   $c = $b.GetPixel(600, 400)
   Write-Host "center: $($c.R),$($c.G),$($c.B)"
   $b.Dispose()
   ```
   面板物理坐标参考：速发窗约 1185×872，面板左上角 ≈(3,3)；搜索框 ≈(23,20)-(1180,67)；标签行 y≈82-114；最近行 y≈131-201；网格从 y≈221 起，格 70px 间距 80px；管理模式窗 1537×1059。具体以你自己 panel_layout 的数值换算（S(v)=v*dpi/96，dpi=120）。
   b. **视觉复核（可选，先试一次）**：先 `Read` 你的 PNG（会返回一个 CDN URL），再调用 `mcp__4_5v_mcp__analyze_image` 工具传入该 URL + 描述问题的 prompt。**最多尝试 2 次**，报错就放弃转像素探针，不要纠缠。
6. 修复一轮问题 → 重新截图 → 再确认一轮 → **停止**（防无限打磨）。写入 `NOTES.md`。
7. 最终交付物：可构建的 exe + 3 张 PNG + `NOTES.md`（设计 token 摘要 / 结构改动 / 自检结果 / 已知不足）。

## 改动边界

- **只改 `src/main.c`**（含 `src/gp.h`/`gp.c` 允许增补 GDI+ flat API 绑定，照现有 `GP_LIST` 宏模式，函数签名从 SDK `gdiplusflat.h` 抄，不要凭记忆写）。
- 不要改 `store.c`/`wicthumb.c`/`util.c`（索引/解码/工具链稳定；wicthumb 里已有 `wic_save_png` 供 -shot 用）。
- 交互结构（搜索/标签筛选/点击发送/管理模式/热键）必须**功能完好**：你改了布局就要同步改命中矩形（`g_searchBox/g_closeRc/g_chipRc/g_grid/g_btnRc` 等必须与绘制一致，否则点击错位=废品）。
- `-shot` 无头模式必须持续可用（它是验收通道）。若你改了 `panel_layout`/`panel_show` 的尺寸策略，确认 shot 三态仍生成完整画面。
- exe 大小软上限 90KB；性能软底线：合成一帧 ≤20ms（预渲染静态件、避免每像素逐帧计算——参照现有 gen_base「resize 生成一次」的模式）。

## GDI 血泪坑（违反=白屏/破图，全部来自项目历史）

1. **绝不用 GDI 直写 32bpp DIB**（TextOut/BitBlt 会把 alpha 写 0）——矢量/文字/位图一律走 GDI+（现有 `gtext/fill_rr/stroke_rr/GdipDrawImageRectI` 模式）。
2. **ULW 模式下绝不调 SetLayeredWindowAttributes**。
3. GDI+ LineBrush 两点是世界坐标——动态位置的对象按需创建/销毁画刷（现有代码即此模式）。
4. `HeapReAlloc` 不接受 NULL；链表哨兵必须显式 -1（memset 0xFF），零初始化的 next 数组会死循环。
5. 改 `gen_base` 的阴影/发光参数时注意：出血只有 3px，任何向面板外溢出的效果会被硬裁切——要么加大 bleed（`g_bleed`，entry 里），要么把效果收在面板内。
6. 布局数据先更新、再 rebuild_surface 依赖它的缓存（panel_layout 里的顺序是承重墙，别动）。
7. 无 CRT：没有 libm。需要 sqrt/exp 用现有 `xsqrt`，或预生成位图查表。浮点可用（GDI+ 本身就是 float）。
8. 批处理/源码文件里注释保持 UTF-8 无问题（/utf-8 已开），但**别改 build.bat 的编码**。

## 可用系统字体（LOGFONTW lfFaceName）

`Segoe UI`（默认 UI）/ `Microsoft YaHei UI`（中文）/ `SimSun 宋体` / `KaiTi 楷体` / `Consolas`（等宽）/ `Georgia`（衬线西文）/ `Tahoma` / `Impact`（重标题）/ `Segoe UI Emoji`。中文字符在非中文字体上 GDI+ 会自动 fallback，但混排宽度需实测（`gtext_w` 可量）。

## 设计纪律（来自 impeccable 方法论）

- **Brief 优先**：你的 BRIEF.md 指定的美学方向、材质、字体、配色是硬约束，不要往「安全深色玻璃」回缩——那已经有了（当前版）。大胆做出区分度。
- **完整性**：三态（normal/hover/active）+ 空态 + 管理模式全部要有完整的主题化设计，不能只改面板底色。
- **一个间隙系统**：间距用一致的模数（如 4/8 的倍数），不要随机数值。
- **渐变/发光只给激活态**的原则可以按你的主题重新解释，但「强调色只干一件事」保留。
- 完成度 > 细节完美：一张 85 分的完整变体远胜一张 95 分的半成品。
