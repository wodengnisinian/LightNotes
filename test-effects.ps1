param([string]$DataRoot,[string]$OutputDirectory,[switch]$SkipGlass)
$ErrorActionPreference='Stop'
if(!$DataRoot){$DataRoot=Join-Path $env:TEMP ('LightNotesEffects-'+[guid]::NewGuid())}
if(!$OutputDirectory){$OutputDirectory=Join-Path $DataRoot 'captures'}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
. (Join-Path $PSScriptRoot 'test-support.ps1') -DataRoot $DataRoot
Add-Type -TypeDefinition @'
using System;using System.Runtime.InteropServices;
public static class EffectsFixture {
 public delegate IntPtr Procedure(IntPtr h,uint m,IntPtr w,IntPtr l);
 [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)]public struct WC{public uint style;public IntPtr proc;public int extra,windowExtra;public IntPtr instance,icon,cursor,brush;public string menu,cls;}
 [StructLayout(LayoutKind.Sequential)]public struct R{public int left,top,right,bottom;}
 [StructLayout(LayoutKind.Sequential)]public struct P{public int x,y;}
 [StructLayout(LayoutKind.Sequential)]public struct MI{public uint size;public R monitor,work;public uint flags;}
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)]static extern IntPtr GetModuleHandle(string name);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern ushort RegisterClass(ref WC wc);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern IntPtr CreateWindowEx(uint ex,string cls,string text,uint style,int x,int y,int w,int h,IntPtr parent,IntPtr menu,IntPtr instance,IntPtr data);
 [DllImport("user32.dll")]static extern IntPtr DefWindowProc(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr h,int show);
 [DllImport("user32.dll")]public static extern bool DestroyWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int w,int height,uint flags);
 [DllImport("user32.dll")]static extern IntPtr GetDC(IntPtr h);
 [DllImport("user32.dll")]static extern int ReleaseDC(IntPtr h,IntPtr dc);
 [DllImport("user32.dll")]static extern int FillRect(IntPtr dc,ref R r,IntPtr brush);
 [DllImport("gdi32.dll")]static extern IntPtr CreateSolidBrush(uint color);
 [DllImport("gdi32.dll")]static extern bool DeleteObject(IntPtr p); [DllImport("gdi32.dll")]static extern bool GdiFlush();
 [DllImport("gdi32.dll")]public static extern bool BitBlt(IntPtr dst,int x,int y,int w,int h,IntPtr src,int sx,int sy,uint operation);
 [DllImport("user32.dll")]static extern IntPtr MonitorFromPoint(P p,uint flags);
 [DllImport("user32.dll")]static extern bool GetMonitorInfo(IntPtr monitor,ref MI info);
 [DllImport("user32.dll")]public static extern uint GetDpiForWindow(IntPtr h);
 static Procedure callback=(h,m,w,l)=>DefWindowProc(h,m,w,l);
 public static R Work(){var i=new MI();i.size=(uint)Marshal.SizeOf(typeof(MI));GetMonitorInfo(MonitorFromPoint(new P(),1),ref i);return i.work;}
 public static IntPtr Create(){var r=Work();var wc=new WC();wc.cls="LightNotesGlassFixture"+System.Diagnostics.Process.GetCurrentProcess().Id;wc.proc=Marshal.GetFunctionPointerForDelegate(callback);wc.instance=GetModuleHandle(null);RegisterClass(ref wc);var h=CreateWindowEx(0x80,wc.cls,"Glass Test",0x80000000,r.left,r.top,r.right-r.left,r.bottom-r.top,IntPtr.Zero,IntPtr.Zero,wc.instance,IntPtr.Zero);ShowWindow(h,4);ShowWindow(h,4);SetWindowPos(h,new IntPtr(-1),0,0,0,0,0x13);Draw(h,0);return h;}
 public static void Draw(IntPtr h,int theme){var r=Work();var dc=GetDC(h);var a=CreateSolidBrush(theme==0?0x00543618u:0x002B6FE9u);var b=CreateSolidBrush(theme==0?0x00FCEAD6u:0x00C6B089u);for(int y=0;y<r.bottom-r.top;y+=16)for(int x=0;x<r.right-r.left;x+=16){var cell=new R{left=x,top=y,right=x+16,bottom=y+16};FillRect(dc,ref cell,((x/16+y/16)%2==0)?a:b);}GdiFlush();DeleteObject(a);DeleteObject(b);ReleaseDC(h,dc);}
 public static bool ScreenCopy(IntPtr dc,int x,int y,int w,int h){var screen=GetDC(IntPtr.Zero);bool ok=BitBlt(dc,0,0,w,h,screen,x,y,0x00CC0020);ReleaseDC(IntPtr.Zero,screen);return ok;}
}
'@
function Save-Window($h,[string]$name) {
 [NotesTest+Rect]$r=New-Object NotesTest+Rect;[NotesTest]::GetWindowRect($h,[ref]$r)|Out-Null
 $bitmap=New-Object Drawing.Bitmap ($r.right-$r.left),($r.bottom-$r.top)
 $g=[Drawing.Graphics]::FromImage($bitmap);$dc=$g.GetHdc();$ok=[NotesTest]::PrintWindow($h,$dc,0);$g.ReleaseHdc($dc);$g.Dispose()
 if(!$ok){$bitmap.Dispose();throw 'Window capture failed'}
 $bitmap.Save((Join-Path $OutputDirectory $name),[Drawing.Imaging.ImageFormat]::Png);return $bitmap
}
function Capture-Glass([string]$name) {
 [NotesTest]::SendMessage($note,0x8002,[IntPtr]1,[IntPtr]::Zero)|Out-Null
 try { return Save-Window $note $name } finally { [NotesTest]::SendMessage($note,0x8002,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null }
}
function Mean-Red($bitmap) { $sum=0;$count=0;for($y=300;$y -lt 460;$y+=4){for($x=35;$x -lt 330;$x+=4){$sum+=$bitmap.GetPixel($x,$y).R;$count++}};return $sum/$count }
function Detail($bitmap) { $sum=0;$count=0;for($y=300;$y -lt 460;$y+=4){for($x=35;$x -lt 330;$x+=4){$sum+=[Math]::Abs($bitmap.GetPixel($x,$y).R-$bitmap.GetPixel($x+4,$y).R);$count++}};return $sum/$count }
function Apply([int]$corner,[int]$material) {
 [NotesTest]::SendMessage([NotesTest]::GetDlgItem($settings,210),0x14E,[IntPtr]$corner,[IntPtr]::Zero)|Out-Null
 [NotesTest]::SendMessage([NotesTest]::GetDlgItem($appearance,211),0x14E,[IntPtr]$material,[IntPtr]::Zero)|Out-Null
 Command $settings 217;Command $appearance 217
}
function Dock-Check([int]$corner) {
 Apply $corner 0
 [NotesTest+Rect]$r=New-Object NotesTest+Rect;[NotesTest]::GetWindowRect($note,[ref]$r)|Out-Null;$work=[EffectsFixture]::Work();$margin=[int](16*[EffectsFixture]::GetDpiForWindow($note)/96)
 $x=if($corner -eq 1 -or $corner -eq 3){$work.right-$margin-($r.right-$r.left)}else{$work.left+$margin};$y=if($corner -ge 2){$work.bottom-$margin-($r.bottom-$r.top)}else{$work.top+$margin}
 if($r.left -ne $x -or $r.top -ne $y){throw "FAIL corner ${corner}: got $($r.left),$($r.top), expected $x,$y"};"PASS fixed corner $corner"
}
$previousDpi=[NotesTest]::SetThreadDpiAwarenessContext([IntPtr](-4));$fixture=[IntPtr]::Zero
try {
 $script:app=Start-Process -FilePath $exe -ArgumentList @('--test-mode','--data-dir',('"'+$DataRoot+'"')) -WindowStyle Hidden -PassThru
 Wait-Until { ([NotesTest]::Find($app.Id,'LightNotesSettings').Count -eq 1 -and [NotesTest]::Find($app.Id,'LightNotesStyle').Count -eq 1) } 'settings exists on launch'
 $settings=[NotesTest]::Find($app.Id,'LightNotesSettings')[0];$appearance=[NotesTest]::Find($app.Id,'LightNotesStyle')[0];$note=[NotesTest]::Find($app.Id,'DesktopNotes')[0];$hostWindow=[NotesTest]::Find($app.Id,'LightNotesController')[0]
 Enable-VisualTopmost $note
 if(![NotesTest]::IsWindowVisible($note) -or [NotesTest]::IsWindowVisible($settings)){throw 'FAIL launch must show notes and hide settings'};'PASS launch opens note page directly'
 [EffectsFixture]::ShowWindow($settings,1)|Out-Null;$image=Save-Window $settings 'settings-preview.png';$image.Dispose()
 foreach($corner in 0..3){Dock-Check $corner}
 [NotesTest]::SendMessage([NotesTest]::GetDlgItem($appearance,212),0x405,[IntPtr]1,[IntPtr]80)|Out-Null
 [NotesTest]::SendMessage([NotesTest]::GetDlgItem($appearance,213),0x405,[IntPtr]1,[IntPtr]24)|Out-Null
 Apply 0 2
 [NotesTest]::SendText([NotesTest]::GetDlgItem($note,110),0xC,[IntPtr]::Zero,"桌面上的灵感`r`n把计划留在角落。`r`n撕下后，下一张会露出来。")|Out-Null
 if(!$SkipGlass){
 $fixture=[EffectsFixture]::Create(); if(![NotesTest]::IsWindowVisible($fixture)){throw 'Fixture is hidden'}; [EffectsFixture]::SetWindowPos($note,[IntPtr](-1),0,0,0,0,0x13)|Out-Null
 try { Wait-Until { [NotesTest]::GetProp($note,'GlassReady') -ne [IntPtr]::Zero } 'real backdrop capture available' } catch { Write-Host ('Note visibility: '+[NotesTest]::IsWindowVisible($note)); Write-Host ('DXGI error: '+[NotesTest]::GetProp($note,'GlassError'));  Write-Host ([NotesTest]::Text([NotesTest]::GetDlgItem($note,130))); $errorImage=Save-Window $note 'glass-error.png'; $errorImage.Dispose(); throw }
 Start-Sleep -Milliseconds 400
 $smooth=Capture-Glass 'glass-smooth.png';$smoothDetail=Detail $smooth;$smoothMean=Mean-Red $smooth
 Apply 0 1
 Wait-Until { [NotesTest]::GetProp($note,'GlassReady') -ne [IntPtr]::Zero } 'frosted frame rendered'
 $frost=Capture-Glass 'glass-frosted.png';$frostDetail=Detail $frost
 if($smoothDetail -lt 10 -or $frostDetail -ge $smoothDetail*0.65){throw "FAIL real blur: smooth=$smoothDetail frosted=$frostDetail"}
 "PASS pixel detail decreases with real blur: smooth=$([Math]::Round($smoothDetail,2)), frost=$([Math]::Round($frostDetail,2))"
 $smooth.Dispose();$frost.Dispose()
 [EffectsFixture]::Draw($fixture,1);Start-Sleep -Milliseconds 700
 $changed=Capture-Glass 'glass-live-background.png';$newMean=Mean-Red $changed;$changed.Dispose()
 if([Math]::Abs($newMean-$smoothMean) -lt 15){throw "FAIL backdrop failed to refresh: old=$smoothMean new=$newMean"};'PASS changing real background updates glass'
 }else{'SKIP controlled glass pixel checks; interactive foreground interference';Apply 0 0}
 [NotesTest+Rect]$beforeAdd=New-Object NotesTest+Rect;[NotesTest]::GetWindowRect($note,[ref]$beforeAdd)|Out-Null
 if([NotesTest]::GetDlgItem($note,149) -ne [IntPtr]::Zero -or ![NotesTest]::IsWindowVisible([NotesTest]::GetDlgItem($note,101))){throw 'FAIL add button or obsolete minus control'}
 [NotesTest]::SendMessage([NotesTest]::GetDlgItem($note,101),0xF5,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null;Command $note 101
 Wait-Until { [NotesTest]::Find($app.Id,'DesktopNotes').Count -eq 3 } 'three-note stack'
 $frontAdd=[NotesTest]::GetProp($hostWindow,'StackTop');[NotesTest+Rect]$afterAdd=New-Object NotesTest+Rect;[NotesTest]::GetWindowRect($frontAdd,[ref]$afterAdd)|Out-Null;if(($afterAdd.right-$afterAdd.left) -ne ($beforeAdd.right-$beforeAdd.left) -or ($afterAdd.bottom-$afterAdd.top) -ne ($beforeAdd.bottom-$beforeAdd.top)){throw 'FAIL new page changed pad dimensions'};'PASS adding pages preserves pad dimensions'
 $cards=[NotesTest]::Find($app.Id,'DesktopNotes'); foreach($card in $cards){[NotesTest]::SendText([NotesTest]::GetDlgItem($card,110),0xC,[IntPtr]::Zero,"今天的计划`r`n• 整理思路`r`n• 阅读三十分钟`r`n`r`n一叠纸，留住灵感。")|Out-Null}; $top=[NotesTest]::GetProp($hostWindow,'StackTop')
 Command $hostWindow 906;$next=[NotesTest]::GetProp($hostWindow,'StackTop');if($top -eq $next){throw 'FAIL next card'};'PASS stack cycles front card'
 $positions=@();foreach($card in $cards){[NotesTest+Rect]$r=New-Object NotesTest+Rect;[NotesTest]::GetWindowRect($card,[ref]$r)|Out-Null;$positions+=@($r)}
 if(@($positions | ForEach-Object top | Sort-Object -Unique).Count -ne 1){throw 'FAIL fixed paper alignment'};'PASS all pages share fixed geometry'
 $note=$next
 [NotesTest]::SendMessage($note,0x8002,[IntPtr]1,[IntPtr]::Zero)|Out-Null
 $left=($positions | Measure-Object left -Minimum).Minimum-8;$topEdge=($positions | Measure-Object top -Minimum).Minimum-8;$right=($positions | Measure-Object right -Maximum).Maximum+8;$bottom=($positions | Measure-Object bottom -Maximum).Maximum+8
 $pile=New-Object Drawing.Bitmap ([int]($right-$left)),([int]($bottom-$topEdge));$g=[Drawing.Graphics]::FromImage($pile);$dc=$g.GetHdc();[EffectsFixture]::ScreenCopy($dc,$left,$topEdge,$pile.Width,$pile.Height)|Out-Null;$g.ReleaseHdc($dc);$g.Dispose();$pile.Save((Join-Path $OutputDirectory 'stack-preview.png'),[Drawing.Imaging.ImageFormat]::Png);$pile.Dispose();[NotesTest]::SendMessage($note,0x8002,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
 Apply 1 0
 Start-Sleep -Milliseconds 250
 $tearButton=[NotesTest]::GetDlgItem($note,107)
[NotesTest]::SendMessage($tearButton,0x201,[IntPtr]1,[IntPtr]((10 -shl 16)+10))|Out-Null
[NotesTest]::SendMessage($tearButton,0x200,[IntPtr]1,[IntPtr]((110 -shl 16)+10))|Out-Null
[NotesTest]::SendMessage($tearButton,0x202,[IntPtr]::Zero,[IntPtr]((110 -shl 16)+10))|Out-Null
 Wait-Until { (Get-ChildItem (Join-Path $DataRoot 'Recycled') -Filter '*.note').Count -eq 1 } 'tear saves to recoverable recycle area'
 [NotesTest+Rect]$a=New-Object NotesTest+Rect;[NotesTest]::GetWindowRect($note,[ref]$a)|Out-Null;Start-Sleep -Milliseconds 1000
 [NotesTest+Rect]$b=New-Object NotesTest+Rect;[NotesTest]::GetWindowRect($note,[ref]$b)|Out-Null;if($b.top -ne $a.top -or $b.left -ne $a.left){throw 'FAIL fixed header moved'};'PASS header remains fixed while paper falls'
 Wait-Until { [NotesTest]::Find($app.Id,'DesktopNotes').Count -eq 2 } 'torn card disappears after animation'
 Command $hostWindow 904
 Wait-Until { [NotesTest]::Find($app.Id,'DesktopNotes').Count -eq 3 } 'torn card restored from recycle area'
 $front=[NotesTest]::GetProp($hostWindow,'StackTop');$button=[NotesTest]::GetDlgItem($front,107)
 [NotesTest]::SendMessage($button,513,[IntPtr]1,[IntPtr]655370)|Out-Null
 [NotesTest]::SendMessage($button,514,[IntPtr]::Zero,[IntPtr]655370)|Out-Null
 Wait-Until { [NotesTest]::Find($app.Id,'DesktopNotes').Count -eq 2 } 'second tear remains recoverable'
 Command $hostWindow 904
 Wait-Until { [NotesTest]::Find($app.Id,'DesktopNotes').Count -eq 3 } 'second torn page restored'
 $second=Start-Process -FilePath $exe -ArgumentList @('--test-mode','--data-dir',('"'+$DataRoot+'"')) -WindowStyle Hidden -PassThru
 if(!$second.WaitForExit(10000)){throw 'FAIL second launcher did not hand off'}
 if(![NotesTest]::IsWindowVisible([NotesTest]::GetProp($hostWindow,'StackTop'))){throw 'FAIL note reopen'};'PASS launcher reopens note page'
 Apply 1 1
 Exit-App
 $script:app=Start-Process -FilePath $exe -ArgumentList @('--test-mode','--data-dir',('"'+$DataRoot+'"')) -WindowStyle Hidden -PassThru
 Wait-Until { ([NotesTest]::Find($app.Id,'LightNotesSettings').Count -eq 1 -and [NotesTest]::Find($app.Id,'LightNotesStyle').Count -eq 1) } 'settings restored after restart'
 $settings=[NotesTest]::Find($app.Id,'LightNotesSettings')[0];$appearance=[NotesTest]::Find($app.Id,'LightNotesStyle')[0];$script:hostWindow=[NotesTest]::Find($app.Id,'LightNotesController')[0]
 if([NotesTest]::SendMessage([NotesTest]::GetDlgItem($appearance,211),0x147,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32() -ne 1 -or [NotesTest]::SendMessage([NotesTest]::GetDlgItem($appearance,212),0x400,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32() -ne 80){throw 'FAIL appearance settings persistence'}
 'PASS appearance settings persist after restart'
 Exit-App
 'ALL EFFECT CHECKS PASSED'
} finally {
 if($fixture -ne [IntPtr]::Zero){[EffectsFixture]::DestroyWindow($fixture)|Out-Null}
 if($script:app -and !$script:app.HasExited){Command $script:hostWindow 903}
 [NotesTest]::SetThreadDpiAwarenessContext($previousDpi)|Out-Null
}
