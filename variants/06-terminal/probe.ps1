# probe.ps1 - pixel-probe self check for 06-terminal shots (ASCII only)
# Geometry at dpi120 (MulDiv rounds half up): bleed=10 padX=23 padT=13 titleH=30
# searchTop=T+58 searchH=38 chipY=T+111 chipH=33 ry=T+157 cell=70 pitch=80
# gridTop=T+245 | mgr: opY=T+111 opH=38 gridTop=T+164 cell=120 gap=13 pitch=133
Add-Type -AssemblyName System.Drawing
$script:fail = 0

function Check($name, $cond, $detail) {
    if ($cond) { Write-Host ("[PASS] " + $name + "  " + $detail) }
    else { Write-Host ("[FAIL] " + $name + "  " + $detail); $script:fail++ }
}
function CountGreen($mp, $x1, $y1, $x2, $y2, $gmin) {
    $n = 0
    for ($y = $y1; $y -lt $y2; $y++) {
        for ($x = $x1; $x -lt $x2; $x++) {
            $c = $mp.GetPixel($x, $y)
            if ($c.G -gt $gmin -and $c.G -gt $c.R + 50 -and $c.G -gt $c.B + 50) { $n++ }
        }
    }
    return $n
}
function CountAmber($mp, $x1, $y1, $x2, $y2) {
    $n = 0
    for ($y = $y1; $y -lt $y2; $y++) {
        for ($x = $x1; $x -lt $x2; $x++) {
            $c = $mp.GetPixel($x, $y)
            if ($c.R -gt 170 -and $c.G -gt 90 -and $c.G -lt 220 -and $c.B -lt 80 -and $c.R -gt $c.G + 25) { $n++ }
        }
    }
    return $n
}
function DetectPanel($mp) {
    $w = $mp.Width; $h = $mp.Height
    $my = [int]($h / 2); $mx = [int]($w / 2)
    $rl = -1; $rr = -1
    for ($x = 0; $x -lt $w; $x++) {
        $c = $mp.GetPixel($x, $my)
        if ($c.G -gt 120 -and $c.G -gt $c.R + 40) { if ($rl -lt 0) { $rl = $x }; $rr = $x }
    }
    $rt = -1; $rb = -1
    for ($y = 0; $y -lt $h; $y++) {
        $c = $mp.GetPixel($mx, $y)
        if ($c.G -gt 120 -and $c.G -gt $c.R + 40) { if ($rt -lt 0) { $rt = $y }; $rb = $y }
    }
    return @($rl, $rt, $rr, $rb)
}

# ================= shot_panel =================
$mp = New-Object System.Drawing.Bitmap("shot_panel.png")
$imgW = $mp.Width; $imgH = $mp.Height
Write-Host ("== shot_panel " + $imgW + "x" + $imgH + " ==")
$q = DetectPanel $mp; $pL = $q[0]; $pT = $q[1]; $pR = $q[2]; $pB = $q[3]
Check "panel-bounds" ($pL -gt 0 -and $pT -gt 0 -and $pR -gt $pL + 500 -and $pB -gt $pT + 400) "L=$pL T=$pT R=$pR B=$pB"

# 1. scanlines: phase analysis at x=R-14 (margin band), rows 300..340
$sx = $pR - 14
$phAvg = @(0.0, 0.0, 0.0); $phN = @(0, 0, 0)
for ($y = 300; $y -lt 340; $y++) {
    $ph = ($y - $pT) % 3
    if ($ph -lt 0) { $ph += 3 }
    $phAvg[$ph] += $mp.GetPixel($sx, $y).G; $phN[$ph]++
}
for ($i = 0; $i -lt 3; $i++) { $phAvg[$i] = $phAvg[$i] / $phN[$i] }
$minV = ($phAvg | Measure-Object -Minimum).Minimum
$minPh = [array]::IndexOf($phAvg, $minV)
$otherAvg = (($phAvg | Where-Object { $_ -ne $minV }) | Measure-Object -Average).Average
Check "scanline-phase2-darkest" ($minPh -eq 2) ("avgG/phase: " + [math]::Round($phAvg[0],1) + "," + [math]::Round($phAvg[1],1) + "," + [math]::Round($phAvg[2],1))
Check "scanline-depth" (($otherAvg - $minV) -ge 1.2) ("delta=" + [math]::Round($otherAvg - $minV, 2))

