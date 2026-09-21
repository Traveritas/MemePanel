# 10 复古像素 RETRO-PIXEL — 交付说明

STATUS: DONE

## Token 摘要

| Token | 值 | 用途 |
|---|---|---|
| 面板底 | `#DDDDDD` 不透明（经典 Mac 浅灰，选 Mac 灰与 05 瑞士白区分） | gen_base 行填充 |
| 面板外框 | 2px 纯黑硬边 + 内 1px 白高光（上左）+ 1px `#808080`（下右） | double-bevel，圆角 0 |
| 标题栏 | 顶部 `S(26)` 水平条纹带（黑白 2px 相间，System 7）+ 白底铭牌「MemePanel」Tahoma bold + 右端小方块关闭钮（黑边白底 / hover 黑底白 X）+ 底部 2px 黑分隔线 | 条纹烤进底图；铭牌/关闭每帧画 |
| 强调 | Mac 系统蓝 `#0000FF`（激活标签字 / hover 选中框 / 选中蓝罩）；警示红 `#FF0000`（删除武装） | |
| 斜面按钮 | 常态 灰底+上左 1px 白+下右 1px 黑；按下/激活 反转 + `#BBBBBB` 底 + 蓝字；hover `#E8E8E8` + 外 1px 黑点线框（点距 3px，Mac focus ring） | `bevel_cell()` |
| 搜索框 | 白底 + 黑 2px 边凹槽（内凹斜面：上左 `#808080`）+ 黑放大镜 + 黑 caret | |
| 网格 | 表格：白底 + 1px 黑外框 + cell 间 1px 黑线、0px 间距 0 圆角；尾列余量 ≥ 2/3 格宽时收窄尾列 | 线在图片之后画，图片内缩 2px 不遮线 |
| hover | 蓝 2px 框（扩 1px）+ 底部黑条白字文件名（状态栏样式，9px Tahoma） | |
| 滚动条 | 真 Win95：track `#E0E0E0` + 竖向刻度线、thumb 斜面灰块带 3 条 `#808080` 刻痕、上下箭头黑像素三角（`tri_px()` 行填充），宽 `S(16)` 贴表格右侧 | |
| 状态栏 | 凹槽（上左 `#808080` 下右白）+ 黑字；左：悬停文件名/管理状态；右：`N items` / `选中 N · 共 M`（Consolas） | |
| 大图预览 | 「窗中窗」浮窗：白底 + 黑 2px 边 + 条纹小标题「Preview」（fTinyB 铭牌） | |
| 管理模式 | 大格（96px 逻辑）表格 + 格底常驻文件名；选中 = 整格蓝 α200 + 文件名反白（Mac 文件选中式）；武装删除 = 红底白字 + 反转斜面 | |
| GIF 角标 | 黑底白字方块「GIF」 | |
| 字体 | Tahoma 12/10/9（正文/小字/名字条）、标题栏 Tahoma bold 12、计数 Consolas 10；文字渲染 SingleBitPerPixelGridFit + SmoothingModeNone（点阵感） | |
| 图标 | 灰底方窗 + 3px 黑边 + 条纹标题 + 黑像素笑脸（复古化） | |

## 改动清单（仅 src/main.c）

