# probe.ps1 - 07-candy pixel self-check (dpi=120, S(v)=round(v*1.25), bleed=4)
Add-Type -AssemblyName System.Drawing
$fail = 0
function Chk($name, $cond, $detail) {
    if ($cond) { Write-Host ("[PASS] " + $name + "  " + $detail) }
    else { Write-Host ("[FAIL] " + $name + "  " + $detail); $script:fail++ }
}
function Px($b, $x, $y) { $c = $b.GetPixel([int]$x, [int]$y); return @($c.R, $c.G, $c.B) }
function Scan($b, $x0, $y0, $x1, $y1, $pred) {
    $y = $y0
    while ($y -le $y1) {
        $x = $x0
        while ($x -le $x1) {
            $c = $b.GetPixel([int]$x, [int]$y)
            if ((& $pred @(@($c.R, $c.G, $c.B)))) { return @($x, $y) }
            $x++
        }
        $y++
    }
    return $null
}
function Sum3($c) { return [int]$c[0] + [int]$c[1] + [int]$c[2] }

# active mint chip = solid #4DD8A8-ish
$mintPred    = { param($c) $c[1] -gt 175 -and $c[0] -gt 20 -and $c[0] -lt 135 -and $c[2] -gt 120 -and $c[2] -lt 215 -and ($c[1] - $c[0]) -gt 55 }
# inactive chips are translucent tints; their 1.5px stroke is the saturated ring
$orangePred  = { param($c) $c[0] -gt 150 -and $c[1] -gt 85 -and $c[1] -lt 175 -and $c[2] -lt 120 -and ($c[0] - $c[2]) -gt 50 }
$strawPred   = { param($c) $c[0] -gt 150 -and $c[1] -lt 130 -and $c[2] -gt 95 -and $c[2] -lt 175 -and ($c[0] - $c[1]) -gt 60 }
$whitePred   = { param($c) $c[0] -gt 215 -and $c[1] -gt 215 -and $c[2] -gt 215 }

$bleed = 4

# ---------- shot_panel ----------
$b1 = New-Object System.Drawing.Bitmap("shot_panel.png")
$W = $b1.Width; $H = $b1.Height
Write-Host ("== shot_panel " + $W + "x" + $H + " ==")
$PL = $bleed; $PT = $bleed; $PR = $W - $bleed; $PB = $H - $bleed
# layout
$padX = 20; $padT = 18
$searchTop = $PT + $padT; $searchBot = $searchTop + 50
$chipY = $searchBot + 18; $chipH = 35
$ry = $chipY + $chipH + 20; $rcell = 80
$gridTop = $ry + $rcell + 23
$heartX = $PL + $padX + 13; $heartCY = $ry + 40
$midX = [int]($W * 0.5)

# 1. desktop bg in bleed corner vs panel purple night
$bgc = Px $b1 2 2
$pc = Px $b1 $midX ($chipY + $chipH + 8)
Chk "panel purple night (B>G, B>R, dark)" ($pc[2] -gt $pc[1] + 6 -and $pc[2] -gt $pc[0] + 4 -and $pc[0] -lt 80) ("bg=(" + ($bgc -join ',') + ") panel=(" + ($pc -join ',') + ")")
# 2. vertical gradient top lighter than bottom
$gtop = Px $b1 $midX ($PT + 6)
$gbot = Px $b1 $midX ($PB - 8)
Chk "vertical gradient (top lighter)" ($gtop[2] - $gbot[2] -ge 6) ("top=(" + ($gtop -join ',') + ") bottom=(" + ($gbot -join ',') + ")")
# 3. three candy colors in tag row (left half = chips only)
$m = Scan $b1 $PL $chipY ([int]($W * 0.5)) ($chipY + $chipH) $mintPred
Chk "active mint chip in tag row" ($null -ne $m) ("at " + ($m -join ','))
$o = Scan $b1 $PL $chipY ([int]($W * 0.5)) ($chipY + $chipH) $orangePred
Chk "orange chip (dongwu) in tag row" ($null -ne $o) ("at " + ($o -join ','))
$s = Scan $b1 $PL $chipY ([int]($W * 0.5)) ($chipY + $chipH) $strawPred
Chk "strawberry chip (maomao) in tag row" ($null -ne $s) ("at " + ($s -join ','))
# 4. jelly 3D: active mint pill center column, top vs bottom
$top2 = Px $b1 60 ($chipY + 6)
$bot2 = Px $b1 60 ($chipY + 29)
$d = [Math]::Abs($top2[0] - $bot2[0]) + [Math]::Abs($top2[1] - $bot2[1]) + [Math]::Abs($top2[2] - $bot2[2])
Chk "jelly pill top-vs-bottom diff >= 30" ($d -ge 30) ("top=(" + ($top2 -join ',') + ") bottom=(" + ($bot2 -join ',') + ") diff=$d")
# 5. search inset darker than panel
$sbc = Px $b1 ($PL + 70) ([int](($searchTop + $searchBot) / 2) + 12)
Chk "search inset darker than panel" ($sbc[2] -lt 60) ("searchbg=(" + ($sbc -join ',') + ")")
# 6. mint heart icon at recents left
$h = Scan $b1 ($heartX - 16) ($heartCY - 16) ($heartX + 16) ($heartCY + 16) $mintPred
Chk "mint heart icon at recents left" ($null -ne $h) ("at " + ($h -join ','))
# 7. recent row shows image content
$div = 0; $prev = $null
$xx = 64 + 10
while ($xx -lt 64 + 80) {
    $c = Px $b1 $xx ($ry + 40)
    if ($null -ne $prev) {
        $dd = [Math]::Abs($c[0] - $prev[0]) + [Math]::Abs($c[1] - $prev[1]) + [Math]::Abs($c[2] - $prev[2])
        if ($dd -gt 40) { $div++ }
    }
    $prev = $c
    $xx += 6
}
Chk "recent row shows image content" ($div -ge 2) ("transitions=$div")
$b1.Dispose()

