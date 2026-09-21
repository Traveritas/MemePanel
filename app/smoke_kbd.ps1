# smoke_kbd.ps1 — v0.12 纯键盘真机冒烟（keybd_event 真实键盘合成）
# 链路：Ctrl+I 开抽屉（窗口加宽断言）→ Tab×2+→+Enter 打标签（index.bin 落盘断言）
#       → Esc×4 分层收起（宽度复原 + 隐藏断言）→ 呼出 + Ctrl+D 收藏落盘（对称还原）
# 注意：动作键用 GetKeyState 查 Ctrl——必须真实键盘输入，PostMessage 不设键盘状态。
$ErrorActionPreference = 'Stop'
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class KB {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string c, string t);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool attach);
    [DllImport("user32.dll")] public static extern bool OpenClipboard(IntPtr h);
    [DllImport("user32.dll")] public static extern bool CloseClipboard();
    [DllImport("user32.dll")] public static extern bool IsClipboardFormatAvailable(uint fmt);
    [DllImport("user32.dll")] public static extern int GetSystemMetrics(int i);
    public struct RECT { public int l, t, r, b; }
}
"@
[void][KB]::SetProcessDPIAware()
$WM_HOTKEY = 0x0312
function Rect([IntPtr]$h) { $r = New-Object KB+RECT; [void][KB]::GetWindowRect($h, [ref]$r); $r }
function Key([byte]$vk) { [KB]::keybd_event($vk, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 60; [KB]::keybd_event($vk, 0, 2, [UIntPtr]::Zero); Start-Sleep -Milliseconds 60 }
function KeyCtrl([byte]$vk) {
    [KB]::keybd_event(0x11, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 60
    [KB]::keybd_event($vk, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 60
    [KB]::keybd_event($vk, 0, 2, [UIntPtr]::Zero); Start-Sleep -Milliseconds 40
    [KB]::keybd_event(0x11, 0, 2, [UIntPtr]::Zero); Start-Sleep -Milliseconds 60
}
function Fg([IntPtr]$h) {
    if ([KB]::GetForegroundWindow() -eq $h) { Start-Sleep -Milliseconds 50; return }
    # 保险路径：AttachThreadInput + ALT 解锁（正常应已由真实热键呼出取得前台）
    $dummy = [uint32]0
    $me = [KB]::GetCurrentThreadId()
    $tgt = [KB]::GetWindowThreadProcessId($h, [ref]$dummy)
    $cur = [KB]::GetWindowThreadProcessId([KB]::GetForegroundWindow(), [ref]$dummy)
    [void][KB]::AttachThreadInput($me, $tgt, $true)
    if ($cur -ne $tgt) { [void][KB]::AttachThreadInput($me, $cur, $true) }
    [KB]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero); [KB]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)   # ALT 点按解锁前台锁
    [void][KB]::SetForegroundWindow($h)
    if ($cur -ne $tgt) { [void][KB]::AttachThreadInput($me, $cur, $false) }
    [void][KB]::AttachThreadInput($me, $tgt, $false)
    Start-Sleep -Milliseconds 150
    if ([KB]::GetForegroundWindow() -ne $h) { Write-Host 'WARN: foreground grab failed' }
}
# 真实全局热键 Ctrl+Shift+.（系统路由给注册进程 → 面板获得前台权限 → panel_show 抢前台成功）
function SummonHotkey {
    [KB]::keybd_event(0x11, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 50
    [KB]::keybd_event(0x10, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 50
    [KB]::keybd_event(0xBE, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 60
    [KB]::keybd_event(0xBE, 0, 2, [UIntPtr]::Zero); Start-Sleep -Milliseconds 40
    [KB]::keybd_event(0x10, 0, 2, [UIntPtr]::Zero); Start-Sleep -Milliseconds 40
    [KB]::keybd_event(0x11, 0, 2, [UIntPtr]::Zero); Start-Sleep -Milliseconds 120
}

$exe = "$PSScriptRoot\out\MemePanel.exe"
$prefs = "$PSScriptRoot\out\prefs.cfg"
$idx = "$PSScriptRoot\out\index.bin"
$idxBak = $null
if (Test-Path $idx) { $idxBak = [System.IO.File]::ReadAllBytes($idx) }
Remove-Item $prefs -ErrorAction SilentlyContinue
Stop-Process -Name MemePanel -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 400

try {
    $sw = [KB]::GetSystemMetrics(0); $sh = [KB]::GetSystemMetrics(1)
    [void][KB]::SetCursorPos([int]($sw/2), [int]($sh/2))
    Start-Process $exe   # 静默驻留（不用 -show：走真实热键路径取前台权限）
    Start-Sleep -Milliseconds 1200
    $h = [KB]::FindWindowW('MemePanelWnd', 'MemePanel')
    if ($h -eq [IntPtr]::Zero) { throw 'panel not found' }
    SummonHotkey
    Start-Sleep -Milliseconds 1200   # 淡入
    if (-not [KB]::IsWindowVisible($h)) { throw 'real hotkey did not summon panel' }
    if ([KB]::GetForegroundWindow() -ne $h) { throw 'panel is not foreground after real hotkey summon' }
    $r0 = Rect $h
    $w0 = $r0.r - $r0.l
    Write-Host ("[k1] hotkey summoned: fg=panel w={0}" -f $w0)

    # ---- Ctrl+I 开抽屉：窗口加宽 ~S(300)@125%DPI=375 ----
    Fg $h; KeyCtrl 0x49   # 'I'
    Start-Sleep -Milliseconds 500
    $r1 = Rect $h
    $w1 = $r1.r - $r1.l
    Write-Host ("[k2] Ctrl+I drawer: w {0} -> {1} (expect +~375)" -f $w0, $w1)
    if ($w1 -lt $w0 + 300) { throw 'Ctrl+I did not open drawer' }

    # ---- k3 v0.13 新路径：Ctrl+I 开抽屉（焦点落索引框）→ ↑ 进 chips → → + Enter 打标签 ----
    $mt0 = (Get-Item $idx).LastWriteTime
    Fg $h; Key 0x26; Key 0x27; Key 0x0D   # ↑(索引框→chips) → Enter 打标
    Start-Sleep -Milliseconds 500
    $mt1 = (Get-Item $idx).LastWriteTime
    Write-Host ("[k3] chips(via UP)+Enter tag toggle: index.bin {0} -> {1}" -f $mt0.ToString('HH:mm:ss.fff'), $mt1.ToString('HH:mm:ss.fff'))
    if ($mt1 -le $mt0) { throw 'tag toggle did not save index.bin' }
    Key 0x0D   # 再 Enter 同一焦点 = toggle 还原
    Start-Sleep -Milliseconds 400

    # ---- Esc×4 分层收起：chips→索引框→搜索→关抽屉→隐藏（宽度复原 + 不可见）----
    Fg $h; Key 0x1B; Key 0x1B; Key 0x1B
    Start-Sleep -Milliseconds 400
    $r2 = Rect $h
    if (($r2.r - $r2.l) -gt $w0 + 100) { throw 'Esc did not close drawer (width still wide)' }
    Key 0x1B
    Start-Sleep -Milliseconds 700   # 淡出
    Write-Host ("[k4] Esc layers: w back to {0}, visible={1} (expect False)" -f ($r2.r - $r2.l), [KB]::IsWindowVisible($h))
    if ([KB]::IsWindowVisible($h)) { throw 'final Esc did not hide panel' }

    # ---- 真热键呼出 + ↓选中 + Ctrl+D 收藏落盘（选中跟随重排，对称 toggle 还原）----
    SummonHotkey
    Start-Sleep -Milliseconds 1200
    if ([KB]::GetForegroundWindow() -ne $h) { Fg $h }
    Key 0x28   # ↓ 选中首行（kbd_target 需要键盘选中）
    $mt2 = (Get-Item $idx).LastWriteTime
    Fg $h; KeyCtrl 0x44   # 'D'
    Start-Sleep -Milliseconds 500
    $mt3 = (Get-Item $idx).LastWriteTime
    Write-Host ("[k5] Ctrl+D fav: index.bin {0} -> {1}" -f $mt2.ToString('HH:mm:ss.fff'), $mt3.ToString('HH:mm:ss.fff'))
    if ($mt3 -le $mt2) { throw 'Ctrl+D did not save index.bin' }
    KeyCtrl 0x44   # 还原
    Start-Sleep -Milliseconds 400

    # ---- 收尾：导航 + Tab 开合 + 三层 Esc 干净退出（v0.13：Tab 开抽屉 → Esc 需三连）----
    Fg $h; Key 0x28; Key 0x27; Key 0x25; Key 0x09; Key 0x1B; Key 0x1B; Key 0x1B
    Start-Sleep -Milliseconds 700
    Write-Host ("[k6] nav roundtrip: visible={0} (expect False)" -f [KB]::IsWindowVisible($h))
    if ([KB]::IsWindowVisible($h)) { throw 'nav roundtrip left panel in odd state' }

    # ---- k7 最近行贯通即发：↓选中首格 → ↑进最近行 → →移动 → Enter 发送 → 面板隐藏 + 剪贴板 HDROP ----
    SummonHotkey
    Start-Sleep -Milliseconds 1200
    if ([KB]::GetForegroundWindow() -ne $h) { Fg $h }
    Fg $h
    Key 0x28      # ↓ 选中网格首格
    Key 0x26      # ↑ 首行贯通 → 最近行（列对齐）
    Key 0x27      # → 移到第 2 格
    Key 0x0D      # Enter 即发
    Start-Sleep -Milliseconds 900   # 发送 + 淡出
    Write-Host ("[k7] recent row send: visible={0} (expect False)" -f [KB]::IsWindowVisible($h))
    if ([KB]::IsWindowVisible($h)) { throw 'recent-row Enter did not send/hide panel' }
    $clip = [KB]::IsClipboardFormatAvailable(15)   # CF_HDROP
    Write-Host ("[k7] clipboard CF_HDROP available: {0}" -f $clip)
    if (-not $clip) { throw 'recent-row send did not put CF_HDROP on clipboard' }

    # ---- k8 v0.13 Tab = 抽屉开合：Tab 开（+375）→ Tab 收（复原）----
    SummonHotkey
    Start-Sleep -Milliseconds 1200
    if ([KB]::GetForegroundWindow() -ne $h) { Fg $h }
    Fg $h
    Key 0x09   # Tab 开抽屉（单目标 → 焦点落索引框）
    Start-Sleep -Milliseconds 500
    $rT1 = Rect $h
    Key 0x09   # Tab 收抽屉
    Start-Sleep -Milliseconds 500
    $rT2 = Rect $h
    Write-Host ("[k8] Tab toggle: w {0} -> {1} -> {2}" -f $w0, ($rT1.r - $rT1.l), ($rT2.r - $rT2.l))
    if (($rT1.r - $rT1.l) -lt $w0 + 300) { throw 'Tab did not open drawer' }
    if (($rT2.r - $rT2.l) -gt $w0 + 100) { throw 'Tab did not close drawer' }
    Key 0x1B   # 收尾隐藏
    Start-Sleep -Milliseconds 700

    Write-Host 'KBD SMOKE OK'
}
finally {
    Stop-Process -Name MemePanel -Force -ErrorAction SilentlyContinue
    if ($idxBak) { [System.IO.File]::WriteAllBytes($idx, $idxBak) }   # 字节级还原索引
    Remove-Item $prefs -ErrorAction SilentlyContinue
}
