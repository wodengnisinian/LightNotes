param([string]$DataRoot,[string]$Screenshot)
$ErrorActionPreference='Stop'
if(!$DataRoot){$DataRoot=Join-Path $env:TEMP ('LightNotesTasks-'+[guid]::NewGuid())}
$requestedScreenshot=$Screenshot
. (Join-Path $PSScriptRoot 'test-support.ps1') -DataRoot $DataRoot
$Screenshot=$requestedScreenshot
Add-Type -TypeDefinition @"
using System;using System.Text;using System.Runtime.InteropServices;
public static class TasksCheck{
 [StructLayout(LayoutKind.Sequential)]public struct R{public int left,top,right,bottom;}
 [DllImport("user32.dll",EntryPoint="SendMessageW")]static extern IntPtr RectMessage(IntPtr h,uint m,IntPtr w,ref R rect);
 [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")]static extern IntPtr TextMessage(IntPtr h,uint m,IntPtr w,StringBuilder text);
 [DllImport("user32.dll",EntryPoint="GetWindowLongPtrW")]public static extern IntPtr Style(IntPtr h,int index);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 public static R Format(IntPtr h){var r=new R();RectMessage(h,178,IntPtr.Zero,ref r);return r;}
 public static string Read(IntPtr h){var text=new StringBuilder(600000);TextMessage(h,13,(IntPtr)text.Capacity,text);return text.ToString();}
}
"@
function Point($x,$y){return [IntPtr](([long]$y*65536)+$x)}
function Body{[TasksCheck]::Read($script:editor)}
function Saved{if(!$script:noteFile){$script:noteFile=(Get-ChildItem $DataRoot -Filter '*.note'|Select-Object -First 1).FullName};$b=[IO.File]::ReadAllBytes($script:noteFile);[Text.Encoding]::Unicode.GetString($b,56,$b.Length-56)}
function Content([string]$value){[NotesTest]::SendText($script:editor,12,[IntPtr]::Zero,$value)|Out-Null;Command $script:note (110 -bor (0x300 -shl 16));Wait-Until {(Saved) -eq $value} 'text autosaved'}
function Spacing{$first=[NotesTest]::SendMessage($script:editor,0xBB,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32();$second=[NotesTest]::SendMessage($script:editor,0xBB,[IntPtr]1,[IntPtr]::Zero).ToInt32();$a=[NotesTest]::SendMessage($script:editor,0xD6,[IntPtr]$first,[IntPtr]::Zero).ToInt64();$b=[NotesTest]::SendMessage($script:editor,0xD6,[IntPtr]$second,[IntPtr]::Zero).ToInt64();$height=(($b -shr 16) -band 65535)-(($a -shr 16) -band 65535);if($height -le 0){throw 'Cannot measure actual EDIT row height'};return $height}
function ClickTask([int]$visualLine,[bool]$async=$false){$format=[TasksCheck]::Format($script:editor);[NotesTest+Rect]$r=New-Object NotesTest+Rect;[NotesTest]::GetWindowRect($script:gutter,[ref]$r)|Out-Null;$point=Point ([int](($r.right-$r.left)/2)) ($format.top+[int]($script:rowHeight/2)+$visualLine*$script:rowHeight);[NotesTest]::SendMessage($script:gutter,0x201,[IntPtr]1,$point)|Out-Null;if($async){[TasksCheck]::PostMessage($script:gutter,0x202,[IntPtr]::Zero,$point)|Out-Null}else{[NotesTest]::SendMessage($script:gutter,0x202,[IntPtr]::Zero,$point)|Out-Null}}
function Expect([string]$value,[string]$label){Wait-Until {(Body) -eq $value -and (Saved) -eq $value} $label;if([NotesTest]::Text([NotesTest]::GetDlgItem($script:note,152)) -ne '1 / 1'){throw 'Completion changed paper count'}}
try{
 Launch;Wait-Until {[NotesTest]::Find($app.Id,'LightNotesStyle').Count -eq 1} 'style ready';Command $note 140
 [NotesTest]::SetThreadDpiAwarenessContext([IntPtr](-4))|Out-Null
 $script:editor=[NotesTest]::GetDlgItem($note,110);$script:gutter=[NotesTest]::GetDlgItem($note,153)
 if($gutter -eq [IntPtr]::Zero){throw 'Task checkbox gutter missing'}
 $numbers=(1..11|ForEach-Object {[string]$_}) -join "`r`n";Content $numbers;$script:rowHeight=Spacing
 if($Screenshot){[NotesTest+Rect]$r=New-Object NotesTest+Rect;[NotesTest]::GetWindowRect($note,[ref]$r)|Out-Null;$bmp=New-Object Drawing.Bitmap ($r.right-$r.left),($r.bottom-$r.top);$g=[Drawing.Graphics]::FromImage($bmp);$dc=$g.GetHdc();[NotesTest]::PrintWindow($note,$dc,0)|Out-Null;$g.ReleaseHdc($dc);$g.Dispose();$bmp.Save($Screenshot);$bmp.Dispose()}
 ClickTask 1;Expect (((1..11|Where-Object {$_ -ne 2})|ForEach-Object {[string]$_}) -join "`r`n") 'checking second event removes only that event and persists immediately'
 Content "第一条`r`n中间条`r`n最后条";ClickTask 2;Expect "第一条`r`n中间条" 'last completion leaves no extra newline';ClickTask 0;Expect '中间条' 'first completion shifts remaining event upward';ClickTask 0;Expect '' 'last event clears content without adding paper'
 Content "甲`r`n`r`n乙";$script:rowHeight=Spacing;ClickTask 1;Expect "甲`r`n`r`n乙" 'blank rule has no completable event'
 $long='这是一条会自动换行的完整事件，不能只删除其中一行。'*5;Content ($long+"`r`n后面的事件");$script:rowHeight=Spacing;ClickTask 0;Expect '后面的事件' 'wrapped event removed as one event'
 Content "锁定事件`r`n保留事件";Command $note 106;$script:rowHeight=Spacing;ClickTask 0;Expect "锁定事件`r`n保留事件" 'readonly content cannot be completed';Command $note 106
 $tmp=$script:noteFile+'.tmp';New-Item -ItemType Directory -Path $tmp|Out-Null;ClickTask 0 $true
 Wait-Until {[NotesTest]::Find($app.Id,'#32770').Count -eq 1} 'failed completion reports save error';$dialog=[NotesTest]::Find($app.Id,'#32770')[0];Command $dialog 1
 Wait-Until {(Body) -eq "锁定事件`r`n保留事件"} 'save failure restores event';Remove-Item -LiteralPath $tmp
 if((Saved) -ne "锁定事件`r`n保留事件"){throw 'Failed completion altered stored note'}
 $many=(1..40|ForEach-Object {'固定事件'+$_}) -join "`r`n";Content $many
 if(([TasksCheck]::Style($editor,-16).ToInt64() -band 0x300000) -ne 0){throw 'Scroll bar style remains'}
 foreach($h in $editor,$gutter){[NotesTest]::SendMessage($h,0x20A,[IntPtr](-7864320),[IntPtr]::Zero)|Out-Null;[NotesTest]::SendMessage($h,0x115,[IntPtr]1,[IntPtr]::Zero)|Out-Null}
 [NotesTest]::SendMessage($editor,0xB6,[IntPtr]::Zero,[IntPtr]20)|Out-Null;[NotesTest]::SendMessage($editor,0xB7,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
 for($i=0;$i -lt 40;$i++){[NotesTest]::SendMessage($editor,0x100,[IntPtr]40,[IntPtr]::Zero)|Out-Null}
 if([NotesTest]::SendMessage($editor,0xCE,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32() -ne 0 -or (Body) -ne $many){throw 'Viewport moved or overflow content lost'}
 'PASS scrollbars, wheel and automatic viewport movement disabled; stored overflow preserved'
 $script:rowHeight=Spacing;ClickTask 0;Expect ((2..40|ForEach-Object {'固定事件'+$_}) -join "`r`n") 'completion shifts existing overflow into fixed viewport'
 [NotesTest]::SendMessage($editor,0xC7,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null;Expect $many 'undo completion restores event and saves'
 Content "重启后保留`r`n重启前完成";$script:rowHeight=Spacing;ClickTask 1;Expect '重启后保留' 'completion saved before restart'
 Exit-App;Launch;Wait-Until {[NotesTest]::Find($app.Id,'LightNotesStyle').Count -eq 1} 'restart ready';$script:note=[NotesTest]::GetProp($hostWindow,'StackTop');$script:editor=[NotesTest]::GetDlgItem($note,110)
 Expect '重启后保留' 'completed event stays gone after restart';Exit-App
 'ALL RULED TODO AND FIXED VIEWPORT CHECKS PASSED'
}finally{if($app -and !$app.HasExited){Command $hostWindow 903}}
