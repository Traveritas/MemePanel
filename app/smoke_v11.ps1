# smoke_v11.ps1 — v0.11 三件套真机冒烟（PowerShell + user32 合成输入）
# 验证：① 无记忆位时光标锚定定位 ② Ctrl+滚轮透明度 + prefs v3 落盘
#       ③ 死区拖动 + 记忆位 ④ 热键重呼出沿用记忆位（同屏）
$ErrorActionPreference = 'Stop'
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class SM {
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string cls, string title);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr e);
    [DllImport("user32.dll")] public static extern int GetSystemMetrics(int i);
    [DllImport("user32.dll")] public static extern bool SystemParametersInfoW(uint a, uint b, out RECT r, uint d);
    public struct POINT { public int x, y; }
    public struct RECT { public int l, t, r, b; }
}
"@
$WM_MOUSEWHEEL = 0x020A; $WM_LBUTTONDOWN = 0x0201; $WM_HOTKEY = 0x0312
$MK_CONTROL = 0x0008
[void][SM]::SetProcessDPIAware()   # 与 app 同物理坐标系（否则坐标被 DPI 虚拟化 1/1.5 缩放）
function RectStr([IntPtr]$h) { $r = New-Object SM+RECT; [void][SM]::GetWindowRect($h, [ref]$r); return $r }
function Prefs() { [System.IO.File]::ReadAllBytes("$PSScriptRoot\out\prefs.cfg") }

$exe = "$PSScriptRoot\out\MemePanel.exe"
$prefs = "$PSScriptRoot\out\prefs.cfg"
Stop-Process -Name MemePanel -Force -ErrorAction SilentlyContinue   # 清残留（单实例互斥会弹框）
Start-Sleep -Milliseconds 400
$bak = $null
if (Test-Path $prefs) { $bak = [System.IO.File]::ReadAllBytes($prefs); Remove-Item $prefs }

