<#
.SYNOPSIS
    Pulls the behavior and resource packs out of a Minecraft Bedrock server or
    world on this PC and packages them as .mcpack / .mcaddon files.

.DESCRIPTION
    Two sources:

    * A server folder (the one with bedrock_server.exe) or a world folder.
      This is the only place behavior packs exist: the server never sends
      them to players, so no client-side tool can get them. The packs the
      world actually uses are read from world_behavior_packs.json and
      world_resource_packs.json and matched to their folders by UUID.

    * The game's download cache (-FromCache). When you join a server that has
      resource packs, the game downloads and keeps them. Only resource packs
      appear here. Packs the server encrypted are reported and skipped.

    Works in Windows PowerShell 5.1 (built into Windows 10/11) and PowerShell 7.

.EXAMPLE
    .\Extract-Packs.ps1 -ServerPath "C:\bedrock-server"

.EXAMPLE
    .\Extract-Packs.ps1 -FromCache

.EXAMPLE
    .\Extract-Packs.ps1 -ServerPath "C:\bedrock-server" -All -Folders
#>
[CmdletBinding()]
param(
    # A Bedrock Dedicated Server folder, or a single world folder.
    [string]$ServerPath,

    # Take resource packs from the game's download cache instead.
    [switch]$FromCache,

    # Scan this folder for downloaded packs instead of the default game folders.
    [string]$CachePath,

    # Where the output goes. Defaults to an "extracted" folder next to this script.
    [string]$OutDir,

    # Export every pack in the server folder, not only the ones the world uses.
    [switch]$All,

    # Also write each pack out as a plain folder, next to its .mcpack.
    [switch]$Folders
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$Sep = [IO.Path]::DirectorySeparatorChar

# ---------------------------------------------------------------------------
# JSON

# Pack JSON routinely has // and /* */ comments and trailing commas, which
# ConvertFrom-Json in PowerShell 5.1 rejects.
function ConvertFrom-LenientJson([string]$Text) {
    $Text = $Text.TrimStart([char]0xFEFF)
    $sb = New-Object System.Text.StringBuilder
    $n = $Text.Length
    $i = 0
    $inString = $false
    while ($i -lt $n) {
        $c = $Text[$i]
        if ($inString) {
            [void]$sb.Append($c)
            if ($c -eq [char]'\' -and $i + 1 -lt $n) {
                [void]$sb.Append($Text[$i + 1])
                $i += 2
                continue
            }
            if ($c -eq [char]'"') { $inString = $false }
            $i++
            continue
        }
        if ($c -eq [char]'"') {
            $inString = $true
            [void]$sb.Append($c)
            $i++
            continue
        }
        if ($c -eq [char]'/' -and $i + 1 -lt $n) {
            $d = $Text[$i + 1]
            if ($d -eq [char]'/') {
                while ($i -lt $n -and $Text[$i] -ne [char]"`n") { $i++ }
                continue
            }
            if ($d -eq [char]'*') {
                $end = $Text.IndexOf('*/', $i + 2)
                if ($end -lt 0) { $i = $n } else { $i = $end + 2 }
                continue
            }
        }
        [void]$sb.Append($c)
        $i++
    }
    $clean = [regex]::Replace($sb.ToString(), ',(\s*[\]}])', '$1')
    return ($clean | ConvertFrom-Json)
}

function Get-Prop($Object, [string]$Name) {
    if ($null -eq $Object) { return $null }
    $p = $Object.PSObject.Properties[$Name]
    if ($null -eq $p) { return $null }
    return $p.Value
}

# Manifest versions are [1, 0, 0] in format 1/2 and "1.0.0" in format 3.
function Format-Version($Version) {
    if ($null -eq $Version) { return '0.0.0' }
    if ($Version -is [string]) { return $Version }
    return (@($Version) -join '.')
}

# ---------------------------------------------------------------------------
# Reading packs, from a folder or from inside a .mcpack/.zip

function Read-PackBytes($Pack, [string]$Relative) {
    if ($Pack.Kind -eq 'Dir') {
        $file = Join-Path $Pack.Path ($Relative.Replace('/', $Sep))
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { return $null }
        return , [IO.File]::ReadAllBytes($file)
    }
    $zip = [IO.Compression.ZipFile]::OpenRead($Pack.Path)
    try {
        $want = $Pack.Prefix + $Relative
        foreach ($e in $zip.Entries) {
            if ($e.FullName.Replace('\', '/') -ieq $want) {
                $ms = New-Object IO.MemoryStream
                $s = $e.Open()
                try { $s.CopyTo($ms) } finally { $s.Dispose() }
                return , $ms.ToArray()
            }
        }
        return $null
    } finally { $zip.Dispose() }
}

function Read-PackText($Pack, [string]$Relative) {
    $bytes = Read-PackBytes $Pack $Relative
    if ($null -eq $bytes) { return $null }
    return [Text.Encoding]::UTF8.GetString($bytes).TrimStart([char]0xFEFF)
}

# Names are often localisation keys ("pack.name") that live in texts/en_US.lang.
function Resolve-PackName($Pack, [string]$Name) {
    if ($Name -and $Name -notmatch '^[\w.]+$') { return $Name }
    if (-not $Name) { $Name = 'pack.name' }
    $lang = Read-PackText $Pack 'texts/en_US.lang'
    if ($lang) {
        foreach ($line in ($lang -split "`r?`n")) {
            $eq = $line.IndexOf('=')
            if ($eq -gt 0 -and $line.Substring(0, $eq).Trim() -eq $Name) {
                $value = $line.Substring($eq + 1)
                $hash = $value.IndexOf("`t#")
                if ($hash -ge 0) { $value = $value.Substring(0, $hash) }
                if ($value.Trim()) { return $value.Trim() }
            }
        }
    }
    return $Name
}

# An encrypted pack's contents.json starts with a version, then this magic.
function Test-Encrypted($Pack) {
    $b = Read-PackBytes $Pack 'contents.json'
    if ($null -eq $b -or $b.Length -lt 8) { return $false }
    return ($b[4] -eq 0xFC -and $b[5] -eq 0xB9 -and $b[6] -eq 0xCF -and $b[7] -eq 0x9B)
}

function New-PackInfo([string]$Kind, [string]$Path, [string]$Prefix, [string]$Display) {
    $pack = [pscustomobject]@{
        Kind      = $Kind
        Path      = $Path
        Prefix    = $Prefix
        Display   = $Display
        Uuid      = $null
        Version   = $null
        Name      = $null
        Type      = 'other'
        Encrypted = $false
        Modified  = $null
    }
    try {
        $manifest = ConvertFrom-LenientJson (Read-PackText $pack 'manifest.json')
    } catch {
        Write-Warning "Skipping $Display : manifest.json does not parse ($($_.Exception.Message))"
        return $null
    }
    $header = Get-Prop $manifest 'header'
    $uuid = Get-Prop $header 'uuid'
    if (-not $uuid) { return $null }

    $pack.Uuid = ([string]$uuid).ToLowerInvariant()
    $pack.Version = Format-Version (Get-Prop $header 'version')
    $pack.Name = Resolve-PackName $pack ([string](Get-Prop $header 'name'))

    $types = @(Get-Prop $manifest 'modules' | ForEach-Object { [string](Get-Prop $_ 'type') })
    if ($types -contains 'resources') {
        $pack.Type = 'resource'
    } elseif ($types -contains 'data' -or $types -contains 'script' -or $types -contains 'javascript') {
        $pack.Type = 'behavior'
    }
    $pack.Encrypted = Test-Encrypted $pack
    return $pack
}

function Test-Under([string]$Path, $Roots) {
    foreach ($r in $Roots) {
        if ($Path.StartsWith($r.TrimEnd('\', '/') + $Sep, [StringComparison]::OrdinalIgnoreCase)) { return $true }
    }
    return $false
}

# Every pack under $Dir: folders holding a manifest.json (the outermost one,
# so subpacks are not counted twice) and .mcpack/.zip files.
function Find-Packs([string]$Dir) {
    $found = New-Object System.Collections.ArrayList
    if (-not (Test-Path -LiteralPath $Dir -PathType Container)) { return , $found }

    $roots = New-Object System.Collections.ArrayList
    $manifests = @(Get-ChildItem -LiteralPath $Dir -Recurse -File -Force -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -ieq 'manifest.json' } |
        Sort-Object { $_.FullName.Length })
    foreach ($mf in $manifests) {
        $root = $mf.DirectoryName
        if (Test-Under $root $roots) { continue }
        [void]$roots.Add($root)
        $p = New-PackInfo 'Dir' $root '' $root
        if ($p) {
            $p.Modified = $mf.LastWriteTime
            [void]$found.Add($p)
        }
    }

    $archives = @(Get-ChildItem -LiteralPath $Dir -Recurse -File -Force -ErrorAction SilentlyContinue |
        Where-Object { $_.Extension -ieq '.mcpack' -or $_.Extension -ieq '.zip' -or $_.Extension -ieq '.mcaddon' })
    foreach ($a in $archives) {
        if (Test-Under $a.FullName $roots) { continue }
        try {
            $zip = [IO.Compression.ZipFile]::OpenRead($a.FullName)
            try {
                $prefixes = @($zip.Entries |
                    ForEach-Object { $_.FullName.Replace('\', '/') } |
                    Where-Object { $_ -match '(^|/)manifest\.json$' } |
                    ForEach-Object { $_.Substring(0, $_.Length - 'manifest.json'.Length) } |
                    Sort-Object Length)
            } finally { $zip.Dispose() }
        } catch {
            continue
        }
        $taken = @()
        foreach ($prefix in $prefixes) {
            $nested = $false
            foreach ($t in $taken) { if ($prefix.StartsWith($t)) { $nested = $true } }
            if ($nested) { continue }
            $taken += $prefix
            $p = New-PackInfo 'Zip' $a.FullName $prefix ($a.FullName + $(if ($prefix) { " -> $prefix" } else { '' }))
            if ($p) {
                $p.Modified = $a.LastWriteTime
                [void]$found.Add($p)
            }
        }
    }
    return , $found
}

# ---------------------------------------------------------------------------
# Writing packs

function Get-SafeName([string]$Name) {
    $bad = [IO.Path]::GetInvalidFileNameChars() + [char[]]'<>:"/\|?*'
    $clean = -join ($Name.ToCharArray() | ForEach-Object { if ($bad -contains $_) { '_' } else { $_ } })
    $clean = ($clean -replace ([string][char]0xA7 + '.'), '').Trim(' ', '.')
    if (-not $clean) { $clean = 'pack' }
    return $clean
}

function Export-PackArchive($Pack, [string]$Destination) {
    $fs = [IO.File]::Open($Destination, [IO.FileMode]::CreateNew)
    $out = New-Object IO.Compression.ZipArchive -ArgumentList $fs, ([IO.Compression.ZipArchiveMode]::Create)
    try {
        if ($Pack.Kind -eq 'Dir') {
            $root = (Resolve-Path -LiteralPath $Pack.Path).ProviderPath.TrimEnd('\', '/')
            foreach ($f in @(Get-ChildItem -LiteralPath $root -Recurse -File -Force)) {
                $rel = $f.FullName.Substring($root.Length + 1).Replace('\', '/')
                [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($out, $f.FullName, $rel)
            }
        } else {
            $src = [IO.Compression.ZipFile]::OpenRead($Pack.Path)
            try {
                foreach ($e in $src.Entries) {
                    $name = $e.FullName.Replace('\', '/')
                    if (-not $name.StartsWith($Pack.Prefix)) { continue }
                    $rel = $name.Substring($Pack.Prefix.Length)
                    if ($rel -eq '' -or $rel.EndsWith('/')) { continue }
                    $entry = $out.CreateEntry($rel)
                    $w = $entry.Open()
                    $r = $e.Open()
                    try { $r.CopyTo($w) } finally { $r.Dispose(); $w.Dispose() }
                }
            } finally { $src.Dispose() }
        }
    } finally {
        $out.Dispose()
        $fs.Dispose()
    }
}

function Get-FreePath([string]$Dir, [string]$Base, [string]$Ext) {
    $path = Join-Path $Dir ($Base + $Ext)
    $n = 2
    while (Test-Path -LiteralPath $path) {
        $path = Join-Path $Dir ("$Base ($n)" + $Ext)
        $n++
    }
    return $path
}

# Writes each pack as an .mcpack (and a folder with -Folders). Returns the
# .mcpack paths written.
function Export-Packs($Packs, [string]$Dir) {
    $written = New-Object System.Collections.ArrayList
    foreach ($p in $Packs) {
        $tag = switch ($p.Type) { 'behavior' { 'BP' } 'resource' { 'RP' } default { 'pack' } }
        if ($p.Encrypted) {
            Write-Host ("  skip  [{0}] {1} {2} - encrypted by the server, not extracted" -f $tag, $p.Name, $p.Version) -ForegroundColor Yellow
            continue
        }
        $base = Get-SafeName ("{0} {1} [{2}]" -f $p.Name, $p.Version, $tag)
        $file = Get-FreePath $Dir $base '.mcpack'
        Export-PackArchive $p $file
        [void]$written.Add($file)
        Write-Host ("  ok    [{0}] {1} {2}" -f $tag, $p.Name, $p.Version) -ForegroundColor Green
        Write-Host ("        from {0}" -f $p.Display) -ForegroundColor DarkGray
        if ($Folders) {
            $folder = Get-FreePath $Dir $base ''
            [IO.Compression.ZipFile]::ExtractToDirectory($file, $folder)
        }
    }
    return , $written
}

function Export-Addon($McPacks, [string]$Dir, [string]$Name) {
    if ($McPacks.Count -lt 2) { return }
    $file = Get-FreePath $Dir (Get-SafeName $Name) '.mcaddon'
    $fs = [IO.File]::Open($file, [IO.FileMode]::CreateNew)
    $out = New-Object IO.Compression.ZipArchive -ArgumentList $fs, ([IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($m in $McPacks) {
            [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($out, $m, [IO.Path]::GetFileName($m))
        }
    } finally {
        $out.Dispose()
        $fs.Dispose()
    }
    Write-Host ("  ok    everything together: {0}" -f [IO.Path]::GetFileName($file)) -ForegroundColor Green
}

# ---------------------------------------------------------------------------
# Server / world

function Get-LevelName([string]$Server) {
    $props = Join-Path $Server 'server.properties'
    foreach ($line in [IO.File]::ReadAllLines($props)) {
        if ($line -match '^\s*level-name\s*=\s*(.*?)\s*$') { return $Matches[1] }
    }
    return 'Bedrock level'
}

function Read-WorldPackList([string]$File) {
    if (-not (Test-Path -LiteralPath $File -PathType Leaf)) { return @() }
    $text = [IO.File]::ReadAllText($File)
    if (-not $text.Trim()) { return @() }
    return @(ConvertFrom-LenientJson $text)
}

function Test-BuiltIn($Pack) {
    # BDS ships the vanilla game packs next to yours; they are not worth exporting.
    $leaf = Split-Path -Leaf $Pack.Path
    return ($leaf -match '^(vanilla|chemistry|experimental|editor)')
}

function Invoke-ServerExtract([string]$Target, [string]$Out) {
    $Target = (Resolve-Path -LiteralPath $Target).ProviderPath
    $searchDirs = @()
    if (Test-Path -LiteralPath (Join-Path $Target 'server.properties')) {
        $level = Get-LevelName $Target
        $world = Join-Path (Join-Path $Target 'worlds') $level
        Write-Host "Server: $Target"
        Write-Host "World:  $level"
        foreach ($d in 'behavior_packs', 'resource_packs', 'development_behavior_packs', 'development_resource_packs') {
            $searchDirs += (Join-Path $world $d)
            $searchDirs += (Join-Path $Target $d)
        }
    } elseif (Test-Path -LiteralPath (Join-Path $Target 'level.dat')) {
        $world = $Target
        $level = Split-Path -Leaf $world
        $nameFile = Join-Path $world 'levelname.txt'
        if (Test-Path -LiteralPath $nameFile) { $level = ([IO.File]::ReadAllText($nameFile)).Trim() }
        Write-Host "World:  $level ($world)"
        foreach ($d in 'behavior_packs', 'resource_packs') { $searchDirs += (Join-Path $world $d) }
        # A singleplayer world can use packs installed in the game rather than
        # copied into it: those live in com.mojang, next to minecraftWorlds.
        $worlds = Split-Path -Parent $world
        if ((Split-Path -Leaf $worlds) -ieq 'minecraftWorlds') {
            $comMojang = Split-Path -Parent $worlds
            foreach ($d in 'behavior_packs', 'resource_packs', 'development_behavior_packs', 'development_resource_packs') {
                $searchDirs += (Join-Path $comMojang $d)
            }
        }
    } else {
        throw "'$Target' is not a server folder (no server.properties) or a world folder (no level.dat)."
    }
    if (-not (Test-Path -LiteralPath $world -PathType Container)) {
        throw "The world folder '$world' does not exist. Has the server been started once?"
    }

    $available = New-Object System.Collections.ArrayList
    foreach ($d in $searchDirs) {
        foreach ($p in (Find-Packs $d)) { [void]$available.Add($p) }
    }

    $applied = @()
    $applied += Read-WorldPackList (Join-Path $world 'world_behavior_packs.json')
    $applied += Read-WorldPackList (Join-Path $world 'world_resource_packs.json')

    $chosen = New-Object System.Collections.ArrayList
    if ($All -or $applied.Count -eq 0) {
        if (-not $All) {
            Write-Host 'The world lists no packs, so exporting every pack found instead.' -ForegroundColor Yellow
        }
        foreach ($p in $available) {
            if (-not (Test-BuiltIn $p)) { [void]$chosen.Add($p) }
        }
    } else {
        foreach ($entry in $applied) {
            $id = ([string](Get-Prop $entry 'pack_id')).ToLowerInvariant()
            $ver = Format-Version (Get-Prop $entry 'version')
            $same = @($available | Where-Object { $_.Uuid -eq $id })
            $match = @($same | Where-Object { $_.Version -eq $ver }) + $same | Select-Object -First 1
            if ($null -eq $match) {
                Write-Host "  miss  $id $ver - the world uses it but no folder holds it" -ForegroundColor Yellow
                continue
            }
            if ($match.Version -ne $ver) {
                Write-Host "  note  $($match.Name): the world asks for $ver, found $($match.Version)" -ForegroundColor Yellow
            }
            if (-not ($chosen -contains $match)) { [void]$chosen.Add($match) }
        }
    }

    if ($chosen.Count -eq 0) {
        Write-Host 'No packs found.' -ForegroundColor Yellow
        return
    }
    [void](New-Item -ItemType Directory -Force -Path $Out)
    $written = Export-Packs $chosen $Out
    Export-Addon $written $Out $level
}

# ---------------------------------------------------------------------------
# Game download cache

function Get-GameRoots {
    $roots = @()
    if ($env:APPDATA) {
        $roots += (Join-Path $env:APPDATA 'Minecraft Bedrock')
        $roots += (Join-Path $env:APPDATA 'Minecraft Bedrock Preview')
    }
    if ($env:LOCALAPPDATA) {
        $packages = Join-Path $env:LOCALAPPDATA 'Packages'
        $roots += (Join-Path $packages 'Microsoft.MinecraftUWP_8wekyb3d8bbwe')
        $roots += (Join-Path $packages 'Microsoft.MinecraftWindowsBeta_8wekyb3d8bbwe')
    }
    return @($roots | Where-Object { Test-Path -LiteralPath $_ -PathType Container })
}

function Invoke-CacheExtract([string]$Out) {
    $scan = @()
    if ($CachePath) {
        $scan += (Resolve-Path -LiteralPath $CachePath).ProviderPath
    } else {
        $roots = Get-GameRoots
        if ($roots.Count -eq 0) {
            throw 'Could not find Minecraft''s data folder. Point -CachePath at the folder holding downloaded packs.'
        }
        # Downloaded server packs sit in the game's cache folders, apart from
        # the packs you installed yourself.
        $caches = New-Object System.Collections.ArrayList
        foreach ($r in $roots) {
            Write-Host "Searching $r"
            $dirs = @(Get-ChildItem -LiteralPath $r -Recurse -Directory -Force -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -match 'cache' } |
                Sort-Object { $_.FullName.Length })
            foreach ($d in $dirs) {
                if (-not (Test-Under $d.FullName $caches)) { [void]$caches.Add($d.FullName) }
            }
        }
        $scan += $caches
    }

    $seen = @{}
    $packs = New-Object System.Collections.ArrayList
    foreach ($d in $scan) {
        foreach ($p in (Find-Packs $d)) {
            $key = $p.Uuid + '@' + $p.Version
            if ($seen.ContainsKey($key)) { continue }
            $seen[$key] = $true
            [void]$packs.Add($p)
        }
    }
    if ($packs.Count -eq 0) {
        Write-Host 'No downloaded packs found. Join the server once so the game downloads them,' -ForegroundColor Yellow
        Write-Host 'or point -CachePath at the folder they were saved to.' -ForegroundColor Yellow
        return
    }
    $sorted = @($packs | Sort-Object Modified -Descending)
    [void](New-Item -ItemType Directory -Force -Path $Out)
    [void](Export-Packs $sorted $Out)
    Write-Host ''
    Write-Host 'Only resource packs are ever sent to players. For behavior packs, run this' -ForegroundColor Cyan
    Write-Host 'on the server''s own folder instead (-ServerPath).' -ForegroundColor Cyan
}

# ---------------------------------------------------------------------------

function Test-TargetFolder([string]$Dir) {
    if (-not $Dir) { return $false }
    return ((Test-Path -LiteralPath (Join-Path $Dir 'server.properties')) -or
            (Test-Path -LiteralPath (Join-Path $Dir 'level.dat')))
}

if (-not $OutDir) {
    $OutDir = Join-Path (Join-Path $PSScriptRoot 'extracted') (Get-Date -Format 'yyyy-MM-dd_HH-mm-ss')
}

if (-not $ServerPath -and -not $FromCache -and -not $CachePath) {
    foreach ($candidate in @($PSScriptRoot, (Get-Location).ProviderPath)) {
        if (Test-TargetFolder $candidate) { $ServerPath = $candidate; break }
    }
}
if (-not $ServerPath -and -not $FromCache -and -not $CachePath) {
    Write-Host 'Drag your server folder (the one with bedrock_server.exe) or a world folder'
    Write-Host 'onto this window and press Enter.'
    Write-Host 'Leave it empty to take resource packs from the game''s download cache instead.'
    $answer = (Read-Host '>').Trim().Trim('"', "'").Trim()
    if ($answer) { $ServerPath = $answer } else { $FromCache = $true }
}

try {
    if ($ServerPath) {
        Invoke-ServerExtract $ServerPath $OutDir
    } else {
        Invoke-CacheExtract $OutDir
    }
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

if (Test-Path -LiteralPath $OutDir) {
    Write-Host ''
    Write-Host "Saved to $OutDir"
    Write-Host 'Double-click an .mcpack or .mcaddon to import it into Minecraft.'
}
