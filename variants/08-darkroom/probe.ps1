# DARKROOM pixel probes — exact geometry (MulDiv rounds .5 up; screen ~2560x1600, dpi=120)
# quick window 1185x872, panel (4,4)-(1181,868), grid.top=201, rowH=105, cell=70, gap=81 pitch
# mgr   window 1544x1064, panel (4,4)-(1540,1060), grid.top=120, rowH=158, cell=120, pitch=133
Add-Type -AssemblyName System.Drawing
$fail = 0
function Check($name, $cond, $detail) {
    if ($cond) { Write-Host ("[PASS] {0}  {1}" -f $name, $detail) }
    else { Write-Host ("[FAIL] {0}  {1}" -f $name, $detail); $script:fail++ }
}
function CountRegion($b, $x0, $y0, $x1, $y1, $pred) {
    $n = 0
    for ($x = $x0; $x -le $x1; $x++) {
        for ($y = $y0; $y -le $y1; $y++) {
            $c = $b.GetPixel($x, $y)
            if (& $pred $c.R $c.G $c.B) { $n++ }
        }
    }
    return $n
}

# ---------- shot_panel.png (quick, no hover) ----------
$b = New-Object System.Drawing.Bitmap("shot_panel.png")
$c = $b.GetPixel(123, 240)
Check "panel-body charcoal" ($c.R -ge 20 -and $c.R -le 40 -and $c.B -ge 22 -and $c.B -le 45) ("($($c.R),$($c.G),$($c.B))")
$c = $b.GetPixel(280, 28)
Check "band brown" ($c.R -gt $c.G -and $c.G -gt $c.B -and $c.R -ge 40 -and $c.R -le 90) ("($($c.R),$($c.G),$($c.B))")
$n = CountRegion $b 47 11 262 49 { param($r,$g,$bl) $r -gt 170 -and ($r - $bl) -gt 30 }
Check "band label text" ($n -gt 40) ("aged-yellow px=$n")
$dark = 0; $trans = 0; $prevDark = $false
for ($y = 62; $y -le 855; $y++) {
    $c = $b.GetPixel(18, $y)
    $isDark = (($c.R + $c.G + $c.B) / 3) -lt 22
    if ($isDark) { $dark++ }
    if ($isDark -ne $prevDark) { $trans++ }
    $prevDark = $isDark
}
Check "sprocket holes dark rows" ($dark -gt 150) ("darkpx=$dark")
Check "sprocket hole period" ($trans -gt 40) ("transitions=$trans")
$n = CountRegion $b 47 280 117 296 { param($r,$g,$bl) $r -gt 120 -and ($r - $bl) -gt 25 }
Check "frame number text" ($n -gt 8) ("px=$n")
$n = CountRegion $b 52 201 112 203 { param($r,$g,$bl) ($r + $g + $bl) / 3 -lt 25 }
Check "film edge band" ($n -gt 100) ("dark px=$n/183")
$n = CountRegion $b 47 106 99 184 { param($r,$g,$bl) $r -gt 110 -and ($r - $bl) -gt 20 }
Check "REC label" ($n -gt 10) ("px=$n")
$n = CountRegion $b 47 69 260 97 { param($r,$g,$bl) $r -gt 90 -and $g -gt 80 }
Check "chip row text" ($n -gt 15) ("px=$n")
$n = CountRegion $b 950 832 1160 854 { param($r,$g,$bl) $r -gt 100 -and ($r - $bl) -gt 20 }
Check "ROLL counter" ($n -gt 10) ("px=$n")
$n = CountRegion $b 47 278 1100 298 { param($r,$g,$bl) $r -gt 190 -and $g -gt 90 -and $g -lt 200 -and ($r - $bl) -gt 110 }
Check "GIF amber frame no." ($n -ge 3) ("px=$n")
$c = $b.GetPixel(500, 29)
Check "search slot dark" (($c.R + $c.G + $c.B) / 3 -lt 30) ("($($c.R),$($c.G),$($c.B))")
$b.Dispose()

