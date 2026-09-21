# probe.ps1 - SWISS v3 pixel self-check (dpi=120, bleed=S(3)=4; ASCII only!)
# v3 geometry: search row first -> beam (3px) -> chip row -> hairline -> wall.
# Drawer open: window +S(300)=375px, 2px black vertical separator at drawerLeft,
# grid area geometry identical to closed state (drawerLeft == closed panel right).
# Shots: shot_panel / shot_hover(-hover 5) / shot_drawer(-drawer 5) /
#        shot_drawer_batch / shot_blue_drawer(-accent 1F4AFF -drawer 5) /
#        shot_keyboard(-hover 12).
Add-Type -AssemblyName System.Drawing

function Sw([int]$v) { return [math]::Floor($v * 1.25 + 0.5) }

function IsRed($c)  { return ([math]::Abs($c.R - 255) -le 45) -and ([math]::Abs($c.G - 43) -le 55) -and ([math]::Abs($c.B - 30) -le 55) -and (($c.R - $c.G) -ge 95) }
function IsBlue($c) { return ([math]::Abs($c.R - 31) -le 40) -and ([math]::Abs($c.G - 74) -le 45) -and ([math]::Abs($c.B - 255) -le 40) -and (($c.B - $c.R) -ge 80) }
function IsAcc($c)  { return (IsRed $c) -or (IsBlue $c) }
function IsDark($c) { return ($c.R -le 85) -and ($c.G -le 85) -and ($c.B -le 85) }
function IsTxt($c)  { return ($c.R -le 150) -and ($c.G -le 150) -and ($c.B -le 150) }
function IsPaper($c){ return ($c.R -ge 222) -and ($c.G -ge 222) -and ($c.B -ge 215) }
function IsNotPaper($c) { return -not (IsPaper $c) }

$script:fail = 0
function Chk($name, $cond) {
    if ($cond) { Write-Host ("[PASS] " + $name) } else { Write-Host ("[FAIL] " + $name); $script:fail++ }
}
function CountIf($img, $x0, $y0, $x1, $y1, $fn) {
    $n = 0
    for ($y = $y0; $y -le $y1; $y++) {
        for ($x = $x0; $x -le $x1; $x++) {
            if (& $fn $img.GetPixel($x, $y)) { $n++ }
        }
    }
    return $n
}

# Panel geometry. $drawer = drawer open (window is S(300) wider; grid identical).
function Get-Geom([int]$Wd, [int]$Ht, [bool]$drawer) {
    $g = @{}
    $g.PL = 4; $g.PT = 4; $g.PR = $Wd - 4; $g.PB = $Ht - 4
    $padX = 25
    $g.drawer = $drawer
    $g.drawerLeft = $g.PR - (Sw 300)          # separator x (open) == closed PR
    $g.gridR = if ($drawer) { $g.drawerLeft } else { $g.PR }
    $g.searchT = $g.PT + (Sw 14)
    $g.searchB = $g.searchT + (Sw 32)
    $g.beamY = $g.searchB + (Sw 8)            # beam top; height Sw 2 = 3
    $g.chipT = $g.beamY + (Sw 2) + (Sw 10)
    $g.chipB = $g.chipT + (Sw 26)
    $g.hairY = $g.chipB + (Sw 8)
    $g.gap = Sw 3
    $g.gridL = $g.PL + $padX
    $avail = ($g.gridR - $padX) - $g.gridL
    $g.wallT = $g.hairY + 1 + (Sw 10)
    $minCell = Sw 52
    $g.cols = [math]::Floor(($avail + $g.gap) / ($minCell + $g.gap)); if ($g.cols -lt 1) { $g.cols = 1 }
    $g.cell = [math]::Floor(($avail - ($g.cols - 1) * $g.gap) / $g.cols)
    $g.gridT = $g.wallT + $g.cell + $g.gap
    $g.gridB = $g.PB - (Sw 10)
    $g.stride = $g.cell + $g.gap
    # drawer content bounds
    $g.dL = $g.drawerLeft + 2 + $padX
    $g.dR = $g.PR - $padX
    $g.dT0 = $g.PT + (Sw 14)
    return $g
}

