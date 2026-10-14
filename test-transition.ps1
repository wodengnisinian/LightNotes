param([string]$DataRoot)
$ErrorActionPreference='Stop'
if(!$DataRoot){$DataRoot=Join-Path $env:TEMP ('LightNotesTransition-'+[guid]::NewGuid())}
. (Join-Path $PSScriptRoot 'test-support.ps1') -DataRoot $DataRoot
function Frame([IntPtr]$window){[NotesTest+Rect]$r=New-Object NotesTest+Rect;[NotesTest]::GetWindowRect($window,[ref]$r)|Out-Null;$bmp=New-Object Drawing.Bitmap ($r.right-$r.left),($r.bottom-$r.top);$g=[Drawing.Graphics]::FromImage($bmp);$dc=$g.GetHdc();$ok=[NotesTest]::PrintWindow($window,$dc,0);$g.ReleaseHdc($dc);$g.Dispose();if(!$ok){$bmp.Dispose();throw 'PrintWindow failed'};return $bmp}
foreach($material in 0,1,2){
 $variant=Join-Path $DataRoot ('mode-'+$material);New-Item -ItemType Directory -Force -Path $variant|Out-Null
 $firstImage=$null;$nextImage=$null
 try{
  $script:app=Start-Process -FilePath $exe -ArgumentList '--test-mode','--data-dir',('"'+$variant+'"') -PassThru
  Wait-Until { [NotesTest]::Find($app.Id,'LightNotesStyle').Count -eq 1 } ('ready material '+$material)
  Wait-Until { $controller=[NotesTest]::Find($app.Id,'LightNotesController')[0]; $front=[NotesTest]::GetProp($controller,'StackTop'); $front -ne [IntPtr]::Zero -and [NotesTest]::IsWindowVisible($front) -and [NotesTest]::Find($app.Id,'LightNotesGeometry').Count -eq 1 } 'startup presentation complete'
  $script:hostWindow=[NotesTest]::Find($app.Id,'LightNotesController')[0];$first=[NotesTest]::GetProp($hostWindow,'StackTop');Command $first 101;Command $first 101;Command $first (140+$material)
  Command $first 146;$settings=[NotesTest]::Find($app.Id,'LightNotesSettings')[0];Command $settings 246;$geometry=[NotesTest]::Find($app.Id,'LightNotesGeometry')[0]
  [NotesTest]::SendMessage([NotesTest]::GetDlgItem($geometry,404),0x14E,[IntPtr]4,[IntPtr]::Zero)|Out-Null
  foreach($item in @(@(400,460),@(401,520),@(402,110),@(403,90))){[NotesTest]::SendText([NotesTest]::GetDlgItem($geometry,$item[0]),12,[IntPtr]::Zero,[string]$item[1])|Out-Null}
  Command $geometry 405
  if($material){Wait-Until { [NotesTest]::GetProp($first,'GlassReady') -ne [IntPtr]::Zero } 'source glass frame available'}
  [NotesTest]::SetThreadDpiAwarenessContext([IntPtr](-4))|Out-Null
  [NotesTest]::SendMessage($first,0x8002,[IntPtr]1,[IntPtr]::Zero)|Out-Null
  $firstImage=Frame $first
  [NotesTest]::SendMessage([NotesTest]::GetDlgItem($first,109),0xF5,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
  $previous=[NotesTest]::GetProp($hostWindow,'StackTop');if($previous -eq $first){throw 'Previous page did not switch'}
  [NotesTest]::SendMessage($previous,0x8002,[IntPtr]1,[IntPtr]::Zero)|Out-Null
  $previousImage=Frame $previous
  try{for($y=180;$y -lt 260;$y+=4){for($x=60;$x -lt 160;$x+=4){if($firstImage.GetPixel($x,$y).ToArgb() -ne $previousImage.GetPixel($x,$y).ToArgb()){throw "Previous page changed material pixels, mode=$material"}}}}finally{$previousImage.Dispose()}
  "PASS previous page preserves actual painted background pixels, mode=$material"
  [NotesTest]::SendMessage([NotesTest]::GetDlgItem($previous,108),0xF5,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
  if([NotesTest]::GetProp($hostWindow,'StackTop') -ne $first){throw 'Next did not return to original page'}
  $button=[NotesTest]::GetDlgItem($first,107);[NotesTest]::SendMessage($button,513,[IntPtr]1,[IntPtr]655370)|Out-Null;[NotesTest]::SendMessage($button,514,[IntPtr]::Zero,[IntPtr]655370)|Out-Null
  $next=[NotesTest]::GetProp($hostWindow,'StackTop');if($next -eq $first){throw 'No next page'}
  if($material -and [NotesTest]::GetProp($next,'GlassReady') -eq [IntPtr]::Zero){throw 'Glass not prepared before next page shown'}
  [NotesTest]::SendMessage($next,0x8002,[IntPtr]1,[IntPtr]::Zero)|Out-Null
  $nextImage=Frame $next
  $sum=0;$count=0;for($y=180;$y -lt 260;$y+=4){for($x=60;$x -lt 160;$x+=4){$a=$firstImage.GetPixel($x,$y);$b=$nextImage.GetPixel($x,$y);$sum+=[Math]::Abs($a.R-$b.R)+[Math]::Abs($a.G-$b.G)+[Math]::Abs($a.B-$b.B);$count+=3}}
  $difference=$sum/$count;if($difference -gt 1){throw "Visual material changed during tear mode=${material}, difference=$difference"}
  "PASS actual painted pixels match immediately during tear, mode=$material difference=$difference"
  [NotesTest]::SendMessage($next,0x8002,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
  Wait-Until { [NotesTest]::Find($app.Id,'DesktopNotes').Count -eq 2 } 'tear animation completes'
  Exit-App
 }finally{if($firstImage){$firstImage.Dispose()};if($nextImage){$nextImage.Dispose()};if($script:app -and !$script:app.HasExited){Command $script:hostWindow 903}}
}
'ALL VISUAL TRANSITION CHECKS PASSED'