try {
    # ---- ① 无记忆位：光标锚定 ----
    # 注意：面板位置在 panel_show 瞬间由 GetCursorPos 决定；-show 的启动链路太长
    # （进程创建→淡入 1.2s），期间真机用户动鼠标会干扰。故启动后先隐藏，
    # 再紧贴二次呼出前设光标（间隔 <50ms），干扰窗口最小化。
    $sw = [SM]::GetSystemMetrics(0); $sh = [SM]::GetSystemMetrics(1)
    Start-Process $exe -ArgumentList '-show'
    Start-Sleep -Milliseconds 1500
    $h = [SM]::FindWindowW('MemePanelWnd', 'MemePanel')   # 本机 FindWindow(cls,$null) 不匹配，需带标题
    if ($h -eq [IntPtr]::Zero) { throw 'panel window not found' }
    [void][SM]::PostMessageW($h, $WM_HOTKEY, [IntPtr]1, [IntPtr]::Zero)   # 隐藏（清掉启动位）
    Start-Sleep -Milliseconds 900
    $cx = [int]($sw * 0.7); $cy = [int]($sh * 0.6)
    [void][SM]::SetCursorPos($cx, $cy)
    [void][SM]::PostMessageW($h, $WM_HOTKEY, [IntPtr]1, [IntPtr]::Zero)   # 立即呼出
    Start-Sleep -Milliseconds 1200   # 淡入完成
    $r = RectStr $h
    $w = $r.r - $r.l; $ht = $r.b - $r.t
    $expX = $cx - [int]($w / 2); $expY = $cy - [int]($ht * 2 / 5)
    Write-Host ("[1] cursor-anchor: win=({0},{1}) expect=({2},{3}) size={4}x{5}" -f $r.l, $r.t, $expX, $expY, $w, $ht)
    if ([Math]::Abs($r.l - $expX) -gt 8 -or [Math]::Abs($r.t - $expY) -gt 8) { throw 'anchor position mismatch' }

    # ---- ② Ctrl+滚轮（delta=-120）→ opacity 255-13=242，prefs v3 ----
    $wp = [IntPtr](((0xFF88) -shl 16) -bor $MK_CONTROL)   # MAKEWPARAM(MK_CONTROL, -120)
    [void][SM]::PostMessageW($h, $WM_MOUSEWHEEL, $wp, [IntPtr]((300 -shl 16) -bor 300))
    Start-Sleep -Milliseconds 300
    $p = Prefs
    Write-Host ("[2] prefs: len={0} magic={1:x8} op={2}" -f $p.Length, [BitConverter]::ToUInt32($p, 0), [BitConverter]::ToUInt32($p, 20))
    if ($p.Length -ne 24) { throw 'prefs not v3 (24B)' }
    if ([BitConverter]::ToUInt32($p, 0) -ne 0x3350504D) { throw 'prefs magic != MPP3' }
    if ([BitConverter]::ToUInt32($p, 20) -ne 242) { throw 'opacity != 242' }

    # ---- ③ 死区拖动：全程真实输入（物理左键按下，模态移动循环才会持续）——
    #         光标归位顶带死区 → LEFTDOWN → app 转 WM_NCLBUTTONDOWN 模态循环 →
    #         相对移动 +300/+150 → LEFTUP → 记忆位 = 终位
    #         mouse_event：MOVE=0x1 LEFTDOWN=0x2 LEFTUP=0x4 ----
    $r0 = RectStr $h
    [void][SM]::SetCursorPos($r0.l + 200, $r0.t + 10)   # 顶带死区（client ≈ 200,10）
    Start-Sleep -Milliseconds 200
    [SM]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)         # LEFTDOWN（真实）
    Start-Sleep -Milliseconds 400
    [SM]::mouse_event(0x0001, 300, 150, 0, [UIntPtr]::Zero)     # MOVE +300,+150（喂给移动循环）
    Start-Sleep -Milliseconds 150
    [SM]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)         # LEFTUP → 循环退出 → app 记忆位
    Start-Sleep -Milliseconds 400
    $r1 = RectStr $h
    Write-Host ("[3] drag: ({0},{1}) -> ({2},{3})" -f $r0.l, $r0.t, $r1.l, $r1.t)
    if (($r1.l - $r0.l) -lt 100 -or ($r1.t - $r0.t) -lt 50) { throw 'drag did not move window' }
    $p = Prefs
    $sx = [BitConverter]::ToInt32($p, 12); $sy = [BitConverter]::ToInt32($p, 16)
    Write-Host ("[3] saved pos: ({0},{1})" -f $sx, $sy)
    if ($sx -ne $r1.l -or $sy -ne $r1.t) { throw 'saved pos != window pos' }

    # ---- ④ 热键隐藏 → 光标挪同屏别处 → 重呼出沿用记忆位（越界部分按设计夹回工作区）----
    [void][SM]::PostMessageW($h, $WM_HOTKEY, [IntPtr]1, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 900   # 淡出完成
    [void][SM]::SetCursorPos([int]($sw * 0.2), [int]($sh * 0.2))   # 同屏左上
    [void][SM]::PostMessageW($h, $WM_HOTKEY, [IntPtr]1, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 1200
    $r2 = RectStr $h
    $wa2 = New-Object SM+RECT; [void][SM]::SystemParametersInfoW(0x0030, 0, [ref]$wa2, 0)   # SPI_GETWORKAREA
    $exX = [Math]::Min($r1.l, $wa2.r - $w); $exY = [Math]::Min($r1.t, $wa2.b - $ht)
    if ($exX -lt $wa2.l) { $exX = $wa2.l }
    if ($exY -lt $wa2.t) { $exY = $wa2.t }
    Write-Host ("[4] re-summon same monitor: ({0},{1}) expect clamped-remembered ({2},{3}) workarea {4},{5}-{6},{7}" -f $r2.l, $r2.t, $exX, $exY, $wa2.l, $wa2.t, $wa2.r, $wa2.b)
    if ($r2.l -ne $exX -or $r2.t -ne $exY) { throw 're-summon did not reuse saved pos' }

    # ---- ⑤ 呼出跟随光标开关（prefs bit2）：重启进程加载后，呼出 = 光标锚定 ≠ 记忆位 ----
    Stop-Process -Name MemePanel -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 500
    $p = Prefs
    $flags = [BitConverter]::ToUInt32($p, 4); $rX = [BitConverter]::ToUInt32($p, 12); $rY = [BitConverter]::ToUInt32($p, 16)
    $nb = New-Object byte[] 24
    [BitConverter]::GetBytes([UInt32]0x3350504D).CopyTo($nb, 0)
    [BitConverter]::GetBytes([UInt32]($flags -bor 4)).CopyTo($nb, 4)   # PF_FOLLOWCURSOR
    [BitConverter]::GetBytes([UInt32]7).CopyTo($nb, 8)
    [BitConverter]::GetBytes([UInt32]$rX).CopyTo($nb, 12)              # 记忆位保留（应被忽略）
    [BitConverter]::GetBytes([UInt32]$rY).CopyTo($nb, 16)
    [BitConverter]::GetBytes([UInt32]255).CopyTo($nb, 20)
    [System.IO.File]::WriteAllBytes($prefs, $nb)
    Start-Process $exe -ArgumentList '-show'
    Start-Sleep -Milliseconds 1500
    $h2 = [SM]::FindWindowW('MemePanelWnd', 'MemePanel')
    if ($h2 -eq [IntPtr]::Zero) { throw 'panel window not found (follow-cursor round)' }
    [void][SM]::PostMessageW($h2, $WM_HOTKEY, [IntPtr]1, [IntPtr]::Zero)   # 隐藏
    Start-Sleep -Milliseconds 900
    $cx2 = [int]($sw * 0.8); $cy2 = [int]($sh * 0.5)
    [void][SM]::SetCursorPos($cx2, $cy2)
    [void][SM]::PostMessageW($h2, $WM_HOTKEY, [IntPtr]1, [IntPtr]::Zero)   # 紧贴呼出
    Start-Sleep -Milliseconds 1200
    $r3 = RectStr $h2
    $exX2 = [Math]::Min($cx2 - [int](($r3.r - $r3.l) / 2), $wa2.r - ($r3.r - $r3.l))
    $exY2 = [Math]::Min($cy2 - [int](($r3.b - $r3.t) * 2 / 5), $wa2.b - ($r3.b - $r3.t))
    if ($exX2 -lt $wa2.l) { $exX2 = $wa2.l }
    if ($exY2 -lt $wa2.t) { $exY2 = $wa2.t }
    Write-Host ("[5] follow-cursor ON: ({0},{1}) expect cursor-anchor ({2},{3}) remembered-was ({4},{5})" -f $r3.l, $r3.t, $exX2, $exY2, $rX, $rY)
    if ([Math]::Abs($r3.l - $exX2) -gt 2 -or [Math]::Abs($r3.t - $exY2) -gt 2) { throw 'follow-cursor mode did not anchor at cursor' }   # ±2：PS [int] 四舍五入 vs C 截断
    if ($r3.l -eq $rX -and $r3.t -eq $rY) { throw 'follow-cursor mode wrongly reused saved pos' }

    Write-Host 'SMOKE OK — all five assertions passed'
}
finally {
    Stop-Process -Name MemePanel -Force -ErrorAction SilentlyContinue
    if ($bak) { [System.IO.File]::WriteAllBytes($prefs, $bak) }   # 还原用户 prefs
    else { Remove-Item $prefs -ErrorAction SilentlyContinue }
}