# ================= shot_panel.png =================
Write-Host "===== shot_panel.png ====="
$img = New-Object System.Drawing.Bitmap("shot_panel.png")
$Wd = $img.Width; $Ht = $img.Height
Write-Host ("size: $Wd x $Ht")
$g = Get-Geom $Wd $Ht $false
Write-Host ("geom: cell=$($g.cell) cols=$($g.cols) stride=$($g.stride) searchT=$($g.searchT) beamY=$($g.beamY) wallT=$($g.wallT) gridT=$($g.gridT) gridB=$($g.gridB)")

Chk "outside corner is desktop bg (1,1)" (-not (IsPaper ($img.GetPixel(1,1))))
Chk "inside corner is paper white (8,8)" (IsPaper ($img.GetPixel(8,8)))
Chk "black outer border left (5,300)" (IsDark ($img.GetPixel(5,300)))
Chk "black outer border top (300,5)" (IsDark ($img.GetPixel(300,5)))
Chk "black outer border right (PR-2,300)" (IsDark ($img.GetPixel(($g.PR - 2),300)))
Chk "square corner: (2,8) not panel" (-not (IsPaper ($img.GetPixel(2,8))))

# corner cleanliness: no text ink above search row / below wall
$topInk = CountIf $img ($g.PL + 8) ($g.PT + 5) ($g.PR - 8) ($g.searchT - 2) { param($c) IsTxt $c }
Chk "no text ink above search row ($topInk px)" ($topInk -eq 0)
$botInk = CountIf $img ($g.PL + 8) ($g.gridB + 2) ($g.PR - 8) ($g.PB - 4) { param($c) IsTxt $c }
Chk "no text ink below wall ($botInk px)" ($botInk -eq 0)

# beam: >=3 consecutive full-width black rows under search row
$beamRows = 0
for ($dy = 0; $dy -le 5; $dy++) {
    $y = $g.beamY + $dy
    $n = CountIf $img ($g.PL + 8) $y ($g.PR - 8) $y { param($c) IsDark $c }
    $frac = $n / ($g.PR - $g.PL - 16)
    if ($frac -ge 0.9) { $beamRows++ }
}
Chk "beam >= 3px full-width black" ($beamRows -ge 3)

# search box: white fill + black underline + accent caret
Chk "search box white fill" (IsPaper ($img.GetPixel(($g.PL + 85), ($g.searchT + 8))))
$ln = CountIf $img ($g.PL + 26) ($g.searchB - 2) ($g.PL + 25 + 90) ($g.searchB - 1) { param($c) IsDark $c }
Chk "search black underline ($ln px)" ($ln -ge 120)
$caret = CountIf $img ($g.PL + 25) $g.searchT ($g.PL + 25 + 40) $g.searchB { param($c) IsRed $c }
Chk "accent caret block present ($caret px)" ($caret -ge 20)

# chip row: active black box + accent preceding square + tag text
$boxDark = CountIf $img ($g.PL + 40) $g.chipT ($g.PL + 25 + 80) $g.chipB { param($c) IsDark $c }
Chk "active chip black box ($boxDark px)" ($boxDark -ge 150)
$redSq = CountIf $img ($g.PL + 25) ($g.chipT + 8) ($g.PL + 25 + 16) ($g.chipB - 8) { param($c) IsRed $c }
Chk "active chip preceding accent square ($redSq px)" ($redSq -ge 20)
$tagTxt = CountIf $img ($g.PL + 25 + 110) $g.chipT ($g.PL + 25 + 320) $g.chipB { param($c) IsTxt $c }
Chk "tag row text TIERE/KATZEN ($tagTxt px)" ($tagTxt -ge 40)

# hairlines
$c2 = $img.GetPixel([math]::Floor($Wd / 2), $g.hairY)
Chk "tag-row hairline visible" (($c2.R -ge 195) -and ($c2.R -le 232))
$hx = $g.gridL + $g.stride - $g.gap + [math]::Floor($g.gap / 2)
$hy = $g.wallT + [math]::Floor($g.cell / 2)
$c = $img.GetPixel($hx, $hy)
Chk "wall hairline darker than paper" (($c.R -ge 195) -and ($c.R -le 232))

