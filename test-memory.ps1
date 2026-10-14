param([string]$DataRoot,[string]$Screenshot)
$ErrorActionPreference='Stop'
if(!$DataRoot){$DataRoot=Join-Path $env:TEMP ('LightNotesMemory-'+[guid]::NewGuid())}
$requestedScreenshot=$Screenshot
. (Join-Path $PSScriptRoot 'test-support.ps1') -DataRoot $DataRoot
$Screenshot=$requestedScreenshot
Add-Type -TypeDefinition @'
using System;using System.Text;using System.Runtime.InteropServices;
public static class MemoryCheck{
 [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")]static extern IntPtr ReadText(IntPtr h,uint m,IntPtr w,StringBuilder text);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 public static string Read(IntPtr h){var text=new StringBuilder(600000);ReadText(h,13,(IntPtr)text.Capacity,text);return text.ToString();}
}
'@
function Front{[NotesTest]::GetProp($hostWindow,'StackTop')}
function Body($h){[MemoryCheck]::Read([NotesTest]::GetDlgItem($h,110))}
function Click($h,$id){[NotesTest]::SendMessage([NotesTest]::GetDlgItem($h,$id),0xF5,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null}
function Start-App{Launch;Wait-Until {[NotesTest]::Find($app.Id,'LightNotesGeometry').Count -eq 1 -and (Front) -ne [IntPtr]::Zero} 'memory startup ready'}
function Settings($h){Click $h 146;return [NotesTest]::Find($app.Id,'LightNotesSettings')[0]}
function Checked($h){[NotesTest]::SendMessage([NotesTest]::GetDlgItem($h,248),0xF0,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32() -eq 1}
function WriteBody($h,$text){[NotesTest]::SendText([NotesTest]::GetDlgItem($h,110),12,[IntPtr]::Zero,$text)|Out-Null;Command $h (110 -bor (0x300 -shl 16))}
function Stored{$result=@();foreach($file in Get-ChildItem $DataRoot -Filter '*.note'){$b=[IO.File]::ReadAllBytes($file.FullName);$result+=[Text.Encoding]::Unicode.GetString($b,56,$b.Length-56)};return $result}
function Assert($ok,$label){if(!$ok){throw $label};'PASS '+$label}
try{
 [NotesTest]::SetThreadDpiAwarenessContext([IntPtr](-4))|Out-Null
 Start-App;$first=Front;$general=Settings $first
 Assert (Checked $general) 'memory is enabled by default'
 Assert ([NotesTest]::GetDlgItem([NotesTest]::Find($app.Id,'LightNotesStyle')[0],248) -eq [IntPtr]::Zero) 'memory belongs only to general settings'
 if($Screenshot){[NotesTest]::SetThreadDpiAwarenessContext([IntPtr](-4))|Out-Null;[NotesTest+Rect]$r=New-Object NotesTest+Rect;[NotesTest]::GetWindowRect($general,[ref]$r)|Out-Null;$bmp=New-Object Drawing.Bitmap ($r.right-$r.left),($r.bottom-$r.top);$g=[Drawing.Graphics]::FromImage($bmp);$dc=$g.GetHdc();[NotesTest]::PrintWindow($general,$dc,0)|Out-Null;$g.ReleaseHdc($dc);$g.Dispose();$bmp.Save($Screenshot);$bmp.Dispose()}
 Click $general 225;Command $first 140
 WriteBody $first "未完成甲`r`n已经完成乙`r`n保留丙"
 Wait-Until {(Stored) -contains "未完成甲`r`n已经完成乙`r`n保留丙"} 'events saved'
 $editor=[NotesTest]::GetDlgItem($first,110);$gutter=[NotesTest]::GetDlgItem($first,153)
 $a=[NotesTest]::SendMessage($editor,0xD6,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64();$index=[NotesTest]::SendMessage($editor,0xBB,[IntPtr]1,[IntPtr]::Zero)
 $b=[NotesTest]::SendMessage($editor,0xD6,$index,[IntPtr]::Zero).ToInt64();$row=(($b -shr 16) -band 65535)-(($a -shr 16) -band 65535)
 [NotesTest+Rect]$r=New-Object NotesTest+Rect;[NotesTest]::GetWindowRect($gutter,[ref]$r)|Out-Null;$y=(($b -shr 16) -band 65535)+[int]($row/2);$point=[IntPtr](([long]$y*65536)+[int](($r.right-$r.left)/2))
 [NotesTest]::SendMessage($gutter,0x201,[IntPtr]1,$point)|Out-Null;[NotesTest]::SendMessage($gutter,0x202,[IntPtr]::Zero,$point)|Out-Null
 Assert ((Body $first) -eq "未完成甲`r`n保留丙") 'real checkbox removes completed event'
 Click $first 101;Click $first 108;$second=Front;WriteBody $second '第二页未完成丁'
 # Quit before the editing debounce finishes: explicit exit must still save this page.
 Exit-App;Start-App;$second=Front
 Assert ((Body $second) -eq '第二页未完成丁') 'restart returns to last unfinished page'
 Assert ([NotesTest]::Text([NotesTest]::GetDlgItem($second,152)) -eq '2 / 2') 'page order survives restart'
 Assert (!((Stored) -join "`n").Contains('已经完成乙')) 'completed event does not return'
 Click $second 109;Assert ((Body (Front)) -eq "未完成甲`r`n保留丙") 'other unfinished events are restored'
 Click (Front) 101;Click (Front) 109;Assert ((Body (Front)) -eq '') 'new empty last page selected'
 Exit-App;Start-App
 Assert ((Body (Front)) -ne '') 'startup selects unfinished events instead of remembered empty page'
 $general=Settings (Front);Click $general 248;Click $general 225;$general=Settings (Front)
 Assert (Checked $general) 'closing settings discards memory draft'
 Click $general 248;Click $general 217
 Assert ((Get-Content (Join-Path $DataRoot 'settings.ini') -Raw) -match 'remember=0') 'disabled memory setting persisted'
 $eventFiles=@{};foreach($file in Get-ChildItem $DataRoot -Filter '*.note'){$b=[IO.File]::ReadAllBytes($file.FullName);if($b.Length -gt 56){$eventFiles[$file.FullName]=(Get-FileHash $file.FullName).Hash}}
 Exit-App;Start-App
 Assert ((Body (Front)) -eq '' -and [NotesTest]::Find($app.Id,'DesktopNotes').Count -eq 1) 'disabled memory opens one empty page'
 $general=Settings (Front);Assert (!(Checked $general)) 'disabled checkbox restored';Click $general 225
 foreach($file in $eventFiles.Keys){Assert ((Get-FileHash $file).Hash -eq $eventFiles[$file]) 'disabled startup preserves saved event file'}
 $count=@(Get-ChildItem $DataRoot -Filter '*.note').Count;Exit-App;Start-App
 Assert (@(Get-ChildItem $DataRoot -Filter '*.note').Count -eq $count) 'repeated disabled startup reuses empty sheet'
 $general=Settings (Front);Click $general 248;Click $general 217;Exit-App;Start-App
 $contents=@([NotesTest]::Find($app.Id,'DesktopNotes')|ForEach-Object{Body $_})
 Assert ($contents -contains "未完成甲`r`n保留丙" -and $contents -contains '第二页未完成丁' -and (Body (Front)) -ne '') 're-enabling memory restores preserved unfinished events'
 # A real metadata-write failure must leave the previous snapshot and keep the app open.
 $session=Join-Path $DataRoot 'session.ini';Command (Front) 102;Start-Sleep -Milliseconds 650;$original=(Get-FileHash $session).Hash
 $blocked=$session+'.tmp';New-Item -ItemType Directory -Path $blocked|Out-Null
 [MemoryCheck]::PostMessage($hostWindow,0x111,[IntPtr]903,[IntPtr]::Zero)|Out-Null
 Wait-Until {[NotesTest]::Find($app.Id,'#32770').Count -eq 1} 'failed memory save reports error';Command ([NotesTest]::Find($app.Id,'#32770')[0]) 1
 Assert (!$app.HasExited) 'failed memory save keeps application open'
 Assert ((Get-FileHash $session).Hash -eq $original) 'failed memory save preserves previous snapshot'
 Remove-Item -LiteralPath $blocked;Exit-App
 # Invalid/duplicate references cannot load files outside the data root or duplicate pages.
 $name=(Get-ChildItem $DataRoot -Filter '*.note'|Select-Object -First 1).Name
 [IO.File]::WriteAllText($session,"[session]`r`ncount=3`r`npage0=../foreign.note`r`npage1=$name`r`npage2=$name`r`nactive=../foreign.note`r`n")
 Start-App;Assert ([NotesTest]::Find($app.Id,'DesktopNotes').Count -eq $count -and (Body (Front)) -ne '') 'invalid session references fall back to local unfinished events'
 Exit-App;'ALL MEMORY SETTING AND EVENT RESTORE CHECKS PASSED'
}finally{if($script:app -and !$script:app.HasExited){Command $script:hostWindow 903}}
