import { STICKERS } from "../media";

export type MemeKind = "image" | "text";

export type Meme = {
  id: string;
  kind: MemeKind;
  src?: string;
  text?: string;
  bg?: string;
  fg?: string;
  tags: string[];
  category: string;
  gif?: boolean;
  name: string;
  contain?: boolean;
};

export type Category = {
  id: string;
  name: string;
  fkey: string;
  count: number;
};

export const CATEGORIES: Category[] = [
  { id: "all", name: "全部", fkey: "F1", count: 2847 },
  { id: "cats", name: "猫猫", fkey: "F2", count: 412 },
  { id: "panda", name: "熊猫头", fkey: "F3", count: 238 },
  { id: "text", name: "文字", fkey: "F4", count: 640 },
  { id: "animals", name: "动物", fkey: "F5", count: 890 },
  { id: "sweat", name: "流汗", fkey: "F6", count: 156 },
  { id: "code", name: "代码", fkey: "F7", count: 94 },
  { id: "untagged", name: "未分类", fkey: "F8", count: 417 },
];

const IMAGES: Omit<Meme, "id">[] = [
  {
    kind: "image",
    src: STICKERS.catShock,
    tags: ["猫", "震惊", "问号", "猫猫"],
    category: "cats",
    gif: true,
    name: "猫_震惊.gif",
  },
  {
    kind: "image",
    src: STICKERS.pandaSmug,
    tags: ["熊猫头", "得意", "呵呵", "不屑"],
    category: "panda",
    name: "熊猫_得意.png",
    contain: true,
  },
  {
    kind: "image",
    src: STICKERS.dogSideeye,
    tags: ["狗", "侧目", "怀疑", "柴犬"],
    category: "animals",
    gif: true,
    name: "柴犬_侧目.gif",
  },
  {
    kind: "image",
    src: STICKERS.hamster,
    tags: ["仓鼠", "吃", "塞满"],
    category: "animals",
    name: "仓鼠_塞满.png",
  },
  {
    kind: "image",
    src: STICKERS.frog,
    tags: ["青蛙", "无语", "盯"],
    category: "animals",
    name: "青蛙_无语.png",
  },
  {
    kind: "image",
    src: STICKERS.capybara,
    tags: ["水豚", "空白", "禅"],
    category: "animals",
    gif: true,
    name: "水豚_空白.gif",
  },
  {
    kind: "image",
    src: STICKERS.chicken,
    tags: ["鸡", "尖叫", "崩溃"],
    category: "animals",
    gif: true,
    name: "鸡_尖叫.gif",
  },
  {
    kind: "image",
    src: STICKERS.seal,
    tags: ["海豹", "鼓掌", "好"],
    category: "animals",
    name: "海豹_鼓掌.png",
  },
  {
    kind: "image",
    src: STICKERS.rabbit,
    tags: ["兔", "疑惑", "听不清"],
    category: "animals",
    name: "兔_疑惑.png",
  },
];

