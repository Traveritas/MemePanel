# 变体编排状态（2026-09-19 夜间自主会话）—— **终态：10/10 完成，10/10 验收通过**

| 变体 | 状态 | 验收 |
|---|---|---|
| 01-halo-refine | DONE | ✅ 通过（悬停/取景角标/滚动条/精致度对比全确认） |
| 02-liquid | DONE | ✅ 通过（材质层次/色散/受光确认） |
| 03-paper-ink | DONE | ✅ 通过（宣纸/印章/无框搜索确认） |
| 04-editorial | DONE | ✅ 通过（刊头/发丝线/图注/红纪律确认；限流重启后完成） |
| 05-swiss | DONE | ✅ 通过（瑞士气质/横梁/图片墙/红克制确认） |
| 06-terminal | DONE | ✅ 通过（CRT 气质/扫描线/双线框/命令行确认） |
| 07-candy | DONE | ✅ 通过（糖果氛围/立体感/鼓起/糖纸边确认） |
| 08-darkroom | DONE | ✅ 通过（齿孔/标签条/帧编号/grease 红圈确认；数据统一后复验 3 胶囊） |
| 09-palette | DONE | ✅ 通过（竖长条/行结构/选中态/命令行确认） |
| 10-retro-pixel | DONE | ✅ 通过（System 7 气质/条纹/斜面/表格确认） |

数据已统一（04 的 index.bin：动物/猫猫标签 + 保留 size/mtime）覆盖 app/out + 全部变体，33 张截图全部重生成，画廊引用全部校验通过。

## 基线 bug 回馈清单（收尾时做，勿忘）

1. **LineBrush 静默失败**：本机 GdipCreateLineBrushI/F 返回 NotImplemented(6)——基线渐变胶囊/hover scrim 从未渲染。修法参考 01（阶梯实心条）或 07（逐 1px 条带）。要修 app/src/main.c。
2. **g_footer.left/right 未赋值**：panel_layout 两分支都只赋 top/bottom，footer/mgr 状态行画在零宽/负宽矩形不可见。修 app/src/main.c。
3. **演示数据无标签**：app/out/index.bin nTags=0（与记忆中「动物/猫猫」不符，v0.7 期间丢失）。收尾：用 07-candy/out/index.bin（已重植 动物/猫猫）统一覆盖 app/out + 全部变体，并重新生成全部截图（含基线），再做最终快验。
4. 顺带把 footer 尾随·修复与无头模式已入基线（本次会话开头完成）。

## 验收流程（每个变体完成后）

1. 检查 variants/NN/ 下 NOTES.md 的 STATUS、三张 PNG 存在、exe 存在。
2. 视觉验收：Read PNG 得 CDN URL → mcp__4_5v_mcp__analyze_image（prompt 要点：brief 核心特征是否实现 + 明显缺陷）。每变体至少验 shot_panel.png；hover/mgr 抽查。注意 429 限流——串行慢速，失败重试一次。
3. P0/P1 问题 → SendMessage 对应 agent 修；小瑕疵自己修。
4. 更新本表状态列。
5. 全部完成 + 数据统一 + 重截图后：画廊页插入全部变体段 → HANDOFF → 记忆。

## 收尾清单

- [ ] 全部验收后：variants/GALLERY.html（对比画廊：基线 + 10 变体 × 3 图 + NOTES 摘要 + 推荐排序）
- [ ] HANDOFF.md 加「2026-09-19 夜间变体探索」段
- [ ] 更新记忆文件 memepanel-project-status.md（无头验证方法 + variants 索引）
- [ ] app/ 主线保留无头模式（已做）；变体不动 app/

## 备注

- 用户在睡觉：全程无窗口运行、无合成输入。视觉验证只走 -shot PNG + analyze_image。
- 基线像素坐标参考（速发窗 1185×872 物理）：面板左上 (3,3)；搜索框 y 20-67；标签行 y 82-114；最近行 y 131-201；网格 y 221 起，cell 70px 间 80px；hover 名字条为格底 25px。
- analyze_image 有时对含 `+`/`/` 的 CDN URL 报 1210 解析错——重试一次新 URL，仍失败就跳过该图改像素探针。
