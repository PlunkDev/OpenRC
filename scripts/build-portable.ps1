param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'RelWithDebInfo'
)

$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$toolsRoot = Join-Path $projectRoot 'local\tools'
$buildRoot = Join-Path $projectRoot 'build-portable-cmake'
$packageRoot = Join-Path $projectRoot 'build-portable'

$compiler = Get-ChildItem -Path $toolsRoot -Recurse -File -Filter 'x86_64-w64-mingw32-clang++.exe' |
    Select-Object -First 1 -ExpandProperty FullName
$make = Get-ChildItem -Path $toolsRoot -Recurse -File -Filter 'mingw32-make.exe' |
    Select-Object -First 1 -ExpandProperty FullName
$cmake = Get-ChildItem -Path $toolsRoot -Recurse -File -Filter 'cmake.exe' |
    Where-Object { $_.FullName -match 'cmake-[^\\]+-windows-x86_64' } |
    Select-Object -First 1 -ExpandProperty FullName
$readObject = Get-ChildItem -Path $toolsRoot -Recurse -File -Filter 'llvm-readobj.exe' |
    Select-Object -First 1 -ExpandProperty FullName

if (-not $compiler -or -not $make -or -not $cmake -or -not $readObject) {
    throw 'Portable llvm-mingw, llvm-readobj, and CMake were not found below local\tools.'
}

function Assert-ProjectChildPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$ExpectedLeaf
    )

    $fullProjectRoot = [System.IO.Path]::GetFullPath($projectRoot).TrimEnd('\', '/')
    $fullPath = [System.IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $parent = [System.IO.Path]::GetDirectoryName($fullPath)
    $leaf = [System.IO.Path]::GetFileName($fullPath)
    if (-not [System.StringComparer]::OrdinalIgnoreCase.Equals($parent, $fullProjectRoot) -or
        -not [System.StringComparer]::Ordinal.Equals($leaf, $ExpectedLeaf)) {
        throw ('Refusing to publish outside the expected project child path: ' + $fullPath)
    }
    return $fullPath
}

function Assert-PortableExecutable {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $metadataLines = & $readObject --file-headers --coff-imports $Path 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw ('llvm-readobj failed for portable executable: ' + $Path)
    }
    $metadata = $metadataLines -join [System.Environment]::NewLine
    if ($metadata -notmatch '(?m)^Format:\s+COFF-x86-64\s*$' -or
        $metadata -notmatch '(?m)^\s*Machine:\s+IMAGE_FILE_MACHINE_AMD64\s+\(0x8664\)\s*$') {
        throw ('Portable executable is not AMD64/COFF-x86-64: ' + $Path)
    }
    if ($metadata -match '(?im)^\s*Name:\s*(?:libc\+\+|libunwind)\.dll\s*$') {
        throw ('Portable executable has a forbidden dynamic C++/unwind import: ' + $Path)
    }
}

& $cmake -S $projectRoot -B $buildRoot -G 'MinGW Makefiles' `
    ('-DCMAKE_CXX_COMPILER=' + $compiler) `
    ('-DCMAKE_MAKE_PROGRAM=' + $make) `
    ('-DCMAKE_BUILD_TYPE=' + $Configuration) `
    '-DOPENRC_STATIC_MINGW_RUNTIME=ON' `
    '-DOPENRC_BUILD_TESTS=ON' `
    '-DOPENRC_BUILD_LAUNCHER=ON' `
    '-DOPENRC_BUILD_RUNTIME=ON'
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }

& $cmake --build $buildRoot --parallel
if ($LASTEXITCODE -ne 0) { throw 'OpenRC build failed.' }

& $cmake --build $buildRoot --target test
if ($LASTEXITCODE -ne 0) { throw 'OpenRC tests failed.' }

$publishId = [System.Guid]::NewGuid().ToString('N')
$stagingLeaf = 'build-portable.staging-' + $publishId
$previousLeaf = 'build-portable.previous-' + $publishId
$packageRoot = Assert-ProjectChildPath -Path $packageRoot -ExpectedLeaf 'build-portable'
$stagingRoot = Assert-ProjectChildPath `
    -Path (Join-Path $projectRoot $stagingLeaf) `
    -ExpectedLeaf $stagingLeaf
$previousRoot = Assert-ProjectChildPath `
    -Path (Join-Path $projectRoot $previousLeaf) `
    -ExpectedLeaf $previousLeaf
$executables = @('openrc-cli.exe', 'openrc-launcher.exe', 'openrc-runtime.exe')

if (Test-Path -LiteralPath $stagingRoot) {
    throw ('Refusing to reuse an existing portable staging directory: ' + $stagingRoot)
}
if (Test-Path -LiteralPath $previousRoot) {
    throw ('Refusing to reuse an existing portable backup directory: ' + $previousRoot)
}

New-Item -ItemType Directory -Path $stagingRoot | Out-Null
try {
    foreach ($executable in $executables) {
        $source = Join-Path $buildRoot $executable
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw ('Portable build output is missing: ' + $source)
        }

        $destination = Join-Path $stagingRoot $executable
        Copy-Item -LiteralPath $source -Destination $destination
        Assert-PortableExecutable -Path $destination

        $sourceHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
        $destinationHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
        if ($sourceHash -cne $destinationHash) {
            throw ('Portable executable hash mismatch after staging: ' + $executable)
        }
    }

    $stagedFiles = @(Get-ChildItem -LiteralPath $stagingRoot -File)
    if ($stagedFiles.Count -ne $executables.Count) {
        throw 'Portable staging directory contains an unexpected number of files.'
    }

    $hadPreviousPackage = Test-Path -LiteralPath $packageRoot
    if ($hadPreviousPackage) {
        $existingPackage = Get-Item -LiteralPath $packageRoot -Force
        if (($existingPackage.Attributes -band
             [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw ('Refusing to replace a reparse-point package path: ' + $packageRoot)
        }
        Move-Item -LiteralPath $packageRoot -Destination $previousRoot
    }

    try {
        Move-Item -LiteralPath $stagingRoot -Destination $packageRoot
    } catch {
        if ($hadPreviousPackage -and
            (Test-Path -LiteralPath $previousRoot) -and
            -not (Test-Path -LiteralPath $packageRoot)) {
            Move-Item -LiteralPath $previousRoot -Destination $packageRoot
        }
        throw
    }

    if ($hadPreviousPackage -and (Test-Path -LiteralPath $previousRoot)) {
        Remove-Item -LiteralPath $previousRoot -Recurse -Force
    }
} catch {
    if (Test-Path -LiteralPath $stagingRoot) {
        Remove-Item -LiteralPath $stagingRoot -Recurse -Force
    }
    throw
}

Write-Host ('OpenRC portable package completed: ' + $packageRoot)
