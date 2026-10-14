param([string]$DataRoot,[string]$Screenshot)
$ErrorActionPreference='Stop'
if(!$DataRoot){$DataRoot=Join-Path $env:TEMP ('LightNotesMaterial-'+[guid]::NewGuid())}
$requestedScreenshot=$Screenshot
. (Join-Path $PSScriptRoot 'test-support.ps1') -DataRoot $DataRoot
$Screenshot=$requestedScreenshot
if(@(Get-ChildItem $DataRoot -Filter '*.note').Count){throw 'Use a fresh isolated test folder'}
Add-Type -TypeDefinition @"
using System;using System.Runtime.InteropServices;
public static class BorderCheck{[DllImport("user32.dll",EntryPoint="GetWindowLongPtrW")]public static extern IntPtr Style(IntPtr window,int index);[DllImport("user32.dll")]public static extern uint GetDpiForWindow(IntPtr window);}
"@
[IO.File]::WriteAllText((Join-Path $DataRoot 'settings.ini'),"[appearance]`r`nmaterial=0`r`nblur=12`r`ntransparency=50`r`n")
for($i=1;$i -le 3;$i++){$stream=New-Object IO.MemoryStream;$writer=New-Object IO.BinaryWriter $stream;foreach($v in 0x4E4F5433,0,0,100,100,410,560){$writer.Write([int]$v)};$writer.Write([long]0);foreach($v in 0,18,($i%3),12,50){$writer.Write([int]$v)};$writer.Write([Text.Encoding]::Unicode.GetBytes('Legacy material page '+$i));[IO.File]::WriteAllBytes((Join-Path $DataRoot ($i.ToString()+'.note')),$stream.ToArray());$writer.Dispose();$stream.Dispose()}
function Materials([int]$expected){foreach($h in [NotesTest]::Find($app.Id,'DesktopNotes')){if([NotesTest]::GetProp($h,'NoteMaterial').ToInt32() -ne $expected){throw 'Mixed note materials'}};"PASS all pages use material $($expected-1)"}
try{
 $script:app=Start-Process -FilePath $exe -ArgumentList '--test-mode','--data-dir',('"'+$DataRoot+'"') -PassThru
 Wait-Until { [NotesTest]::Find($app.Id,'LightNotesStyle').Count -eq 1 } 'mixed legacy notes loaded'
 $script:hostWindow=[NotesTest]::Find($app.Id,'LightNotesController')[0]
 Wait-Until {[NotesTest]::GetProp($hostWindow,'AppReady') -eq [IntPtr]1} 'material application initialization complete'
 $note=[NotesTest]::GetProp($hostWindow,'StackTop');Materials 1
 if([NotesTest]::Find($app.Id,'Static').Count -ne 0){throw 'Unexpected base overlay'};'PASS no independent base window'
 if(([BorderCheck]::Style($note,-16).ToInt64() -band 0x40000) -ne 0){throw 'Native resize frame remains'};'PASS native frame removed'
 Command $note 147;$style=[NotesTest]::Find($app.Id,'LightNotesStyle')[0];[NotesTest]::SendMessage([NotesTest]::GetDlgItem($style,211),0x14E,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null;Command $style 217
 foreach($file in Get-ChildItem $DataRoot -Filter '*.note'){if([BitConverter]::ToInt32([IO.File]::ReadAllBytes($file.FullName),44) -ne 0){throw 'Paper style not persisted for whole stack'}};'PASS paper style stored across all notes'
 [NotesTest]::SetThreadDpiAwarenessContext([IntPtr](-4))|Out-Null
 [NotesTest+Rect]$r=New-Object NotesTest+Rect;[NotesTest]::GetWindowRect($note,[ref]$r)|Out-Null
 $point=([long]($r.bottom-2)*65536)+($r.right-2);[NotesTest]::SendMessage($note,0xA1,[IntPtr]17,[IntPtr]$point)|Out-Null
 $point=([long]($r.bottom-$r.top+40)*65536)+($r.right-$r.left+30);[NotesTest]::SendMessage($note,0x200,[IntPtr]1,[IntPtr]$point)|Out-Null;[NotesTest]::SendMessage($note,0x202,[IntPtr]::Zero,[IntPtr]$point)|Out-Null
 [NotesTest+Rect]$after=New-Object NotesTest+Rect;[NotesTest]::GetWindowRect($note,[ref]$after)|Out-Null
 if(($after.right-$after.left) -le ($r.right-$r.left) -or ($after.bottom-$after.top) -le ($r.bottom-$r.top)){throw 'Frameless resize failed'};'PASS resizing preserved without native frame'
 if($Screenshot){Start-Sleep -Milliseconds 300;$bmp=New-Object Drawing.Bitmap ($after.right-$after.left),($after.bottom-$after.top);$g=[Drawing.Graphics]::FromImage($bmp);$dc=$g.GetHdc();[NotesTest]::PrintWindow($note,$dc,0)|Out-Null;$g.ReleaseHdc($dc);$g.Dispose();$bmp.Save($Screenshot);$bmp.Dispose();('Capture DPI: '+[BorderCheck]::GetDpiForWindow($note))}
 $button=[NotesTest]::GetDlgItem($note,107);[NotesTest]::SendMessage($button,513,[IntPtr]1,[IntPtr]655370)|Out-Null;[NotesTest]::SendMessage($button,514,[IntPtr]::Zero,[IntPtr]655370)|Out-Null
 Wait-Until { [NotesTest]::Find($app.Id,'DesktopNotes').Count -eq 2 } 'tear finished';Materials 1
 Command $hostWindow 904;Wait-Until { [NotesTest]::Find($app.Id,'DesktopNotes').Count -eq 3 } 'torn note restored';Materials 1
 Exit-App
 $script:app=Start-Process -FilePath $exe -ArgumentList '--test-mode','--data-dir',('"'+$DataRoot+'"') -PassThru
 Wait-Until { [NotesTest]::Find($app.Id,'LightNotesStyle').Count -eq 1 } 'restart ready';$script:hostWindow=[NotesTest]::Find($app.Id,'LightNotesController')[0];Materials 1;Exit-App
 'ALL MATERIAL AND FRAME CHECKS PASSED'
}finally{if($script:app -and !$script:app.HasExited){Command $script:hostWindow 903}}
