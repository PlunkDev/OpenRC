param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'RelWithDebInfo'
)

$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$toolsRoot = Join-Path $projectRoot 'local\tools'
$buildRoot = Join-Path $projectRoot 'build-portable-cmake'

$compiler = Get-ChildItem -Path $toolsRoot -Recurse -File -Filter 'x86_64-w64-mingw32-clang++.exe' |
    Select-Object -First 1 -ExpandProperty FullName
$make = Get-ChildItem -Path $toolsRoot -Recurse -File -Filter 'mingw32-make.exe' |
    Select-Object -First 1 -ExpandProperty FullName
$cmake = Get-ChildItem -Path $toolsRoot -Recurse -File -Filter 'cmake.exe' |
    Where-Object { $_.FullName -match 'cmake-[^\\]+-windows-x86_64' } |
    Select-Object -First 1 -ExpandProperty FullName

if (-not $compiler -or -not $make -or -not $cmake) {
    throw 'Portable llvm-mingw and CMake were not found below local\tools.'
}

& $cmake -S $projectRoot -B $buildRoot -G 'MinGW Makefiles' `
    ('-DCMAKE_CXX_COMPILER=' + $compiler) `
    ('-DCMAKE_MAKE_PROGRAM=' + $make) `
    ('-DCMAKE_BUILD_TYPE=' + $Configuration) `
    '-DCMAKE_EXE_LINKER_FLAGS=-static' `
    '-DOPENRC_BUILD_TESTS=ON' `
    '-DOPENRC_BUILD_LAUNCHER=ON'
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }

& $cmake --build $buildRoot --parallel
if ($LASTEXITCODE -ne 0) { throw 'OpenRC build failed.' }

& $cmake --build $buildRoot --target test
if ($LASTEXITCODE -ne 0) { throw 'OpenRC tests failed.' }

Write-Host ('OpenRC portable build completed: ' + $buildRoot)
