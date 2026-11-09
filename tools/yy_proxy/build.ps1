param(
    [Parameter(Mandatory)][string]$MinHookRoot,
    [Parameter(Mandatory)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../../build_tools.ps1')
$t = Get-AssistantToolchain
$MinHookRoot = (Resolve-Path -LiteralPath $MinHookRoot).Path
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Choose a new output directory' }
$out = (New-Item -ItemType Directory -Path $OutputDirectory).FullName
$oldTemp, $oldTmp = $env:TEMP, $env:TMP
$env:TEMP = $out
$env:TMP = $out
$c, $s, $v = $t.Compiler, $t.Sdk, $t.Version
$cl = Join-Path $c 'bin/Hostx64/x86/cl.exe'
Push-Location $out
try {
    $sources = 'buffer.c', 'hook.c', 'trampoline.c', 'hde/hde32.c' |
        ForEach-Object { Join-Path $MinHookRoot "src/$_" }
    & $cl /nologo /LD /MT /EHsc /O2 /W4 /wd4191 /utf-8 /std:c++17 `
        "/I$c/include" "/I$s/Include/$v/ucrt" "/I$s/Include/$v/shared" `
        "/I$s/Include/$v/um" "/I$MinHookRoot/include" `
        (Join-Path $PSScriptRoot 'version_proxy.cpp') (Join-Path $PSScriptRoot 'volume_restore.cpp') `
        @sources /link "/DEF:$PSScriptRoot/version_proxy.def" /OUT:version.dll `
        "/LIBPATH:$c/lib/x86" "/LIBPATH:$s/Lib/$v/ucrt/x86" "/LIBPATH:$s/Lib/$v/um/x86" `
        kernel32.lib user32.lib bcrypt.lib
    if ($LASTEXITCODE -ne 0) { throw 'YY volume patch build failed' }
} finally {
    Pop-Location
    $env:TEMP, $env:TMP = $oldTemp, $oldTmp
}
