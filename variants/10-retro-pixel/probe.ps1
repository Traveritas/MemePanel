# probe.ps1 — RETRO-PIXEL 像素自检（dpi=120 / bleed=4 / 速发窗 1185x872 / 管理窗 1544x1064）
# 几何换算（速发）：L=T=4 R=1181 B=868 | 标题条纹 y7..39 | 搜索 [24,55]-[1161,95]
#   chips y108..138 | recents y153..223 | 表格 [24,238]-[1141,817] cols=16(尾列66px) rows=8
#   滚动条 x1141..1161 | footer [24,830]-[1161,855] | 预览 [947,601]-[1161,820]
# 管理：R=1540 B=1060 | op 行 y110..140 | 表格 [24,155]-[1500,1009] cols=12 rows=7 cell=120
#   滚动条 x1500..1520 | footer [24,1022]-[1520,1047]
Add-Type -AssemblyName System.Drawing

$pass = 0; $fail = 0
function Check($name, $ok) {
    if ($ok) { $script:pass++; Write-Host ("[PASS] " + $name) -ForegroundColor Green }
    else     { $script:fail++; Write-Host ("[FAIL] " + $name) -ForegroundColor Red }
}
function IsBlack($c) { $c.R -lt 70 -and $c.G -lt 70 -and $c.B -lt 70 }
function IsWhite($c) { $c.R -gt 235 -and $c.G -gt 235 -and $c.B -gt 235 }
function IsGray80($c) { [math]::Abs($c.R-128) -lt 14 -and [math]::Abs($c.G-128) -lt 14 -and [math]::Abs($c.B-128) -lt 14 }
function IsPanel($c)  { [math]::Abs($c.R-221) -lt 8 -and [math]::Abs($c.G-221) -lt 8 -and [math]::Abs($c.B-221) -lt 8 }
function IsBlue($c)   { $c.B -gt 230 -and $c.R -lt 90 -and $c.G -lt 90 }
function ScanAny($b, $l, $t, $r, $bt, $pred) {
    for ($y = $t; $y -lt $bt; $y++) {
        for ($x = $l; $x -lt $r; $x++) {
            if (& $pred $b.GetPixel($x, $y)) { return $true }
        }
    }
    return $false
}

# ============ shot_panel.png（速发模式）============
$b = New-Object System.Drawing.Bitmap("shot_panel.png")
Write-Host "== shot_panel.png ($($b.Width)x$($b.Height)) ==" -ForegroundColor Cyan

Check "panel outer 2px black border (left)"    (IsBlack $b.GetPixel(4, 434))
Check "panel outer 2px black border (top)"     (IsBlack $b.GetPixel(590, 4))
Check "panel inner white highlight (top)"      (IsWhite $b.GetPixel(590, 6))
Check "panel inner #808080 (bottom)"           (IsGray80 $b.GetPixel(590, 865))
Check "panel body #DDDDDD (between rows)"      (IsPanel $b.GetPixel(590, 100))

# 标题栏条纹：2px 周期黑白交替（x=300 处避开铭牌/关闭钮）
$stripesOk = $true
for ($y = 7; $y -le 14; $y++) {
    $c = $b.GetPixel(300, $y)
    $wantBlack = ([math]::Floor(($y - 7) / 2) % 2) -eq 0
    if ($wantBlack -and -not (IsBlack $c)) { $stripesOk = $false }
    if (-not $wantBlack -and -not (IsWhite $c)) { $stripesOk = $false }
}
Check "title stripes 2px black/white period (y7..14)" $stripesOk
Check "title plate white behind text"          (IsWhite $b.GetPixel(18, 14))
Check "close btn black 1px border"             (IsBlack $b.GetPixel(1145, 23))
Check "close btn white interior"               (IsWhite $b.GetPixel(1150, 14))
Check "close X glyph dark (center)"            (IsBlack $b.GetPixel(1158, 23))

# 搜索框：白底黑 2px 边凹槽
Check "search black 2px border (left)"         (IsBlack $b.GetPixel(25, 75))
Check "search white field"                     (IsWhite $b.GetPixel(60, 75))
Check "search inset #808080 (top)"             (IsGray80 $b.GetPixel(60, 57))
Check "search caret / magnifier dark pixels"   (ScanAny $b 34 60 70 92 { param($c) $c.R -lt 80 -and $c.G -lt 80 -and $c.B -lt 80 })

# 标签斜面按钮（全部=激活：按下态 + 蓝字）
Check "chip pressed bevel: top edge black"     (IsBlack $b.GetPixel(30, 108))
Check "chip pressed bevel: bottom edge white"  (IsWhite $b.GetPixel(30, 137))
Check "chip pressed fill #BBBBBB"              ([math]::Abs($b.GetPixel(30,122).R-187) -lt 12)
Check "chip active blue text present"          (ScanAny $b 24 108 90 138 { param($c) $c.B -gt 200 -and $c.R -lt 110 -and $c.G -lt 110 })

# 最近行：1px 黑边白底小方格
Check "recent tile black border"               (IsBlack $b.GetPixel(24, 188))
Check "recent tile row exists (2nd tile border)" (IsBlack $b.GetPixel(102, 160))

