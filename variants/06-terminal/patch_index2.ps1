# patch_index2.ps1 - fix tag names in out/index.bin (ASCII-only script; tags via char codes)
# Fixes mojibake from patch_index.ps1 (PS5.1 read UTF-8 script as GBK).
# 动 = U+52A8, 物 = U+7269, 猫 = U+732B
$ErrorActionPreference = "Stop"
$path = "D:\Documents\HTA\My Projects\MemePanel\variants\06-terminal\out\index.bin"
$b = [System.IO.File]::ReadAllBytes($path)
$pos = 0
function U32([byte[]]$bb, [ref]$p) {
    $v = [BitConverter]::ToUInt32($bb, $p.Value); $p.Value += 4; return $v
}
function U64([byte[]]$bb, [ref]$p) {
    $v = [BitConverter]::ToUInt64($bb, $p.Value); $p.Value += 8; return $v
}
$magic = U32 $b ([ref]$pos)
if ($magic -ne 0x3158504D) { throw "bad magic" }
$ver = U32 $b ([ref]$pos)
$nTags = U32 $b ([ref]$pos)
for ($t = 0; $t -lt $nTags; $t++) {
    $chars = U32 $b ([ref]$pos)
    $old = [System.Text.Encoding]::Unicode.GetString($b, $pos, $chars * 2)
    $pos += $chars * 2
    Write-Host ("old tag " + $t + " (len " + $chars + ")")
}
$nEnt = U32 $b ([ref]$pos)
$names = @(); $masks = @(); $sizes = @(); $mtimes = @(); $useds = @()
for ($k = 0; $k -lt $nEnt; $k++) {
    $chars = U32 $b ([ref]$pos)
    $name = [System.Text.Encoding]::Unicode.GetString($b, $pos, $chars * 2)
    $pos += $chars * 2
    $names += $name
    $masks += (U64 $b ([ref]$pos))
    $sizes += (U64 $b ([ref]$pos))
    $mtimes += (U64 $b ([ref]$pos))
    $useds += (U64 $b ([ref]$pos))
}
if ($pos -ne $b.Length) { throw "parse mismatch" }

$tagA = [string][char]0x52A8 + [string][char]0x7269     # dong wu (animal)
$tagB = [string][char]0x732B + [string][char]0x732B     # mao mao (cat)
Write-Host ("new tags: " + (($tagA.ToCharArray() | ForEach-Object { '{0:X4}' -f [int]$_ }) -join ' ') + " / " + (($tagB.ToCharArray() | ForEach-Object { '{0:X4}' -f [int]$_ }) -join ' '))

$ms = New-Object System.IO.MemoryStream
$w = New-Object System.IO.BinaryWriter($ms)
$w.Write([uint32]0x3158504D)
$w.Write([uint32]1)
$w.Write([uint32]2)
foreach ($t in @($tagA, $tagB)) {
    $tb = [System.Text.Encoding]::Unicode.GetBytes($t)
    $w.Write([uint32]($tb.Length / 2)); $w.Write($tb)
}
$w.Write([uint32]$nEnt)
for ($k = 0; $k -lt $nEnt; $k++) {
    $nb = [System.Text.Encoding]::Unicode.GetBytes($names[$k])
    $w.Write([uint32]($nb.Length / 2)); $w.Write($nb)
    $w.Write([uint64]$masks[$k]); $w.Write([uint64]$sizes[$k])
    $w.Write([uint64]$mtimes[$k]); $w.Write([uint64]$useds[$k])
}
$w.Flush()
[System.IO.File]::WriteAllBytes($path, $ms.ToArray())
$w.Close(); $ms.Close()

# verify round-trip
$b2 = [System.IO.File]::ReadAllBytes($path)
$ok1 = $false; $ok2 = $false
for ($i = 0; $i -lt $b2.Length - 3; $i++) {
    if (-not $ok1 -and [BitConverter]::ToUInt16($b2, $i) -eq 0x52A8) { $ok1 = $true }
    if (-not $ok2 -and [BitConverter]::ToUInt16($b2, $i) -eq 0x732B) { $ok2 = $true }
}
Write-Host ("verify bytes: dong=$ok1 mao=$ok2  size=" + $b2.Length)