# ---------- shot_hover ----------
$b2 = New-Object System.Drawing.Bitmap("shot_hover.png")
$b1r = New-Object System.Drawing.Bitmap("shot_panel.png")
Write-Host ("== shot_hover " + $b2.Width + "x" + $b2.Height + " ==")
$cx0 = 24 + 5 * 80; $cy0 = $gridTop
$cy0lift = $cy0 - 3   # S(2)=2.5 -> MulDiv rounds to 3
# 1. hover cell mint border at top edge (stroke ~1.5px at y = cy0lift)
$edge = Px $b2 ($cx0 + 35) $cy0lift
Chk "hover cell mint border" ($edge[1] -gt 120 -and ($edge[1] - $edge[0]) -gt 15) ("edge=(" + ($edge -join ',') + ")")
# 2. gloss lightens interior vs plain shot (same thumbnail)
$hvi = Px $b2 ($cx0 + 35) ($cy0lift + 12)
$pli = Px $b1r ($cx0 + 35) ($cy0 + 12)
Chk "hover gloss lightens cell" ((Sum3 $hvi) - (Sum3 $pli) -ge 12) ("hover=(" + ($hvi -join ',') + ") plain=(" + ($pli -join ',') + ")")
# 3. bottom dark band (band over bottom 4px inside cell, above name-bar text)
$hvb = Px $b2 ($cx0 + 35) ($cy0lift + 68)
$plb = Px $b1r ($cx0 + 35) ($cy0 + 68)
Chk "hover bottom dark band" ((Sum3 $hvb) - (Sum3 $plb) -le -20) ("hover=(" + ($hvb -join ',') + ") plain=(" + ($plb -join ',') + ")")
# 4. preview cream wrapper ring
$footerTop = $PB - 13 - 23
$pvB = $footerTop - 15
$pvR = $PR - 18
$pvL = $pvR - 210
$pvT = $pvB - 210
$wrapT = Px $b2 ([int](($pvL + $pvR) / 2)) ($pvT + 3)
Chk "preview cream wrapper ring" ($wrapT[0] -gt 150 -and $wrapT[1] -gt 145 -and $wrapT[2] -gt 135) ("wrap=(" + ($wrapT -join ',') + ")")
# 5. footer candy filename chip
$fc = Scan $b2 $PL $footerTop ($PL + 300) ($PB - 13) $mintPred
Chk "footer candy filename chip" ($null -ne $fc) ("at " + ($fc -join ','))
$b2.Dispose(); $b1r.Dispose()

# ---------- shot_mgr_sel ----------
$b3 = New-Object System.Drawing.Bitmap("shot_mgr_sel.png")
$W3 = $b3.Width; $H3 = $b3.Height
Write-Host ("== shot_mgr_sel " + $W3 + "x" + $H3 + " ==")
$PL3 = $bleed; $PT3 = $bleed; $PR3 = $W3 - $bleed; $PB3 = $H3 - $bleed
$sTop = $PT3 + 18; $sBot = $sTop + 50
$opY = $sBot + 18; $opH = 40
$gTop3 = $opY + $opH + 20
# 1. three candy colors: chips on left, strawberry anywhere (delete pill)
$m3 = Scan $b3 $PL3 $opY 700 ($opY + $opH) $mintPred
$o3 = Scan $b3 $PL3 $opY 700 ($opY + $opH) $orangePred
$s3 = Scan $b3 $PL3 $opY 700 ($opY + $opH) $strawPred
Chk "mgr: mint chip" ($null -ne $m3) ("at " + ($m3 -join ','))
Chk "mgr: orange chip" ($null -ne $o3) ("at " + ($o3 -join ','))
Chk "mgr: strawberry chip" ($null -ne $s3) ("at " + ($s3 -join ','))
# 2. selected cell 3 check bubble
$c3x = 24 + 3 * 135; $c3y = $gTop3
$sel = Scan $b3 ($c3x + 8) ($c3y + 8) ($c3x + 34) ($c3y + 34) $mintPred
Chk "mgr: candy check bubble on selected cell" ($null -ne $sel) ("at " + ($sel -join ',') + " expect near " + ($c3x + 19) + "," + ($c3y + 19))
# 3. white check inside bubble
$wchk = Scan $b3 ($c3x + 10) ($c3y + 10) ($c3x + 32) ($c3y + 32) $whitePred
Chk "mgr: white check inside bubble" ($null -ne $wchk) ("at " + ($wchk -join ','))
# 4. selected cell candy stroke at right edge
$e3 = Px $b3 ($c3x + 119) ($c3y + 60)
Chk "mgr: selected cell candy stroke" ($e3[1] -gt 100 -and ($e3[1] - $e3[0]) -gt 15) ("edge=(" + ($e3 -join ',') + ")")
# 5. gap between cells shows panel
$gapc = Px $b3 ($c3x + 127) ($c3y + 60)
Chk "mgr: gap between cells shows panel" ($gapc[2] -gt $gapc[0] + 4 -and $gapc[2] -gt $gapc[1] + 6) ("gap=(" + ($gapc -join ',') + ")")
$b3.Dispose()
Write-Host ("== TOTAL FAIL: " + $fail + " ==")
exit $fail
