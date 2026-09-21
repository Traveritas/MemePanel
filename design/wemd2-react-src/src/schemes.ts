export type SchemeId = "sortes" | "prompt" | "lightbox" | "cassette" | "halo";

export type Scheme = {
  id: SchemeId;
  no: string;
  name: string;
  latin: string;
  subtitle: string;
  pageBg: string;
  ink: string;
  muted: string;
  hairline: string;
  accent: string;
  navBg: string;
  panel: { w: number; h: number };
  deskPos: { x: number; y: number };
};

export const SCHEMES: Record<SchemeId, Scheme> = {
  sortes: {
    id: "sortes",
    no: "01",
    name: "活字",
    latin: "SORTES",
    subtitle: "铅字目录",
    pageBg: "#E6DFD2",
    ink: "#1C1916",
    muted: "rgba(28,25,22,0.52)",
    hairline: "rgba(28,25,22,0.18)",
    accent: "#B43A2F",
    navBg: "#E6DFD2",
    panel: { w: 712, h: 476 },
    deskPos: { x: 448, y: 78 },
  },
  prompt: {
    id: "prompt",
    no: "02",
    name: "指令",
    latin: "PROMPT",
    subtitle: "命令面板",
    pageBg: "#0B0B0B",
    ink: "#D7C49A",
    muted: "rgba(215,196,154,0.55)",
    hairline: "rgba(215,196,154,0.22)",
    accent: "#E8D09A",
    navBg: "#0B0B0B",
    panel: { w: 640, h: 412 },
    deskPos: { x: 470, y: 118 },
  },
  lightbox: {
    id: "lightbox",
    no: "03",
    name: "灯箱",
    latin: "LIGHTBOX",
    subtitle: "接触印样",
    pageBg: "#6A6A6A",
    ink: "#F3F3F3",
    muted: "rgba(243,243,243,0.62)",
    hairline: "rgba(243,243,243,0.22)",
    accent: "#FFFFFF",
    navBg: "#6A6A6A",
    panel: { w: 844, h: 392 },
    deskPos: { x: 372, y: 198 },
  },
  cassette: {
    id: "cassette",
    no: "04",
    name: "匣",
    latin: "CASSETTE",
    subtitle: "双尺匣子",
    pageBg: "#14110E",
    ink: "#E8DCC8",
    muted: "rgba(232,220,200,0.55)",
    hairline: "rgba(232,220,200,0.16)",
    accent: "#B08A4F",
    navBg: "#14110E",
    panel: { w: 548, h: 368 },
    deskPos: { x: 508, y: 248 },
  },
  halo: {
    id: "halo",
    no: "05",
    name: "琉璃",
    latin: "HALO",
    subtitle: "玻璃浮层",
    pageBg: "#101319",
    ink: "#E7EBF6",
    muted: "rgba(231,235,246,0.55)",
    hairline: "rgba(231,235,246,0.14)",
    accent: "#7AA2FF",
    navBg: "#101319",
    panel: { w: 700, h: 476 },
    deskPos: { x: 300, y: 128 },
  },
};

export const SCHEME_ORDER: SchemeId[] = ["sortes", "prompt", "lightbox", "cassette", "halo"];
