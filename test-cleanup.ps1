param([string]$DataRoot)
if(!$DataRoot){$DataRoot=Join-Path $env:TEMP ('LightNotesCleanup-'+[guid]::NewGuid())}
. (Join-Path $PSScriptRoot 'test-support.ps1') -DataRoot $DataRoot
function Header{[IO.File]::ReadAllBytes((Get-ChildItem $DataRoot -Filter '*.note'|Select-Object -First 1).FullName)}
function Assert($ok,$label){if(!$ok){throw $label};'PASS '+$label}
try{
 # A normal app instance must not retain obsolete test-only controls or commands.
 $script:app=Start-Process -FilePath $exe -ArgumentList '--data-dir',('"'+$DataRoot+'"') -WindowStyle Hidden -PassThru
 Wait-Until {[NotesTest]::Find($app.Id,'LightNotesStyle').Count -eq 1} 'normal app panels ready'
 $script:hostWindow=[NotesTest]::Find($app.Id,'LightNotesController')[0]
 $note=@([NotesTest]::Find($app.Id,'DesktopNotes')|Where-Object {[NotesTest]::IsWindowVisible($_)})[0]
 Assert ([NotesTest]::GetDlgItem($note,130) -eq [IntPtr]::Zero) 'normal app does not create a hidden test status control'
 Assert ([NotesTest]::GetProp($note,'GlassReady') -eq [IntPtr]::Zero -and [NotesTest]::GetProp($hostWindow,'StackTop') -eq [IntPtr]::Zero) 'diagnostic properties exist only in test mode'
 $before=Header;Command $note 103;Command $note 140;Command $note 141;Command $note 142;$after=Header
 Assert ([BitConverter]::ToInt32($before,4) -eq [BitConverter]::ToInt32($after,4) -and [BitConverter]::ToInt32($before,44) -eq [BitConverter]::ToInt32($after,44)) 'obsolete hidden style commands do nothing in normal mode'
 Command $note 147;$style=[NotesTest]::Find($app.Id,'LightNotesStyle')[0]
 [NotesTest]::SendMessage([NotesTest]::GetDlgItem($style,211),0x14E,[IntPtr]0,[IntPtr]::Zero)|Out-Null
 [NotesTest]::SendMessage([NotesTest]::GetDlgItem($style,216),0x14E,[IntPtr]1,[IntPtr]::Zero)|Out-Null
 Command $style 217;$after=Header
 Assert ([BitConverter]::ToInt32($after,4) -eq 1 -and [BitConverter]::ToInt32($after,44) -eq 0) 'visible style panel still applies color and material in normal mode'
 [NotesTest]::SendMessage([NotesTest]::GetDlgItem($note,110),0x100,[IntPtr]27,[IntPtr]::Zero)|Out-Null
 Assert (![NotesTest]::IsWindowVisible($note)) 'shared shortcut handler preserves Escape hide behavior'
 Command $hostWindow 901;Assert ([NotesTest]::IsWindowVisible($note)) 'normal app reopens after hide'
 Exit-App;'ALL NORMAL-MODE CLEANUP CHECKS PASSED'
}finally{if($script:app -and !$script:app.HasExited){Command $script:hostWindow 903}}
