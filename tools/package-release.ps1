<#
.SYNOPSIS
  Builds the Release configuration and packages the Windows release ZIP into dist\.
  Local only: nothing is published, tagged or uploaded.

.DESCRIPTION
  Builds into build-release\ (the normal build\ folder is not touched), stages the files
  the program needs, refuses to continue if the staging folder holds game or user files,
  then writes dist\HardCorpsUprising-v<version>-windows-x64.zip and its .sha256 file.
  The version is the project VERSION in CMakeLists.txt. The build rebuilds the patched ReXGlue runtime and GPU
  plugin first when they do not match patches\rexglue-sdk; the packaging stops unless they do.
  The update helper, trg-updater.exe, is built with MSYS2 CLANG64 by build-updater.ps1; its debug information,
  trg-updater.debug, goes to dist\HardCorpsUprising-v<version>-windows-x64-symbols\, which is not packaged.
  The ZIP holds the files at its root; it is extracted again and checked.

.EXAMPLE
  .\tools\package-release.ps1              # incremental Release build, then package
  .\tools\package-release.ps1 -Clean       # clean Release build first
  .\tools\package-release.ps1 -SkipBuild   # package the existing build-release\ output
  .\tools\package-release.ps1 -SkipBuild -OutDir build-release\package   # write somewhere other than dist\
  .\tools\package-release.ps1 -VerifyOnly dist\HardCorpsUprising-v0.7.0-windows-x64.zip   # check a staged folder or a ZIP
#>
param(
    [switch]$Clean,
    [switch]$SkipBuild,
    [string]$OutDir,     # relative to the repository root; default: dist
    [string]$VerifyOnly  # only check a staged folder or a release ZIP for game or user files, then exit
)
$ErrorActionPreference = 'Stop'

$Root = Split-Path $PSScriptRoot -Parent
$ReleaseBuild = Join-Path $Root 'build-release'
$UpdaterBuild = Join-Path $ReleaseBuild 'trg-updater-clang64'
$Packaging = Join-Path $Root 'packaging'
$SdkRoot = Join-Path $Root 'tools\rexglue\win-amd64'
$Sdk = Join-Path $SdkRoot 'bin'
. (Join-Path $PSScriptRoot 'rexruntime-patch.ps1')

$Utf8NoBom = New-Object System.Text.UTF8Encoding $false
$RootFiles = @('hard_corps_uprising.exe', 'trg-updater.exe', 'rexruntime.dll', 'rexgpu-xenos.dll', 'hard_corps_uprising.toml', 'README.txt', 'LICENSE')
$LicenseFiles = @('ReXGlue-LICENSE.txt', 'THIRD-PARTY-LICENSES.txt')
$AssetFiles = @('assets\launcher\header-art.png')
# Names that mean game data, a package, saves, settings, caches, logs, symbols, updater leftovers or debugging tools.
$ForbiddenNames = @('default.xex', 'DataXENON', 'game', 'game-install-temp', 'game-install-backup', 'user_data',
    'user_data.migrating', 'content', 'cache', 'logs', 'settings.toml', 'settings.toml.bak', 'ArcadeInfo.xml', 'nxeart', '58410A18',
    'vulkan-1.dll')
$ForbiddenPatterns = @('*.xex', '*.log', '*.dmp', '*.pdb', '*.debug', '*.xsh', '*.xpso', '*.bak', '*.tmp', '*.sav', 'trg-update-*',
    'VkLayer_*', 'renderdoc*')
# First bytes of an Xbox 360 package or executable.
$ForbiddenMagic = @('LIVE', 'PIRS', 'CON ', 'XEX2')

