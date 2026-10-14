param([string]$DataRoot,[string]$Screenshot)
$ErrorActionPreference='Stop'
if(!$DataRoot){$DataRoot=Join-Path $env:TEMP ('LightNotesStartup-'+[guid]::NewGuid()+' 空格')}
$requestedScreenshot=$Screenshot
. (Join-Path $PSScriptRoot 'test-support.ps1') -DataRoot $DataRoot
$Screenshot=$requestedScreenshot
$registryPath='Software\LightNotes\TestStartup\'+(Split-Path $DataRoot -Leaf)
$registryKeys=@($registryPath)
$productionKey=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Software\Microsoft\Windows\CurrentVersion\Run')
$productionBefore=$productionKey.GetValue('LightNotes',$null);$productionKey.Dispose()
Add-Type -TypeDefinition @'
using System;using System.Text;using System.Runtime.InteropServices;
public static class StartupCheck{
 [DllImport("user32.dll",EntryPoint="GetWindowLongPtrW")]public static extern IntPtr Style(IntPtr h,int index);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern IntPtr GetWindow(IntPtr h,uint command);
 [DllImport("user32.dll")]public static extern bool SetWindowPos(IntPtr h,IntPtr order,int x,int y,int w,int height,uint flags);
 [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")]static extern IntPtr ReadText(IntPtr h,uint m,IntPtr w,StringBuilder text);
 [DllImport("shell32.dll",CharSet=CharSet.Unicode)]static extern IntPtr CommandLineToArgvW(string text,out int count);
 [DllImport("kernel32.dll")]static extern IntPtr LocalFree(IntPtr pointer);
 public static string Read(IntPtr h){var text=new StringBuilder(10000);ReadText(h,13,(IntPtr)text.Capacity,text);return text.ToString();}
 public static string[] Args(string text){int n;IntPtr p=CommandLineToArgvW(text,out n);try{var result=new string[n];for(int i=0;i<n;i++)result[i]=Marshal.PtrToStringUni(Marshal.ReadIntPtr(p,i*IntPtr.Size));return result;}finally{LocalFree(p);}}
}
'@
function Assert($ok,$label){if(!$ok){throw $label};'PASS '+$label}
function Front{[NotesTest]::GetProp($hostWindow,'StackTop')}
function Click($h,$id){[NotesTest]::SendMessage([NotesTest]::GetDlgItem($h,$id),0xF5,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null}
function Check($h,$id,$value){[NotesTest]::SendMessage([NotesTest]::GetDlgItem($h,$id),0xF1,[IntPtr]$value,[IntPtr]::Zero)|Out-Null}
function Checked($h,$id){[NotesTest]::SendMessage([NotesTest]::GetDlgItem($h,$id),0xF0,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32() -eq 1}
function Start-App{Launch;Wait-Until {[NotesTest]::Find($app.Id,'LightNotesGeometry').Count -eq 1 -and (Front) -ne [IntPtr]::Zero} 'startup ready'}
function Settings{Click (Front) 146;return [NotesTest]::Find($app.Id,'LightNotesSettings')[0]}
function StartupValue{$key=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($registryPath);if(!$key){return $null};try{return $key.GetValue('LightNotes',$null)}finally{$key.Dispose()}}
function AssertTopmost($enabled){foreach($h in [NotesTest]::Find($app.Id,'DesktopNotes')){$actual=([StartupCheck]::Style($h,-20).ToInt64() -band 8) -ne 0;if($actual -ne $enabled){throw ("Topmost mismatch: handle=$h visible="+[NotesTest]::IsWindowVisible($h)+" actual=$actual expected=$enabled")};'PASS whole pad topmost band matches preference'}}
function TearPage($h){$b=[NotesTest]::GetDlgItem($h,107);[NotesTest]::SendMessage($b,513,[IntPtr]1,[IntPtr]655370)|Out-Null;[NotesTest]::SendMessage($b,514,[IntPtr]::Zero,[IntPtr]655370)|Out-Null}
try{
 [NotesTest]::SetThreadDpiAwarenessContext([IntPtr](-4))|Out-Null
 Start-App;$general=Settings;$style=[NotesTest]::Find($app.Id,'LightNotesStyle')[0]
 Assert (!(Checked $general 250) -and !(Checked $general 251)) 'new installations start with both options off'
 Assert ([NotesTest]::GetDlgItem($style,250) -eq [IntPtr]::Zero -and [NotesTest]::GetDlgItem($style,251) -eq [IntPtr]::Zero) 'behavior options belong only to settings'
 Click $general 250;Click $general 251;Click $general 225;$general=Settings
 Assert (!(Checked $general 250) -and !(Checked $general 251) -and !(StartupValue)) 'closing without apply discards both drafts'
 Click $general 250;Click $general 251;Click $general 217
 AssertTopmost $true;$registered=StartupValue
 foreach($tip in [NotesTest]::Find($app.Id,'tooltips_class32')){
  Assert ([StartupCheck]::GetWindow($tip,4) -eq $hostWindow) 'tooltip is owned by controller instead of note'
  [StartupCheck]::SetWindowPos($tip,[IntPtr](-2),0,0,0,0,0x13)|Out-Null
 };AssertTopmost $true
 'PASS tooltip demotion never demotes the note'
 $arguments=[StartupCheck]::Args($registered)
 Assert ($arguments.Count -eq 4 -and $arguments[0] -eq $exe -and $arguments[1] -eq '--test-mode' -and $arguments[2] -eq '--data-dir' -and $arguments[3] -eq $DataRoot) 'registered command correctly quotes executable and data path with spaces'
 $key=[Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($registryPath);$key.SetValue('OtherApplication','preserve-me');$key.Dispose()
 $first=Front;Command $first 140
 [NotesTest]::SendText([NotesTest]::GetDlgItem($first,110),12,[IntPtr]::Zero,'开机后保留的未完成事件')|Out-Null;Command $first (110 -bor (0x300 -shl 16))
 Click $first 101;Click $first 101;Click $first 108;AssertTopmost $true
 $current=Front;Command $current 102;AssertTopmost $true
 Assert ([NotesTest]::GetProp($current,'PadFixed') -ne [IntPtr]::Zero) 'position fixed remains independent of topmost'
 $general=Settings;Assert (([StartupCheck]::Style($general,-20).ToInt64() -band 8) -ne 0) 'settings remain accessible above a topmost note'
 if($Screenshot){[NotesTest+Rect]$r=New-Object NotesTest+Rect;[NotesTest]::GetWindowRect($general,[ref]$r)|Out-Null;$bmp=New-Object Drawing.Bitmap ($r.right-$r.left),($r.bottom-$r.top);$g=[Drawing.Graphics]::FromImage($bmp);$dc=$g.GetHdc();[NotesTest]::PrintWindow($general,$dc,0)|Out-Null;$g.ReleaseHdc($dc);$g.Dispose();$bmp.Save($Screenshot);$bmp.Dispose()}
 Click $general 225;TearPage (Front);Wait-Until {[NotesTest]::Find($app.Id,'DesktopNotes').Count -eq 2} 'tear removes one sheet';AssertTopmost $true
 # Exercise the actual registered command, including argument parsing and singleton behavior.
 $second=Start-Process -FilePath $arguments[0] -ArgumentList $registered.Substring($registered.IndexOf('"',1)+1).Trim() -WindowStyle Hidden -PassThru
 Assert ($second.WaitForExit(10000)) 'registered command does not create a duplicate instance'
 Exit-App
 $script:app=Start-Process -FilePath $arguments[0] -ArgumentList $registered.Substring($registered.IndexOf('"',1)+1).Trim() -WindowStyle Hidden -PassThru
 Wait-Until {[NotesTest]::Find($app.Id,'LightNotesGeometry').Count -eq 1} 'registered command relaunches successfully'
 $script:hostWindow=[NotesTest]::Find($app.Id,'LightNotesController')[0];AssertTopmost $true;$general=Settings
 Assert ((Checked $general 250) -and (Checked $general 251)) 'topmost and startup state restored'
 Assert (@([NotesTest]::Find($app.Id,'DesktopNotes')|Where-Object {[StartupCheck]::Read([NotesTest]::GetDlgItem($_,110)) -eq '开机后保留的未完成事件'}).Count -eq 1) 'registered launch restores unfinished events'
 Click $general 250;Click $general 251;Click $general 217;AssertTopmost $false
 Assert (!(StartupValue)) 'disabling startup removes only this app value'
 $key=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($registryPath);Assert ($key.GetValue('OtherApplication') -eq 'preserve-me') 'other startup values remain intact';$key.Dispose()
 $general=Settings;Click $general 250;Click $general 251
 $blocked=Join-Path $DataRoot 'settings.ini.tmp';New-Item -ItemType Directory -Path $blocked|Out-Null
 [StartupCheck]::PostMessage($general,0x111,[IntPtr]217,[IntPtr]::Zero)|Out-Null
 Wait-Until {[NotesTest]::Find($app.Id,'#32770').Count -eq 1} 'save failure reports error'
 Click ([NotesTest]::Find($app.Id,'#32770')[0]) 1;Remove-Item -LiteralPath $blocked
 Assert (!(StartupValue)) 'failed preference save never registers startup';AssertTopmost $false
 Click $general 225;Exit-App;Start-App;$general=Settings
 Assert (!(Checked $general 250) -and !(Checked $general 251)) 'disabled states survive restart and failed edit'
 Click $general 250;Click $general 217
 while((Front) -ne [IntPtr]::Zero){$count=[NotesTest]::Find($app.Id,'DesktopNotes').Count;TearPage (Front);if($count -gt 1){Wait-Until {[NotesTest]::Find($app.Id,'DesktopNotes').Count -eq $count-1} 'remaining sheet torn'}else{Wait-Until {(Front) -eq [IntPtr]::Zero} 'empty pad created'}}
 Wait-Until {[NotesTest]::Find($app.Id,'Static').Count -eq 0} 'falling sheet gone';AssertTopmost $true
 Command ([NotesTest]::Find($app.Id,'DesktopNotes')[0]) 101;AssertTopmost $true
 Exit-App
 # A valid local data path can still exceed the Run command's documented limit.
 $DataRoot=Join-Path $env:TEMP ('LightNotesStartupLong-'+[guid]::NewGuid()+'-'+('x'*85))
 New-Item -ItemType Directory -Path $DataRoot|Out-Null
 $registryPath='Software\LightNotes\TestStartup\'+(Split-Path $DataRoot -Leaf);$registryKeys+= $registryPath
 Start-App;$general=Settings;Click $general 250;Click $general 251
 [StartupCheck]::PostMessage($general,0x111,[IntPtr]217,[IntPtr]::Zero)|Out-Null
 Wait-Until {[NotesTest]::Find($app.Id,'#32770').Count -eq 1} 'invalid startup command reports error'
 $dialog=[NotesTest]::Find($app.Id,'#32770')[0]
 Assert (!(StartupValue)) 'rejected command does not create a startup entry'
 Click $dialog 1;AssertTopmost $false
 Assert (!(Checked $general 250) -and !(Checked $general 251) -and ([IO.File]::ReadAllText((Join-Path $DataRoot 'settings.ini')) -match 'topmost=0')) 'registry failure restores draft, saved preferences and window state'
 Click $general 225;Exit-App;'ALL TOPMOST AND STARTUP CHECKS PASSED'
}finally{
 if($script:app -and !$script:app.HasExited){Command $script:hostWindow 903;$script:app.WaitForExit(10000)|Out-Null}
 foreach($keyPath in $registryKeys){[Microsoft.Win32.Registry]::CurrentUser.DeleteSubKeyTree($keyPath,$false)}
 $key=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Software\Microsoft\Windows\CurrentVersion\Run');$after=$key.GetValue('LightNotes',$null);$key.Dispose()
 Assert ($after -eq $productionBefore) 'isolated tests never modify real logon startup'
}
