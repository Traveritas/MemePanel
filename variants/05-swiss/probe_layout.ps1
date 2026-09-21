# probe_layout.ps1 - v3.5 rect single-source-of-truth checks (ASCII only!)
# Shots (all -imedebug -accent FF2B1E):
#   shot_layout1 = -drawer 5      (single target, index box focus, fake comp "kaixin")
#   shot_layout2 = -drawer batch  (batch: index rect must be zero -> NO debug cross)
#   shot_layout3 = no drawer      (must not crash; chip row / search box intact)
Add-Type -AssemblyName System.Drawing

function Sw([int]$v) { return [math]::Floor($v * 1.25 + 0.5) }
function IsRed($c)  { return ([math]::Abs($c.R - 255) -le 45) -and ([math]::Abs($c.G - 43) -le 55) -and ([math]::Abs($c.B - 30) -le 55) -and (($c.R - $c.G) -ge 95) }
function IsBlue($c) { return ($c.B -gt 200) -and ($c.R -lt 100) -and ($c.G -lt 110) }
function IsDark($c) { return ($c.R -le 85) -and ($c.G -le 85) -and ($c.B -le 85) }
function IsTxt($c)  { return ($c.R -le 150) -and ($c.G -le 150) -and ($c.B -le 150) }
function CountIf($img, $x0, $y0, $x1, $y1, $fn) {
    $n = 0
    for ($y = $y0; $y -le $y1; $y++) { for ($x = $x0; $x -le $x1; $x++) {
        if (& $fn $img.GetPixel($x, $y)) { $n++ } } }
    return $n
}
# longest horizontal run of predicate along row $y starting at found pixel
function HRun($img, $x, $y, $fn) {
    $n = 0; $xx = $x
    while ($xx -lt $img.Width - 1 -and (& $fn $img.GetPixel($xx, $y))) { $n++; $xx++ }
    return $n
}

$script:fail = 0
function Chk($name, $cond) {
    if ($cond) { Write-Host ("[PASS] " + $name) } else { Write-Host ("[FAIL] " + $name); $script:fail++ }
}

# ============ shot_layout1.png (-drawer 5 -imedebug) ============
Write-Host "===== shot_layout1.png (single drawer + ime debug) ====="
$img = New-Object System.Drawing.Bitmap("shot_layout1.png")
$Wd = $img.Width; $Ht = $img.Height
# geometry (dpi=120): drawerLeft=1181, dL=1208, T0=22, pvB=142, secY=162, chipY=187,
# idxY=235, index box = (1208,258)-(1531,296)
$crossX = -1; $crossY = -1; $crossRun = 0
for ($y = 240; $y -le 320; $y++) {
    for ($x = 1200; $x -lt $Wd - 12; $x++) {
        if (IsRed ($img.GetPixel($x, $y))) {
            $run = HRun $img $x $y { param($c) IsRed $c }
            if ($run -ge 20) { $crossX = $x; $crossY = $y; $crossRun = $run; break }
        }
    }
    if ($crossX -ge 0) { break }
}
Chk "a1: accent cross h-run>=20 in drawer caret zone" ($crossX -ge 0)
Write-Host ("     cross h-run start at ($crossX,$crossY) run=$crossRun")
# vertical arm of the cross: accent pixels above/below the cross CENTER column
$cc = $crossX + [math]::Floor($crossRun / 2)
$varm = CountIf $img $cc ($crossY - 12) ($cc + 1) ($crossY + 12) { param($c) IsRed $c }
Chk "a2: cross has vertical arm at center col $cc (varm=$varm px)" ($varm -ge 20)
# blue exclusion rect: top-left corner near (1208,250+-20)
$blueX = -1; $blueY = -1
for ($y = 230; $y -le 270; $y++) { for ($x = 1188; $x -le 1228; $x++) {
    if (IsBlue ($img.GetPixel($x, $y))) { $blueX = $x; $blueY = $y; break } }
    if ($blueX -ge 0) { break } }