# This machine's folders (the repository and the user profile), as text or UTF-16, in either slash style.
$Latin1 = [Text.Encoding]::GetEncoding(28591)
$LocalPaths = foreach ($path in @($Root, $env:USERPROFILE) | Where-Object { $_ }) {
    foreach ($style in @($path, $path.Replace('\', '/'))) {
        $style
        $Latin1.GetString([Text.Encoding]::Unicode.GetBytes($style))
    }
}

function Test-ReleaseFolder([string]$Dir) {
    $problems = New-Object System.Collections.Generic.List[string]
    if (-not (Test-Path -LiteralPath $Dir -PathType Container)) { throw "Not a folder: $Dir" }
    $base = (Resolve-Path -LiteralPath $Dir).Path.TrimEnd('\')
    foreach ($item in Get-ChildItem -LiteralPath $base -Recurse -Force) {
        $relative = $item.FullName.Substring($base.Length + 1)
        if (-not $item.PSIsContainer) {
            $text = $Latin1.GetString([IO.File]::ReadAllBytes($item.FullName))
            foreach ($path in $LocalPaths) {
                if ($text.IndexOf($path, [StringComparison]::OrdinalIgnoreCase) -ge 0) { $problems.Add("local path in file: $relative"); break }
            }
            if ($text.IndexOf('DEBUG_SAULO', [StringComparison]::Ordinal) -ge 0) { $problems.Add("DEBUG_SAULO marker in file: $relative") }
            if ($item.Length -ge 4 -and ($ForbiddenMagic -contains $text.Substring(0, 4))) { $problems.Add("Xbox 360 package or executable: $relative") }
        }
        if ($ForbiddenNames -contains $item.Name) { $problems.Add("forbidden name: $relative") }
        foreach ($pattern in $ForbiddenPatterns) {
            if ($item.Name -like $pattern) { $problems.Add("forbidden file type: $relative") }
        }
        $parts = $relative -split '\\'
        if ($parts.Count -eq 1) {
            if ($item.PSIsContainer) { if ($item.Name -notin @('licenses', 'assets')) { $problems.Add("unexpected folder: $relative") } }
            elseif ($RootFiles -notcontains $item.Name) { $problems.Add("unexpected file: $relative") }
        } elseif ($parts[0] -eq 'assets') {
            if ($item.PSIsContainer) {
                if ($relative -ne 'assets\launcher') { $problems.Add("unexpected folder: $relative") }
            } elseif ($AssetFiles -notcontains $relative) {
                $problems.Add("unexpected item: $relative")
            }
        } elseif ($parts[0] -ne 'licenses' -or $parts.Count -gt 2 -or $item.PSIsContainer -or $LicenseFiles -notcontains $item.Name) {
            $problems.Add("unexpected item: $relative")
        }
    }
    foreach ($leaf in $RootFiles) {
        if (-not (Test-Path -LiteralPath (Join-Path $base $leaf) -PathType Leaf)) { $problems.Add("missing: $leaf") }
    }
    foreach ($leaf in $LicenseFiles) {
        if (-not (Test-Path -LiteralPath (Join-Path $base "licenses\$leaf") -PathType Leaf)) { $problems.Add("missing: licenses\$leaf") }
    }
    foreach ($asset in $AssetFiles) {
        if (-not (Test-Path -LiteralPath (Join-Path $base $asset) -PathType Leaf)) { $problems.Add("missing: $asset") }
    }
    return $problems
}

function Write-Problems($problems) {
    Write-Host ''
    Write-Host 'RELEASE CHECK FAILED' -ForegroundColor Red
    foreach ($problem in $problems) { Write-Host "  $problem" -ForegroundColor Red }
}

function Expand-ReleaseZip([string]$Zip) {
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $target = Join-Path ([IO.Path]::GetTempPath()) ('hcu_release_zip_' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory $target | Out-Null
    $archive = [IO.Compression.ZipFile]::OpenRead($Zip)
    try {
        foreach ($entry in $archive.Entries) {
            if ($entry.FullName -match '^[\\/]|\.\.|:|\\') { throw "The ZIP holds an unsafe entry name: $($entry.FullName)" }
            if (-not $entry.Name) { throw "The ZIP holds a folder entry: $($entry.FullName)" }
            $path = Join-Path $target $entry.FullName
            New-Item -ItemType Directory -Force (Split-Path $path) | Out-Null
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $path)
        }
    } finally { $archive.Dispose() }
    return $target
}

if ($VerifyOnly) {
    $path = (Resolve-Path -LiteralPath $VerifyOnly).Path
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        $folder = Expand-ReleaseZip $path
        try { $problems = Test-ReleaseFolder $folder } finally { Remove-Item -LiteralPath $folder -Recurse -Force -ErrorAction SilentlyContinue }
    } else {
        $problems = Test-ReleaseFolder $path
    }
    if ($problems.Count) { Write-Problems $problems; exit 1 }
    Write-Host "Release folder is clean: $VerifyOnly"
    return
}

$cmake = Get-Content (Join-Path $Root 'CMakeLists.txt') -Raw
if ($cmake -notmatch '(?m)^\s*project\(\s*hard_corps_uprising\s+VERSION\s+(\d+\.\d+\.\d+)') { throw 'Project VERSION not found in CMakeLists.txt' }
$Version = $Matches[1]
$Name = "HardCorpsUprising-v$Version-windows-x64"
$Dist = if (-not $OutDir) { Join-Path $Root 'dist' } elseif ([IO.Path]::IsPathRooted($OutDir)) { $OutDir } else { Join-Path $Root $OutDir }
$Stage = Join-Path $Dist $Name
$Symbols = Join-Path $Dist "$Name-symbols"
$Zip = Join-Path $Dist "$Name.zip"
$ShaFile = Join-Path $Dist "$Name.sha256"

Write-Host ''
Write-Host "Hard Corps: Uprising $Version - Windows release package (local only)"
$SourceState = (git -C $Root rev-parse --short HEAD 2>$null)
if ($LASTEXITCODE -ne 0) { $SourceState = 'unknown' }
else {
    $changes = @(git -C $Root status --porcelain 2>$null)
    if ($LASTEXITCODE -ne 0) { $SourceState += ', working tree state unknown' }
    elseif ($changes.Count) { $SourceState += ' plus uncommitted changes' }
}
Write-Host "Source: $SourceState"

if (-not $SkipBuild) {
    $build = @{ Config = 'Release'; BuildDir = $ReleaseBuild }
    if ($Clean) { $build.Clean = $true }
    & (Join-Path $PSScriptRoot 'build.ps1') @build
    & (Join-Path $PSScriptRoot 'build-updater.ps1') -OutDir $UpdaterBuild
}

# rexruntime.dll and rexgpu-xenos.dll must be the project's patched builds of the current patches.
$runtimeProblems = Test-PatchedRuntime $SdkRoot
if ($runtimeProblems.Count) { Write-Problems $runtimeProblems; throw 'tools\rexglue does not hold the current patched runtime and GPU plugin (build without -SkipBuild to update them)' }

foreach ($leaf in @('hard_corps_uprising.exe', 'rexruntime.dll', 'rexgpu-xenos.dll', 'hard_corps_uprising.toml')) {
    if (-not (Test-Path (Join-Path $ReleaseBuild $leaf))) { throw "Release build output is missing $leaf (run without -SkipBuild)" }
}
$Updater = Join-Path $UpdaterBuild 'trg-updater.exe'
$UpdaterDebug = Join-Path $UpdaterBuild 'trg-updater.debug'
foreach ($file in $Updater, $UpdaterDebug) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing $file (run without -SkipBuild)" }
}
$updaterTime = (Get-Item -LiteralPath $Updater).LastWriteTimeUtc
$updaterInputs = @(Get-ChildItem -LiteralPath (Join-Path $Root 'src\updater') -Recurse -File) + @(Get-Item -LiteralPath (Join-Path $PSScriptRoot 'build-updater.ps1'))
$newer = @($updaterInputs | Where-Object { $_.LastWriteTimeUtc -gt $updaterTime })
if ($newer.Count) { throw "$Updater is older than $($newer[0].FullName) (run without -SkipBuild)" }

New-Item -ItemType Directory -Force $Dist | Out-Null
foreach ($old in $Stage, $Symbols) { if (Test-Path -LiteralPath $old) { Remove-Item -LiteralPath $old -Recurse -Force } }
New-Item -ItemType Directory $Stage, (Join-Path $Stage 'licenses'), $Symbols | Out-Null
Copy-Item -LiteralPath $UpdaterDebug $Symbols

foreach ($leaf in @('hard_corps_uprising.exe', 'rexruntime.dll', 'rexgpu-xenos.dll', 'hard_corps_uprising.toml')) {
    Copy-Item (Join-Path $ReleaseBuild $leaf) $Stage
}
Copy-Item -LiteralPath $Updater $Stage
foreach ($leaf in @('rexruntime.dll', 'rexgpu-xenos.dll')) {
    $built = (Get-FileHash (Join-Path $Stage $leaf) -Algorithm SHA256).Hash
    $expected = (Get-FileHash (Join-Path $Sdk $leaf) -Algorithm SHA256).Hash
    if ($built -ne $expected) { throw "$leaf is not the file installed in tools\rexglue" }
}
$RootLicense = Join-Path $Root 'LICENSE'
if (-not (Test-Path -LiteralPath $RootLicense -PathType Leaf)) { throw "Missing project license: $RootLicense" }
Copy-Item -LiteralPath $RootLicense $Stage
foreach ($leaf in $LicenseFiles) {
    $source = Join-Path $Packaging "licenses\$leaf"
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing license file: $source" }
    Copy-Item -LiteralPath $source (Join-Path $Stage 'licenses')
}
$asset_dir = Join-Path $Stage 'assets\launcher'
New-Item -ItemType Directory -Force $asset_dir | Out-Null
Copy-Item -LiteralPath (Join-Path $Root 'assets\launcher\header-art.png') $asset_dir
$readme = (Get-Content (Join-Path $Packaging 'README.txt') -Raw).Replace('@VERSION@', $Version) -replace "\r?\n", "`r`n"
[IO.File]::WriteAllText((Join-Path $Stage 'README.txt'), $readme, $Utf8NoBom)

# The update helper runs alone from a temporary folder, so it must not need any runtime library from the installation.
$helperText = $Latin1.GetString([IO.File]::ReadAllBytes((Join-Path $Stage 'trg-updater.exe')))
$helper = @()
foreach ($dependency in 'vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll', 'ucrtbase.dll', 'libc++.dll', 'libunwind.dll',
    'libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll', 'msys-2.0.dll', 'rexruntime.dll', 'rexgpu-xenos.dll',
    'hard_corps_uprising.exe') {
    if ($helperText.IndexOf($dependency, [StringComparison]::OrdinalIgnoreCase) -ge 0) { $helper += "trg-updater.exe refers to $dependency" }
}
if ($helper.Count) { Write-Problems $helper; throw 'trg-updater.exe is not self-contained' }
if ($helperText.IndexOf('.debug_', [StringComparison]::Ordinal) -ge 0 -or $helperText.IndexOf('trg-updater.debug', [StringComparison]::Ordinal) -lt 0) {
    throw 'trg-updater.exe is not the build-updater.ps1 build without debug information (run without -SkipBuild)'
}

$problems = Test-ReleaseFolder $Stage
if ($problems.Count) { Write-Problems $problems; throw 'The staging folder holds files that must not be released' }

Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
if (Test-Path -LiteralPath $Zip) { Remove-Item -LiteralPath $Zip }
$files = Get-ChildItem -LiteralPath $Stage -Recurse -File | Sort-Object { $_.FullName.Substring($Stage.Length + 1).Replace('\', '/') }
$stream = [IO.File]::Open($Zip, [IO.FileMode]::CreateNew)
try {
    $archive = New-Object IO.Compression.ZipArchive($stream, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($file in $files) {
            $entry = $file.FullName.Substring($Stage.Length + 1).Replace('\', '/')
            [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, $entry, [IO.Compression.CompressionLevel]::Optimal)
        }
    } finally { $archive.Dispose() }
} finally { $stream.Dispose() }

$entries = @($files | ForEach-Object { $_.FullName.Substring($Stage.Length + 1).Replace('\', '/') })
$extracted = Expand-ReleaseZip $Zip
try {
    $listed = @(Get-ChildItem -LiteralPath $extracted -Recurse -File | ForEach-Object { $_.FullName.Substring($extracted.Length + 1).Replace('\', '/') } | Sort-Object)
    if (Compare-Object $entries $listed) { throw 'The ZIP does not hold exactly the staged files' }
    foreach ($entry in $entries) {
        $relative = $entry.Replace('/', '\')
        $staged = (Get-FileHash -LiteralPath (Join-Path $Stage $relative) -Algorithm SHA256).Hash
        if ($staged -ne (Get-FileHash -LiteralPath (Join-Path $extracted $relative) -Algorithm SHA256).Hash) { throw "$entry in the ZIP differs from the staged file" }
    }
    $problems = Test-ReleaseFolder $extracted
    if ($problems.Count) { Write-Problems $problems; throw 'The ZIP holds files that must not be released' }
} finally { Remove-Item -LiteralPath $extracted -Recurse -Force -ErrorAction SilentlyContinue }

$hash = (Get-FileHash $Zip -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText($ShaFile, "$hash  $Name.zip`n", $Utf8NoBom)

Write-Host ''
Write-Host '===================================================='
Write-Host ' RELEASE PACKAGE READY (nothing was published)'
Write-Host '===================================================='
Write-Host "  ZIP:     $Zip ($([math]::Round((Get-Item $Zip).Length / 1MB, 1)) MB)"
Write-Host "  SHA-256: $hash"
Write-Host "  Hash file: $ShaFile"
Write-Host "  Symbols: $Symbols (not in the package)"
Write-Host "  Source:  $SourceState"
Write-Host '  Contents:'
foreach ($entry in $entries) { Write-Host "    $entry" }
Write-Host ''
