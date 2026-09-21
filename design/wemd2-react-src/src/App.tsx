import { useCallback, useEffect, useMemo, useState, type ReactNode } from "react";
import { COPY } from "./copy";
import { CATEGORIES, DEFAULT_RECENTS, filterMemes, getMeme } from "./data/memes";
import { DesktopScene, ScaledStage } from "./components/Desktop";
import { CassettePanel } from "./components/panels/CassettePanel";
import { HaloPanel } from "./components/panels/HaloPanel";
import { LightboxPanel } from "./components/panels/LightboxPanel";
import { PromptPanel } from "./components/panels/PromptPanel";
import { SortesPanel } from "./components/panels/SortesPanel";
import type { ChatMsg, PanelProps } from "./panel-types";
import { SCHEME_ORDER, SCHEMES, type SchemeId } from "./schemes";

type Page = SchemeId | "compare" | "constraints";
type Stage = "scene" | "oneone";

const SEED: ChatMsg[] = [
  { id: "s1", from: "them", text: "晚上吃什么" },
  { id: "s2", from: "me", text: "随便，你定" },
  { id: "s3", from: "them", text: "那你先把上次那个猫发我" },
  { id: "s4", from: "them", text: "就那个震惊的" },
];

export default function App() {
  const [page, setPage] = useState<Page>("sortes");
  const [stage, setStage] = useState<Stage>("scene");
  const [query, setQuery] = useState("");
  const [category, setCategory] = useState("all");
  const [recents, setRecents] = useState<string[]>(DEFAULT_RECENTS);
  const [selectedId, setSelectedId] = useState<string | null>(DEFAULT_RECENTS[0]);
  const [hoverId, setHoverId] = useState<string | null>(null);
  const [previewId, setPreviewId] = useState<string | null>(null);
  const [flashId, setFlashId] = useState<string | null>(null);
  const [panelOpen, setPanelOpen] = useState(true);
  const [messages, setMessages] = useState<ChatMsg[]>(SEED);
  const [toast, setToast] = useState<string | null>(null);
  const [menu, setMenu] = useState<{ id: string; x: number; y: number } | null>(null);

  const schemeId: SchemeId = page === "compare" || page === "constraints" ? "sortes" : page;
  const scheme = SCHEMES[schemeId];
  const journal = page === "compare" || page === "constraints";
  const pageBg = journal ? "#E6DFD2" : scheme.pageBg;
  const ink = journal ? "#1C1916" : scheme.ink;
  const muted = journal ? "rgba(28,25,22,0.52)" : scheme.muted;
  const hairline = journal ? "rgba(28,25,22,0.18)" : scheme.hairline;
  const accent = journal ? "#B43A2F" : scheme.accent;

  useEffect(() => {
    if (!hoverId) {
      setPreviewId(null);
      return;
    }
    const t = window.setTimeout(() => setPreviewId(hoverId), 280);
    return () => window.clearTimeout(t);
  }, [hoverId]);

  const send = useCallback((id: string) => {
    const meme = getMeme(id);
    if (!meme) return;
    setSelectedId(id);
    setFlashId(id);
    setRecents((r) => [id, ...r.filter((x) => x !== id)].slice(0, 9));
    setMessages((m) => [...m, { id: `m-${Date.now()}`, from: "me", meme }]);
    setToast("已粘贴到微信");
    setMenu(null);
    window.setTimeout(() => setFlashId(null), 400);
    window.setTimeout(() => setToast(null), 1400);
  }, []);

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      const typing = document.activeElement?.tagName === "INPUT";
      if (e.key === "Escape") {
        setMenu(null);
        setPanelOpen(false);
        (document.activeElement as HTMLElement | null)?.blur();
        return;
      }
      if ((e.ctrlKey || e.metaKey) && e.shiftKey && e.key.toLowerCase() === "m") {
        e.preventDefault();
        setPanelOpen((o) => !o);
        return;
      }
      if (!typing && e.key === "[") {
        cycle(-1);
      }
      if (!typing && e.key === "]") {
        cycle(1);
      }
      if (!typing && e.key >= "1" && e.key <= "9" && panelOpen) {
        const id = recents[Number(e.key) - 1];
        if (id) send(id);
      }
      if (!typing && (e.key === "Enter") && selectedId && panelOpen) {
        send(selectedId);
      }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [recents, selectedId, panelOpen, page, send]);

  function cycle(dir: number) {
    if (page === "compare" || page === "constraints") {
      setPage(dir > 0 ? "sortes" : "cassette");
      return;
    }
    const i = SCHEME_ORDER.indexOf(page);
    const next = SCHEME_ORDER[(i + dir + SCHEME_ORDER.length) % SCHEME_ORDER.length];
    setPage(next);
    setPanelOpen(true);
  }

  const panelProps: PanelProps = {
    query,
    onQuery: setQuery,
    category,
    onCategory: setCategory,
    recents,
    selectedId,
    onSelect: setSelectedId,
    onSend: send,
    onClose: () => setPanelOpen(false),
    hoverId,
    onHover: setHoverId,
    onContext: (id, x, y) => setMenu({ id, x, y }),
    flashId,
    previewId,
  };

  const panel = renderPanel(schemeId, panelProps);

  return (
    <div
      className="flex h-full min-h-0"
      style={{
        background: pageBg,
        color: ink,
        transition: "background-color 280ms linear, color 280ms linear",
      }}
      onClick={() => setMenu(null)}
    >
      <aside
        className="flex h-full w-[220px] shrink-0 flex-col"
        style={{
          borderRight: `1px solid ${hairline}`,
          background: journal ? "#E6DFD2" : scheme.navBg,
        }}
      >
        <div className="px-5 pt-7 pb-6">
          <div
            style={{
              fontFamily: "'Noto Serif SC', serif",
              fontSize: 28,
              lineHeight: 1,
              letterSpacing: "0.12em",
            }}
          >
            即帖
          </div>
          <div
            className="mt-2"
            style={{
              fontFamily: "'Instrument Serif', serif",
              fontStyle: "italic",
              fontSize: 16,
              letterSpacing: "0.18em",
              opacity: 0.7,
            }}
          >
            JÌTIĒ
          </div>
          <p
            className="mt-4"
            style={{
              fontSize: 11,
              lineHeight: 1.65,
              color: muted,
              fontFamily: "'Noto Sans SC', sans-serif",
            }}
          >
            Win32 GDI 自绘
            <br />
            悬浮表情面板 · 五案
          </p>
        </div>

        <nav className="flex-1 px-2">
          {SCHEME_ORDER.map((id) => {
            const s = SCHEMES[id];
            const on = page === id;
            return (
              <button
                type="button"
                key={id}
                onClick={() => {
                  setPage(id);
                  setPanelOpen(true);
                }}
                className="flex w-full items-baseline gap-3 px-3 py-2.5 text-left"
                style={{
                  background: on ? "transparent" : "transparent",
                  boxShadow: on ? `inset 2px 0 0 0 ${accent}` : undefined,
                }}
              >
                <span
                  className="tabular w-7"
                  style={{
                    fontFamily: "'Instrument Serif', serif",
                    fontStyle: "italic",
                    fontSize: 18,
                    color: on ? accent : muted,
                  }}
                >
                  {s.no}
                </span>
                <span className="min-w-0">
                  <span
                    className="block"
                    style={{
                      fontFamily: "'Noto Serif SC', serif",
                      fontSize: 15,
                      opacity: on ? 1 : 0.72,
                    }}
                  >
                    {s.name}
                  </span>
                  <span
                    className="block"
                    style={{
                      fontSize: 10,
                      letterSpacing: "0.16em",
                      color: muted,
                      marginTop: 1,
                      fontFamily: "'IBM Plex Mono', monospace",
                    }}
                  >
                    {s.latin}
                  </span>
                </span>
              </button>
            );
          })}

          <div className="mx-3 my-4" style={{ height: 1, background: hairline }} />

          <TextNav
            label="对照"
            hint="五案同屏"
            on={page === "compare"}
            muted={muted}
            accent={accent}
            onClick={() => setPage("compare")}
          />
          <TextNav
            label="约束"
            hint="GDI · 动线"
            on={page === "constraints"}
            muted={muted}
            accent={accent}
            onClick={() => setPage("constraints")}
          />
        </nav>

        <div
          className="px-5 pb-6 pt-4"
          style={{
            fontSize: 10,
            lineHeight: 1.7,
            color: muted,
            fontFamily: "'Noto Sans SC', sans-serif",
          }}
        >
          <div style={{ letterSpacing: "0.14em" }}>[ ] 切案 · 1–9 最近</div>
          <div>Ctrl+Shift+M 呼出</div>
          <div>单击即贴 · Esc 收起</div>
        </div>
      </aside>

      <main className="flex min-w-0 flex-1 flex-col">
        <header
          className="flex h-12 shrink-0 items-center px-6"
          style={{ borderBottom: `1px solid ${hairline}` }}
        >
          <div
            style={{
              fontFamily: "'Noto Serif SC', serif",
              fontSize: 14,
              letterSpacing: "0.08em",
            }}
          >
            {page === "compare"
              ? "对照"
              : page === "constraints"
                ? "约束"
                : `${scheme.no}  ${scheme.name}  ·  ${scheme.subtitle}`}
          </div>
          {(page === "compare" || page === "constraints") ? null : (
            <div className="ml-auto flex items-center gap-1" style={{ fontSize: 12 }}>
              <StageBtn on={stage === "scene"} onClick={() => setStage("scene")} muted={muted}>
                场景
              </StageBtn>
              <span style={{ opacity: 0.3, padding: "0 4px" }}>/</span>
              <StageBtn on={stage === "oneone"} onClick={() => setStage("oneone")} muted={muted}>
                1:1
              </StageBtn>
            </div>
          )}
          {page === "compare" || page === "constraints" ? (
            <div className="ml-auto" style={{ fontSize: 11, color: muted, letterSpacing: "0.12em" }}>
              配色只为气质 · 结构才是产品
            </div>
          ) : null}
        </header>

        <div className="min-h-0 flex-1">
          {page === "constraints" ? (
            <Constraints ink={ink} muted={muted} hairline={hairline} />
          ) : page === "compare" ? (
            <Compare panelProps={panelProps} />
          ) : stage === "scene" ? (
            <ScaledStage>
              <DesktopScene
                schemeId={schemeId}
                panel={panel}
                messages={messages}
                panelOpen={panelOpen}
                toast={toast}
                onToggle={() => setPanelOpen(true)}
              />
            </ScaledStage>
          ) : (
            <OneToOne schemeId={schemeId} panel={panel} open={panelOpen} onOpen={() => setPanelOpen(true)} />
          )}
        </div>

        {page !== "compare" && page !== "constraints" && (
          <IntentFooter id={schemeId} ink={ink} muted={muted} hairline={hairline} />
        )}
      </main>

      {menu && (
        <ContextMenu
          x={menu.x}
          y={menu.y}
          schemeId={journal ? "sortes" : schemeId}
          onSend={() => send(menu.id)}
          onClose={() => setMenu(null)}
        />
      )}
    </div>
  );
}

