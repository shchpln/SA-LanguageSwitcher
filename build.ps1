param(
    [string]$PluginSdk = (Join-Path $PSScriptRoot '..\plugin-sdk')
)

$ErrorActionPreference = 'Stop'
$sdkRoot = (Resolve-Path -LiteralPath $PluginSdk).Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$visualStudio = & $vswhere -latest -products '*' -version '[17.0,18.0)' `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$visualStudio) {
    throw 'Visual Studio 2022 with MSVC v143 x86 tools and the Windows SDK is required.'
}

# Plugin-SDK revision: 15f15b60bbf74c106e1b496ff92c98764abf4605.
$sdkSources = @('Patch.cpp', 'GameVersion.cpp', 'DynAddress.cpp', 'game\CRGBA.cpp') | ForEach-Object {
    Join-Path $sdkRoot "shared\$_"
}
$gameSources = @('CFont', 'CMenuManager', 'CText', 'CSprite2d', 'CTxdStore', 'RenderWare') | ForEach-Object {
    Join-Path $sdkRoot "plugin_sa\game_sa\$_.cpp"
}
$pluginSources = Join-Path $PSScriptRoot 'LanguageSwitcher.cpp'
$pluginFile = Join-Path $PSScriptRoot 'LanguageSwitcher.asi'

$buildDir = Join-Path $PSScriptRoot 'build\msvc-x86'
New-Item -ItemType Directory -Path $buildDir -Force | Out-Null

# Run the compiler in a child shell so its toolchain environment stays local to the build.
$devCmd = Join-Path $visualStudio 'Common7\Tools\VsDevCmd.bat'
$includeDirs = @('shared', 'shared\game', 'injector', 'plugin_sa\game_sa\rw', 'plugin_sa\game_sa', 'plugin_sa\game_sa\enums')
$includes = $includeDirs | ForEach-Object { '/I"' + (Join-Path $sdkRoot $_) + '"' }
$sources = (@($pluginSources) + $sdkSources + $gameSources) | ForEach-Object { '"' + $_ + '"' }
$stagedPlugin = Join-Path $buildDir 'LanguageSwitcher.asi'
$exports = Join-Path $PSScriptRoot 'LanguageSwitcher.def'
$command = 'call "{0}" -no_logo -arch=x86 -host_arch=x64 && cl.exe /nologo /LD /std:c++latest /EHsc /utf-8 /O2 /Gy /MT /GL /DGTASA /DRW /D_CRT_SECURE_NO_WARNINGS {1} {2} /link /LTCG /OPT:REF /MACHINE:X86 /OUT:"{3}" /DEF:"{4}" user32.lib && dumpbin.exe /headers /dependents /exports "{3}"' -f `
    $devCmd, ($includes -join ' '), ($sources -join ' '), $stagedPlugin, $exports

Push-Location -LiteralPath $buildDir
try {
    & $env:ComSpec /d /c $command
    if ($LASTEXITCODE -ne 0) {
        throw "MSVC build or PE inspection failed with exit code $LASTEXITCODE."
    }
    Copy-Item -LiteralPath $stagedPlugin -Destination $pluginFile -Force
    Write-Output "Built: $pluginFile"
} finally {
    Pop-Location
}
