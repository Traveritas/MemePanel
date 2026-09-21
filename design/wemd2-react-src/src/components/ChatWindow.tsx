import { useEffect, useRef } from "react";
import type { ChatMsg } from "../panel-types";
import { MemeView } from "./MemeView";

export function ChatWindow({
  messages,
  compact,
}: {
  messages: ChatMsg[];
  compact?: boolean;
}) {
  const end = useRef<HTMLDivElement>(null);
  useEffect(() => {
    end.current?.scrollIntoView({ behavior: "smooth" });
  }, [messages.length]);

  return (
    <div
      className="win-chat"
      style={{
        width: compact ? 340 : 380,
        height: compact ? 480 : 540,
        background: "#F5F5F5",
        borderRadius: 8,
        overflow: "hidden",
        display: "flex",
        flexDirection: "column",
        fontFamily: "'Noto Sans SC', sans-serif",
        color: "#111",
        border: "1px solid rgba(0,0,0,0.12)",
      }}
    >
      <div
        style={{
          height: 48,
          background: "#EDEDED",
          display: "flex",
          alignItems: "center",
          padding: "0 12px",
          gap: 10,
          borderBottom: "1px solid #E0E0E0",
        }}
      >
        <div
          style={{
            width: 32,
            height: 32,
            borderRadius: 4,
            background: "#6B8F71",
            color: "#fff",
            display: "flex",
            alignItems: "center",
            justifyContent: "center",
            fontSize: 13,
          }}
        >
          王
        </div>
        <div style={{ flex: 1 }}>
          <div style={{ fontSize: 13, fontWeight: 500 }}>老王</div>
          <div style={{ fontSize: 11, color: "#888" }}>微信</div>
        </div>
        <div style={{ display: "flex", gap: 8, color: "#666", fontSize: 12 }}>
          <span style={{ opacity: 0.5 }}>—</span>
          <span style={{ opacity: 0.5 }}>□</span>
          <span style={{ opacity: 0.5 }}>×</span>
        </div>
      </div>

      <div
        className="thin-scroll"
        style={{ flex: 1, overflow: "auto", padding: "14px 12px", color: "#bbb" }}
      >
        {messages.map((m) => (
          <div
            key={m.id}
            style={{
              display: "flex",
              justifyContent: m.from === "me" ? "flex-end" : "flex-start",
              marginBottom: 10,
              gap: 8,
            }}
          >
            {m.from === "them" && <Avatar letter="王" color="#6B8F71" />}
            <Bubble msg={m} />
            {m.from === "me" && <Avatar letter="我" color="#5B7C99" />}
          </div>
        ))}
        <div ref={end} />
      </div>

      <div
        style={{
          background: "#F5F5F5",
          borderTop: "1px solid #E6E6E6",
          padding: "6px 8px 8px",
        }}
      >
        <div style={{ display: "flex", gap: 10, padding: "2px 4px 6px", color: "#666", fontSize: 13 }}>
          <span>☺</span>
          <span>✂</span>
          <span>🖼</span>
        </div>
        <div
          style={{
            height: 52,
            background: "#fff",
            border: "1px solid #E5E5E5",
            borderRadius: 4,
            padding: "6px 8px",
            fontSize: 13,
            color: "#AAA",
          }}
        >
          Ctrl+Shift+M 呼出即帖
        </div>
      </div>
    </div>
  );
}

function Avatar({ letter, color }: { letter: string; color: string }) {
  return (
    <div
      style={{
        width: 28,
        height: 28,
        borderRadius: 4,
        background: color,
        color: "#fff",
        fontSize: 12,
        display: "flex",
        alignItems: "center",
        justifyContent: "center",
        flexShrink: 0,
      }}
    >
      {letter}
    </div>
  );
}

function Bubble({ msg }: { msg: ChatMsg }) {
  if (msg.meme) {
    return (
      <div
        style={{
          borderRadius: 4,
          overflow: "hidden",
          boxShadow: "0 1px 2px rgba(0,0,0,0.08)",
        }}
      >
        <MemeView meme={msg.meme} size={108} />
      </div>
    );
  }
  const mine = msg.from === "me";
  return (
    <div
      style={{
        maxWidth: 200,
        background: mine ? "#95EC69" : "#fff",
        padding: "8px 10px",
        borderRadius: 4,
        fontSize: 13,
        lineHeight: 1.45,
        boxShadow: "0 1px 1px rgba(0,0,0,0.06)",
      }}
    >
      {msg.text}
    </div>
  );
}
