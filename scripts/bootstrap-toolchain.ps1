# Pobiera przypiety toolchain (llvm-mingw + CMake) do local/tools.
# Windows PowerShell 5.1, idempotentny. Nie zmienia PATH, rejestru ani systemu.
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$repo = Split-Path -Parent $PSScriptRoot
$toolsDir = Join-Path $repo 'local\tools'

# SHA256 wedlug upstreamu: pole digest assetu w API wydan GitHub;
# dla CMake zgodne z cmake-4.4.2-SHA-256.txt.
$tools = @(
    @{
        Dir    = 'llvm-mingw-20260616-ucrt-x86_64'
        Url    = 'https://github.com/mstorsjo/llvm-mingw/releases/download/20260616/llvm-mingw-20260616-ucrt-x86_64.zip'
        Sha256 = 'b9b68a4d276e16fa25802aaba458e4638f64b3884c290aaccdc2d87083b6ca35'
        Probe  = 'bin\x86_64-w64-mingw32-clang++.exe'
        Also   = 'bin\mingw32-make.exe'
        Match  = 'llvm-mingw-*'
    },
    @{
        Dir    = 'cmake-4.4.2-windows-x86_64'
        Url    = 'https://github.com/Kitware/CMake/releases/download/v4.4.2/cmake-4.4.2-windows-x86_64.zip'
        Sha256 = 'e8139d85b3813bc38833142ae1940472e9a587e9b5d2718ac1804c60f4e57a64'
        Probe  = 'bin\cmake.exe'
        Also   = $null
        Match  = 'cmake-*'
    }
)

New-Item -ItemType Directory -Force $toolsDir | Out-Null

# Odmow, gdy w local/tools jest inny lub drugi toolchain.
foreach ($t in $tools) {
    $others = @(Get-ChildItem -LiteralPath $toolsDir -Directory -Filter $t.Match |
        Where-Object { $_.Name -ne $t.Dir })
    if ($others.Count -gt 0) {
        throw "local/tools zawiera inny toolchain ($($others[0].Name)); oczekiwano tylko $($t.Dir). Usun go recznie."
    }
}

foreach ($t in $tools) {
    $dest = Join-Path $toolsDir $t.Dir
    $ok = (Test-Path (Join-Path $dest $t.Probe)) -and
        (($null -eq $t.Also) -or (Test-Path (Join-Path $dest $t.Also)))
    if ($ok) {
        Write-Host "OK: $($t.Dir) juz zainstalowany."
        continue
    }
    if (Test-Path $dest) {
        throw "$dest istnieje, ale jest niekompletny. Usun go recznie i uruchom ponownie."
    }

    $work = Join-Path ([IO.Path]::GetTempPath()) ('openrc-bootstrap-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory $work | Out-Null
    try {
        $zip = Join-Path $work 'archive.zip'
        $extract = Join-Path $work 'extract'
        Write-Host "Pobieranie $($t.Url)"
        $ProgressPreference = 'SilentlyContinue'
        Invoke-WebRequest -Uri $t.Url -OutFile $zip -UseBasicParsing
        $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $zip).Hash.ToLowerInvariant()
        if ($hash -ne $t.Sha256) {
            throw "Niezgodna suma SHA256 dla $($t.Dir): $hash (oczekiwano $($t.Sha256))."
        }
        New-Item -ItemType Directory $extract | Out-Null
        Expand-Archive -LiteralPath $zip -DestinationPath $extract -Force
        $src = Join-Path $extract $t.Dir
        if (-not (Test-Path (Join-Path $src $t.Probe))) {
            throw "Archiwum nie zawiera $($t.Dir)\$($t.Probe)."
        }
        Move-Item -LiteralPath $src -Destination $dest
        Write-Host "Zainstalowano $dest"
    }
    finally {
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    }
}
Write-Host 'Toolchain gotowy.'