function renderPanel(id: SchemeId, props: PanelProps) {
  if (id === "sortes") return <SortesPanel {...props} />;
  if (id === "prompt") return <PromptPanel {...props} />;
  if (id === "lightbox") return <LightboxPanel {...props} />;
  if (id === "halo") return <HaloPanel {...props} />;
  return <CassettePanel {...props} />;
}

function TextNav({
  label,
  hint,
  on,
  muted,
  accent,
  onClick,
}: {
  label: string;
  hint: string;
  on: boolean;
  muted: string;
  accent: string;
  onClick: () => void;
}) {
  return (
    <button
      type="button"
      onClick={onClick}
      className="flex w-full items-baseline justify-between px-3 py-2 text-left"
      style={{ boxShadow: on ? `inset 2px 0 0 0 ${accent}` : undefined }}
    >
      <span style={{ fontFamily: "'Noto Serif SC', serif", fontSize: 14, opacity: on ? 1 : 0.72 }}>
        {label}
      </span>
      <span style={{ fontSize: 10, color: muted, letterSpacing: "0.08em" }}>{hint}</span>
    </button>
  );
}

function StageBtn({
  on,
  onClick,
  muted,
  children,
}: {
  on: boolean;
  onClick: () => void;
  muted: string;
  children: string;
}) {
  return (
    <button type="button" onClick={onClick} style={{ opacity: on ? 1 : 0.45, color: on ? undefined : muted }}>
      {children}
    </button>
  );
}

