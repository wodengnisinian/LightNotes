param([string]$DataRoot)
$ErrorActionPreference='Stop'
if(!$DataRoot){$DataRoot=Join-Path $env:TEMP ('LightNotesGeometry-'+[guid]::NewGuid())}
. (Join-Path $PSScriptRoot 'test-support.ps1') -DataRoot $DataRoot
Add-Type -TypeDefinition @"
using System;using System.Runtime.InteropServices;
public static class GeometryCheck{[StructLayout(LayoutKind.Sequential)]public struct Box{public int left,top,right,bottom;}[DllImport("user32.dll")]public static extern uint GetDpiForWindow(IntPtr h);[DllImport("user32.dll")]public static extern int GetWindowRgn(IntPtr h,IntPtr region);[DllImport("gdi32.dll")]public static extern IntPtr CreateRectRgn(int l,int t,int r,int b);[DllImport("gdi32.dll")]public static extern int GetRgnBox(IntPtr region,out Box box);[DllImport("gdi32.dll")]public static extern bool DeleteObject(IntPtr h);}
"@
function Rect([IntPtr]$h){[NotesTest+Rect]$r=New-Object NotesTest+Rect;[NotesTest]::GetWindowRect($h,[ref]$r)|Out-Null;return $r}
function SetField($h,$id,$value){[NotesTest]::SendText([NotesTest]::GetDlgItem($h,$id),12,[IntPtr]::Zero,[string]$value)|Out-Null}
function SameRect($a,$b){return $a.left -eq $b.left -and $a.top -eq $b.top -and $a.right -eq $b.right -and $a.bottom -eq $b.bottom}
function AssertStack($expected){foreach($h in [NotesTest]::Find($app.Id,'DesktopNotes')){if(!(SameRect (Rect $h) $expected)){throw 'Stack geometry differs'}};'PASS whole pad has identical geometry'}
function TearPage($h){$b=[NotesTest]::GetDlgItem($h,107);[NotesTest]::SendMessage($b,513,[IntPtr]1,[IntPtr]655370)|Out-Null;[NotesTest]::SendMessage($b,514,[IntPtr]::Zero,[IntPtr]655370)|Out-Null}
function Frame($h){$r=Rect $h;$bmp=New-Object Drawing.Bitmap ($r.right-$r.left),($r.bottom-$r.top);$g=[Drawing.Graphics]::FromImage($bmp);$dc=$g.GetHdc();[NotesTest]::PrintWindow($h,$dc,0)|Out-Null;$g.ReleaseHdc($dc);$g.Dispose();return $bmp}
function Pixel($bmp,$x,$y){return $bmp.GetPixel([int]($x*$scale),[int]($y*$scale))}
try{
 Launch
 [NotesTest]::SetThreadDpiAwarenessContext([IntPtr](-4))|Out-Null
 $first=[NotesTest]::GetProp($hostWindow,'StackTop');$scale=[GeometryCheck]::GetDpiForWindow($first)/96.0
 Command $first 101;Command $first 101;Command $first 146
 $settings=[NotesTest]::Find($app.Id,'LightNotesSettings')[0];Command $settings 246
 $geo=[NotesTest]::Find($app.Id,'LightNotesGeometry')[0]
 if(![NotesTest]::IsWindowVisible($geo) -or [NotesTest]::IsWindowVisible($settings)){throw 'Position panel entry failed'};'PASS settings opens dedicated size and position panel'
 [NotesTest]::SendMessage([NotesTest]::GetDlgItem($geo,404),0x14E,[IntPtr]4,[IntPtr]::Zero)|Out-Null
 foreach($item in @(@(400,480),@(401,540),@(402,180),@(403,120))){SetField $geo $item[0] $item[1]}
 Command $geo 405;$r=Rect $first
 if($r.left -ne 180 -or $r.top -ne 120 -or ($r.right-$r.left) -ne [Math]::Round(480*$scale) -or ($r.bottom-$r.top) -ne [Math]::Round(540*$scale)){throw 'Numeric geometry not applied'}
 'PASS numeric dimensions and screen coordinates';AssertStack $r
 # Exercise the actual caption hit-test, then captured mouse movement and release.
 $sx=$r.left+[int](120*$scale);$sy=$r.top+[int](39*$scale);$point=([long]$sy*65536)+$sx
 if([NotesTest]::SendMessage($first,0x84,[IntPtr]::Zero,[IntPtr]$point).ToInt32() -ne 2){throw 'Header is not draggable'}
 [NotesTest]::SendMessage($first,0xA1,[IntPtr]2,[IntPtr]$point)|Out-Null
 $point=([long]([int](39*$scale)+30)*65536)+[int](120*$scale)+50
 [NotesTest]::SendMessage($first,0x200,[IntPtr]1,[IntPtr]$point)|Out-Null;[NotesTest]::SendMessage($first,0x202,[IntPtr]::Zero,[IntPtr]$point)|Out-Null
 $moved=Rect $first;if($moved.left -ne 230 -or $moved.top -ne 150){throw 'Caption drag failed'};'PASS drag header freely and save';AssertStack $moved
 $point=([long]($moved.bottom-2)*65536)+$moved.right-2;[NotesTest]::SendMessage($first,0xA1,[IntPtr]17,[IntPtr]$point)|Out-Null
 $point=([long]($moved.bottom-$moved.top+28)*65536)+$moved.right-$moved.left+38
 [NotesTest]::SendMessage($first,0x200,[IntPtr]1,[IntPtr]$point)|Out-Null;[NotesTest]::SendMessage($first,0x202,[IntPtr]::Zero,[IntPtr]$point)|Out-Null
 $resized=Rect $first;if(($resized.right-$resized.left) -le ($moved.right-$moved.left) -or ($resized.bottom-$resized.top) -le ($moved.bottom-$moved.top)){throw 'Edge resize failed'};'PASS edge resize updates dimensions';AssertStack $resized
 Command $first 108;$next=[NotesTest]::GetProp($hostWindow,'StackTop');AssertStack $resized
 TearPage $next;Wait-Until {[NotesTest]::Find($app.Id,'DesktopNotes').Count -eq 2} 'tear completes after geometry change';AssertStack $resized
 Exit-App;Launch;$first=[NotesTest]::GetProp($hostWindow,'StackTop');AssertStack $resized;'PASS restart restores size and free position'
 # Paper mode must remain uniformly colored at the seam and both former thickness edges.
 Command $first 140
 foreach($color in 2,3){Command $first 147;$style=[NotesTest]::Find($app.Id,'LightNotesStyle')[0];Command $style (300+$color);Command $style 217;$bmp=Frame $first
  try{$base=Pixel $bmp 40 80;$points=@(@(40,[int](48*$scale)),@(($bmp.Width-[int](4*$scale)),[int](150*$scale)),@(40,($bmp.Height-[int](4*$scale))))
   foreach($point in $points){$pixel=$bmp.GetPixel([int]$point[0],[int]$point[1]);if($pixel.ToArgb() -ne $base.ToArgb()){throw 'Shadow or side/bottom thickness remains on flat paper'}}
  }finally{$bmp.Dispose()}
 }
 'PASS actual painted seam and former side/bottom edges match flat paper color'
 # Empty pad remains a header when its dimensions are changed.
 while([NotesTest]::GetProp($hostWindow,'StackTop') -ne [IntPtr]::Zero){$top=[NotesTest]::GetProp($hostWindow,'StackTop');$before=[NotesTest]::Find($app.Id,'DesktopNotes').Count;TearPage $top;if($before -gt 1){Wait-Until {[NotesTest]::Find($app.Id,'DesktopNotes').Count -eq $before-1} 'tear remaining page'}else{Wait-Until {[NotesTest]::GetProp($hostWindow,'StackTop') -eq [IntPtr]::Zero} 'empty pad'}}
 Wait-Until {![NotesTest]::IsWindowVisible([NotesTest]::GetDlgItem($top,107))} 'empty pad hides body buttons'
 Wait-Until {[NotesTest]::Find($app.Id,'Static').Count -eq 0} 'last falling page disappears';$settings=[NotesTest]::Find($app.Id,'LightNotesSettings')[0];$geo=[NotesTest]::Find($app.Id,'LightNotesGeometry')[0];Command $top 146;Command $settings 246;SetField $geo 400 430;SetField $geo 401 470;Command $geo 405
 $region=[GeometryCheck]::CreateRectRgn(0,0,0,0);[GeometryCheck+Box]$box=New-Object GeometryCheck+Box
 try{[GeometryCheck]::GetWindowRgn($top,$region)|Out-Null;[GeometryCheck]::GetRgnBox($region,[ref]$box)|Out-Null;if($box.bottom -gt [Math]::Round(46*$scale)){throw 'Empty pad body reappeared after resizing'}}finally{[GeometryCheck]::DeleteObject($region)|Out-Null}
 'PASS resizing empty pad preserves header-only shape';Exit-App
 'ALL GEOMETRY AND FLAT PAPER CHECKS PASSED'
}finally{if($script:app -and !$script:app.HasExited){Command $script:hostWindow 903}}
