$src = [System.IO.File]::ReadAllBytes("out\index.bin")
$pos = 12
$nEnt = [BitConverter]::ToUInt32($src, $pos); $pos += 4
$entries = @()
for ($k = 0; $k -lt $nEnt; $k++) {
    $l = [BitConverter]::ToUInt32($src, $pos); $pos += 4
    $name = [System.Text.Encoding]::Unicode.GetString($src, $pos, $l * 2); $pos += $l * 2
    $tm = [BitConverter]::ToUInt64($src, $pos)
    $sz = [BitConverter]::ToUInt64($src, $pos + 8)
    $mt = [BitConverter]::ToUInt64($src, $pos + 16)
    $us = [BitConverter]::ToUInt64($src, $pos + 24)
    $pos += 32
    $entries += , @($name, $tm, $sz, $mt, $us)
}
$ms = New-Object System.IO.MemoryStream
$w = New-Object System.IO.BinaryWriter($ms)
$w.Write([BitConverter]::ToUInt32([System.Text.Encoding]::ASCII.GetBytes("MPX1"), 0))
$w.Write([UInt32]1)
$t1 = [string][char]0x52A8 + [string][char]0x7269      # 动物
$t2 = [string][char]0x732B + [string][char]0x732B      # 猫猫
$tags = @($t1, $t2)
$w.Write([UInt32]$tags.Count)
foreach ($t in $tags) {
    $tb = [System.Text.Encoding]::Unicode.GetBytes($t)
    $w.Write([UInt32]($t.Length)); $w.Write($tb)
}
$w.Write([UInt32]$entries.Count)
foreach ($e in $entries) {
    $name = $e[0]
    $mask = [UInt64]$e[1]
    if ($name -match "capybara|dog|chicken") { $mask = $mask -bor 1 }
    if ($name -match "cat") { $mask = $mask -bor 2 }
    $nb = [System.Text.Encoding]::Unicode.GetBytes($name)
    $w.Write([UInt32]$name.Length); $w.Write($nb)
    $w.Write([UInt64]$mask); $w.Write([UInt64]$e[2]); $w.Write([UInt64]$e[3]); $w.Write([UInt64]$e[4])
}
$w.Flush()
[System.IO.File]::WriteAllBytes("out\index.bin", $ms.ToArray())
$w.Close()
Write-Host ("index.bin rewritten: 2 tags, " + $entries.Count + " entries")