function IntentFooter({
  id,
  ink,
  muted,
  hairline,
}: {
  id: SchemeId;
  ink: string;
  muted: string;
  hairline: string;
}) {
  const c = COPY[id];
  const s = SCHEMES[id];
  return (
    <footer
      className="grid shrink-0 grid-cols-4 gap-6 px-6 py-4"
      style={{
        borderTop: `1px solid ${hairline}`,
        fontFamily: "'Noto Serif SC', serif",
      }}
    >
      <div>
        <div style={{ fontSize: 10, letterSpacing: "0.2em", color: muted, marginBottom: 6 }}>意图</div>
        <p style={{ fontSize: 12, lineHeight: 1.7, color: ink }}>{c.intent}</p>
      </div>
      <div>
        <div style={{ fontSize: 10, letterSpacing: "0.2em", color: muted, marginBottom: 6 }}>排印</div>
        <p style={{ fontSize: 12, lineHeight: 1.7, color: ink }}>{c.type}</p>
      </div>
      <div>
        <div style={{ fontSize: 10, letterSpacing: "0.2em", color: muted, marginBottom: 6 }}>节奏</div>
        <p style={{ fontSize: 12, lineHeight: 1.7, color: ink }}>{c.rhythm}</p>
      </div>
      <div>
        <div style={{ fontSize: 10, letterSpacing: "0.2em", color: muted, marginBottom: 6 }}>GDI</div>
        <p style={{ fontSize: 12, lineHeight: 1.7, color: ink }}>{c.gdi}</p>
        <p className="mt-2 tabular" style={{ fontSize: 10, color: muted, fontFamily: "'IBM Plex Mono', monospace" }}>
          {s.panel.w}×{s.panel.h} · {s.latin}
        </p>
      </div>
    </footer>
  );
}

