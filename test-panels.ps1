param([string]$DataRoot,[string]$OutputDirectory)
$ErrorActionPreference='Stop'
if(!$DataRoot){$DataRoot=Join-Path $env:TEMP ('LightNotesPanels-'+[guid]::NewGuid())}
. (Join-Path $PSScriptRoot 'test-support.ps1') -DataRoot $DataRoot
function Click([IntPtr]$parent,[int]$id){[NotesTest]::SendMessage([NotesTest]::GetDlgItem($parent,$id),0xF5,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null}
function Select-Value([IntPtr]$parent,[int]$id,[int]$value){[NotesTest]::SendMessage([NotesTest]::GetDlgItem($parent,$id),0x14E,[IntPtr]$value,[IntPtr]::Zero)|Out-Null}
function Slider([IntPtr]$parent,[int]$id,[int]$value){$slider=[NotesTest]::GetDlgItem($parent,$id);[NotesTest]::SendMessage($slider,0x405,[IntPtr]1,[IntPtr]$value)|Out-Null;[NotesTest]::SendMessage($parent,0x114,[IntPtr]5,$slider)|Out-Null}
function Check([IntPtr]$parent,[int]$id,[int]$value){[NotesTest]::SendMessage([NotesTest]::GetDlgItem($parent,$id),0xF1,[IntPtr]$value,[IntPtr]::Zero)|Out-Null}
function Assert([bool]$ok,[string]$message){if(!$ok){throw ('FAIL '+$message)};'PASS '+$message}
function Header{[IO.File]::ReadAllBytes((Get-ChildItem $DataRoot -Filter '*.note'|Select-Object -First 1).FullName)}
function Screenshot([IntPtr]$window,[string]$name){if(!$OutputDirectory){return};New-Item -ItemType Directory -Force -Path $OutputDirectory|Out-Null;[NotesTest+Rect]$r=New-Object NotesTest+Rect;[NotesTest]::GetWindowRect($window,[ref]$r)|Out-Null;$bmp=New-Object Drawing.Bitmap ($r.right-$r.left),($r.bottom-$r.top);$g=[Drawing.Graphics]::FromImage($bmp);$dc=$g.GetHdc();[NotesTest]::PrintWindow($window,$dc,0)|Out-Null;$g.ReleaseHdc($dc);$g.Dispose();$bmp.Save((Join-Path $OutputDirectory $name));$bmp.Dispose()}
try{
 $script:app=Start-Process -FilePath $exe -ArgumentList '--test-mode','--data-dir',('"'+$DataRoot+'"') -PassThru
 Wait-Until { [NotesTest]::Find($app.Id,'LightNotesStyle').Count -eq 1 } 'both panels created'
 $script:hostWindow=[NotesTest]::Find($app.Id,'LightNotesController')[0];$note=[NotesTest]::GetProp($hostWindow,'StackTop');$general=[NotesTest]::Find($app.Id,'LightNotesSettings')[0];$style=[NotesTest]::Find($app.Id,'LightNotesStyle')[0]
 Click $note 146
 Assert ([NotesTest]::IsWindowVisible($general) -and ![NotesTest]::IsWindowVisible($style)) 'settings button opens only general settings'
 Assert ([NotesTest]::GetDlgItem($general,210) -ne [IntPtr]::Zero -and [NotesTest]::GetDlgItem($general,211) -eq [IntPtr]::Zero) 'general settings contains position and no material selector'
 [NotesTest]::SetThreadDpiAwarenessContext([IntPtr](-4))|Out-Null;Screenshot $general 'general-settings.png'
 Click $note 147
 Assert ([NotesTest]::IsWindowVisible($style) -and ![NotesTest]::IsWindowVisible($general)) 'style button opens only appearance'
 Assert ([NotesTest]::GetDlgItem($style,211) -ne [IntPtr]::Zero -and [NotesTest]::GetDlgItem($style,210) -eq [IntPtr]::Zero) 'style contains material and no position selector'
 Select-Value $style 211 0;Click $style 302;Slider $style 214 22;Slider $style 212 75;Slider $style 213 10
 Screenshot $style 'appearance-settings.png'
 Click $style 217
 Assert ([NotesTest]::GetDlgItem($general,250) -ne [IntPtr]::Zero) 'topmost is a separate general option'
 $bytes=Header
 Assert ([BitConverter]::ToInt32($bytes,4) -eq 2 -and [BitConverter]::ToInt32($bytes,40) -eq 22 -and [BitConverter]::ToInt32($bytes,44) -eq 0) 'style applies color font and material to note files'
 Assert ((Get-Content (Join-Path $DataRoot 'settings.ini') -Raw) -match 'corner=1') 'style leaves desktop corner unchanged'
 Click $note 146;Select-Value $general 210 2;Check $general 230 0;Check $general 231 1;Check $general 232 1;Click $general 217
 Assert ([NotesTest]::SendMessage([NotesTest]::GetDlgItem($general,250),0xF0,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32() -eq 0) 'general settings preserve default topmost off'
 $bytes=Header
 Assert ([BitConverter]::ToInt32($bytes,8) -eq 0 -and [BitConverter]::ToInt32($bytes,36) -eq 1) 'general settings applies readonly and preserves non-topmost state'
 Assert ([BitConverter]::ToInt32($bytes,4) -eq 2 -and [BitConverter]::ToInt32($bytes,40) -eq 22 -and [BitConverter]::ToInt32($bytes,44) -eq 0) 'general settings does not overwrite appearance'
 $config=Get-Content (Join-Path $DataRoot 'settings.ini') -Raw
 Assert ($config -match 'corner=2' -and $config -match 'sound=0' -and $config -match 'lowPower=1') 'general behavior preferences persisted'
 Click $note 146;Click $general 242
 $bytes=Header;Assert ([BitConverter]::ToInt64($bytes,28) -gt [DateTimeOffset]::Now.ToUnixTimeSeconds()) 'current-page reminder writes future due time'
 Click $general 243;$bytes=Header;Assert ([BitConverter]::ToInt64($bytes,28) -eq 0) 'cancel reminder clears due time'
 Click $note 147;Slider $style 214 30;Click $style 225;Click $note 147
 Assert ([NotesTest]::SendMessage([NotesTest]::GetDlgItem($style,214),0x400,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32() -eq 22) 'closing style without apply discards draft'
 Click $style 225;Exit-App
 $script:app=Start-Process -FilePath $exe -ArgumentList '--test-mode','--data-dir',('"'+$DataRoot+'"') -PassThru
 Wait-Until { [NotesTest]::Find($app.Id,'LightNotesStyle').Count -eq 1 } 'restart panels created'
 $script:hostWindow=[NotesTest]::Find($app.Id,'LightNotesController')[0];$note=[NotesTest]::GetProp($hostWindow,'StackTop');$general=[NotesTest]::Find($app.Id,'LightNotesSettings')[0];$style=[NotesTest]::Find($app.Id,'LightNotesStyle')[0]
 Click $note 146
 Assert ([NotesTest]::SendMessage([NotesTest]::GetDlgItem($general,210),0x147,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32() -eq 2 -and [NotesTest]::SendMessage([NotesTest]::GetDlgItem($general,231),0xF0,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32() -eq 1) 'general settings restored after restart'
 Click $note 147
 Assert ([NotesTest]::SendMessage([NotesTest]::GetDlgItem($style,216),0x147,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32() -eq 2 -and [NotesTest]::SendMessage([NotesTest]::GetDlgItem($style,214),0x400,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32() -eq 22) 'appearance restored after restart'
 Exit-App;'ALL PANEL CHECKS PASSED'
}finally{if($script:app -and !$script:app.HasExited){Command $script:hostWindow 903}}
