# smoke_imp.ps1 - v0.14 import content-addressing smoke test (ASCII only!)
# Sandbox copy of the exe under %TEMP%; real app\out data is only READ (source images).
# Phase 1: crafted v3 index.bin -> startup migration assertions
# Phase 2: -import folder -> dedup/filter/tag assertions + idempotency
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe  = Join-Path $root 'out\MemePanel.exe'
$sb   = Join-Path $env:TEMP 'mp_smoke14'
$memes = Join-Path $sb 'memes'
$FAIL = 0

function Fail($msg) { $script:FAIL++; Write-Host "[FAIL] $msg" -ForegroundColor Red }
function Ok($msg)   { Write-Host "[ ok ] $msg" -ForegroundColor Green }
function Check($cond, $msg) { if ($cond) { Ok $msg } else { Fail $msg } }

function Run-Exe($arglist) {
    $p = Start-Process -FilePath (Join-Path $sb 'MemePanel.exe') -ArgumentList $arglist -PassThru
    if (-not $p.WaitForExit(30000)) { $p.Kill(); throw 'exe timeout' }
}

function Write-IndexV3($path, $entries) {
    $w = [System.IO.BinaryWriter]::new([System.IO.File]::Create($path))
    $w.Write([uint32]0x3158504D)   # MPX1
    $w.Write([uint32]3)            # version 3
    $w.Write([uint32]1)            # 1 tag
    $t = [System.Text.Encoding]::Unicode.GetBytes('anim')
    $w.Write([uint32]($t.Length / 2)); $w.Write($t)
    $w.Write([uint32]$entries.Count)
    foreach ($e in $entries) {
        $n = [System.Text.Encoding]::Unicode.GetBytes($e.name)
        $w.Write([uint32]($n.Length / 2)); $w.Write($n)
        $w.Write([uint64]$e.tagmask); $w.Write([uint64]$e.size)
        $w.Write([uint64]$e.mtime);   $w.Write([uint64]$e.used)
        if ($e.text) { $it = [System.Text.Encoding]::Unicode.GetBytes($e.text); $w.Write([uint32]($it.Length / 2)); $w.Write($it) }
        else { $w.Write([uint32]0) }
        $w.Write([uint64]$e.flags)
    }
    $w.Close()
}

function Read-Index($path) {
    $r = [System.IO.BinaryReader]::new([System.IO.File]::OpenRead($path))
    $null = $r.ReadUInt32()            # magic
    $ver = $r.ReadUInt32()
    $nTags = $r.ReadUInt32(); $tags = @()
    for ($i = 0; $i -lt $nTags; $i++) { $l = $r.ReadUInt32(); $tags += [System.Text.Encoding]::Unicode.GetString($r.ReadBytes($l * 2)) }
    $nEnt = $r.ReadUInt32(); $ents = @()
    for ($i = 0; $i -lt $nEnt; $i++) {
        $l = $r.ReadUInt32(); $name = [System.Text.Encoding]::Unicode.GetString($r.ReadBytes($l * 2))
        $tagmask = $r.ReadUInt64(); $size = $r.ReadUInt64(); $mtime = $r.ReadUInt64(); $used = $r.ReadUInt64()
        $l = $r.ReadUInt32(); $text = $null; if ($l) { $text = [System.Text.Encoding]::Unicode.GetString($r.ReadBytes($l * 2)) }
        $flags = $r.ReadUInt64(); $hash = $r.ReadUInt64()
        $l = $r.ReadUInt32(); $orig = $null; if ($l) { $orig = [System.Text.Encoding]::Unicode.GetString($r.ReadBytes($l * 2)) }
        $ents += @{ name = $name; tagmask = $tagmask; size = $size; mtime = $mtime; used = $used; text = $text; flags = $flags; hash = $hash; orig = $orig }
    }
    $r.Close()
    return @{ ver = $ver; tags = $tags; ents = $ents }
}

# ---------- sandbox setup ----------
if (Test-Path $sb) { Remove-Item $sb -Recurse -Force }
New-Item -ItemType Directory $sb | Out-Null
New-Item -ItemType Directory $memes | Out-Null
Copy-Item $exe $sb