# ---------- shot_hover.png ----------
$b = New-Object System.Drawing.Bitmap("shot_hover.png")
# grease circle: frame (452,201)-(522,279); ellipse (448,197)-(526,285)
$cx = 487; $cy = 241; $rx = 39; $ry = 44
$red = 0; $miss = 0
foreach ($a in 0,45,90,135,180,225,270,315) {
    $rad = $a * [Math]::PI / 180
    $px = [int]($cx + $rx * [Math]::Cos($rad))
    $py = [int]($cy + $ry * [Math]::Sin($rad))
    $c = $b.GetPixel($px, $py)
    $isRed = ($c.R -gt 150 -and ($c.R - $c.G) -gt 60)
    if ($isRed) { $red++ } else { $miss++ }
    Write-Host ("  circle probe ${a}deg ($px,$py) -> ($($c.R),$($c.G),$($c.B))")
}
Check "grease circle gap (>=1 of 8 missing)" ($miss -ge 1) ("missing=$miss red=$red")
$n = 0; $tot = 0
foreach ($a in (@(15..26) + @(64..116) + @(154..175)) ) {
    $rad = $a * [Math]::PI / 180
    $tot++
    $px = [int]($cx + $rx * [Math]::Cos($rad)); $py = [int]($cy + $ry * [Math]::Sin($rad))
    $c = $b.GetPixel($px, $py)
    if ($c.R -gt 150 -and ($c.R - $c.G) -gt 60) { $n++ }
}
Check "grease ring density" ($n -ge $tot * 0.7) ("red=$n/$tot")
$gapRed = 0
foreach ($a in 358, 1, 2, 177, 179, 183) {
    $rad = $a * [Math]::PI / 180
    $px = [int]($cx + $rx * [Math]::Cos($rad)); $py = [int]($cy + $ry * [Math]::Sin($rad))
    $c = $b.GetPixel($px, $py)
    if ($c.R -gt 150 -and ($c.R - $c.G) -gt 60) { $gapRed++ }
}
Check "grease gap empty" ($gapRed -le 1) ("gap red=$gapRed/6")
$n = CountRegion $b 47 832 500 854 { param($r,$g,$bl) $r -gt 130 -and $g -gt 120 }
Check "footer filename" ($n -gt 30) ("px=$n")
$n = CountRegion $b 950 832 1160 854 { param($r,$g,$bl) $r -gt 100 -and ($r - $bl) -gt 20 }
Check "FRM counter" ($n -gt 10) ("px=$n")
$c = $b.GetPixel(945, 604)
Check "preview paper white" ($c.R -gt 225 -and $c.G -gt 220) ("($($c.R),$($c.G),$($c.B))")
$n = CountRegion $b 943 602 973 632 { param($r,$g,$bl) $r -gt 150 -and ($r - $g) -gt 60 }
Check "loupe icon red" ($n -gt 5) ("px=$n")
$n = CountRegion $b 1123 782 1153 812 { param($r,$g,$bl) $r -gt 130 -and ($r - $g) -gt 50 }
Check "loupe crosshair" ($n -gt 3) ("px=$n")
$b.Dispose()

# ---------- shot_mgr_sel.png ----------
$b = New-Object System.Drawing.Bitmap("shot_mgr_sel.png")
# selected cell 3: frame (446,120)-(566,248); ellipse (440,114)-(572,257)
$cx = 506; $cy = 186; $rx = 66; $ry = 72
$red = 0; $miss = 0
foreach ($a in 0,45,90,135,180,225,270,315) {
    $rad = $a * [Math]::PI / 180
    $px = [int]($cx + $rx * [Math]::Cos($rad)); $py = [int]($cy + $ry * [Math]::Sin($rad))
    $c = $b.GetPixel($px, $py)
    $isRed = ($c.R -gt 150 -and ($c.R - $c.G) -gt 60)
    if ($isRed) { $red++ } else { $miss++ }
    Write-Host ("  sel circle probe ${a}deg ($px,$py) -> ($($c.R),$($c.G),$($c.B))")
}
Check "mgr sel circle gap" ($miss -ge 1) ("missing=$miss red=$red")
$n = 0; $tot = 0
foreach ($a in (@(20..27) + @(63..117) + @(153..160) + @(200..207) + @(243..297) + @(333..340)) ) {
    $rad = $a * [Math]::PI / 180
    $tot++
    $px = [int]($cx + $rx * [Math]::Cos($rad)); $py = [int]($cy + $ry * [Math]::Sin($rad))
    $c = $b.GetPixel($px, $py)
    if ($c.R -gt 150 -and ($c.R - $c.G) -gt 60) { $n++ }
}
Check "mgr sel ring density" ($n -ge $tot * 0.7) ("red=$n/$tot")
$n = CountRegion $b 548 114 574 138 { param($r,$g,$bl) $r -gt 190 -and $g -gt 100 -and $g -lt 210 -and $bl -lt 120 }
Check "mgr sel amber X" ($n -gt 8) ("px=$n")
$n = CountRegion $b 47 1024 300 1046 { param($r,$g,$bl) $r -gt 170 -and $g -gt 90 -and $bl -lt 120 }
Check "DARKROOM prefix" ($n -gt 15) ("px=$n")
$n = CountRegion $b 1390 1024 1515 1046 { param($r,$g,$bl) $r -gt 100 -and ($r - $bl) -gt 20 }
Check "mgr SEL/ROLL counter" ($n -gt 10) ("px=$n")
$n = CountRegion $b 1150 69 1480 104 { param($r,$g,$bl) $r -gt 100 -and ($r - $bl) -gt 15 }
Check "mgr buttons outlined" ($n -gt 30) ("px=$n")
$c = $b.GetPixel(280, 28)
Check "mgr band brown" ($c.R -gt $c.G -and $c.G -gt $c.B) ("($($c.R),$($c.G),$($c.B))")
$dark = 0
for ($y = 62; $y -le 1047; $y++) {
    $c = $b.GetPixel(18, $y)
    if ((($c.R + $c.G + $c.B) / 3) -lt 22) { $dark++ }
}
Check "mgr sprocket holes" ($dark -gt 150) ("darkpx=$dark")
$n = CountRegion $b 47 249 167 265 { param($r,$g,$bl) $r -gt 120 -and ($r - $bl) -gt 25 }
Check "mgr frame numbers" ($n -gt 8) ("px=$n")
$n = CountRegion $b 52 121 112 122 { param($r,$g,$bl) ($r + $g + $bl) / 3 -lt 25 }
Check "mgr film edge band" ($n -gt 80) ("dark px=$n/122")
$c = $b.GetPixel(172, 190)
Check "mgr body charcoal" ($c.R -ge 20 -and $c.R -le 40) ("($($c.R),$($c.G),$($c.B))")
$b.Dispose()

Write-Host ""
if ($fail -eq 0) { Write-Host "ALL PROBES PASSED" } else { Write-Host "$fail PROBES FAILED" }