param([switch]$IncludeEffects)
$ErrorActionPreference='Stop'
$tests=@('cleanup','integration','pages','tasks','panels','geometry','fixed','material','transition','memory','startup','flicker','resize','move')
if($IncludeEffects){$tests+='effects'}
foreach($name in $tests){
    Write-Host ('Running test-'+$name)
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot ('test-'+$name+'.ps1'))
    if($LASTEXITCODE -ne 0){throw ('Test failed: '+$name)}
}
Write-Host ('Passed '+$tests.Count+' native regression suites.')