# recents row content
$wallContent = CountIf $img ($g.gridL + 2) ($g.wallT + 4) ($g.gridL + 400) ($g.wallT + $g.cell - 4) { param($c) IsNotPaper $c }
Chk "recents row images content ($wallContent px)" ($wallContent -ge 800)

# accent only in allowed slots (UI zones outside image cells)
$badAcc = 0
foreach ($y in (($g.PT)..($g.searchT - 2))) { for ($x = $g.PL; $x -lt ($g.PR - 2); $x += 2) { if (IsRed ($img.GetPixel($x, $y))) { $badAcc++ } } }
foreach ($y in (($g.searchB + 2)..($g.beamY + 5))) { for ($x = $g.PL; $x -lt ($g.PR - 2); $x += 2) { if (IsRed ($img.GetPixel($x, $y))) { $badAcc++ } } }
foreach ($y in (($g.chipB + 2)..($g.hairY - 1))) { for ($x = $g.PL; $x -lt ($g.PR - 2); $x += 2) { if (IsRed ($img.GetPixel($x, $y))) { $badAcc++ } } }
foreach ($y in (($g.hairY + 2)..($g.wallT - 2))) { for ($x = $g.PL; $x -lt ($g.PR - 2); $x += 2) { if (IsRed ($img.GetPixel($x, $y))) { $badAcc++ } } }
foreach ($y in (($g.wallT)..($g.gridB - 2))) { for ($x = 7; $x -le 28; $x += 2) { if (IsRed ($img.GetPixel($x, $y))) { $badAcc++ } } }
foreach ($y in (($g.gridB + 2)..($g.PB - 3))) { for ($x = $g.PL; $x -lt ($g.PR - 2); $x += 2) { if (IsRed ($img.GetPixel($x, $y))) { $badAcc++ } } }
Chk "no accent outside allowed slots (bad=$badAcc)" ($badAcc -eq 0)
$img.Dispose()

# ================= shot_hover.png (-hover 5) =================
Write-Host "===== shot_hover.png ====="
$img = New-Object System.Drawing.Bitmap("shot_hover.png")
$Wd = $img.Width; $Ht = $img.Height
$g = Get-Geom $Wd $Ht $false
$L = $g.gridL + 5 * $g.stride; $T = $g.gridT
$R = $L + $g.cell; $cellB = $T + $g.cell
Write-Host ("hover cell rect: ($L,$T)-($R,$cellB)")
Chk "accent frame top"    (IsRed ($img.GetPixel(($L + 30), ($T - 1))))
Chk "accent frame bottom" (IsRed ($img.GetPixel(($L + 30), $cellB)))
Chk "accent frame left"   (IsRed ($img.GetPixel(($L - 1), ($T + 30))))
Chk "accent frame right"  (IsRed ($img.GetPixel($R, ($T + 30))))
Chk "accent number badge topleft" (IsRed ($img.GetPixel(($L + 4), ($T + 4))))
$strip = $img.GetPixel(($L + [math]::Floor($g.cell / 2)), ($cellB - 4))
Chk "white name strip bottom" ($strip.R -ge 215 -and $strip.G -ge 215)
$stripH = Sw 14
$grayN = CountIf $img ($L + 5) ($cellB - $stripH + 2) ($L + [math]::Floor($g.cell / 2)) ($cellB - 2) { param($c) IsTxt $c }
Chk "name strip gray text ($grayN px)" ($grayN -ge 3)
# preview anchored to panel bottom right (drawer closed)
$prR = $g.PR - (Sw 14); $prBtm = $g.PB - (Sw 12); $prL = $prR - (Sw 168); $prT = $prBtm - (Sw 168)
Chk "preview frame left" (IsDark ($img.GetPixel(($prL + 1), ($prT + 100))))
Chk "preview frame top"  (IsDark ($img.GetPixel(($prL + 100), ($prT + 1))))
$pv = CountIf $img ($prL + 8) ($prT + 8) ($prR - 8) ($prT + 60) { param($c) IsNotPaper $c }
Chk "preview image content ($pv px)" ($pv -ge 150)
$img.Dispose()