# 2. double border + corner reinforcement ticks
$c1 = $mp.GetPixel($pL - 3, $pT)
$c2 = $mp.GetPixel($pL, $pT - 3)
Check "corner-tick-h" ($c1.G -gt 150 -and $c1.G -gt $c1.R + 60) ("px($($pL-3),$pT)=RGB($($c1.R),$($c1.G),$($c1.B))")
Check "corner-tick-v" ($c2.G -gt 150 -and $c2.G -gt $c2.R + 60) ("px($pL,$($pT-3))=RGB($($c2.R),$($c2.G),$($c2.B))")
$midx2 = [int]($imgW / 2)
$cIn = $mp.GetPixel($midx2, $pT + 5)
Check "inner-border" ($cIn.G -gt 40 -and $cIn.G -gt $cIn.B + 15) ("inner RGB($($cIn.R),$($cIn.G),$($cIn.B))")

# 3. title text green (Impact MEME://TERMINAL)
$nTitle = CountGreen $mp ($pL + 23) ($pT + 13) ($pL + 260) ($pT + 47) 140
Check "title-green-text" ($nTitle -gt 20) ("green px=$nTitle")

# 4. active chip [全部] inverted + tag chips present ([动物][猫猫], dimmer)
$nChip = CountGreen $mp ($pL + 23) ($pT + 108) ($pL + 220) ($pT + 150) 190
Check "chip-active-inverted" ($nChip -gt 200) ("solid green px=$nChip")
$nTag = CountGreen $mp ($pL + 115) ($pT + 108) ($pL + 340) ($pT + 150) 100
Check "tag-chips-present" ($nTag -gt 30) ("green px=$nTag")

# 5. GIF amber badges exist somewhere in grid
$nAmb = CountAmber $mp ($pL + 20) ($pT + 245) ($pR - 30) ($pB - 60)
Check "gif-amber-badges" ($nAmb -gt 8) ("amber px=$nAmb")

# 6. footer: READY text + block progress bar
$nFoot = CountGreen $mp ($pL + 23) ($pB - 34) ($pL + 160) ($pB - 10) 140
Check "footer-ready-text" ($nFoot -gt 5) ("green px=$nFoot")
$nBlk = CountGreen $mp ($pR - 280) ($pB - 34) ($pR - 30) ($pB - 10) 150
Check "footer-blocks" ($nBlk -gt 40) ("green px=$nBlk")

# 7. dim-green cell frame on normal cell (exact geometry: grid.left = L+23, grid.top = T+245)
$cEdge = $mp.GetPixel($pL + 23, $pT + 280)
Check "grid-cell-frame" ($cEdge.G -gt 35 -and $cEdge.G -gt $cEdge.R + 10) ("frame RGB($($cEdge.R),$($cEdge.G),$($cEdge.B))")
$mp.Dispose()

# ================= shot_hover =================
$mp = New-Object System.Drawing.Bitmap("shot_hover.png")
$imgW = $mp.Width; $imgH = $mp.Height
Write-Host ("== shot_hover " + $imgW + "x" + $imgH + " ==")
$q = DetectPanel $mp; $pL = $q[0]; $pT = $q[1]; $pR = $q[2]; $pB = $q[3]
Check "panel-bounds-hover" ($pL -gt 0 -and $pT -gt 0) "L=$pL T=$pT R=$pR B=$pB"

