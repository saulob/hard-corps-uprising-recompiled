<#
.SYNOPSIS
  Checks that package-release.ps1 -VerifyOnly accepts a fresh extraction and refuses player data, logs, game files,
  symbols, updater leftovers, debugging tools and development markers. Scratch folders only; nothing is built,
  launched or published.
#>
$ErrorActionPreference = 'Stop'
$ReleaseCheck = Join-Path $PSScriptRoot 'package-release.ps1'
$StageRoot = Join-Path ([IO.Path]::GetTempPath()) 'hcu_release_check'
$Cases = [ordered]@{
    'fresh extraction'   = $null
    'empty user_data'    = 'user_data\'
    'scores'             = 'user_data\58410A18\live\stats.toml'
    'achievements'       = 'user_data\achievements\58410A18.toml'
    'settings'           = 'user_data\settings.toml'
    'cache'              = 'cache\shaders\a.bin'
    'migration leftover' = 'user_data.migrating\'
    'log'                = 'logs\hard_corps_uprising_001.log'
    'game file'          = 'game\default.xex'
    'updater pdb'        = 'trg-updater.pdb'
    'debug file'         = 'trg-updater.debug'
    'updater workspace'  = 'trg-update-1234-5678\job.txt'
    'renderdoc'          = 'renderdoc.dll'
    'vulkan layer'       = 'VkLayer_test.json'
    'vulkan loader'      = 'vulkan-1.dll'
    'Xbox package'       = 'rexruntime.dll'
    'Xbox executable'    = 'rexgpu-xenos.dll'
    'DEBUG_SAULO marker' = 'README.txt'
    'repository path'    = 'hard_corps_uprising.toml'
    'user profile path'  = 'hard_corps_uprising.toml'
}
$Content = @{
    'Xbox package'       = 'LIVE'
    'Xbox executable'    = 'XEX2'
    'DEBUG_SAULO marker' = 'DEBUG_SAULO'
    'repository path'    = "cache_root = '$(Split-Path $PSScriptRoot -Parent)'"
    'user profile path'  = "cache_root = '$env:USERPROFILE'"
}
$failures = 0
try {
    foreach ($case in $Cases.Keys) {
        $stage = Join-Path $StageRoot ($case -replace '\W', '_')
        New-Item -ItemType Directory -Force (Join-Path $stage 'licenses') | Out-Null
        foreach ($leaf in 'hard_corps_uprising.exe', 'trg-updater.exe', 'rexruntime.dll', 'rexgpu-xenos.dll', 'hard_corps_uprising.toml', 'README.txt', 'LICENSE',
            'licenses\ReXGlue-LICENSE.txt', 'licenses\THIRD-PARTY-LICENSES.txt', 'assets\launcher\header-art.png') {
            $file = Join-Path $stage $leaf
            New-Item -ItemType Directory -Force (Split-Path $file) | Out-Null
            Set-Content -LiteralPath $file -Value 'placeholder'
        }
        $extra = $Cases[$case]
        if ($extra) {
            $target = Join-Path $stage $extra
            if ($extra.EndsWith('\')) {
                New-Item -ItemType Directory -Force $target | Out-Null
            } else {
                New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
                $text = if ($Content.Contains($case)) { $Content[$case] } else { 'placeholder' }
                Set-Content -LiteralPath $target -Value $text -NoNewline
            }
        }
        $global:LASTEXITCODE = 0
        & $ReleaseCheck -VerifyOnly $stage *> $null
        $accepted = $LASTEXITCODE -eq 0
        if ($accepted -ne ($null -eq $extra)) {
            Write-Host "  FAIL $case"
            $failures++
        }
    }
} finally {
    Remove-Item -LiteralPath $StageRoot -Recurse -Force -ErrorAction SilentlyContinue
}
Write-Host "$($Cases.Count) release check cases, $failures failures"
if ($failures) { throw 'Release check tests failed' }
$global:LASTEXITCODE = 0