function OneToOne({
  schemeId,
  panel,
  open,
  onOpen,
}: {
  schemeId: SchemeId;
  panel: ReactNode;
  open: boolean;
  onOpen: () => void;
}) {
  const s = SCHEMES[schemeId];
  const desk =
    schemeId === "sortes"
      ? "#C9C0B0"
      : schemeId === "prompt"
        ? "#050505"
        : schemeId === "lightbox"
          ? "#4E4E4E"
          : "#0E0C0A";
  return (
    <div className="relative flex h-full items-center justify-center" style={{ background: desk }}>
      {open ? (
        panel
      ) : (
        <button
          type="button"
          onClick={onOpen}
          style={{
            fontFamily: "'IBM Plex Mono', monospace",
            fontSize: 12,
            letterSpacing: "0.12em",
            opacity: 0.7,
          }}
        >
          Ctrl+Shift+M
        </button>
      )}
      <div
        className="absolute bottom-5 left-1/2 -translate-x-1/2 tabular"
        style={{
          fontFamily: "'IBM Plex Mono', monospace",
          fontSize: 11,
          letterSpacing: "0.14em",
          opacity: 0.5,
        }}
      >
        {s.panel.w} × {s.panel.h} px · 1:1
      </div>
    </div>
  );
}

function Compare({ panelProps }: { panelProps: PanelProps }) {
  const visible = useMemo(
    () => filterMemes(panelProps.query, panelProps.category).length,
    [panelProps.query, panelProps.category],
  );
  return (
    <div className="h-full overflow-auto px-8 py-6" style={{ color: "#1C1916" }}>
      <div className="mb-5 flex flex-wrap items-end justify-between gap-6">
        <p
          style={{
            fontFamily: "'Noto Serif SC', serif",
            fontSize: 15,
            lineHeight: 1.7,
            maxWidth: 560,
          }}
        >
          同一检索、同一库。差别只在气质、排版、空间节奏。上一稿的问题是四案共用一套圆角深色卡片——结构没变，换皮无效。
        </p>
        <div className="flex flex-col items-end gap-2">
          <input
            value={panelProps.query}
            onChange={(e) => panelProps.onQuery(e.target.value)}
            placeholder="五案同步检索"
            style={{
              width: 220,
              background: "transparent",
              border: "none",
              borderBottom: "1px solid #1C1916",
              outline: "none",
              fontFamily: "'Noto Serif SC', serif",
              fontSize: 14,
              padding: "4px 0",
              borderRadius: 0,
            }}
          />
          <div
            className="tabular"
            style={{ fontFamily: "'IBM Plex Mono', monospace", fontSize: 11, opacity: 0.5 }}
          >
            可见 {visible} · 库 2,847
          </div>
        </div>
      </div>
      <div className="mb-8 flex flex-wrap gap-4" style={{ fontFamily: "'Noto Sans SC', sans-serif", fontSize: 12 }}>
        {CATEGORIES.map((c) => {
          const on = panelProps.category === c.id;
          return (
            <button
              type="button"
              key={c.id}
              onClick={() => panelProps.onCategory(c.id)}
              style={{
                color: on ? "#B43A2F" : "rgba(28,25,22,0.5)",
                borderBottom: on ? "1px solid #B43A2F" : "1px solid transparent",
                paddingBottom: 2,
                letterSpacing: "0.08em",
              }}
            >
              {c.name}
            </button>
          );
        })}
      </div>
      <div className="grid grid-cols-2 gap-10">
        {SCHEME_ORDER.map((id) => {
          const s = SCHEMES[id];
              const scale = id === "lightbox" ? 0.52 : id === "sortes" ? 0.58 : 0.64;
              return (
            <div key={id}>
              <div className="mb-3 flex items-baseline gap-3">
                <span
                  style={{
                    fontFamily: "'Instrument Serif', serif",
                    fontStyle: "italic",
                    fontSize: 22,
                    color: "#B43A2F",
                  }}
                >
                  {s.no}
                </span>
                <span style={{ fontFamily: "'Noto Serif SC', serif", fontSize: 16 }}>{s.name}</span>
                <span
                  style={{
                    fontFamily: "'IBM Plex Mono', monospace",
                    fontSize: 10,
                    letterSpacing: "0.16em",
                    opacity: 0.45,
                  }}
                >
                  {s.latin} · {s.panel.w}×{s.panel.h}
                </span>
              </div>
              <div
                style={{
                  width: s.panel.w * scale + 8,
                  height: s.panel.h * scale + 8,
                  position: "relative",
                }}
              >
                <div
                  style={{
                    transform: `scale(${scale})`,
                    transformOrigin: "top left",
                  }}
                >
                  {renderPanel(id, { ...panelProps, autoFocus: false, previewId: null })}
                </div>
              </div>
            </div>
          );
        })}
      </div>
    </div>
  );
}

