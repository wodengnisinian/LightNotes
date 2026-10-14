param([string]$DataRoot)
$ErrorActionPreference='Stop'
if(!$DataRoot){$DataRoot=Join-Path $env:TEMP ('LightNotesFixed-'+[guid]::NewGuid())}
. (Join-Path $PSScriptRoot 'test-support.ps1') -DataRoot $DataRoot
Add-Type -TypeDefinition @"
using System;using System.Runtime.InteropServices;
public static class FixedCheck{[DllImport("user32.dll")]public static extern bool IsWindowEnabled(IntPtr h);[DllImport("user32.dll")]public static extern uint GetDpiForWindow(IntPtr h);[DllImport("user32.dll",EntryPoint="GetWindowLongPtrW")]public static extern IntPtr Style(IntPtr h,int index);}
"@
[IO.File]::WriteAllText((Join-Path $DataRoot 'settings.ini'),"[appearance]`r`nmaterial=0`r`npinned=1`r`n")
function Rect($h){[NotesTest+Rect]$r=New-Object NotesTest+Rect;[NotesTest]::GetWindowRect($h,[ref]$r)|Out-Null;return $r}
function Point($x,$y){return [IntPtr](([long]$y*65536)+$x)}
function Field($h,$id,$value){[NotesTest]::SendText([NotesTest]::GetDlgItem($h,$id),12,[IntPtr]::Zero,[string]$value)|Out-Null}
function AssertPad($r,[bool]$fixed){foreach($h in [NotesTest]::Find($app.Id,'DesktopNotes')){$actual=Rect $h;if($actual.left -ne $r.left -or $actual.top -ne $r.top -or $actual.right -ne $r.right -or $actual.bottom -ne $r.bottom){throw 'Whole pad dimensions or position differ'};if(([NotesTest]::GetProp($h,'PadFixed') -ne [IntPtr]::Zero) -ne $fixed){throw 'Page does not share pad fixed state'};if(([FixedCheck]::Style($h,-20).ToInt64() -band 8) -ne 0){throw 'Note is topmost'}};'PASS whole pad shares layout and position lock without topmost'}
function OpenGeometry($h){Command $h 146;$g=[NotesTest]::Find($app.Id,'LightNotesSettings')[0];Command $g 246;return [NotesTest]::Find($app.Id,'LightNotesGeometry')[0]}
function TearPage($h){$b=[NotesTest]::GetDlgItem($h,107);[NotesTest]::SendMessage($b,513,[IntPtr]1,[IntPtr]655370)|Out-Null;[NotesTest]::SendMessage($b,514,[IntPtr]::Zero,[IntPtr]655370)|Out-Null}
function Drag($h,$dx,$dy){$r=Rect $h;$cx=[int](120*$scale);$cy=[int](39*$scale);[NotesTest]::SendMessage($h,0xA1,[IntPtr]2,(Point ($r.left+$cx) ($r.top+$cy)))|Out-Null;[NotesTest]::SendMessage($h,0x200,[IntPtr]1,(Point ($cx+$dx) ($cy+$dy)))|Out-Null;[NotesTest]::SendMessage($h,0x202,[IntPtr]::Zero,(Point ($cx+$dx) ($cy+$dy)))|Out-Null}
try{
 Launch;Wait-Until {[NotesTest]::Find($app.Id,'LightNotesGeometry').Count -eq 1} 'panels ready'
 [NotesTest]::SetThreadDpiAwarenessContext([IntPtr](-4))|Out-Null
 $first=[NotesTest]::GetProp($hostWindow,'StackTop');$scale=[FixedCheck]::GetDpiForWindow($first)/96.0;$geo=OpenGeometry $first
 [NotesTest]::SendMessage([NotesTest]::GetDlgItem($geo,404),0x14E,[IntPtr]4,[IntPtr]::Zero)|Out-Null
 foreach($pair in @(@(400,460),@(401,500),@(402,140),@(403,120))){Field $geo $pair[0] $pair[1]};Command $geo 405
 Command $first 101;Command $first 101;$original=Rect $first;Command $first 102
 AssertPad $original $true
 if([NotesTest]::Text([NotesTest]::GetDlgItem($first,102)) -ne '已固定'){throw 'Fixed button label wrong'}
 $caption=Point ($original.left+[int](120*$scale)) ($original.top+[int](39*$scale))
 if([NotesTest]::SendMessage($first,0x84,[IntPtr]::Zero,$caption).ToInt32() -ne 1){throw 'Fixed caption still draggable'}
 Drag $first 60 40;AssertPad $original $true;'PASS fixed pad cannot be dragged'
 $geo=OpenGeometry $first
 if([FixedCheck]::IsWindowEnabled([NotesTest]::GetDlgItem($geo,402)) -or [FixedCheck]::IsWindowEnabled([NotesTest]::GetDlgItem($geo,404))){throw 'Locked position fields remain enabled'}
 if(![FixedCheck]::IsWindowEnabled([NotesTest]::GetDlgItem($geo,400)) -or ![FixedCheck]::IsWindowEnabled([NotesTest]::GetDlgItem($geo,405))){throw 'Fixed pad cannot adjust size'}
 foreach($pair in @(@(400,430),@(401,460),@(402,999),@(403,888))){Field $geo $pair[0] $pair[1]};[NotesTest]::SendMessage([NotesTest]::GetDlgItem($geo,404),0x14E,[IntPtr]0,[IntPtr]::Zero)|Out-Null;Command $geo 405
 $sized=Rect $first;if($sized.left -ne $original.left -or $sized.top -ne $original.top -or ($sized.right-$sized.left) -ge ($original.right-$original.left)){throw 'Numeric resizing moved fixed position'}
 AssertPad $sized $true;'PASS numeric size remains adjustable, position fields cannot move fixed pad'
 [NotesTest]::SendMessage($first,0xA1,[IntPtr]17,(Point ($sized.right-2) ($sized.bottom-2)))|Out-Null
 $mouse=Point ($sized.right-$sized.left+28) ($sized.bottom-$sized.top+18)
 [NotesTest]::SendMessage($first,0x200,[IntPtr]1,$mouse)|Out-Null;[NotesTest]::SendMessage($first,0x202,[IntPtr]::Zero,$mouse)|Out-Null
 $resized=Rect $first;if($resized.left -ne $sized.left -or $resized.top -ne $sized.top -or $resized.right -le $sized.right){throw 'Edge resize moved position or failed'};AssertPad $resized $true;'PASS resizing right or bottom edge keeps fixed position'
 Command $first 101;Wait-Until {[NotesTest]::Find($app.Id,'DesktopNotes').Count -eq 4} 'add page';AssertPad $resized $true
 Command $first 109;$last=[NotesTest]::GetProp($hostWindow,'StackTop');TearPage $last;Wait-Until {[NotesTest]::Find($app.Id,'DesktopNotes').Count -eq 3} 'tear added page';AssertPad $resized $true
 Command $first 108;$current=[NotesTest]::GetProp($hostWindow,'StackTop');AssertPad $resized $true
 $general=[NotesTest]::Find($app.Id,'LightNotesSettings')[0];Command $current 146
 if([NotesTest]::SendMessage([NotesTest]::GetDlgItem($general,250),0xF0,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32() -ne 0){throw 'Position lock unexpectedly enables topmost'}
 [NotesTest]::SendMessage([NotesTest]::GetDlgItem($general,210),0x14E,[IntPtr]0,[IntPtr]::Zero)|Out-Null;Command $general 217;AssertPad $resized $true
 TearPage $current;Wait-Until {[NotesTest]::Find($app.Id,'DesktopNotes').Count -eq 2} 'tear fixed page';AssertPad $resized $true
 $current=[NotesTest]::GetProp($hostWindow,'StackTop');$edit=[NotesTest]::GetDlgItem($current,110);[NotesTest]::SendText($edit,12,[IntPtr]::Zero,'固定只锁位置，内容仍可编辑')|Out-Null;Command $current (110 -bor (0x300 -shl 16))
 Exit-App;Launch;Wait-Until {[NotesTest]::Find($app.Id,'LightNotesGeometry').Count -eq 1} 'restart ready';AssertPad $resized $true
 $content=@([NotesTest]::Find($app.Id,'DesktopNotes') | Where-Object {[NotesTest]::Text([NotesTest]::GetDlgItem($_,110)) -eq '固定只锁位置，内容仍可编辑'});if($content.Count -ne 1){throw 'Fixed pad blocked editing'};'PASS content editing and restart preserve position lock'
 $current=[NotesTest]::GetProp($hostWindow,'StackTop');Command $current 102;Drag $current 35 25;$unlocked=Rect $current
 if($unlocked.left -ne $resized.left+35 -or $unlocked.top -ne $resized.top+25){throw 'Unlock did not allow moving pad'};AssertPad $unlocked $false;'PASS one button unlocks entire pad for movement'
 Command $current 102
 while([NotesTest]::GetProp($hostWindow,'StackTop') -ne [IntPtr]::Zero){$current=[NotesTest]::GetProp($hostWindow,'StackTop');$count=[NotesTest]::Find($app.Id,'DesktopNotes').Count;TearPage $current;if($count -gt 1){Wait-Until {[NotesTest]::Find($app.Id,'DesktopNotes').Count -eq $count-1} 'tear remaining'}else{Wait-Until {[NotesTest]::GetProp($hostWindow,'StackTop') -eq [IntPtr]::Zero} 'empty pad'}}
 Wait-Until {[NotesTest]::Find($app.Id,'Static').Count -eq 0} 'last falling page gone'
 Command $current 101;Wait-Until {[NotesTest]::GetProp($hostWindow,'StackTop') -ne [IntPtr]::Zero} 'new page from empty pad';AssertPad $unlocked $true;'PASS empty pad and newly added page keep whole-pad position lock'
 Exit-App;'ALL WHOLE PAD POSITION LOCK CHECKS PASSED'
}finally{if($script:app -and !$script:app.HasExited){Command $script:hostWindow 903}}