# ================= shot_keyboard.png (-hover 12) =================
Write-Host "===== shot_keyboard.png ====="
$img = New-Object System.Drawing.Bitmap("shot_keyboard.png")
$Wd = $img.Width; $Ht = $img.Height
$g = Get-Geom $Wd $Ht $false
$L = $g.gridL + 12 * $g.stride; $T = $g.gridT
$R = $L + $g.cell; $cellB = $T + $g.cell
Write-Host ("keyboard cell rect: ($L,$T)-($R,$cellB)")
Chk "kbd frame top"    (IsRed ($img.GetPixel(($L + 30), ($T - 1))))
Chk "kbd frame bottom" (IsRed ($img.GetPixel(($L + 30), $cellB)))
Chk "kbd frame left"   (IsRed ($img.GetPixel(($L - 1), ($T + 30))))
Chk "kbd frame right"  (IsRed ($img.GetPixel($R, ($T + 30))))
Chk "kbd accent number badge" (IsRed ($img.GetPixel(($L + 4), ($T + 4))))
$img.Dispose()

# ================= shot_drawer.png (-drawer 5) =================
Write-Host "===== shot_drawer.png ====="
$img = New-Object System.Drawing.Bitmap("shot_drawer.png")
$Wd = $img.Width; $Ht = $img.Height
$g = Get-Geom $Wd $Ht $true
Write-Host ("size: $Wd x $Ht ; drawerLeft=$($g.drawerLeft) dL=$($g.dL) dR=$($g.dR) T0=$($g.dT0)")
# 1. drawer width: window = closed width + Sw(300) (closed = shot_panel width)
$drawerW = $Wd - 1185
Chk "drawer adds S(300)=375 px to window (delta=$drawerW)" ([math]::Abs($drawerW - (Sw 300)) -le 2)
# 2. 2px black vertical separator (both columns >=85% dark over full height)
$sepDark = CountIf $img $g.drawerLeft $g.PT ($g.drawerLeft + 1) ($g.PB) { param($c) IsDark $c }
$sepMax = 2 * ($g.PB - $g.PT)
Chk "2px black separator column ($sepDark of $sepMax px)" ($sepDark -ge [int](0.85 * $sepMax))
Chk "separator right neighbor is paper" (IsPaper ($img.GetPixel(($g.drawerLeft + 4), 400)))
# 3. beam stops at separator (dark just left of it, paper just right of it on same row)
Chk "beam stops at separator (left dark)" (IsDark ($img.GetPixel(($g.drawerLeft - 3), ($g.beamY + 1))))
Chk "beam does not enter drawer (right paper)" (IsPaper ($img.GetPixel(($g.drawerLeft + 4), ($g.beamY + 1))))
# 4. preview box: contain image + 1px hairline frame (white liner fully covered by
#    this square cat photo; the liner is proven in batch count block letterbox below)
$pvT = $g.dT0; $pvB = $g.dT0 + (Sw 96); $pvR = $g.dL + (Sw 96)
$imgCt = CountIf $img ($g.dL + 4) ($pvT + 4) ($pvR - 4) ($pvB - 4) { param($c) IsNotPaper $c }
Chk "preview image content ($imgCt px)" ($imgCt -ge 800)
$frameC = 0
for ($x = ($g.dL + 2); $x -le ($pvR - 2); $x++) {
    $c = $img.GetPixel($x, ($pvB - 1))
    if ($c.R -lt 246) { $frameC++ }
}
Chk "preview 1px hairline frame bottom row ($frameC px)" ($frameC -ge 40)
# 5. metadata column: gray filename text + size line + open-location underline
$metaL = $pvR + (Sw 12)
$fnTxt = CountIf $img $metaL $g.dT0 ($g.dR - 2) ($g.dT0 + (Sw 14)) { param($c) IsTxt $c }
Chk "metadata filename text ($fnTxt px)" ($fnTxt -ge 15)
$szTxt = CountIf $img $metaL ($g.dT0 + (Sw 18)) ($g.dR - 2) ($g.dT0 + (Sw 34)) { param($c) IsTxt $c }
Chk "metadata size/format text ($szTxt px)" ($szTxt -ge 10)
$openY = $g.dT0 + (Sw 62)
# underline is C_INK(140) on paper -> ~122 gray: use text predicate, not full dark
$openLn = CountIf $img $metaL ($openY - 2) ($g.dR - 2) $openY { param($c) IsTxt $c }
Chk "open-location underline ($openLn px)" ($openLn -ge 15)
# 6. labels section title + chips: both solid (item 5 = cat-shock-v3 has 2 tags)
$secY = $pvB + (Sw 16)
$secTxt = CountIf $img $g.dL $secY ($g.dR - 2) ($secY + (Sw 16)) { param($c) IsTxt $c }
Chk "labels section title ($secTxt px)" ($secTxt -ge 30)
$chipY = $secY + (Sw 20); $chipH = Sw 24
$solidDark = CountIf $img $g.dL $chipY ($g.dL + 240) ($chipY + $chipH) { param($c) IsDark $c }
Chk "two solid chips TIERE+KATZEN ($solidDark px)" ($solidDark -ge 500)
# newtag chip (hollow text, no solid block): sits after the two solid chips
$newTxt = CountIf $img ($g.dL + 170) $chipY ($g.dR - 2) ($chipY + $chipH) { param($c) IsTxt $c }
$newSolid = CountIf $img ($g.dL + 170) $chipY ($g.dR - 2) ($chipY + $chipH) { param($c) IsDark $c }
Chk "newtag chip hollow text ($newTxt px, solid=$newSolid px)" (($newTxt -ge 10) -and ($newSolid -lt 30))
# 7. index section: title + input box (white fill + black underline + filled text) + hint
$idxY = ($chipY + $chipH) + (Sw 14)
$idxTxt = CountIf $img $g.dL $idxY ($g.dR - 2) ($idxY + (Sw 16)) { param($c) IsTxt $c }
Chk "index section title ($idxTxt px)" ($idxTxt -ge 30)
$bxT = $idxY + (Sw 18); $bxB = $bxT + (Sw 30)
$bxWhite = CountIf $img ($g.dL + 3) ($bxT + 3) ($g.dR - 30) ($bxB - 4) { param($c) IsPaper $c }
$bxTot = ($g.dR - 33 - $g.dL - 3) * ($bxB - 7 - $bxT)
Chk "index box white fill ($bxWhite px)" ($bxWhite -ge [int]($bxTot * 0.5))
$uln = CountIf $img ($g.dL + 4) ($bxB - 2) ($g.dR - 4) ($bxB - 1) { param($c) IsDark $c }
Chk "index box black underline ($uln px)" ($uln -ge 200)
$fillTxt = CountIf $img ($g.dL + 4) ($bxT + 3) ($g.dL + 220) ($bxB - 4) { param($c) IsTxt $c }
Chk "index filled text pixels ($fillTxt px)" ($fillTxt -ge 30)
# no accent caret in index box (focus is search)
$idxAcc = CountIf $img ($g.dL + 2) ($bxT + 2) ($g.dR - 2) ($bxB - 2) { param($c) IsAcc $c }
Chk "no accent caret in unfocused index box ($idxAcc px)" ($idxAcc -eq 0)
$hintTxt = CountIf $img $g.dL ($bxB + (Sw 6)) ($g.dR - 2) ($bxB + (Sw 20)) { param($c) IsTxt $c }
Chk "index hint text ($hintTxt px)" ($hintTxt -ge 10)
# 8. delete button at bottom: accent 2px outline, ink text
$delH = Sw 26; $delT = $g.PB - (Sw 16) - $delH
$delAccL = CountIf $img $g.dL ($delT + 4) ($g.dL + 1) ($delT + $delH - 4) { param($c) IsRed $c }
$delAccT = CountIf $img ($g.dL + 4) $delT ($g.dL + 60) ($delT + 1) { param($c) IsRed $c }
Chk "delete button accent outline left ($delAccL px)" ($delAccL -ge 15)
Chk "delete button accent outline top ($delAccT px)" ($delAccT -ge 15)
$delTxt = CountIf $img ($g.dL + 4) ($delT + 2) ($g.dL + 80) ($delT + $delH - 2) { param($c) IsTxt $c }
Chk "delete button text ($delTxt px)" ($delTxt -ge 15)
# 9. grid side still intact: caret + beam + wall content + scrollbar inside grid area
$caret2 = CountIf $img ($g.PL + 25) $g.searchT ($g.PL + 25 + 40) $g.searchB { param($c) IsRed $c }
Chk "search caret still present ($caret2 px)" ($caret2 -ge 20)
$wallCt = CountIf $img ($g.gridL + 2) ($g.wallT + 4) ($g.gridL + 300) ($g.wallT + $g.cell - 4) { param($c) IsNotPaper $c }
Chk "wall content intact ($wallCt px)" ($wallCt -ge 500)
$img.Dispose()

