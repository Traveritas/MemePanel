# probe.ps1 - PALETTE variant pixel self-check (coords from shot_layout.txt)
Add-Type -AssemblyName System.Drawing
$fail = 0
function Px($b, $x, $y) { $c = $b.GetPixel($x, $y); return @($c.R, $c.G, $c.B) }
function Check($name, $cond, $detail) {
    if ($cond) { Write-Host ("[PASS] " + $name + "  " + $detail) }
    else { Write-Host ("[FAIL] " + $name + "  " + $detail); $script:fail++ }
}
function ScanL($b, $x0, $x1, $y0, $y1, $thr) {
    for ($y = $y0; $y -le $y1; $y++) {
        for ($x = $x0; $x -le $x1; $x++) {
            $c = $b.GetPixel($x, $y)
            if ((($c.R + $c.G + $c.B) / 3) -gt $thr) { return $true }
        }
    }
    return $false
}

# ---------- shot_panel.png  (win 1205x1050, panel 4,4-976,1046) ----------
$b = New-Object System.Drawing.Bitmap("shot_panel.png")
Check "panel-size" ($b.Width -eq 1205 -and $b.Height -eq 1050) ("win=" + $b.Width + "x" + $b.Height)
$pw = 976 - 4; $ph = 1046 - 4
Check "portrait-panel" ($pw -lt $ph) ("panel ${pw}x${ph}")
$c = Px $b 29 331
Check "sel-bar-purple" ($c[0] -gt 110 -and $c[0] -lt 175 -and $c[2] -gt 220 -and ($c[2] - $c[1]) -gt 90) ("(29,331)=" + ($c -join ','))
$c = Px $b 500 331
Check "sel-row-gradient" (($c[2] - $c[0]) -gt 12 -and $c[2] -lt 150) ("(500,331)=" + ($c -join ','))
$c = Px $b 500 406
Check "plain-row" (($c[2] - $c[0]) -lt 12 -and $c[2] -lt 90) ("(500,406)=" + ($c -join ','))
$cl = Px $b 30 75; $cr = Px $b 940 75
Check "sep-purple-end" (($cl[2] - $cl[1]) -gt 30) ("(30,75)=" + ($cl -join ','))
Check "sep-cyan-end" (($cr[1] - $cr[0]) -gt 25) ("(940,75)=" + ($cr -join ','))
$c = Px $b 200 30
Check "panel-base" (($c[0] + $c[1] + $c[2]) / 3 -lt 75 -and $c[2] -gt $c[0]) ("(200,30)=" + ($c -join ','))
Check "recents-header" (ScanL $b 27 90 82 99 70) "RECENTS header"
$cA = Px $b 67 135; $cB = Px $b 500 135
Check "recent-thumb" ((($cA[0] - $cB[0]) * ($cA[0] - $cB[0]) + ($cA[1] - $cB[1]) * ($cA[1] - $cB[1]) + ($cA[2] - $cB[2]) * ($cA[2] - $cB[2])) -gt 900) ("thumb=" + ($cA -join ',') + " bg=" + ($cB -join ','))
Check "chips-present" (ScanL $b 27 300 258 286 60) "chips"
Check "footer-count" (ScanL $b 27 140 1010 1033 60) "count text"
Check "footer-hints" (ScanL $b 780 953 1010 1033 50) "key hints"
$c = Px $b 1100 100
Check "preview-zone-clear" ($c[2] -gt 70) ("(1100,100)=" + ($c -join ','))
$b.Dispose()

# ---------- shot_hover.png  (hover=5 -> row 671..741) ----------
$b = New-Object System.Drawing.Bitmap("shot_hover.png")
$c = Px $b 29 706
Check "hover-bar-purple" ($c[0] -gt 110 -and $c[2] -gt 220 -and ($c[2] - $c[1]) -gt 90) ("(29,706)=" + ($c -join ','))
$c = Px $b 500 706
Check "hover-row-gradient" (($c[2] - $c[0]) -gt 12) ("(500,706)=" + ($c -join ','))
$c = Px $b 29 331
Check "row0-unselected" (-not ($c[2] -gt 220 -and ($c[2] - $c[1]) -gt 90)) ("(29,331)=" + ($c -join ','))
$c = Px $b 1000 430
Check "preview-card" ((($c[0] + $c[1] + $c[2]) / 3) -lt 90) ("(1000,430)=" + ($c -join ','))
$cA = Px $b 1096 525; $cB = Px $b 1100 100
Check "preview-image" ((($cA[0] - $cB[0]) * ($cA[0] - $cB[0]) + ($cA[1] - $cB[1]) * ($cA[1] - $cB[1]) + ($cA[2] - $cB[2]) * ($cA[2] - $cB[2])) -gt 900) ("img=" + ($cA -join ',') + " bg=" + ($cB -join ','))
Check "enter-hint" (ScanL $b 850 953 671 741 50) "ENTER hint"
$b.Dispose()

# ---------- shot_mgr_sel.png  (win 1544x1064, hover=3 -> cell 426..546 x 140..260) ----------
$b = New-Object System.Drawing.Bitmap("shot_mgr_sel.png")
Check "mgr-size" ($b.Width -eq 1544 -and $b.Height -eq 1064) ("win=" + $b.Width + "x" + $b.Height)
$c = Px $b 429 200
Check "mgr-bar-purple" ($c[0] -gt 110 -and $c[2] -gt 220 -and ($c[2] - $c[1]) -gt 90) ("(429,200)=" + ($c -join ','))
$cnt = 0
for ($y = 141; $y -le 259; $y++) {
    for ($x = 427; $x -le 545; $x++) {
        $c = Px $b $x $y
        if (($c[2] - $c[0]) -gt 18 -and $c[2] -lt 190) { $cnt++ }
    }
}
Check "mgr-cell-gradient" ($cnt -gt 40) ("tinted px=" + $cnt)
$found = $false
for ($y = 146; $y -le 162; $y++) {
    for ($x = 434; $x -le 450; $x++) {
        $c = Px $b $x $y
        if ($c[2] -gt 200 -and ($c[2] - $c[1]) -gt 80) { $found = $true; break }
    }
    if ($found) { break }
}
Check "mgr-check-badge" $found "check badge purple"
Check "mgr-status" (ScanL $b 27 600 1024 1047 60) "status line"
Check "mgr-count" (ScanL $b 1380 1517 1024 1047 50) "sel count"
Check "mgr-buttons" (ScanL $b 1200 1460 85 115 55) "button row"
$cl = Px $b 30 75
Check "mgr-sep" (($cl[2] - $cl[1]) -gt 30) ("(30,75)=" + ($cl -join ','))
$b.Dispose()

Write-Host ("==== FAIL COUNT: " + $fail + " ====")
