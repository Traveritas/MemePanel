import { useEffect, useRef, useState, type ReactNode } from "react";
import { WALLPAPER } from "../media";
import type { ChatMsg } from "../panel-types";
import { SCHEMES, type SchemeId } from "../schemes";
import { ChatWindow } from "./ChatWindow";

export function ScaledStage({
  width = 1280,
  height = 720,
  children,
}: {
  width?: number;
  height?: number;
  children: ReactNode;
}) {
  const ref = useRef<HTMLDivElement>(null);
  const [scale, setScale] = useState(1);

  useEffect(() => {
    const el = ref.current;
    if (!el) return;
    const obs = new ResizeObserver(([entry]) => {
      const s = Math.min(entry.contentRect.width / width, entry.contentRect.height / height);
      setScale(s);
    });
    obs.observe(el);
    return () => obs.disconnect();
  }, [width, height]);

  return (
    <div ref={ref} className="flex h-full w-full items-center justify-center overflow-hidden">
      <div
        style={{
          width,
          height,
          transform: `scale(${scale})`,
          transformOrigin: "center center",
          flexShrink: 0,
        }}
      >
        {children}
      </div>
    </div>
  );
}

export function DesktopScene({
  schemeId,
  panel,
  messages,
  panelOpen,
  toast,
  onToggle,
}: {
  schemeId: SchemeId;
  panel: ReactNode;
  messages: ChatMsg[];
  panelOpen: boolean;
  toast: string | null;
  onToggle: () => void;
}) {
  const scheme = SCHEMES[schemeId];
  const pos = scheme.deskPos;

  return (
    <div
      style={{
        width: 1280,
        height: 720,
        position: "relative",
        overflow: "hidden",
        background: "#1a2330",
      }}
    >
      <img
        src={WALLPAPER}
        alt=""
        style={{
          position: "absolute",
          inset: 0,
          width: "100%",
          height: "100%",
          objectFit: "cover",
        }}
      />
      <div
        style={{
          position: "absolute",
          inset: 0,
          background: "linear-gradient(180deg, rgba(8,12,18,0.08), rgba(8,12,18,0.18))",
        }}
      />

      <div style={{ position: "absolute", left: 44, top: 56 }}>
        <ChatWindow messages={messages} />
      </div>

      {panelOpen ? (
        <div
          style={{
            position: "absolute",
            left: pos.x,
            top: pos.y,
            zIndex: 5,
          }}
        >
          {panel}
        </div>
      ) : (
        <button
          type="button"
          onClick={onToggle}
          style={{
            position: "absolute",
            left: 92,
            bottom: 118,
            zIndex: 5,
            background: "rgba(20,20,20,0.72)",
            color: "#eee",
            padding: "8px 12px",
            fontSize: 12,
            fontFamily: "'IBM Plex Mono', monospace",
            letterSpacing: "0.04em",
            border: "1px solid rgba(255,255,255,0.12)",
          }}
        >
          Ctrl + Shift + M
        </button>
      )}

      {toast && (
        <div
          className="toast-up"
          style={{
            position: "absolute",
            left: "50%",
            bottom: 72,
            transform: "translateX(-50%)",
            background: "rgba(20,18,16,0.88)",
            color: "#F3EDE4",
            padding: "8px 14px",
            fontSize: 12,
            letterSpacing: "0.08em",
            fontFamily: "'Noto Sans SC', sans-serif",
            zIndex: 8,
          }}
        >
          {toast}
        </div>
      )}

      <Taskbar />
    </div>
  );
}

function Taskbar() {
  const [now, setNow] = useState(() => new Date());
  useEffect(() => {
    const t = setInterval(() => setNow(new Date()), 1000);
    return () => clearInterval(t);
  }, []);
  const hh = now.toTimeString().slice(0, 5);
  const dd = `${now.getFullYear()}/${String(now.getMonth() + 1).padStart(2, "0")}/${String(now.getDate()).padStart(2, "0")}`;

  return (
    <div
      className="win-taskbar"
      style={{
        position: "absolute",
        left: 0,
        right: 0,
        bottom: 0,
        height: 48,
        display: "flex",
        alignItems: "center",
        justifyContent: "center",
        color: "#E8E8E8",
        zIndex: 6,
      }}
    >
      <div style={{ display: "flex", alignItems: "center", gap: 6 }}>
        <WinIcon />
        <DockIcon color="#3A8BFF" />
        <DockIcon color="#E8B84A" />
        <DockIcon color="#07C160" />
        <DockIcon color="#12B7F5" />
      </div>
      <div
        style={{
          position: "absolute",
          right: 16,
          textAlign: "right",
          fontSize: 11,
          lineHeight: 1.25,
          fontFamily: "'Segoe UI', 'Noto Sans SC', sans-serif",
        }}
      >
        <div>{hh}</div>
        <div style={{ opacity: 0.75 }}>{dd}</div>
      </div>
    </div>
  );
}

function WinIcon() {
  return (
    <div style={{ width: 34, height: 34, display: "grid", placeItems: "center" }}>
      <div style={{ display: "grid", gridTemplateColumns: "8px 8px", gap: 1 }}>
        <i style={{ width: 8, height: 8, background: "#7FBA00" }} />
        <i style={{ width: 8, height: 8, background: "#F25022" }} />
        <i style={{ width: 8, height: 8, background: "#00A4EF" }} />
        <i style={{ width: 8, height: 8, background: "#FFB900" }} />
      </div>
    </div>
  );
}

function DockIcon({ color }: { color: string }) {
  return (
    <div
      style={{
        width: 34,
        height: 34,
        display: "grid",
        placeItems: "center",
      }}
    >
      <i style={{ width: 18, height: 18, borderRadius: 4, background: color }} />
    </div>
  );
}
