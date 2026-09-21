# 01-halo-refine — 琉璃精修 NOTES

STATUS: DONE

基线身份/结构/交互全保留，工艺按 BRIEF 十项全部落地；构建通过（61,440 bytes，上限 90KB），
三张验收截图 + 两张附加验证截图（空态 / 搜索聚焦）均已用像素探针确认。

## Token 摘要（相对基线的增量）

| token | 基线 | 精修 |
|---|---|---|
| hover 亮边 alpha | 191 | **230** |
| hover 上浮 | 2px | **3px** |
| hover 微光 peak | 0.22 | **0.34** |
| hover 名字条 | 高 20 / 9px / 会折行 | 高 **22** / **10px** / **单行省略** |
| 名字条 scrim | 竖直 LineBrush（实际渲染 ±5/255） | **7 段实心阶梯**（24→206 alpha） |
| 激活胶囊厚度 | 无 | 顶部 1px 内亮线 (α92) + 下缘 1px 接触阴影 (α115 黑) |
| 删除武装态 | accent 渐变 + 红描边 | **红系专属渐变 #FF6E6E→#E34545**（α238）+ 厚度感 |
| 滚动条 | 3px 宽 / 贴网格右缘 / α46 | **4px / 面板边内 2px / r2 / α36 / 两端 3 段渐隐** |
| 大图预览 | α240 底 + 1px 亮边 | **α248 + 双线框（外亮内暗）+ 四角 L 形取景角标（1.5px accent α180）** |
| caret | 1.6px 平头 | **1.8px + 两端 1px 收细**（圆头近似） |
| 空态 | 纯文字 | **虚线圆角拖放区（DashStyle 1.2px）+ accent 下落箭头（1.6px）** |
| GIF 角标 | α168 / 9px | **α185 + 1px 亮边 + 9.5px**（GdipCreateFont REAL emSize）+ 文字 α232 |
| 搜索聚焦 | 无 | **预渲染 glow（peak 0.10）拉伸至框 rect** |
| mgr 状态行 | α150（且实际渲染在负宽矩形里） | **α185 + 修复矩形**；计数 α120 |
| mgr 胶囊对齐 | 与按钮顶对齐 | **垂直居中对齐**（下移 2px） |

## 间距模数表（逻辑 px，全部 4 的倍数）

- **4**：滚动条宽/边距、角标内缩、caret 收细段
- **8**：胶囊间距（7→8）、双模式网格间距（mgr 10→8）、检索→标签、标签→最近、网格→footer
- **12**：关闭钮间隙、预览右/下边距、mgr 操作行→网格、footer 底边距
- **16**：padX、padT（14→16）、最近→网格
- **20**：footer 高（18→20）
- 组件高度：检索 38 / 关闭 28 / 胶囊 26 / 按钮 30 / 名字条 22 / 预览 168

## 改动清单

- `src/main.c`：tokens 区（HOV_EDGE_A/HOV_LIFT/HOV_GLOW/FOCUS_GLOW/DANGER*）；新增 `pill_gradient` / `pill_thickness` / `danger_pill`，`glass_cell` 重构（hover 边 230、激活胶囊厚度线）；`panel_layout` 4px 模数 + 对称间距 + footer 左右界；`draw_search_box` 聚焦发光 + caret 收细；`draw_recents`/`draw_grid` 上浮 3px；`draw_chip` 间距 8；`draw_empty` 虚线区 + 箭头；`draw_grid` 名字条（阶梯 scrim + g_sfNW 单行）、GIF 角标三升级、悬浮滚动条、**选中角标移到图之上**；`draw_preview` 双线框 + 取景角标；`draw_mgr_button` 红渐变武装态；`draw_mgr` 胶囊居中 + 状态行对比度；entry：g_fName(10px)、g_fTiny(9.5px REAL)、g_glowFocus。
- `src/gp.h`/`gp.c`：增补 `GdipSetPenDashStyle`、`GdipCreateFontFamilyFromName`、`GdipCreateFont`（签名抄自 SDK 26100 gdiplusflat.h，宏表自动填充）。

### 顺带修掉的基线潜在 bug（本变体可见性直接受益）

1. **hover 名字条 scrim 从未真正渲染**：竖直、x=0 轴的 LineBrush 在填充区只出 ±5/255（基线像素可证）。改为 7 段实心阶梯，确定性渲染。
2. **名字条文字折成两行**（默认 StringFormat 可换行）→ 新增 `g_sfNW`（NoWrap+省略号）。
3. **mgr 选中角标/描边画在 contain 图之下**被满幅缩略图盖住 → 移到图之后。
4. **`g_footer.left/right` 从未赋值**（保持 0）：footer 文件名从 x=0 起排（压到圆角出血），mgr 状态行/计数矩形为负宽（状态行在 mgr 截图里实际不可见）→ panel_layout 两个分支都补上左右界。
5. mgr 胶囊与按钮顶对齐高低不平 → 垂直居中。

## 自检结果（dpi=120，S(v)=1.25v）

- 构建通过，61,440 bytes。
- shot_panel：面板玻璃 29,31,44 vs 桌面 55,55,63；caret 精确 accent **122,162,255**；激活胶囊 61,73,107 vs 面板 27,31,44。
- shot_hover：hover 边行 99,101,120 vs 普通格基线 31,34,47（+68 亮度、蓝移，另有 4 物理像素上浮 + 名字条）；名字条底部 scrim **35,33,32**（改前 152,134,111）；名字文字单行（白像素集中在 270-279 行）；预览取景角标 117,142,204（accent 调）；footer 文件名行 838-847、起排 x≈24（修复后）。
- shot_mgr_sel：勾角标 **120,157,242**（改前被图盖住为 101,105,86）；选中描边 121,158,229；胶囊行居中采样正常；状态行 max R **173**（修复矩形 + α185 后；修复前该区域无文字像素）。
- shot_empty（附加）：箭头杆 99,130,204（accent）；虚线顶边扫 3 行阈值 55 命中 264/334（≈ dash 占空比）。
- shot_focus（附加）：搜索框上方 +10~14 亮度且蓝偏（peak 0.10 发光）；输入后 caret 仍精确 accent。
- 视觉复核（analyze_image ×2，均成功）：hover 态/名字条/取景角标/激活胶囊确认；mgr 胶囊与按钮居中、单选勾标、状态行+计数单行无重叠确认。复核提出的「状态行对比度过低」已按建议提亮（α150→185）。
- 命中矩形走查：g_searchBox/g_closeRc/g_chipRc/g_grid(cell+gap)/g_recent/g_btnRc 全部由同一布局源计算，绘制与命中一致；删除按钮宽度仍按「确认删除」固定；交互结构零回归。

## 已知不足

- **滚动条溢出态**未能像素验证：演示库 42 张 < 两模式可见格数（98/66），永不触发；代码走查通过（位置/宽度/渐隐阶梯独立于命中）。
- **删除武装态红渐变**无 -shot 开关可达（无武装标志），仅代码走查 + danger_pill 与激活胶囊共用已验证的 pill_gradient 路径。
- 悬停边 1px 在高分辨率下仍是单行像素（AA 后 ~99,101,120）；「缩到 50% 仍可见」主要靠名字条 + 上浮 + 边线三者叠加达成。
- GIF 角标 9.5px 依赖 GdipCreateFont 的 REAL emSize；若系统缺失 Segoe UI family 走 S(9) 兜底（视觉差 0.5px）。
- 名字条 scrim 用 7 段阶梯而非连续渐变（4px 段在人眼 120dpi 下不可分辨，但严格说是近似）。