# 表格网格：白底 + 1px 黑外框 + 1px 黑分隔线
Check "table top 1px black border"             (IsBlack $b.GetPixel(500, 238))
Check "table vertical grid line x=95"          (IsBlack $b.GetPixel(95, 300))
Check "table horizontal grid line y=309"       (IsBlack $b.GetPixel(300, 238+1+70))
Check "table bottom filler white"              (IsWhite $b.GetPixel(500, 810))

# 真 Win95 滚动条（常驻，thumb 满轨）
Check "scrollbar up arrow black triangle"      (IsBlack $b.GetPixel(1151, 246))
Check "scrollbar down arrow black triangle"    (IsBlack $b.GetPixel(1151, 806))
Check "scrollbar thumb #DDDDDD fill"           (IsPanel $b.GetPixel(1151, 300))
Check "scrollbar thumb groove #808080"         (IsGray80 $b.GetPixel(1151, 527))
Check "scrollbar track ticks #C6/#E0 present"  (ScanAny $b 1143 260 1159 780 { param($c) ([math]::Abs($c.R-224) -lt 8) -or ([math]::Abs($c.R-198) -lt 8) })

# 状态栏：凹槽 + 黑字计数
Check "footer sunken top #808080"              (IsGray80 $b.GetPixel(600, 830))
Check "footer sunken bottom white"             (IsWhite $b.GetPixel(600, 854))
Check "footer count text dark (Consolas)"      (ScanAny $b 1050 830 1155 855 { param($c) $c.R -lt 80 -and $c.G -lt 80 -and $c.B -lt 80 })
$b.Dispose()

# ============ shot_hover.png（悬停第 6 格）============
$b = New-Object System.Drawing.Bitmap("shot_hover.png")
Write-Host "== shot_hover.png ==" -ForegroundColor Cyan
Check "hover blue 2px frame (left band)"       (IsBlue $b.GetPixel(374, 270))
Check "hover blue 2px frame (top band)"        (IsBlue $b.GetPixel(410, 239))
Check "hover black name bar"                   (IsBlack $b.GetPixel(400, 292))
Check "hover name bar white text"              (ScanAny $b 377 289 443 307 { param($c) IsWhite $c })
Check "preview window black 2px border"        (IsBlack $b.GetPixel(947, 700))
Check "preview title stripes black row"        (IsBlack $b.GetPixel(1000, 604))
Check "preview title stripes white row"        (IsWhite $b.GetPixel(1000, 606))
Check "preview white plate behind title"       (IsWhite $b.GetPixel(955, 608))
Check "footer filename text on hover"          (ScanAny $b 32 830 600 855 { param($c) $c.R -lt 80 -and $c.G -lt 80 -and $c.B -lt 80 })
Check "footer count still present"             (ScanAny $b 1050 830 1155 855 { param($c) $c.R -lt 80 -and $c.G -lt 80 -and $c.B -lt 80 })
$b.Dispose()

# ============ shot_mgr_sel.png（管理模式 + 选中格）============
$b = New-Object System.Drawing.Bitmap("shot_mgr_sel.png")
Write-Host "== shot_mgr_sel.png ($($b.Width)x$($b.Height)) ==" -ForegroundColor Cyan
Check "mgr title stripes present"              (IsBlack $b.GetPixel(400, 7))
Check "mgr search white field"                 (IsWhite $b.GetPixel(200, 75))
Check "mgr op-row bevel buttons (white edge)"  (ScanAny $b 900 110 1520 140 { param($c) IsWhite $c })
Check "mgr op-row bevel buttons (black edge)"  (ScanAny $b 900 110 1520 140 { param($c) IsBlack $c })
Check "mgr table top border"                   (IsBlack $b.GetPixel(800, 155))
Check "mgr table vertical line x=145"          (IsBlack $b.GetPixel(145, 400))
Check "mgr scrollbar up arrow triangle"        (IsBlack $b.GetPixel(1510, 163))
Check "selected cell blue overlay (name strip)"(ScanAny $b 388 253 502 274 { param($c) $c.B -gt 230 -and $c.R -lt 150 -and $c.G -lt 150 })
Check "selected cell blue frame hover"         (IsBlue $b.GetPixel(384, 216))
Check "mgr cell filename text present"         (ScanAny $b 27 252 143 275 { param($c) $c.R -lt 80 -and $c.G -lt 80 -and $c.B -lt 80 })
Check "mgr status bar text"                    (ScanAny $b 32 1022 700 1047 { param($c) $c.R -lt 80 -and $c.G -lt 80 -and $c.B -lt 80 })
Check "mgr footer count (Consolas)"            (ScanAny $b 1420 1022 1518 1047 { param($c) $c.R -lt 80 -and $c.G -lt 80 -and $c.B -lt 80 })
$b.Dispose()

Write-Host ""
Write-Host ("RESULT: {0} pass, {1} fail" -f $pass, $fail) -ForegroundColor Yellow
exit $(if ($fail -gt 0) { 1 } else { 0 })
