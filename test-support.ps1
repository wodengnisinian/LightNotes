param([string]$DataRoot, [string]$Screenshot)
$ErrorActionPreference='Stop'
if (!$DataRoot) { $DataRoot=Join-Path $env:TEMP ('LightNotesTest-'+[guid]::NewGuid()) }
New-Item -ItemType Directory -Force -Path $DataRoot | Out-Null
$exe=Join-Path $PSScriptRoot 'LightNotes.exe'
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class NotesTest {
 public delegate bool Callback(IntPtr h,IntPtr l);
 [DllImport("user32.dll")] static extern bool EnumWindows(Callback c,IntPtr l);
 [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")] public static extern IntPtr SendText(IntPtr h,uint m,IntPtr w,string l);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(long point);
 [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr h,uint flags);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint flags);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr GetProp(IntPtr h,string key);
 [StructLayout(LayoutKind.Sequential)]public struct Rect { public int left,top,right,bottom; }
 public static IntPtr[] Find(int pid,string cls){var items=new List<IntPtr>();EnumWindows((h,l)=>{uint p;GetWindowThreadProcessId(h,out p);var s=new StringBuilder(128);GetClassName(h,s,128);if(p==pid&&s.ToString()==cls)items.Add(h);return true;},IntPtr.Zero);return items.ToArray();}
 public static string Text(IntPtr h){var s=new StringBuilder(10000);GetWindowText(h,s,s.Capacity);return s.ToString();}
}
'@
function Wait-Until([scriptblock]$Check,[string]$Label) {
 $deadline=[DateTime]::UtcNow.AddSeconds(25)
 do { if (& $Check) { Write-Output "PASS $Label"; return }; Start-Sleep -Milliseconds 100 } while ([DateTime]::UtcNow -lt $deadline)
 throw "FAIL $Label"
}
function Command($h,[int]$id) { [NotesTest]::SendMessage($h,0x111,[IntPtr]$id,[IntPtr]::Zero)|Out-Null }
function Launch {
 $script:app=Start-Process -FilePath $exe -ArgumentList @('--test-mode','--data-dir',('"'+$DataRoot+'"')) -WindowStyle Hidden -PassThru
 Wait-Until { [NotesTest]::Find($script:app.Id,'DesktopNotes').Count -gt 0 } 'native window created'
 $script:note=[NotesTest]::Find($script:app.Id,'DesktopNotes')[0]
 $script:hostWindow=[NotesTest]::Find($script:app.Id,'LightNotesController')[0]
 Wait-Until { [NotesTest]::GetProp($script:hostWindow,'AppReady') -eq [IntPtr]1 } 'application initialization complete'
 Command $script:hostWindow 901
}
function Exit-App {
 Command $script:hostWindow 903
 if (!$script:app.WaitForExit(10000)) { throw 'FAIL graceful exit' }
}
function Enable-VisualTopmost([IntPtr]$Window) {
 Command $Window 146
 $panel=[NotesTest]::Find($script:app.Id,'LightNotesSettings')[0]
 [NotesTest]::SendMessage([NotesTest]::GetDlgItem($panel,250),0xF1,[IntPtr]1,[IntPtr]::Zero)|Out-Null
 Command $panel 217
 Wait-Until { ![NotesTest]::IsWindowVisible($panel) } 'isolated visual subject presented above host windows'
 [NotesTest+Rect]$r=New-Object NotesTest+Rect
 [NotesTest]::GetWindowRect($Window,[ref]$r)|Out-Null
 $x=[int](($r.left+$r.right)/2);$y=[int](($r.top+$r.bottom)/2)
 Wait-Until { [NotesTest]::GetAncestor([NotesTest]::WindowFromPoint(([long]$y*4294967296)+$x),2) -eq $Window } 'visual subject actually covers the sampled desktop'
}
