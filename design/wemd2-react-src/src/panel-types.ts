import type { Meme } from "./data/memes";

export type PanelProps = {
  query: string;
  onQuery: (s: string) => void;
  category: string;
  onCategory: (c: string) => void;
  recents: string[];
  selectedId: string | null;
  onSelect: (id: string) => void;
  onSend: (id: string) => void;
  onClose: () => void;
  hoverId: string | null;
  onHover: (id: string | null) => void;
  onContext: (id: string, x: number, y: number) => void;
  flashId: string | null;
  previewId: string | null;
  autoFocus?: boolean;
};

export type ChatMsg = {
  id: string;
  from: "me" | "them";
  text?: string;
  meme?: Meme;
};
