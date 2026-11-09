function Resolve-AssistantTool([string]$Name) {
    $environmentName='JX3_'+$Name.ToUpperInvariant()
    $configured=[Environment]::GetEnvironmentVariable($environmentName)
    if($configured -and (Test-Path -LiteralPath $configured)){return $configured}
    $command=Get-Command ($Name+'.exe') -ErrorAction SilentlyContinue
    if($command){return $command.Source}
    $cache=Join-Path $PSScriptRoot '.build-native/CMakeCache.txt'
    if(Test-Path -LiteralPath $cache){
        $entry=Select-String -LiteralPath $cache -Pattern '^CMAKE_COMMAND:INTERNAL=(.+)$' | Select-Object -First 1
        if($entry){$candidate=Join-Path (Split-Path $entry.Matches[0].Groups[1].Value) ($Name+'.exe');if(Test-Path -LiteralPath $candidate){return $candidate}}
    }
    throw "Cannot locate $Name; add it to PATH or set $environmentName"
}
function Get-AssistantToolchain {
    $compiler=$env:VCToolsInstallDir
    if(-not $compiler){
        $command=Get-Command vswhere.exe -ErrorAction SilentlyContinue
        $where=if($command){$command.Source}else{Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'}
        if(-not (Test-Path -LiteralPath $where)){throw 'Cannot locate Visual Studio; initialize a developer shell'}
        $installation=& $where -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if(-not $installation){throw 'No C++ compiler installation found'}
        $compiler=(Get-ChildItem -LiteralPath (Join-Path $installation 'VC/Tools/MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName
    }
    $sdk=$env:WindowsSdkDir
    if(-not $sdk){$sdk=(Get-ItemProperty 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows Kits\Installed Roots').KitsRoot10}
    if(-not $sdk){throw 'Cannot locate Windows SDK'}
    $version=(Get-ChildItem -LiteralPath (Join-Path $sdk 'Include') -Directory | Where-Object {Test-Path -LiteralPath (Join-Path $_.FullName 'ucrt')} | Sort-Object Name -Descending | Select-Object -First 1).Name
    if(-not $version){throw 'Windows SDK headers unavailable'}
    return @{Compiler=$compiler;Sdk=$sdk;Version=$version}
}
