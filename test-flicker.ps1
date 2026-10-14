param([string]$DataRoot)
$ErrorActionPreference='Stop'
if(!$DataRoot){$DataRoot=Join-Path $env:TEMP ('LightNotesFlicker-'+[guid]::NewGuid())}
. (Join-Path $PSScriptRoot 'test-support.ps1') -DataRoot $DataRoot
Add-Type -TypeDefinition @'
using System;using System.Runtime.InteropServices;using System.Threading;
public sealed class SwitchSampler:IDisposable{
 [DllImport("dwmapi.dll")]public static extern int DwmFlush();
 [DllImport("user32.dll")]public static extern uint GetDpiForWindow(IntPtr h);
 [DllImport("user32.dll")]static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
 [StructLayout(LayoutKind.Sequential)]struct BitmapInfo{public uint size;public int width,height;public ushort planes,bits;public uint compression,imageSize;public int xppm,yppm;public uint used,important,color;}
 [DllImport("user32.dll")]static extern IntPtr GetDC(IntPtr h);
 [DllImport("user32.dll")]static extern int ReleaseDC(IntPtr h,IntPtr dc);
 [DllImport("gdi32.dll")]static extern IntPtr CreateCompatibleDC(IntPtr dc);
 [DllImport("gdi32.dll")]static extern IntPtr CreateDIBSection(IntPtr dc,ref BitmapInfo info,uint usage,out IntPtr bits,IntPtr section,uint offset);
 [DllImport("gdi32.dll")]static extern IntPtr SelectObject(IntPtr dc,IntPtr o);
 [DllImport("gdi32.dll")]static extern bool DeleteObject(IntPtr o);
 [DllImport("gdi32.dll")]static extern bool DeleteDC(IntPtr dc);
 [DllImport("gdi32.dll")]static extern bool GdiFlush();
 [DllImport("gdi32.dll")]static extern bool BitBlt(IntPtr dest,int x,int y,int w,int h,IntPtr src,int sx,int sy,uint operation);
 public int Frames,Incomplete,SmallDeltaFrames,MaxDelta; public string Error,FirstDifference;volatile bool stop;Thread worker;int x,y,width,height;int[] expected;
 int[] Read(){
  var screen=GetDC(IntPtr.Zero);var memory=CreateCompatibleDC(screen);IntPtr bits;
  var info=new BitmapInfo{size=40,width=width,height=-height,planes=1,bits=32};var bitmap=CreateDIBSection(screen,ref info,0,out bits,IntPtr.Zero,0);var old=SelectObject(memory,bitmap);
  try{if(bits==IntPtr.Zero||!BitBlt(memory,0,0,width,height,screen,x,y,0x00CC0020))throw new Exception("Screen copy failed");GdiFlush();var pixels=new int[width*height];Marshal.Copy(bits,pixels,0,pixels.Length);return pixels;}
  finally{SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(IntPtr.Zero,screen);}
 }
 public SwitchSampler(int x,int y,int width,int height){this.x=x;this.y=y;this.width=width;this.height=height;expected=Read();worker=new Thread(()=>{
  SetThreadDpiAwarenessContext((IntPtr)(-4));
  try{while(!stop){var actual=Read();int delta=0;for(int i=0;i<actual.Length;i++)for(int shift=0;shift<24;shift+=8){int value=Math.Abs(((actual[i]>>shift)&255)-((expected[i]>>shift)&255));if(value>delta)delta=value;
   if(value>4&&FirstDifference==null)FirstDifference="x="+(i%width)+" y="+(i/width)+" expected="+(expected[i]&0xFFFFFF).ToString("X6")+" actual="+(actual[i]&0xFFFFFF).ToString("X6");}
   Interlocked.Increment(ref Frames);MaxDelta=Math.Max(MaxDelta,delta);if(delta>4)Interlocked.Increment(ref Incomplete);else if(delta>0)Interlocked.Increment(ref SmallDeltaFrames);Thread.Sleep(2);}}
  catch(Exception e){Error=e.Message;}
 });worker.IsBackground=true;worker.Start();}
 public void Dispose(){stop=true;worker.Join(3000);}
}
'@
try{
 Launch;Wait-Until {[NotesTest]::Find($app.Id,'LightNotesGeometry').Count -eq 1} 'startup complete'
 [NotesTest]::SetThreadDpiAwarenessContext([IntPtr](-4))|Out-Null
 $first=[NotesTest]::GetProp($hostWindow,'StackTop');Command $first 101;Command $first 101
 Enable-VisualTopmost $first
 foreach($material in 0,1,2){
  $front=[NotesTest]::GetProp($hostWindow,'StackTop');Command $front (140+$material)
  if($material){Wait-Until {[NotesTest]::GetProp($front,'GlassReady') -ne [IntPtr]::Zero} 'glass source ready'}
  # Warm every target surface, then freeze capture for a short screen-sampling pass.
  1..3|ForEach-Object{Command ([NotesTest]::GetProp($hostWindow,'StackTop')) 108}
  $front=[NotesTest]::GetProp($hostWindow,'StackTop');[NotesTest]::SendMessage($front,0x8002,[IntPtr]1,[IntPtr]::Zero)|Out-Null
  [SwitchSampler]::DwmFlush()|Out-Null
  $pages=[NotesTest]::Find($app.Id,'DesktopNotes');$layouts=@{};foreach($h in $pages){$layouts[$h.ToInt64()]=[NotesTest]::GetProp($h,'LayoutPasses').ToInt64()}
  $prepared=[NotesTest]::GetProp($hostWindow,'SwitchPrepared').ToInt64()
  [NotesTest+Rect]$r=New-Object NotesTest+Rect;[NotesTest]::GetWindowRect($front,[ref]$r)|Out-Null;$scale=[SwitchSampler]::GetDpiForWindow($front)/96.0
  # Below the editor's caret: compare the actual background AND ruled lines continuously.
  $sampler=New-Object SwitchSampler ($r.left+[int](45*$scale)),($r.top+[int](260*$scale)),([int](220*$scale)),([int](70*$scale))
  try{1..30|ForEach-Object{Command ([NotesTest]::GetProp($hostWindow,'StackTop')) $(if($_%2){108}else{109})}}
  finally{$sampler.Dispose()}
  if($sampler.Error){throw $sampler.Error};if($sampler.Frames -lt 30){throw 'Insufficient continuous screen samples'}
  if($sampler.Incomplete){throw "Screen contained a background/underline gap: mode=$material incomplete=$($sampler.Incomplete)/$($sampler.Frames) $($sampler.FirstDifference)"}
  foreach($h in $pages){if([NotesTest]::GetProp($h,'LayoutPasses').ToInt64() -ne $layouts[$h.ToInt64()]){throw 'Unchanged page was laid out again during switch'}}
  if([NotesTest]::GetProp($hostWindow,'SwitchPrepared').ToInt64()-$prepared -ne 30){throw 'A page was revealed without the covered prepare step'}
  $visible=@($pages|Where-Object{[NotesTest]::IsWindowVisible($_)});if($visible.Count -ne 1){throw 'More than one final page visible'}
  "PASS mode=$material 30 real switches, $($sampler.Frames) RAM-only screen samples, no background or underline gap, no redundant layout; max channel delta=$($sampler.MaxDelta), small delta frames=$($sampler.SmallDeltaFrames)"
  [NotesTest]::SendMessage($visible[0],0x8002,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
 }
 Exit-App;'ALL CONTINUOUS SWITCH CHECKS PASSED (no recording)'
}finally{if($script:app -and !$script:app.HasExited){Command $script:hostWindow 903}}