Write-Host ("     blue rect first pixel at ($blueX,$blueY)")
Chk "a3: blue exclusion rect TL near (1208,250+-20)" (($blueX -ge 0) -and ([math]::Abs($blueX - 1208) -le 20) -and ([math]::Abs($blueY - 250) -le 20))
# b: index box underline + filled text at legacy probe positions (bxB-2/bxB-1 = 294/295)
$uln = CountIf $img 1212 294 1527 295 { param($c) IsDark $c }
Chk "b1: index box black underline ($uln px)" ($uln -ge 200)
$fillTxt = CountIf $img 1212 261 1428 292 { param($c) IsTxt $c }
Chk "b2: index filled text kai xin mao wu shui ($fillTxt px)" ($fillTxt -ge 30)
$img.Dispose()

# ============ shot_layout2.png (-drawer batch -imedebug) ============
Write-Host "===== shot_layout2.png (batch drawer + ime debug) ====="
$img = New-Object System.Drawing.Bitmap("shot_layout2.png")
$Wd = $img.Width; $Ht = $img.Height
# NO debug cross anywhere in drawer zone (delete button outline lives at y~815, excluded by y<=700)
$badCross = 0
for ($y = 150; $y -le 700; $y++) {
    for ($x = 1150; $x -lt $Wd - 12; $x++) {
        if (IsRed ($img.GetPixel($x, $y))) {
            $run = HRun $img $x $y { param($c) IsRed $c }
            if ($run -ge 20) { $badCross++; break }
        }
    }
}
Chk "c1: NO misplaced cross in batch drawer ($badCross rows)" ($badCross -eq 0)
# no blue exclusion rect either (ime never positioned / index rect zero)
$badBlue = CountIf $img 1150 150 ($Wd - 4) 700 { param($c) IsBlue $c }
Chk "c2: no blue ime rect in batch drawer ($badBlue px)" ($badBlue -eq 0)
# batch drawer still intact: TIERE solid chip + no index underline
$solid = CountIf $img 1208 262 1318 292 { param($c) IsDark $c }
Chk "c3: batch chips still drawn ($solid px)" ($solid -ge 200)
$img.Dispose()

# ============ shot_layout3.png (no drawer) ============
Write-Host "===== shot_layout3.png (no drawer) ====="
$img = New-Object System.Drawing.Bitmap("shot_layout3.png")
$Wd = $img.Width; $Ht = $img.Height
Chk "d0: panel shot size sane" (($Wd -ge 1000) -and ($Ht -ge 700))
$PL = 4; $PT = 4; $PR = $Wd - 4
$searchT = $PT + (Sw 14); $searchB = $searchT + (Sw 32)
$caret = CountIf $img ($PL + 25) $searchT ($PL + 25 + 40) $searchB { param($c) IsRed $c }
Chk "d1: search box accent caret ($caret px)" ($caret -ge 20)
$beamRows = 0
$beamY = $searchB + (Sw 8)
for ($dy = 0; $dy -le 5; $dy++) {
    $n = CountIf $img ($PL + 8) ($beamY + $dy) ($PR - 8) ($beamY + $dy) { param($c) IsDark $c }
    if ($n / ($PR - $PL - 16) -ge 0.9) { $beamRows++ }
}
Chk "d2: beam 3px full-width black" ($beamRows -ge 3)
$chipT = $beamY + (Sw 2) + (Sw 10); $chipB = $chipT + (Sw 26)
$boxDark = CountIf $img ($PL + 40) $chipT ($PL + 25 + 80) $chipB { param($c) IsDark $c }
Chk "d3: active chip row intact ($boxDark px)" ($boxDark -ge 150)
$img.Dispose()

Write-Host ""
if ($script:fail -eq 0) { Write-Host "ALL CHECKS PASSED" } else { Write-Host ("FAILURES: " + $script:fail) }