# hover cell 5: bright 2px frame at left edge (x = L+23+5*80)
$cx0 = $pL + 23 + 5 * 80; $cy0 = $pT + 245
$cHov = $mp.GetPixel($cx0, $cy0 + 35)
Check "hover-cell-frame" ($cHov.G -gt 140 -and $cHov.G -gt $cHov.R + 60) ("px($cx0,$($cy0+35)) RGB($($cHov.R),$($cHov.G),$($cHov.B))")
# hover name bar: dark bar + green "> name" text at cell bottom
$nName = CountGreen $mp ($cx0 + 3) ($cy0 + 49) ($cx0 + 67) ($cy0 + 69) 140
Check "hover-name-bar" ($nName -gt 3) ("green px=$nName")
# preview: frame right edge + VIEW label + info line
$pvR = $pR - 22; $pvB2 = ($pB - 35) - 30; $pvT2 = $pvB2 - 210; $pvL2 = $pvR - 210
$cPv = $mp.GetPixel($pvR - 1, $pvT2 + 60)
Check "preview-frame" ($cPv.G -gt 110 -and $cPv.G -gt $cPv.R + 50) ("px RGB($($cPv.R),$($cPv.G),$($cPv.B))")
$nView = CountGreen $mp ($pvL2 - 10) ($pvT2 - 26) ($pvR + 1) ($pvT2 - 4) 130
Check "preview-VIEW-label" ($nView -gt 3) ("green px=$nView")
$nInf = CountGreen $mp ($pvL2 - 10) ($pvB2 + 5) ($pvR + 1) ($pvB2 + 26) 110
Check "preview-info-line" ($nInf -gt 3) ("green px=$nInf")
# footer filename feedback
$nFname = CountGreen $mp ($pL + 23) ($pB - 34) ($pL + 400) ($pB - 10) 140
Check "footer-filename" ($nFname -gt 3) ("green px=$nFname")
$mp.Dispose()

# ================= shot_mgr_sel =================
$mp = New-Object System.Drawing.Bitmap("shot_mgr_sel.png")
$imgW = $mp.Width; $imgH = $mp.Height
Write-Host ("== shot_mgr_sel " + $imgW + "x" + $imgH + " ==")
$q = DetectPanel $mp; $pL = $q[0]; $pT = $q[1]; $pR = $q[2]; $pB = $q[3]
Check "panel-bounds-mgr" ($pL -gt 0 -and $pT -gt 0 -and $pR -gt $pL + 900) "L=$pL T=$pT R=$pR B=$pB"

# selected cell 3 (big 120px): amber X at center
$mcx = $pL + 23 + 3 * 133; $mcy = $pT + 164
$nX = CountAmber $mp ($mcx + 40) ($mcy + 40) ($mcx + 80) ($mcy + 80)
Check "mgr-amber-X" ($nX -gt 4) ("amber px=$nX")
# selected cell bright frame
$cSel = $mp.GetPixel($mcx, $mcy + 60)
Check "mgr-sel-frame" ($cSel.G -gt 120 -and $cSel.G -gt $cSel.R + 60) ("px RGB($($cSel.R),$($cSel.G),$($cSel.B))")
# op row: bracket buttons (green + delete amber text)
$nBtn = CountGreen $mp ($pR - 720) ($pT + 109) ($pR - 60) ($pT + 151) 130
Check "mgr-buttons" ($nBtn -gt 10) ("green px=$nBtn")
$nDel = CountAmber $mp ($pR - 720) ($pT + 109) ($pR - 60) ($pT + 151)
Check "mgr-delete-amber" ($nDel -gt 2) ("amber px=$nDel")
# SYS: status line
$nSys = CountGreen $mp ($pL + 23) ($pB - 34) ($pL + 320) ($pB - 10) 130
Check "mgr-SYS-status" ($nSys -gt 3) ("green px=$nSys")
# mgr chips active [全部] + tags
$nChipM = CountGreen $mp ($pL + 23) ($pT + 108) ($pL + 220) ($pT + 150) 190
Check "mgr-chip-active" ($nChipM -gt 150) ("solid green px=$nChipM")
$nTagM = CountGreen $mp ($pL + 115) ($pT + 108) ($pL + 340) ($pT + 150) 100
Check "mgr-tag-chips" ($nTagM -gt 30) ("green px=$nTagM")
$mp.Dispose()

Write-Host ("==== TOTAL FAIL: " + $script:fail + " ====")
exit $script:fail