function Constraints({
  ink,
  muted,
  hairline,
}: {
  ink: string;
  muted: string;
  hairline: string;
}) {
  return (
    <div
      className="h-full overflow-auto px-12 py-10"
      style={{ fontFamily: "'Noto Serif SC', serif", color: ink }}
    >
      <div className="grid max-w-4xl grid-cols-2 gap-x-16 gap-y-12">
        <section>
          <h2 style={{ fontSize: 11, letterSpacing: "0.22em", color: muted, marginBottom: 12 }}>产品</h2>
          <p style={{ fontSize: 16, lineHeight: 1.75 }}>
            全局热键呼出的悬浮面板。用户是重度斗图的网民，收藏五百到五千张，以 GIF 为主。核心动线只有一句：呼出，找到，点一张，贴进 QQ / 微信。
          </p>
        </section>
        <section>
          <h2 style={{ fontSize: 11, letterSpacing: "0.22em", color: muted, marginBottom: 12 }}>为何重做</h2>
          <p style={{ fontSize: 16, lineHeight: 1.75 }}>
            前稿四案是同一套深色圆角后台，换了栏位。表情是彩色方块加 Emoji。信息架构正确，但没有物件感，也没有排印。斗图工具应当像输入法，不像仪表盘。
          </p>
        </section>
        <section className="col-span-2" style={{ borderTop: `1px solid ${hairline}`, paddingTop: 28 }}>
          <h2 style={{ fontSize: 11, letterSpacing: "0.22em", color: muted, marginBottom: 12 }}>GDI 硬约束</h2>
          <ul style={{ fontSize: 15, lineHeight: 1.9 }}>
            <li>可做：FillRect、RoundRect、MoveTo/LineTo、DrawText、BitBlt、1px Pen、PS_DOT、GDI+ 第 0 帧缩略图、定时器换帧、WM_DROPFILES、硬偏移阴影。</li>
            <li>不做：高斯模糊、亚克力、渐变玻璃、圆角投影、SVG 滤镜、弹性动画。悬停态是换 Pen 颜色，不是发光。</li>
            <li>五千张：owner-draw 虚拟列表，只绘制可见格；缩略图 LRU。滚动是滚动条矩形，不是惯性弹簧。</li>
            <li>粘贴：CF_HDROP / PNG / GIF 写入剪贴板，SendInput Ctrl+V。热键 RegisterHotKey。</li>
          </ul>
        </section>
        <section>
          <h2 style={{ fontSize: 11, letterSpacing: "0.22em", color: muted, marginBottom: 12 }}>交互公约</h2>
          <p style={{ fontSize: 15, lineHeight: 1.85 }}>
            呼出即聚焦检索。1–9 发送最近。方向键移动选中，Enter 发送。单击即贴，面板默认可留（连发），Shift+单击后收起。右键：发送 / 复制 / 打标 / 删除。Esc 收起。悬停 280ms 出大图——扫视不被打断。
          </p>
        </section>
        <section>
          <h2 style={{ fontSize: 11, letterSpacing: "0.22em", color: muted, marginBottom: 12 }}>五案各管一题</h2>
          <p style={{ fontSize: 15, lineHeight: 1.85 }}>
            活字管「像辞书一样拣」。指令管「键位即界面」。灯箱管「扫图不被 chrome 染色」。匣管「最近是手，库是眼」。琉璃管「系统绘图能力的完全体」。不要五案做成同一产品的换肤。
          </p>
        </section>
        <section className="col-span-2" style={{ borderTop: `1px solid ${hairline}`, paddingTop: 28 }}>
          <h2 style={{ fontSize: 11, letterSpacing: "0.22em", color: muted, marginBottom: 16 }}>密度对照</h2>
          <div
            className="grid grid-cols-4 gap-6"
            style={{ fontFamily: "'IBM Plex Mono', monospace", fontSize: 12 }}
          >
            {[
              ["01 活字", "712×476", "9×n · 56px", "chrome ~22%"],
              ["02 指令", "640×412", "12×n · 48px", "chrome ~24%"],
              ["03 灯箱", "844×392", "11×n · 68px", "chrome ~11%"],
              ["04 匣", "548×368", "72 / 36 双尺", "chrome ~18%"],
            ].map((row) => (
              <div key={row[0]} style={{ borderTop: `1px solid ${hairline}`, paddingTop: 10 }}>
                {row.map((x) => (
                  <div key={x} style={{ lineHeight: 1.8, color: x === row[0] ? ink : muted }}>
                    {x}
                  </div>
                ))}
              </div>
            ))}
          </div>
        </section>
      </div>
    </div>
  );
}