const TEXTS: Omit<Meme, "id">[] = [
  { kind: "text", text: "好家伙", bg: "#F5E642", fg: "#161616", tags: ["好家伙", "惊讶"], category: "text", name: "好家伙.png" },
  { kind: "text", text: "确实", bg: "#161616", fg: "#F5F0E8", tags: ["确实", "同意"], category: "text", name: "确实.png" },
  { kind: "text", text: "？", bg: "#2B2B2B", fg: "#FFFFFF", tags: ["问号", "疑问"], category: "text", name: "问号.png" },
  { kind: "text", text: "典", bg: "#C4A35A", fg: "#1A1A1A", tags: ["典", "讽刺"], category: "text", name: "典.png" },
  { kind: "text", text: "急了", bg: "#B43A2F", fg: "#FFFFFF", tags: ["急了", "破防"], category: "text", name: "急了.png" },
  { kind: "text", text: "蚌埠住了", bg: "#EDE6DB", fg: "#1A1A1A", tags: ["蚌埠", "忍不住"], category: "text", name: "蚌埠住了.png" },
  { kind: "text", text: "啊对对对", bg: "#1A1A1A", fg: "#E8D5A3", tags: ["敷衍", "啊对对对"], category: "sweat", name: "啊对对对.png" },
  { kind: "text", text: "就这", bg: "#4A5560", fg: "#FFFFFF", tags: ["就这", "不屑"], category: "text", name: "就这.png" },
  { kind: "text", text: "破防了", bg: "#6B2D5B", fg: "#F5E6F0", tags: ["破防"], category: "text", name: "破防了.png" },
  { kind: "text", text: "遥遥领先", bg: "#C45C26", fg: "#FFFFFF", tags: ["遥遥领先", "嘲"], category: "text", name: "遥遥领先.png" },
  { kind: "text", text: "已阅", bg: "#2F4A3C", fg: "#D4E8D8", tags: ["已阅", "收到"], category: "text", name: "已阅.png" },
  { kind: "text", text: "晚安", bg: "#1C2430", fg: "#C5D0DC", tags: ["晚安"], category: "text", name: "晚安.png" },
  { kind: "text", text: "哈？", bg: "#E8E0D0", fg: "#1A1A1A", tags: ["哈", "疑问"], category: "text", name: "哈.png" },
  { kind: "text", text: "不是吧叔", bg: "#3D3D3D", fg: "#F0E6D0", tags: ["不是吧", "叔"], category: "text", name: "不是吧叔.png" },
  { kind: "text", text: "我不行了", bg: "#8B3A3A", fg: "#FAD9D9", tags: ["不行了", "笑"], category: "text", name: "我不行了.png" },
  { kind: "text", text: "收到", bg: "#2C5F4F", fg: "#E0F0EA", tags: ["收到"], category: "text", name: "收到.png" },
  { kind: "text", text: "笑死", bg: "#1A1A1A", fg: "#F5E642", tags: ["笑死"], category: "text", name: "笑死.png" },
  { kind: "text", text: "下头", bg: "#4A4A4A", fg: "#E8E8E8", tags: ["下头"], category: "text", name: "下头.png" },
  { kind: "text", text: "尊嘟假嘟", bg: "#F0D5C4", fg: "#3A2010", tags: ["尊嘟假嘟"], category: "text", name: "尊嘟假嘟.png" },
  { kind: "text", text: "救命", bg: "#8B1E1E", fg: "#FFFFFF", tags: ["救命"], category: "text", name: "救命.png" },
  { kind: "text", text: "可以的", bg: "#3A5A40", fg: "#E8F0E0", tags: ["可以的"], category: "text", name: "可以的.png" },
  { kind: "text", text: "不懂就问", bg: "#2A3A5A", fg: "#D0D8E8", tags: ["不懂就问"], category: "text", name: "不懂就问.png" },
  { kind: "text", text: "差不多得了", bg: "#E8E4DC", fg: "#1A1A1A", tags: ["差不多得了"], category: "sweat", name: "差不多得了.png" },
  { kind: "text", text: "你说得对", bg: "#111111", fg: "#AAAAAA", tags: ["你说得对", "敷衍"], category: "sweat", name: "你说得对.png" },
  { kind: "text", text: "属于是", bg: "#D4C4A8", fg: "#1A1A1A", tags: ["属于是"], category: "text", name: "属于是.png" },
  { kind: "text", text: "哈人", bg: "#2A1A1A", fg: "#E8B0B0", tags: ["哈人"], category: "text", name: "哈人.png" },
  { kind: "text", text: "没事了", bg: "#D8D8D8", fg: "#333333", tags: ["没事了"], category: "sweat", name: "没事了.png" },
  { kind: "text", text: "好无聊", bg: "#C8C0B4", fg: "#2A2A2A", tags: ["无聊"], category: "text", name: "好无聊.png" },
  { kind: "text", text: "我看不懂\n但大受震撼", bg: "#1A1A1A", fg: "#E8E0D0", tags: ["震撼", "看不懂"], category: "text", name: "大受震撼.png" },
  { kind: "text", text: "能跑就行", bg: "#1E1E1E", fg: "#9CDCFE", tags: ["代码", "能跑就行"], category: "code", name: "能跑就行.png" },
  { kind: "text", text: "LGTM", bg: "#0D1117", fg: "#3FB950", tags: ["代码", "LGTM"], category: "code", name: "LGTM.png" },
  { kind: "text", text: "报错了", bg: "#2B1212", fg: "#F85149", tags: ["代码", "报错"], category: "code", name: "报错了.png" },
  { kind: "text", text: "undefined", bg: "#1B1B1B", fg: "#C5A5C5", tags: ["代码", "undefined"], category: "code", name: "undefined.png" },
  { kind: "text", text: "好的好的", bg: "#E8E0D0", fg: "#3A3A3A", tags: ["流汗", "好的"], category: "sweat", name: "好的好的.png" },
  { kind: "text", text: "我先润了", bg: "#D9D2C5", fg: "#2A2A2A", tags: ["流汗", "润"], category: "sweat", name: "我先润了.png" },
  { kind: "text", text: "就这？", bg: "#4A4038", fg: "#F0E6D8", tags: ["就这", "熊猫头"], category: "panda", name: "就这问.png" },
  { kind: "text", text: "呵呵", bg: "#F4F0E8", fg: "#222222", tags: ["呵呵", "熊猫头"], category: "panda", name: "呵呵.png" },
];

function withIds(list: Omit<Meme, "id">[], prefix: string): Meme[] {
  return list.map((m, i) => ({ ...m, id: `${prefix}${i + 1}` }));
}

const baseImages = withIds(IMAGES, "img-");
const texts = withIds(TEXTS, "txt-");

const extras: Meme[] = IMAGES.flatMap((m, i) =>
  [0, 1].map((n) => ({
    ...m,
    id: `pack-${i}-${n}`,
    name: m.name.replace(/(\.\w+)$/, `_${n + 2}$1`),
    tags: [...m.tags, n === 0 ? "常用" : "收藏"],
  })),
);

const untagged: Meme[] = IMAGES.slice(0, 5).map((m, i) => ({
  ...m,
  id: `un-${i}`,
  category: "untagged",
  name: `IMG_${8841 + i}${m.gif ? ".gif" : ".png"}`,
  tags: ["未分类", ...m.tags],
}));

export const MEMES: Meme[] = [...baseImages, ...texts, ...extras, ...untagged];

export const MEME_MAP = new Map(MEMES.map((m) => [m.id, m]));

export const DEFAULT_RECENTS = [
  "img-1",
  "txt-1",
  "img-2",
  "txt-5",
  "img-3",
  "txt-3",
  "img-7",
  "txt-17",
  "img-6",
];

export function filterMemes(query: string, category: string): Meme[] {
  const q = query.trim().toLowerCase();
  return MEMES.filter((m) => {
    if (category !== "all" && m.category !== category) return false;
    if (!q) return true;
    const bag = [m.name, m.text ?? "", m.category, ...m.tags].join(" ").toLowerCase();
    return bag.includes(q);
  });
}

export function getMeme(id: string): Meme | undefined {
  return MEME_MAP.get(id);
}