# ================= shot_drawer_batch.png =================
Write-Host "===== shot_drawer_batch.png ====="
$img = New-Object System.Drawing.Bitmap("shot_drawer_batch.png")
$Wd = $img.Width; $Ht = $img.Height
$g = Get-Geom $Wd $Ht $true
# 1. corner marks on 3 selected cells (filtered idx 2,5,9 -> col 2,5,9 of row 0)
foreach ($k in 2,5,9) {
    $L = $g.gridL + $k * $g.stride; $T = $g.gridT
    Chk "corner mark TL at cell $k" (IsRed ($img.GetPixel(($L - 1), ($T - 1))))
    Chk "corner mark BR at cell $k" (IsRed ($img.GetPixel(($L + $g.cell), ($T + $g.cell))))
}
# corner marks are NOT full frames: mid-left edge of a marked cell must not be accent
$L2 = $g.gridL + 2 * $g.stride
Chk "corner marks are not full frame" (-not (IsRed ($img.GetPixel(($L2 - 1), ($g.gridT + [math]::Floor($g.cell / 2))))))
# 2. batch header "yi xuan 3 zhang" + count block (paper liner + thumb + N zhang)
$hdrTxt = CountIf $img $g.dL $g.dT0 ($g.dR - (Sw 30)) ($g.dT0 + (Sw 16)) { param($c) IsTxt $c }
Chk "batch header text ($hdrTxt px)" ($hdrTxt -ge 60)
$xCross = CountIf $img ($g.dR - (Sw 14)) $g.dT0 ($g.dR - 2) ($g.dT0 + (Sw 16)) { param($c) IsTxt $c }
Chk "batch clear-selection X button ($xCross px)" ($xCross -ge 3)
$cbT = $g.dT0 + (Sw 24); $cbB = $cbT + (Sw 96); $cbR = $g.dL + (Sw 96)
$cbCt = CountIf $img ($g.dL + 4) ($cbT + 4) ($cbR - 4) ($cbB - 4) { param($c) IsNotPaper $c }
Chk "count block thumb+text content ($cbCt px)" ($cbCt -ge 200)
# count block white liner: contain in a 56x112 sub-rect guarantees letterbox bands
$cbPaper = CountIf $img ($g.dL + 4) ($cbT + 4) ($g.dL + (Sw 60)) ($cbB - 4) { param($c) IsPaper $c }
Chk "count block white liner letterbox ($cbPaper px)" ($cbPaper -ge 60)
# 3. chips: first-selected (capybara-v4, tag0 only) -> TIERE solid + KATZEN hollow
$secY = $cbB + (Sw 16)
$chipY = $secY + (Sw 20); $chipH = Sw 24
$solid1 = CountIf $img $g.dL $chipY ($g.dL + 110) ($chipY + $chipH) { param($c) IsDark $c }
Chk "TIERE solid chip ($solid1 px)" ($solid1 -ge 200)
$hollowTxt = CountIf $img ($g.dL + 120) $chipY ($g.dL + 250) ($chipY + $chipH) { param($c) IsTxt $c }
$hollowSolid = CountIf $img ($g.dL + 120) $chipY ($g.dL + 250) ($chipY + $chipH) { param($c) IsDark $c }
Chk "KATZEN hollow chip (txt=$hollowTxt px solid=$hollowSolid px)" (($hollowTxt -ge 10) -and ($hollowSolid -lt 30))
# 4. index section shows "multi: not editable" gray note (no input underline)
$idxY = ($chipY + $chipH) + (Sw 14)
$idxNote = CountIf $img $g.dL ($idxY + (Sw 20)) ($g.dR - 2) ($idxY + (Sw 38)) { param($c) IsTxt $c }
Chk "batch index note text ($idxNote px)" ($idxNote -ge 30)
$ulnB = CountIf $img ($g.dL + 4) ($idxY + (Sw 18)) ($g.dR - 4) ($idxY + (Sw 48)) { param($c) IsDark $c }
Chk "batch has no index input underline ($ulnB px)" ($ulnB -lt 60)
# 5. delete button accent outline still present
$delH = Sw 26; $delT = $g.PB - (Sw 16) - $delH
$delAccL2 = CountIf $img $g.dL ($delT + 4) ($g.dL + 1) ($delT + $delH - 4) { param($c) IsRed $c }
Chk "batch delete button accent outline ($delAccL2 px)" ($delAccL2 -ge 15)
$img.Dispose()

