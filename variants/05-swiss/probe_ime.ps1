Add-Type -AssemblyName System.Drawing
$b = New-Object System.Drawing.Bitmap("shot_imedbg.png")
Write-Host "size: $($b.Width)x$($b.Height)"
# find accent-red cross: two perpendicular 2px lines ~28px long, in drawer area (x>1200)
$crossX=-1; $crossY=-1
for ($y=150; $y -lt $b.Height-100; $y++) {
  for ($x=1200; $x -lt $b.Width-10; $x++) {
    $c=$b.GetPixel($x,$y)
    if ($c.R -gt 170 -and $c.G -lt 110 -and $c.B -lt 90) {
      # check horizontal continuation ~10px
      $c2=$b.GetPixel($x+10,$y); $c3=$b.GetPixel($x-10,$y)
      if ($c2.R -gt 170 -and $c3.R -gt 170) { $crossX=$x; $crossY=$y; break }
    }
  }
  if ($crossX -ge 0) { break }
}
Write-Host "accent cross at ($crossX,$crossY)  [drawer index box should be ~x1210+,y250-300]"
# blue exclusion rect: sample for blue lines
$blueX=-1
for ($y=200; $y -lt 320; $y++) { for ($x=1200; $x -lt $b.Width-6; $x++) {
  $c=$b.GetPixel($x,$y); if ($c.B -gt 200 -and $c.R -lt 100 -and $c.G -lt 100) { $blueX=$x; $blueY=$y; break } }
  if ($blueX -ge 0) { break } }
Write-Host "blue rect first hit at ($blueX,$blueY)"
$b.Dispose()
