param([string]$Version='3.1.0',[string]$Destination)
$ErrorActionPreference='Stop'
if($Version -notmatch '^\d+\.\d+\.\d+$'){throw 'Use a semantic version such as 3.1.0.'}
if(!$Destination){$Destination=Join-Path $PSScriptRoot 'dist'}
New-Item -ItemType Directory -Path $Destination -Force|Out-Null
$Destination=[IO.Path]::GetFullPath($Destination)
Add-Type -AssemblyName System.IO.Compression.FileSystem
function Archive([string]$Path,[System.Collections.IDictionary]$Entries){
    $archive=[IO.Compression.ZipFile]::Open($Path,[IO.Compression.ZipArchiveMode]::Create)
    try{foreach($name in $Entries.Keys){[IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive,$Entries[$name],$name,[IO.Compression.CompressionLevel]::Optimal)|Out-Null}}
    finally{$archive.Dispose()}
}
$runtime=[ordered]@{}
foreach($name in 'LightNotes.exe','LightNotes.manifest','LightNotes.ico','README.md'){
    $file=Join-Path $PSScriptRoot $name
    if(!(Test-Path -LiteralPath $file -PathType Leaf)){throw ('Missing runtime file: '+$name)}
    $runtime[$name]=$file
}
$source=[ordered]@{}
$rootNames=@('CMakeLists.txt','.clang-format','.gitignore','.gitattributes','README.md','CHANGELOG.md','LightNotes.manifest','LightNotes.ico')
foreach($file in Get-ChildItem -LiteralPath $PSScriptRoot -File -Force){
    if($file.Extension -in '.cpp','.h','.ps1' -or $file.Name -in $rootNames){$source[$file.Name]=$file.FullName}
}
foreach($directory in 'docs','.github','.vscode'){
    $path=Join-Path $PSScriptRoot $directory
    if(Test-Path -LiteralPath $path){foreach($file in Get-ChildItem -LiteralPath $path -File -Recurse -Force){
        $name=$file.FullName.Substring($PSScriptRoot.Length+1).Replace('\','/')
        $source[$name]=$file.FullName
    }}
}
$runtimePath=Join-Path $Destination ('LightNotes-'+$Version+'-win-x64.zip')
$sourcePath=Join-Path $Destination ('LightNotes-'+$Version+'-source.zip')
# Delete only the two exact output archives; no recursive directory cleanup.
foreach($path in $runtimePath,$sourcePath){if(Test-Path -LiteralPath $path){Remove-Item -LiteralPath $path}}
Archive $runtimePath $runtime;Archive $sourcePath $source
$exePath=Join-Path $Destination 'LightNotes.exe'
if($exePath -ne $runtime['LightNotes.exe']){Copy-Item -LiteralPath $runtime['LightNotes.exe'] -Destination $exePath -Force}
$checksums=foreach($file in $runtimePath,$sourcePath,$exePath){(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()+'  '+[IO.Path]::GetFileName($file)}
[IO.File]::WriteAllLines((Join-Path $Destination 'SHA256SUMS.txt'),$checksums,[Text.UTF8Encoding]::new($false))
Write-Host ('Created runtime ZIP, source ZIP, executable and SHA256SUMS.txt in '+$Destination)