# ================= shot_blue_drawer.png (-accent 1F4AFF -drawer 5) =================
Write-Host "===== shot_blue_drawer.png ====="
$img = New-Object System.Drawing.Bitmap("shot_blue_drawer.png")
$Wd = $img.Width; $Ht = $img.Height
$g = Get-Geom $Wd $Ht $true
# caret blue
$caretB = CountIf $img ($g.PL + 25) $g.searchT ($g.PL + 25 + 40) $g.searchB { param($c) IsBlue $c }
$caretR = CountIf $img ($g.PL + 25) $g.searchT ($g.PL + 25 + 40) $g.searchB { param($c) IsRed $c }
Chk "caret is blue ($caretB px)" ($caretB -ge 20)
Chk "caret is NOT red ($caretR px)" ($caretR -eq 0)
# delete button outline blue
$delH = Sw 26; $delT = $g.PB - (Sw 16) - $delH
$delB = CountIf $img $g.dL ($delT + 4) ($g.dL + 1) ($delT + $delH - 4) { param($c) IsBlue $c }
$delR = CountIf $img $g.dL ($delT + 4) ($g.dL + 1) ($delT + $delH - 4) { param($c) IsRed $c }
Chk "delete outline blue ($delB px)" ($delB -ge 15)
Chk "delete outline NOT red ($delR px)" ($delR -eq 0)
# no #FF2B1E red anywhere in UI zones (drawer + header/chip zones, image areas excluded)
$badRed = 0
foreach ($y in (($g.PT)..($g.wallT - 2))) { for ($x = $g.PL; $x -lt ($g.PR - 2); $x += 2) { if (IsRed ($img.GetPixel($x, $y))) { $badRed++ } } }
foreach ($y in (($g.gridB + 2)..($g.PB - 3))) { for ($x = $g.PL; $x -lt ($g.PR - 2); $x += 2) { if (IsRed ($img.GetPixel($x, $y))) { $badRed++ } } }
# drawer zone below wall too
foreach ($y in (($g.wallT)..($g.gridB - 2))) { for ($x = ($g.drawerLeft + 3); $x -lt ($g.PR - 2); $x += 2) { if (IsRed ($img.GetPixel($x, $y))) { $badRed++ } } }
Chk "blue shot: no red in UI zones (bad=$badRed)" ($badRed -eq 0)
$img.Dispose()

Write-Host ""
if ($script:fail -eq 0) { Write-Host "ALL CHECKS PASSED" } else { Write-Host ("FAILURES: " + $script:fail) }
