param([string]$DataRoot)
$ErrorActionPreference='Stop'
if(!$DataRoot){$DataRoot=Join-Path $env:TEMP ('LightNotesResize-'+[guid]::NewGuid())}
. (Join-Path $PSScriptRoot 'test-support.ps1') -DataRoot $DataRoot
. (Join-Path $PSScriptRoot 'test-backdrop.ps1')
Add-Type -TypeDefinition @'
using System;using System.Threading;using System.Runtime.InteropServices;
public sealed class ResizeSampler:IDisposable{
 [StructLayout(LayoutKind.Sequential)]public struct R{public int left,top,right,bottom;}
 [DllImport("user32.dll")]static extern bool GetWindowRect(IntPtr h,out R r);
 [DllImport("user32.dll")]static extern IntPtr GetDC(IntPtr h);
 [DllImport("user32.dll")]static extern int ReleaseDC(IntPtr h,IntPtr dc);
 [DllImport("user32.dll")]static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
 [DllImport("user32.dll")]static extern bool PrintWindow(IntPtr window,IntPtr dc,uint flags);
 [StructLayout(LayoutKind.Sequential)]struct BitmapInfo{public uint size;public int width,height;public ushort planes,bits;public uint compression,imageSize;public int xppm,yppm;public uint used,important,color;}
 [DllImport("gdi32.dll")]static extern IntPtr CreateCompatibleDC(IntPtr dc);
 [DllImport("gdi32.dll")]static extern IntPtr CreateDIBSection(IntPtr dc,ref BitmapInfo info,uint usage,out IntPtr bits,IntPtr section,uint offset);
 [DllImport("gdi32.dll")]static extern IntPtr SelectObject(IntPtr dc,IntPtr o);
 [DllImport("gdi32.dll")]static extern bool DeleteObject(IntPtr o);
 [DllImport("gdi32.dll")]static extern bool DeleteDC(IntPtr dc);
 [DllImport("gdi32.dll")]static extern bool GdiFlush();
 [DllImport("gdi32.dll")]static extern bool BitBlt(IntPtr dest,int x,int y,int w,int h,IntPtr src,int sx,int sy,uint operation);
 [DllImport("user32.dll")]public static extern uint GetDpiForWindow(IntPtr h);
 [DllImport("dwmapi.dll")]public static extern int DwmFlush();
 IntPtr window;int margin;uint[] reference;Thread thread;volatile bool stop;public int Frames,Incomplete,MaxDelta;public string Error;
 uint[] Read(R r,bool rendered=false){int width=r.right-r.left,height=r.bottom-r.top;var dc=GetDC(IntPtr.Zero);var memory=CreateCompatibleDC(dc);IntPtr bits;
  var info=new BitmapInfo{size=40,width=width,height=-height,planes=1,bits=32};var bitmap=CreateDIBSection(dc,ref info,0,out bits,IntPtr.Zero,0);var old=SelectObject(memory,bitmap);
  try{if(bits==IntPtr.Zero||!(rendered?PrintWindow(window,memory,0):BitBlt(memory,0,0,width,height,dc,r.left,r.top,0x00CC0020)))throw new Exception("Frame copy failed");GdiFlush();var colors=new uint[8];for(int i=0;i<4;i++){
   colors[i]=(uint)Marshal.ReadInt32(bits,((height*(i+1)/5)*width+width-margin)*4);
   colors[4+i]=(uint)Marshal.ReadInt32(bits,((height-margin)*width+width*(i+1)/5)*4);
  }return colors;}finally{SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(IntPtr.Zero,dc);}}
 int Distance(uint[] colors){int delta=0;for(int i=0;i<8;i++)for(int shift=0;shift<24;shift+=8)delta=Math.Max(delta,Math.Abs((int)((colors[i]>>shift)&255)-(int)((reference[i]>>shift)&255)));return delta;}
 public ResizeSampler(IntPtr window,int margin){this.window=window;this.margin=margin;R r;GetWindowRect(window,out r);
  // Establish the expected app-rendered borders before measuring continuous
  // screen frames; do not enroll a transitioning/covered desktop as reference.
  reference=Read(r,true);var deadline=DateTime.UtcNow.AddSeconds(2);
  while(Distance(Read(r))>4){if(DateTime.UtcNow>=deadline)throw new Exception("Resize baseline is covered or not presented");DwmFlush();}
  thread=new Thread(()=>{
  SetThreadDpiAwarenessContext((IntPtr)(-4));try{while(!stop){R before,after;GetWindowRect(window,out before);var colors=Read(before);GetWindowRect(window,out after);
   if(before.left==after.left&&before.top==after.top&&before.right==after.right&&before.bottom==after.bottom){int delta=Distance(colors);
    Frames++;MaxDelta=Math.Max(delta,MaxDelta);if(delta>4)Incomplete++;
   }Thread.Sleep(2);
  }}catch(Exception e){Error=e.Message;}
 });thread.IsBackground=true;thread.Start();}
 public void Dispose(){stop=true;thread.Join(3000);}
}
'@
function Rect($h){[NotesTest+Rect]$r=New-Object NotesTest+Rect;[NotesTest]::GetWindowRect($h,[ref]$r)|Out-Null;return $r}
function Point($x,$y){return [IntPtr](([long]$y*65536)+($x -band 65535))}
function Field($h,$id,$value){[NotesTest]::SendText([NotesTest]::GetDlgItem($h,$id),12,[IntPtr]::Zero,[string]$value)|Out-Null}
$fixture=New-Object NotesBackdrop ([uint32]0x605040)
try{
 $fixture.Show();$fixture.Pump()
 Launch;Wait-Until {[NotesTest]::Find($app.Id,'LightNotesGeometry').Count -eq 1} 'resize panels ready'
 [NotesTest]::SetThreadDpiAwarenessContext([IntPtr](-4))|Out-Null
 $front=[NotesTest]::GetProp($hostWindow,'StackTop');$scale=[ResizeSampler]::GetDpiForWindow($front)/96.0
 Enable-VisualTopmost $front
 Command $front 101
 foreach($material in 0,1,2){
  Command $front (140+$material)
  foreach($hit in 10,11,12,15,17){
   Command $front 146;$general=[NotesTest]::Find($app.Id,'LightNotesSettings')[0];Command $general 246;$geo=[NotesTest]::Find($app.Id,'LightNotesGeometry')[0]
   [NotesTest]::SendMessage([NotesTest]::GetDlgItem($geo,404),0x14E,[IntPtr]4,[IntPtr]::Zero)|Out-Null
   foreach($pair in @(@(400,440),@(401,480),@(402,220),@(403,140))){Field $geo $pair[0] $pair[1]};Command $geo 405
   if($material){Wait-Until {[NotesTest]::GetProp($front,'GlassReady') -ne [IntPtr]::Zero} 'glass prepared';Start-Sleep -Milliseconds 350}
   [NotesTest]::SendMessage($front,0x8002,[IntPtr]1,[IntPtr]::Zero)|Out-Null
   $fixture.Refresh();$fixture.Pump();[ResizeSampler]::DwmFlush()|Out-Null;$r=Rect $front;$width=$r.right-$r.left;$height=$r.bottom-$r.top
   $x=if($hit -eq 10){2}else{$width-2};$y=if($hit -eq 12){2}else{$height-2}
   [NotesTest]::SendMessage($front,0xA1,[IntPtr]$hit,(Point ($r.left+$x) ($r.top+$y)))|Out-Null
   $sampler=New-Object ResizeSampler -ArgumentList @($front,([int](10*$scale)))
   $before=[NotesTest]::GetProp($front,'ResizeFrames').ToInt64()
   try{foreach($offset in @(8,16,24,32,40,48,56,64,72,80,72,64,56,48,40,32,24,16,8,0)){
    $dx=if($hit -eq 10){-$offset}elseif($hit -in 11,17){$offset}else{0}
    $dy=if($hit -eq 12){-$offset}elseif($hit -in 15,17){$offset}else{0}
    $current=Rect $front;$sx=$r.left+$x+$dx;$sy=$r.top+$y+$dy
    [NotesTest]::SendMessage($front,0x200,[IntPtr]1,(Point ($sx-$current.left) ($sy-$current.top)))|Out-Null
    $fixture.Pump();Start-Sleep -Milliseconds 25
   }}finally{$sampler.Dispose()}
   $current=Rect $front;[NotesTest]::SendMessage($front,0x202,[IntPtr]::Zero,(Point ($x) ($y)))|Out-Null
   if($sampler.Error){throw $sampler.Error};if($sampler.Frames -lt 20){throw 'Too few live resize samples'}
   if($sampler.Incomplete){throw "Incomplete border pixels during resize: mode=$material edge=$hit frames=$($sampler.Incomplete)/$($sampler.Frames) maxDelta=$($sampler.MaxDelta)"}
   if([NotesTest]::GetProp($front,'ResizeFrames').ToInt64()-$before -ne 20){throw 'Resize frame commit missing'}
   $final=Rect $front;if($final.left -ne $r.left -or $final.top -ne $r.top -or $final.right -ne $r.right -or $final.bottom -ne $r.bottom){throw 'Back-and-forth resizing drifted'}
   foreach($h in [NotesTest]::Find($app.Id,'DesktopNotes')){$other=Rect $h;if($other.left -ne $final.left -or $other.top -ne $final.top -or $other.right -ne $final.right -or $other.bottom -ne $final.bottom){throw 'Stack size not synchronized on release'}}
   if($material -and [NotesTest]::GetProp($front,'GlassReady') -eq [IntPtr]::Zero){throw 'Resize discarded glass surface'}
   "PASS material=$material edge=$hit 20 resize updates, $($sampler.Frames) live samples, no blank border, max channel delta=$($sampler.MaxDelta)"
   [NotesTest]::SendMessage($front,0x8002,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
   if($material){
    # Also exercise real exclusion-aware desktop capture while dragging; the sampler above
    # intentionally used retained textures so background changes could not mask blank areas.
    $captured=[NotesTest]::GetProp($front,'ResizeGlassCaptures').ToInt64()
    [NotesTest]::SendMessage($front,0xA1,[IntPtr]$hit,(Point ($r.left+$x) ($r.top+$y)))|Out-Null
    foreach($offset in @(12,24,36,48,36,24,12,0)){
     $dx=if($hit -eq 10){-$offset}elseif($hit -in 11,17){$offset}else{0};$dy=if($hit -eq 12){-$offset}elseif($hit -in 15,17){$offset}else{0}
     $current=Rect $front;[NotesTest]::SendMessage($front,0x200,[IntPtr]1,(Point ($r.left+$x+$dx-$current.left) ($r.top+$y+$dy-$current.top)))|Out-Null
    }
    [NotesTest]::SendMessage($front,0x202,[IntPtr]::Zero,(Point $x $y))|Out-Null
    $captures=[NotesTest]::GetProp($front,'ResizeGlassCaptures').ToInt64()-$captured
    if($captures -ne 8){throw "Live glass capture not prepared for each frame: mode=$material edge=$hit captures=$captures"}
    "PASS material=$material edge=$hit all 8 live desktop capture frames prepared before resizing"
   }
  }
 }
 Exit-App;'ALL FOUR-EDGE AND CORNER LIVE RESIZE CHECKS PASSED (no recording)'
}finally{if($script:app -and !$script:app.HasExited){Command $script:hostWindow 903};$fixture.Dispose()}