- **tokens/画刷**：删琉璃 tokens（T_ACC/C_INK/glow/penEdge），新增 12 个 RC_* 常量 + `g_br[BR_COUNT]` 实心画刷缓存（entry 一次创建）。
- **核心工艺**：`fr()`（1px 级实心小矩形，无 AA）、`frc()`、`bevel_cell()`、`sunken_field()`、`black_frame1/2()`、`blue_frame2()`、`stripes()`、`tri_px()`；删 `glass_cell/draw_glow/make_glow/free_statics/fill_rr/stroke_rr/sd_round/xsqrt`。
- **gen_base 重写**：不透明 #DDDDDD 行填充 + 2px 黑边 + 白/#808080 内斜面 + 标题条纹（抵边）+ 2px 黑分隔线；纯像素循环，无 GDI+。
- **panel_layout 重写**：标题栏占顶部（关闭钮移入标题栏右端，命中区=方块本身 S(20)）；速发：搜索(32)→标签(24)→最近 7 格→表格→状态栏(20)；管理：操作行→大格表格→状态栏；表格列/行计算（尾列 2/3 规则 + `g_tblR`）；**补上原版就缺失的 `g_footer.left/right` 赋值**（原版只赋 top/bottom，状态文字一直画在 x≈0 的位置）。
- **cell_from_point**：改按表格 1px 外框基准（`-1` 偏移，stride=g_cell，g_gap=0），滚动条/填缝/底行余量不命中；shot 的 -hover 假鼠标同基准。
- **draw 系列全部重写**：`draw_titlebar`（新增）/ `draw_search_box` / `draw_recents` / `draw_chip` / `draw_empty`（点线拖放框）/ `draw_grid`（表格+hover+选中+GIF 角标）/ `draw_scrollbar`（新增）/ `draw_footer`（凹槽状态栏，管理模式由 draw_mgr 调用）/ `draw_preview`（窗中窗）/ `draw_mgr_button`（斜面+武装红）/ `draw_mgr`（按钮组右界改为贴面板右缘）。
- **entry**：Tahoma/Consolas 字体组（新增 fTinyB/fTitle）+ 画刷初始化；rebuild_surface 改 SmoothingModeNone + SingleBitPerPixelGridFit；图标复古化。
- 交互（搜索/IME/点击发送/管理/热键/拖放/GIF 动画）零改动，仅命中矩形随布局同步。

## 自检结果

- 构建：`cmd //c build.bat` 通过，**out/MemePanel.exe 57,856 字节**（< 90KB 软上限）。
- 三张无头截图：shot_panel.png（1185×872）/ shot_hover.png（-hover 5）/ shot_mgr_sel.png（-mgr -hover 3，1544×1064）。
- **像素探针 `probe.ps1`：50/50 PASS**（PowerShell System.Drawing，坐标由 dpi=120/bleed=4 换算）：
  - 面板黑边/白高光/#808080、条纹 2px 周期交替（y7..14 逐行）、铭牌、关闭钮三态结构；
  - 搜索框黑 2px 边/白底/内凹 #808080/caret；
  - 斜面按钮四边（按下态上黑下白 + #BBBBBB 底 + 蓝字）、管理模式按钮行黑白边齐全；
  - 表格外框/竖线/横线/底部填缝白、白底；
  - 滚动条上下黑三角、thumb #DDDDDD、3 刻痕 #808080、track 刻度；
  - hover：蓝 2px 框（左/上带）、黑名字条+白字、预览浮窗黑边/条纹/白铭牌、footer 文件名；
  - 管理：选中格蓝罩（α200 混色）+ 蓝框、格底文件名、状态栏文字、右端计数。
- **视觉复核（analyze_image）**：shot_panel.png 成功——确认条纹标题栏+铭牌+关闭钮、灰底黑边直角、白底搜索框、激活「全部」斜面钮、7 格最近行、表格网格、Win95 滚动条、凹槽状态栏，无错位/重叠/缺失；提到的「彩色竖条」经查为演示数据中 4 张窄图（如 dance.gif 32×256）contain 适配的正常渲染。shot_mgr_sel.png 复核因 API 报错放弃（按有界规则未重试），该帧已有 12 项像素探针全过。

## 已知不足

- **演示数据无标签**：本目录 out/index.bin 实测 nTags=0（AGENT-GUIDE 所述「动物/猫猫」不在数据里），故标签行只显示「全部」；数据换回含标签的 index 后其余标签按钮即会出现（绘制/命中逻辑与标签数无关）。管理模式 shot 的状态行「已添加标签…」为原版硬编码样例文案。
- 滚动条为常驻显示（内容放得下时 thumb 满轨），箭头/刻痕纯视觉、不可点击拖拽——沿用原版「滚轮/翻页键滚动」的交互边界。
- SingleBitPerPixelGridFit 点阵小字（9px 名字条）在低分屏略毛糙——属于本主题的美学取向。
- 管理模式 hover 不再画黑名字条（与常驻文件名条冲突，仅保留蓝框）；速发模式完整保留。

## 验证命令

```
cmd /c build.bat
out\MemePanel.exe -shot shot_panel.png
out\MemePanel.exe -shot shot_hover.png -hover 5
out\MemePanel.exe -shot shot_mgr_sel.png -mgr -hover 3
powershell -NoProfile -ExecutionPolicy Bypass -File probe.ps1
```