$demo = Get-ChildItem (Join-Path $root 'out\memes') -File | Sort-Object Name
# pick DISTINCT contents (demo library is full of duplicate copies: capybara x6, hamster x4 ...)
$seen = @{}
$distinct = @($demo | Where-Object { $_.Extension -ne '.gif' } | Sort-Object Name | Where-Object {
    $h = (Get-FileHash $_.FullName -Algorithm SHA256).Hash
    if ($seen[$h]) { $false } else { $seen[$h] = $true; $true }
})
$gif = @($demo | Where-Object { $_.Extension -eq '.gif' } | Sort-Object Name | Select-Object -First 1)
$imgs = @($distinct | Select-Object -First 4)
Check ($imgs.Count -ge 4 -and $gif.Count -ge 1) "distinct sources picked ($($imgs.Count) img / $($gif.Count) gif)"

# library: cat(img1) dog(img2) twin_a/twin_b(img3, same content) fake.jpg(gif content)
Copy-Item $imgs[0].FullName (Join-Path $memes 'cat.png')
Copy-Item $imgs[1].FullName (Join-Path $memes 'dog.png')
Copy-Item $imgs[2].FullName (Join-Path $memes 'twin_a.png')
Copy-Item $imgs[2].FullName (Join-Path $memes 'twin_b.png')
Copy-Item $gif[0].FullName (Join-Path $memes 'fake.jpg')

$mk = { param($n, $tm, $used, $txt, $fl) @{ name = $n; tagmask = [uint64]$tm; size = (Get-Item (Join-Path $memes $n)).Length; mtime = [uint64]0; used = [uint64]$used; text = $txt; flags = [uint64]$fl } }
Write-IndexV3 (Join-Path $sb 'index.bin') @(
    ( & $mk 'cat.png'    1 0        'kitty' 1 )
    ( & $mk 'dog.png'    0 900000   $null   0 )
    ( & $mk 'twin_a.png' 0 0        $null   0 )
    ( & $mk 'twin_b.png' 1 777      $null   0 )   # tag bit0 ('anim') + used merge into twin_a
    ( & $mk 'fake.jpg'   0 0        $null   0 )
)

Write-Host "`n== Phase 1: v3 -> v4 migration ==" -ForegroundColor Cyan
Run-Exe @('-shot', (Join-Path $sb 's0.png'))
$idx = Read-Index (Join-Path $sb 'index.bin')
Check ($idx.ver -eq 4) "index.bin upgraded to v4 (got v$($idx.ver))"
Check ($idx.ents.Count -eq 4) "dup pair merged: 5 entries -> $($idx.ents.Count)"
$bad = @($idx.ents | Where-Object { $_.name -notmatch '^[0-9a-f]{16}\.(png|jpg|gif|bmp|webp)$' })
Check ($bad.Count -eq 0) "all disk names are 16-hex + ext"
$files = @(Get-ChildItem $memes -File)
Check ($files.Count -eq 4) "memes/ has 4 files (got $($files.Count))"
$cat = $idx.ents | Where-Object { $_.orig -eq 'cat.png' }
Check ($null -ne $cat -and $cat.flags -eq 1 -and $cat.tagmask -band 1 -and $cat.text -eq 'kitty') "cat entry keeps fav flag + tag + indexText"
$twin = $idx.ents | Where-Object { $_.orig -eq 'twin_a.png' }
Check ($null -ne $twin -and ($twin.tagmask -band 1) -and $twin.used -eq 777) "twin merge ORs tagmask + max used"
Check (@($idx.ents | Where-Object { $_.orig -eq 'twin_b.png' }).Count -eq 0) "twin_b vanished (merged)"
$fake = $idx.ents | Where-Object { $_.orig -eq 'fake.jpg' }
Check ($null -ne $fake -and $fake.name -match '^[0-9a-f]{16}\.gif$') "fake.jpg (gif content) -> hash name with .gif"
Check (Test-Path (Join-Path $sb 'index.bin.bak')) "index.bin.bak backup exists"
$thumbs = @(Get-ChildItem (Join-Path $sb 'thumbs') -File -ErrorAction SilentlyContinue)
Check ($thumbs.Count -ge 4) "thumbs generated for hash names ($($thumbs.Count))"

$bakT = (Get-Item (Join-Path $sb 'index.bin.bak')).LastWriteTimeUtc
Run-Exe @('-shot', (Join-Path $sb 's0b.png'))
$idx2 = Read-Index (Join-Path $sb 'index.bin')
$bakT2 = (Get-Item (Join-Path $sb 'index.bin.bak')).LastWriteTimeUtc
Check ($idx2.ents.Count -eq 4 -and $bakT2 -eq $bakT) "second run: migration skipped (idempotent), no re-backup"

