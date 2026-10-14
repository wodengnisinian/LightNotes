param([string]$DataRoot)
$ErrorActionPreference='Stop'
if(!$DataRoot){$DataRoot=Join-Path $env:TEMP ('LightNotesMove-'+[guid]::NewGuid())}
. (Join-Path $PSScriptRoot 'test-support.ps1') -DataRoot $DataRoot
. (Join-Path $PSScriptRoot 'test-backdrop.ps1')
Add-Type -TypeDefinition @'
using System;using System.Runtime.InteropServices;
public static class MoveScreen{
 [DllImport("user32.dll")]public static extern uint GetDpiForWindow(IntPtr h);
 [DllImport("dwmapi.dll")]public static extern int DwmFlush();
 [DllImport("user32.dll")]public static extern IntPtr WindowFromPoint(long point);
 [DllImport("user32.dll")]public static extern IntPtr GetAncestor(IntPtr h,uint flags);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetClassName(IntPtr h,System.Text.StringBuilder s,int n);
 public static string RootClass(int x,int y){var root=GetAncestor(WindowFromPoint(((long)y<<32)+(uint)x),2);var name=new System.Text.StringBuilder(128);GetClassName(root,name,128);return name.ToString();}
 [StructLayout(LayoutKind.Sequential)]struct BitmapInfo{public uint size;public int width,height;public ushort planes,bits;public uint compression,imageSize;public int xppm,yppm;public uint used,important,color;}
 [DllImport("user32.dll")]static extern IntPtr GetDC(IntPtr h);
 [DllImport("user32.dll")]static extern int ReleaseDC(IntPtr h,IntPtr dc);
 [DllImport("gdi32.dll")]static extern IntPtr CreateCompatibleDC(IntPtr dc);
 [DllImport("gdi32.dll")]static extern IntPtr CreateDIBSection(IntPtr dc,ref BitmapInfo info,uint usage,out IntPtr bits,IntPtr section,uint offset);
 [DllImport("gdi32.dll")]static extern IntPtr SelectObject(IntPtr dc,IntPtr o);
 [DllImport("gdi32.dll")]static extern bool DeleteObject(IntPtr o);
 [DllImport("gdi32.dll")]static extern bool DeleteDC(IntPtr dc);
 [DllImport("gdi32.dll")]public static extern bool GdiFlush();
 [DllImport("gdi32.dll")]static extern bool BitBlt(IntPtr dest,int x,int y,int w,int h,IntPtr src,int sx,int sy,uint operation);
 public static int[] Read(int x,int y,int width,int height){var screen=GetDC(IntPtr.Zero);var memory=CreateCompatibleDC(screen);IntPtr bits;
  var info=new BitmapInfo{size=40,width=width,height=-height,planes=1,bits=32};var bitmap=CreateDIBSection(screen,ref info,0,out bits,IntPtr.Zero,0);var old=SelectObject(memory,bitmap);
  try{if(bits==IntPtr.Zero||!BitBlt(memory,0,0,width,height,screen,x,y,0x00CC0020))throw new Exception("Screen copy failed");GdiFlush();var pixels=new int[width*height];Marshal.Copy(bits,pixels,0,pixels.Length);return pixels;}
  finally{SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(IntPtr.Zero,screen);}
 }
 public static int Delta(int[] a,int[] b){int maximum=0;for(int i=0;i<a.Length;i++)for(int k=0;k<24;k+=8)maximum=Math.Max(maximum,Math.Abs(((a[i]>>k)&255)-((b[i]>>k)&255)));return maximum;}
}
'@
function Rect($h){[NotesTest+Rect]$r=New-Object NotesTest+Rect;[NotesTest]::GetWindowRect($h,[ref]$r)|Out-Null;return $r}
function Point($x,$y){[IntPtr](([long]$y*65536)+($x -band 65535))}
function Field($h,$id,$value){[NotesTest]::SendText([NotesTest]::GetDlgItem($h,$id),12,[IntPtr]::Zero,[string]$value)|Out-Null}
function Configure($front,$width,$height,$x,$y){Command $front 146;$settings=[NotesTest]::Find($app.Id,'LightNotesSettings')[0];Command $settings 246;$geo=[NotesTest]::Find($app.Id,'LightNotesGeometry')[0];[NotesTest]::SendMessage([NotesTest]::GetDlgItem($geo,404),0x14E,[IntPtr]4,[IntPtr]::Zero)|Out-Null;foreach($pair in @(@(400,$width),@(401,$height),@(402,$x),@(403,$y))){Field $geo $pair[0] $pair[1]};Command $geo 405}
function Settle{$fixture.Refresh();$fixture.Pump();[MoveScreen]::GdiFlush()|Out-Null;[MoveScreen]::DwmFlush()|Out-Null}
function Background($x,$y){MoveScreenRead $x $y}
function MoveScreenRead($x,$y){[MoveScreen]::Read($x,$y,24,24)}
function AssertVacated($x,$y,$reference,$label){
 # This check detects persistent old-position pixels; the separate flicker test
 # samples transition frames. GetDC can expose the preceding compositor frame.
 $deadline=[DateTime]::UtcNow.AddMilliseconds(100);$frames=0
 do{Settle;$actual=Background $x $y;$delta=[MoveScreen]::Delta($reference,$actual);if($delta -le 4){if($frames){"PASS $label settled after $frames compositor frame(s)"};return};$frames++}while([DateTime]::UtcNow -lt $deadline)
 $root=[MoveScreen]::RootClass($x+12,$y+12)
 throw "Old-position residue: $label max channel delta=$delta desktop root=$root reference=$($reference[0]) actual=$($actual[0])"
}
$fixture=New-Object NotesBackdrop ([uint32]0x684a2b)
try{
 $fixture.Show();$fixture.Pump();Launch
 Wait-Until {[NotesTest]::Find($app.Id,'LightNotesGeometry').Count -eq 1} 'move panels ready'
 [NotesTest]::SetThreadDpiAwarenessContext([IntPtr](-4))|Out-Null
 $front=[NotesTest]::GetProp($hostWindow,'StackTop');1..6|ForEach-Object{Command $front 101}
 Enable-VisualTopmost $front
 $scale=[MoveScreen]::GetDpiForWindow($front)/96.0
 foreach($material in 0,1,2){
  Command $front (140+$material);Configure $front 410 430 150 130
  if($material){Wait-Until {[NotesTest]::GetProp($front,'GlassReady') -ne [IntPtr]::Zero} 'glass ready'}
  # Sample the real underlay, then exercise movement while keeping the mouse captured.
  Command $hostWindow 902;Settle;$r=Rect $front
  $bx=$r.left+30;$by=$r.top+120;$original=Background $bx $by
  Command $hostWindow 901;Settle
  [NotesTest]::SendMessage($front,0x8002,[IntPtr]1,[IntPtr]::Zero)|Out-Null;Settle
  $hit=[MoveScreen]::WindowFromPoint(([long]($by+12)*4294967296)+$bx+12)
  if([MoveScreen]::GetAncestor($hit,2) -ne $front -or [MoveScreen]::Delta($original,(Background $bx $by)) -le 4){throw 'The note must actually cover the sampled background before dragging'}
  $grabX=[int](120*$scale);$grabY=[int](39*$scale)
  [NotesTest]::SendMessage($front,0xA1,[IntPtr]2,(Point ($r.left+$grabX) ($r.top+$grabY)))|Out-Null
  $steps=@(@(80,0),@(160,40),@(240,80),@(160,40),@(80,0),@(0,0),@(0,100),@(0,180),@(100,180),@(180,180),@(100,100),@(0,0))
  foreach($step in $steps){$old=Rect $front;$sx=$r.left+$grabX+$step[0];$sy=$r.top+$grabY+$step[1];[NotesTest]::SendMessage($front,0x200,[IntPtr]1,(Point ($sx-$old.left) ($sy-$old.top)))|Out-Null;Settle
   $current=Rect $front
   if(($bx+24 -lt $current.left -or $by+24 -lt $current.top) -and $step[0]+$step[1] -gt 0){AssertVacated $bx $by $original "material=$material captured drag"}
   if(@([NotesTest]::Find($app.Id,'DesktopNotes')|Where-Object{[NotesTest]::IsWindowVisible($_)}).Count -ne 1){throw 'Hidden page exposed during move'}
  }
  [NotesTest]::SendMessage($front,0x202,[IntPtr]::Zero,(Point $grabX $grabY))|Out-Null
  [NotesTest]::SendMessage($front,0x8002,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
  # Shrink from a larger sheet, then move away from the formerly occupied lower region.
  Configure $front 440 530 150 130;Command $hostWindow 902;Settle;$large=Rect $front;$px=$large.left+30;$py=$large.bottom-35;$previous=Background $px $py;Command $hostWindow 901;Settle
  [NotesTest]::SendMessage($front,0x8002,[IntPtr]1,[IntPtr]::Zero)|Out-Null;Settle
  [NotesTest]::SendMessage($front,0xA1,[IntPtr]15,(Point ($large.right-3) ($large.bottom-3)))|Out-Null
  [NotesTest]::SendMessage($front,0x200,[IntPtr]1,(Point ($large.right-$large.left-3) ($large.bottom-$large.top-123)))|Out-Null
  AssertVacated $px $py $previous "material=$material shrink while captured"
  [NotesTest]::SendMessage($front,0x202,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
  $small=Rect $front;[NotesTest]::SendMessage($front,0xA1,[IntPtr]2,(Point ($small.left+$grabX) ($small.top+$grabY)))|Out-Null
  [NotesTest]::SendMessage($front,0x200,[IntPtr]1,(Point ($grabX+130) ($grabY-60)))|Out-Null
  AssertVacated $px $py $previous "material=$material move after shrink"
  [NotesTest]::SendMessage($front,0x202,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
  $final=Rect $front;foreach($page in [NotesTest]::Find($app.Id,'DesktopNotes')){$other=Rect $page;if($other.left -ne $final.left -or $other.top -ne $final.top -or $other.right -ne $final.right -or $other.bottom -ne $final.bottom){throw 'Whole-pad layout changed on release'}}
  [NotesTest]::SendMessage($front,0x8002,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
  "PASS material=$material captured movement, shrink, move-after-shrink leave no pixels at old positions; seven pages stay hidden/aligned"
 }
 Exit-App;'ALL MOVEMENT AND VACATED-BACKGROUND CHECKS PASSED (RAM-only, no recording)'
}finally{if($script:app -and !$script:app.HasExited){Command $script:hostWindow 903};$fixture.Dispose()}
