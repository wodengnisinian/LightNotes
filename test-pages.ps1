param([string]$DataRoot,[string]$Screenshot)
$ErrorActionPreference='Stop'
if(!$DataRoot){$DataRoot=Join-Path $env:TEMP ('LightNotesPages-'+[guid]::NewGuid())}
$requestedScreenshot=$Screenshot
. (Join-Path $PSScriptRoot 'test-support.ps1') -DataRoot $DataRoot
$Screenshot=$requestedScreenshot

Add-Type -TypeDefinition @"
using System;using System.Text;using System.Runtime.InteropServices;
public static class PageContent{[DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")]static extern IntPtr ReadText(IntPtr h,uint m,IntPtr size,StringBuilder text);public static string Read(IntPtr h){var text=new StringBuilder(10000);ReadText(h,13,(IntPtr)text.Capacity,text);return text.ToString();}}
"@

function Counter([IntPtr]$note,[string]$expected){Wait-Until { [NotesTest]::Text([NotesTest]::GetDlgItem($note,152)) -eq $expected } ('visible page counter '+$expected)}
function ClickAdd([IntPtr]$note){[NotesTest]::SendMessage([NotesTest]::GetDlgItem($note,101),0xF5,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null}
function ClickPage([IntPtr]$note,[int]$id){[NotesTest]::SendMessage([NotesTest]::GetDlgItem($note,$id),0xF5,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null}
function MouseTear([IntPtr]$note,[bool]$drag=$false){$button=[NotesTest]::GetDlgItem($note,107);[NotesTest]::SendMessage($button,0x201,[IntPtr]1,[IntPtr]655370)|Out-Null;if($drag){[NotesTest]::SendMessage($button,0x200,[IntPtr]1,[IntPtr]7208970)|Out-Null;[NotesTest]::SendMessage($button,0x202,[IntPtr]::Zero,[IntPtr]7208970)|Out-Null}else{[NotesTest]::SendMessage($button,0x202,[IntPtr]::Zero,[IntPtr]655370)|Out-Null}}
try{
 $script:app=Start-Process -FilePath $exe -ArgumentList '--test-mode','--data-dir',('"'+$DataRoot+'"') -PassThru
 Wait-Until { $hosts=[NotesTest]::Find($app.Id,'LightNotesController'); $hosts.Count -eq 1 -and [NotesTest]::GetProp($hosts[0],'StackTop') -ne [IntPtr]::Zero -and [NotesTest]::Find($app.Id,'LightNotesGeometry').Count -eq 1 } 'note presentation ready'
 $script:hostWindow=[NotesTest]::Find($app.Id,'LightNotesController')[0];$first=[NotesTest]::GetProp($hostWindow,'StackTop')
 Counter $first '1 / 1';ClickAdd $first;Counter $first '1 / 2';ClickAdd $first;Counter $first '1 / 3'
 if([NotesTest]::GetProp($hostWindow,'StackTop') -ne $first){throw 'FAIL add switched the front page'};'PASS add preserves front page'
 $dpi=[NotesTest]::SetThreadDpiAwarenessContext([IntPtr](-4))
 [NotesTest+Rect]$pageRect=New-Object NotesTest+Rect;[NotesTest]::GetWindowRect($first,[ref]$pageRect)|Out-Null
 foreach($id in 101,152,146,147,102,148,107,109,108){[NotesTest+Rect]$controlRect=New-Object NotesTest+Rect;[NotesTest]::GetWindowRect([NotesTest]::GetDlgItem($first,$id),[ref]$controlRect)|Out-Null;if($controlRect.right -gt $pageRect.right-14 -or $controlRect.bottom -gt $pageRect.bottom-14){throw "FAIL control $id covers book edge"}}
 if([NotesTest]::GetDlgItem($first,149) -ne [IntPtr]::Zero){throw 'FAIL minus button remains'}
 if([NotesTest]::Text([NotesTest]::GetDlgItem($first,101)) -ne '添加'){throw 'FAIL add button title'}
 [NotesTest+Rect]$addRect=New-Object NotesTest+Rect;[NotesTest+Rect]$tearRect=New-Object NotesTest+Rect
 [NotesTest]::GetWindowRect([NotesTest]::GetDlgItem($first,101),[ref]$addRect)|Out-Null;[NotesTest]::GetWindowRect([NotesTest]::GetDlgItem($first,107),[ref]$tearRect)|Out-Null
 if($addRect.right -gt $tearRect.left -or $addRect.top -ne $tearRect.top -or $addRect.bottom -ne $tearRect.bottom){throw 'FAIL add button is not immediately left of tear'}
 'PASS no top plus/minus; add sits left of tear on the same footer row'
 'PASS toolbar and footer controls stay inside front paper'
 [NotesTest+Rect]$previousRect=New-Object NotesTest+Rect;[NotesTest+Rect]$nextRect=New-Object NotesTest+Rect
 [NotesTest]::GetWindowRect([NotesTest]::GetDlgItem($first,109),[ref]$previousRect)|Out-Null;[NotesTest]::GetWindowRect([NotesTest]::GetDlgItem($first,108),[ref]$nextRect)|Out-Null
 if($tearRect.right -gt $previousRect.left -or $previousRect.right -gt $nextRect.left -or $previousRect.top -ne $tearRect.top -or $nextRect.top -ne $tearRect.top){throw 'FAIL previous page button order or overlap'}
 [NotesTest]::SendText([NotesTest]::GetDlgItem($first,110),12,[IntPtr]::Zero,'第1页')|Out-Null
 ClickPage $first 108;$page2=[NotesTest]::GetProp($hostWindow,'StackTop');Counter $page2 '2 / 3';[NotesTest]::SendText([NotesTest]::GetDlgItem($page2,110),12,[IntPtr]::Zero,'第2页')|Out-Null
 ClickPage $page2 108;$page3=[NotesTest]::GetProp($hostWindow,'StackTop');Counter $page3 '3 / 3';[NotesTest]::SendText([NotesTest]::GetDlgItem($page3,110),12,[IntPtr]::Zero,'第3页')|Out-Null
 ClickPage $page3 109;$current=[NotesTest]::GetProp($hostWindow,'StackTop');Counter $current '2 / 3'
 if($current -ne $page2 -or [PageContent]::Read([NotesTest]::GetDlgItem($current,110)) -ne '第2页'){throw 'FAIL previous page did not return correct second-page content'}
 ClickPage $current 109;$current=[NotesTest]::GetProp($hostWindow,'StackTop');Counter $current '1 / 3'
 if($current -ne $first -or [PageContent]::Read([NotesTest]::GetDlgItem($current,110)) -ne '第1页'){throw 'FAIL previous page did not return to first note'}
 ClickPage $current 109;$current=[NotesTest]::GetProp($hostWindow,'StackTop');Counter $current '3 / 3';ClickPage $current 108;Counter $first '1 / 3'
 'PASS actual previous/next buttons return correct content and cycle available notes'

 if($Screenshot){Command $first 140;Start-Sleep -Milliseconds 300;$bmp=New-Object Drawing.Bitmap ($pageRect.right-$pageRect.left),($pageRect.bottom-$pageRect.top);$g=[Drawing.Graphics]::FromImage($bmp);$dc=$g.GetHdc();[NotesTest]::PrintWindow($first,$dc,0)|Out-Null;$g.ReleaseHdc($dc);$g.Dispose();$bmp.Save($Screenshot);$bmp.Dispose()}
 MouseTear $first
 Wait-Until { [NotesTest]::Find($app.Id,'DesktopNotes').Count -eq 2 } 'first tear completes'
 $second=[NotesTest]::GetProp($hostWindow,'StackTop');Counter $second '2 / 3'
 ClickPage $second 109;$previousLive=[NotesTest]::GetProp($hostWindow,'StackTop');Counter $previousLive '3 / 3';if($previousLive -eq $first){throw 'FAIL previous restored a torn page'};ClickPage $previousLive 109;Counter $second '2 / 3';'PASS previous skips torn notes'
 if(@(Get-ChildItem $DataRoot -Filter '*.note').Count -ne 2){throw 'FAIL first tear file count'}
 MouseTear $second $true
 Wait-Until { [NotesTest]::Find($app.Id,'DesktopNotes').Count -eq 1 } 'second drag tear completes'
 $third=[NotesTest]::GetProp($hostWindow,'StackTop');Counter $third '3 / 3'
 MouseTear $third
 Counter $third '0 / 0'
 if(@(Get-ChildItem $DataRoot -Filter '*.note').Count -ne 0){throw 'FAIL final tear created a new page'}
 if(@(Get-ChildItem (Join-Path $DataRoot 'Recycled') -Filter '*.note').Count -ne 3){throw 'FAIL recycle count'}
 'PASS last tear creates no page; all three remain recoverable'
 Wait-Until { [NotesTest]::IsWindowVisible([NotesTest]::GetDlgItem($third,101)) } 'empty header exposes add button'
 [NotesTest+Rect]$emptyAdd=New-Object NotesTest+Rect;[NotesTest]::GetWindowRect([NotesTest]::GetDlgItem($third,101),[ref]$emptyAdd)|Out-Null
 [NotesTest+Rect]$emptyCounter=New-Object NotesTest+Rect;[NotesTest]::GetWindowRect([NotesTest]::GetDlgItem($third,152),[ref]$emptyCounter)|Out-Null
 if($emptyAdd.top -ne $emptyCounter.top -or $emptyAdd.bottom -ne $emptyCounter.bottom){throw 'FAIL empty pad add button outside header'}
 ClickAdd $third
 Wait-Until { [NotesTest]::GetProp($hostWindow,'StackTop') -ne [IntPtr]::Zero } 'explicit add creates new pad'
 $new=[NotesTest]::GetProp($hostWindow,'StackTop');Counter $new '1 / 1'
 Exit-App
 'ALL PAGE AND BUTTON CHECKS PASSED'
}finally{if($script:app -and !$script:app.HasExited){Command $script:hostWindow 903};if($dpi){[NotesTest]::SetThreadDpiAwarenessContext($dpi)|Out-Null}}