function ContextMenu({
  x,
  y,
  schemeId,
  onSend,
  onClose,
}: {
  x: number;
  y: number;
  schemeId: SchemeId;
  onSend: () => void;
  onClose: () => void;
}) {
  const skin =
    schemeId === "sortes"
      ? { bg: "#EFE8DC", fg: "#1C1916", bd: "#1C1916", sh: "3px 3px 0 0 #1C1916", radius: 0, font: "'Noto Serif SC', serif" }
      : schemeId === "prompt"
        ? { bg: "#0B0B0B", fg: "#D7C49A", bd: "#D7C49A", sh: "none", radius: 0, font: "'IBM Plex Mono', monospace" }
        : schemeId === "lightbox"
          ? { bg: "#5C5C5C", fg: "#F4F4F4", bd: "#F4F4F4", sh: "none", radius: 0, font: "'Noto Sans SC', sans-serif" }
          : { bg: "#1A1612", fg: "#E8DCC8", bd: "#B08A4F", sh: "4px 6px 0 0 rgba(0,0,0,0.45)", radius: 4, font: "'Noto Sans SC', sans-serif" };

  const items = ["发送", "复制", "打标签…", "删除"];
  return (
    <div
      onClick={(e) => e.stopPropagation()}
      style={{
        position: "fixed",
        left: x,
        top: y,
        width: 128,
        background: skin.bg,
        color: skin.fg,
        border: `1px solid ${skin.bd}`,
        boxShadow: skin.sh,
        borderRadius: skin.radius,
        zIndex: 50,
        fontFamily: skin.font,
        fontSize: 12,
        padding: "4px 0",
      }}
    >
      {items.map((label) => (
        <button
          type="button"
          key={label}
          className="block w-full px-3 py-1.5 text-left"
          onClick={() => {
            if (label === "发送") onSend();
            else onClose();
          }}
          style={{ opacity: label === "删除" ? 0.55 : 1 }}
        >
          {label}
        </button>
      ))}
    </div>
  );
}
