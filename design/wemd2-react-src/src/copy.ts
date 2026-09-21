import type { SchemeId } from "./schemes";

export const COPY: Record<
  SchemeId,
  { intent: string; type: string; rhythm: string; gdi: string }
> = {
  sortes: {
    intent:
      "把表情库当成活字盘，而不是又一个深色卡片后台。用户是在拣字，不是在浏览内容流。铅白、墨线和一点朱砂，是为了让五千张图的密度被格子消化，而不是被阴影和圆角吞掉。",
    type: "检索是一条横线，不是胶囊。分类是辞书式书口。计数用等宽数字。朱砂只出现在「当前」与「动」。标题用印：一方朱文。",
    rhythm:
      "左 100px 答「在哪」，右答「发哪张」。最近单独成行并编号——重度用户八成点击发生在这里。槽 6px，格无圆角，选中是 1px 朱框。",
    gdi: "两块矩形。细线 1px Pen。阴影是右下偏移 3px 的实心矩形，不是高斯模糊。虚拟列表只画可见格。拖入用 WM_DROPFILES。",
  },
  prompt: {
    intent:
      "把呼出瞬间变成打一条命令。搜索是第一公民，分类是功能键，最近是数字键。气质接近合成器的 LCD 或老式行情终端：克制、高密、不发光污染。琥珀色不是赛博，是灯下的黄铜。",
    type: "全部等宽。状态栏一行说完。F1–F8 印在表面上，键位即界面。块状光标，反白数字键标。",
    rhythm:
      "零圆角、2px 缝、十一列。面板可以更小，因为它不靠看图的呼吸感，靠键位记忆。先打字，再动手。",
    gdi: "最便宜。FillRect + TextOut。选中黄铜框或 InvertRect。数字键是小 FillRect 反白。无圆角、无渐变、无透明。",
  },
  lightbox: {
    intent:
      "chrome 退到几乎没有。表情是灯箱上的样片。用户在扫，不是在使用软件。中性灰让任何一张 GIF 不受界面染色。选中用角括号，来自裁切工具，不是卡片描边。",
    type: "界面字小、字重 Regular、字距略开。说明出现在底部一行，像照片题注，不挂在图上。检索甚至没有盒子，只有一条字和光标。",
    rhythm:
      "宽幅、浅槽 3px、三行样片加底片条。面板压在屏幕下三分之一，不挡对话上半。悬停 280ms 后才出大图，避免扫视被打断。",
    gdi: "背景一片灰。缩略图 BitBlt。角括号八次 LineTo。无圆角。GIF 悬停用定时器换帧，平时只画第 0 帧。",
  },
  cassette: {
    intent:
      "这是一个物件而不是窗口。铜边、内凹检索、双尺：上面五张最近（手的落点），下面微缩库（眼睛的扫视）。斗图的真实节奏是反复那几张，偶而下潜。",
    type: "「匣」字压在铜边上，像器物铭文。页脚像相机 LCD。分类是铜下划线，不是胶囊。",
    rhythm:
      "外壳 6px 圆角（全案唯一允许的圆），内部直角。最近 72px，库 36px，中间一条虚线像匣盖的缝。贴着输入框生长，点完即走。",
    gdi: "RoundRect 外壳一次。铜边左侧 4px FillRect。内凹是更深的 FillRect 加 1px 暗线。虚线 PS_DOT。双尺只是两套 itemSize 的 owner-draw。",
  },
  halo: {
    intent:
      "把前四案不敢用的系统牌全部打出来：msimg32 的 AlphaBlend 与 GradientFill、UpdateLayeredWindow 的逐像素 alpha、GDI+ 的抗锯齿。面板是一块深色琉璃——半透明、边缘一线高光、脚下是真正的软阴影。信息结构沿用共识（最近优先、检索、标签），全部预算花在质感上。",
    type: "Segoe UI Variable 风格的字阶。计数等宽。数字键标是玻璃小片。发光只给「当前」：一枚渐变胶囊、一圈 6px 的柔光，点到为止。",
    rhythm:
      "圆角 18/12/10 三档。最近七格 68px 带键标，网格 56px 玻璃格。hover 上浮 2px 亮边加微光，悬停大图带软阴影浮在网格上。整窗半透明让壁纸透一点进来——它属于这台桌面，不属于某个软件。",
    gdi: "AlphaBlend 盖预计算 alpha 阴影位图（软阴影）；GradientFill 画渐变胶囊与格子底；GDI+ 抗锯齿圆角与文字；UpdateLayeredWindow 做整窗半透明。全部系统 DLL，零依赖，60fps。",
  },
};

export const BRIEF = {
  product: "即帖",
  latin: "JÌTIĒ",
  line: "全局热键呼出的悬浮面板。点一张，贴进微信 / QQ。",
  users: "重度斗图。收藏 500–5000 张，以 GIF 为主。",
  loop: "呼出 → 浏览 / 检索 → 单击 → 剪贴板粘贴。",
};