# ---------- Phase 2: -import ----------
Write-Host "`n== Phase 2: -import folder ==" -ForegroundColor Cyan
$imp = Join-Path $sb 'import_src'
New-Item -ItemType Directory $imp | Out-Null
New-Item -ItemType Directory (Join-Path $imp 'sub') | Out-Null
Copy-Item $imgs[3].FullName (Join-Path $imp 'A.png')
Copy-Item $imgs[3].FullName (Join-Path $imp 'A2.png')        # same content, new name -> dup
Set-Content (Join-Path $imp 'note.txt') 'not an image'
# 1x1 gif89a (unique content, not in demo lib) named .jpg -> magic sniff -> lands as .gif
[byte[]]$gif1 = (0x47,0x49,0x46,0x38,0x39,0x61,0x01,0x00,0x01,0x00,0x80,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x2C,0x00,0x00,0x00,0x00,0x01,0x00,0x01,0x00,0x00,0x02,0x02,0x44,0x01,0x00,0x3B)
[System.IO.File]::WriteAllBytes((Join-Path $imp 'fake2.jpg'), $gif1)
# 1x1 png inside subfolder -> must be ignored (folder import is non-recursive)
[byte[]]$png1 = (0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,0x00,0x00,0x00,0x0D,0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x08,0x06,0x00,0x00,0x00,0x1F,0x15,0xC4,0x89,0x00,0x00,0x00,0x0D,0x49,0x44,0x41,0x54,0x78,0x9C,0x62,0x00,0x01,0x00,0x00,0x05,0x00,0x01,0x0D,0x0A,0x2D,0xB4,0x00,0x00,0x00,0x00,0x49,0x45,0x4E,0x44,0xAE,0x42,0x60,0x82)
[System.IO.File]::WriteAllBytes((Join-Path $imp 'sub\E.png'), $png1)

Run-Exe @('-import', $imp, '-shot', (Join-Path $sb 's1.png'), '-win', 'import')
$idx3 = Read-Index (Join-Path $sb 'index.bin')
$files3 = @(Get-ChildItem $memes -File)
Check ($files3.Count -eq 6) "memes/ 4 -> 6 files (A + fake2 only; got $($files3.Count))"
Check ($idx3.ents.Count -eq 6) "index has 6 entries"
Check (-not (Test-Path (Join-Path $memes 'note.txt'))) "note.txt rejected (non-image)"
$e = $idx3.ents | Where-Object { $_.orig -eq 'A.png' }
Check ($null -ne $e) "A.png imported with origName"
Check (@($idx3.ents | Where-Object { $_.orig -eq 'A2.png' }).Count -eq 0) "A2.png deduped (no second entry, first-wins orig)"
$f2 = $idx3.ents | Where-Object { $_.orig -eq 'fake2.jpg' }
Check ($null -ne $f2 -and $f2.name -match '^[0-9a-f]{16}\.gif$') "fake2.jpg sniffed -> .gif hash name"
$tagBit = [uint64][math]::Pow(2, [array]::IndexOf($idx3.tags, 'import_src'))
Check ($tagBit -gt 0) "tag 'import_src' auto-created"
$tagged = @($idx3.ents | Where-Object { [uint64]$_.tagmask -band $tagBit })
Check ($tagged.Count -eq 2) "both new files tagged 'import_src' (folder batch tagging; got $($tagged.Count))"
$subHash = ($idx3.ents | Where-Object { $_.orig -eq 'sub\E.png' })
Check ($null -eq $subHash) "subfolder file ignored (non-recursive)"

Run-Exe @('-import', $imp, '-shot', (Join-Path $sb 's1b.png'))
$idx4 = Read-Index (Join-Path $sb 'index.bin')
Check ($idx4.ents.Count -eq 6 -and @(Get-ChildItem $memes -File).Count -eq 6) "re-import same folder: idempotent (still 6)"

Write-Host "`n== panel visual shots ==" -ForegroundColor Cyan
Run-Exe @('-shot', (Join-Path $sb 's2.png'), '-hover', '0')

Write-Host ("`nRESULT: " + $(if ($FAIL -eq 0) { 'ALL PASS' } else { "$FAIL FAILURES" })) -ForegroundColor $(if ($FAIL -eq 0) { 'Green' } else { 'Red' })
exit $(if ($FAIL -eq 0) { 0 } else { 1 })
