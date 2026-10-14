param([string]$DataRoot,[string]$Screenshot)
. (Join-Path $PSScriptRoot 'test-support.ps1') -DataRoot $DataRoot -Screenshot $Screenshot
try {
 Launch
 $edit=[NotesTest]::GetDlgItem($note,110)
 $content="轻笺升级验证`r`n中文、English、待办清单。`r`n明天：整理计划"
 [NotesTest]::SendText($edit,0xC,[IntPtr]::Zero,$content)|Out-Null
 # WM_SETTEXT on a multiline EDIT does not emit EN_CHANGE; send the same notification as typing.
 Command $note (110 -bor (0x300 -shl 16))
 Wait-Until { $files=@(Get-ChildItem $DataRoot -Filter '*.note'); if($files.Count -ne 1){return $false}; $b=[IO.File]::ReadAllBytes($files[0].FullName); return $b.Length -gt 56 -and [Text.Encoding]::Unicode.GetString($b,56,$b.Length-56) -eq $content } 'autosave debounce'
 $file=(Get-ChildItem $DataRoot -Filter '*.note')[0].FullName
 $raw=[IO.File]::ReadAllBytes($file)
 if ([Text.Encoding]::Unicode.GetString($raw,56,$raw.Length-56) -ne $content) { throw 'FAIL saved UTF-16 text' }; 'PASS saved text matches'
 Command $note 102; Command $note 103; Command $note 106
 if((Get-Content (Join-Path $DataRoot 'settings.ini')) -notcontains 'fixed=1'){throw 'FAIL pad fixed state persistence'}; 'PASS whole-pad fixed state persisted' 
 $raw=[IO.File]::ReadAllBytes($file)
 if ([BitConverter]::ToInt32($raw,4) -ne 1 -or [BitConverter]::ToInt32($raw,8) -ne 0 -or [BitConverter]::ToInt32($raw,36) -ne 1) { throw 'FAIL color readonly and non-topmost persistence' }; 'PASS color / readonly / non-topmost persisted'
 Command $note 106
 [NotesTest]::SendMessage($note,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
 if ([NotesTest]::IsWindowVisible($note)) { throw 'FAIL close hides note' }; 'PASS close hides; process remains active'
 Command $hostWindow 901
 if (![NotesTest]::IsWindowVisible($note)) { throw 'FAIL show all' }; 'PASS tray show all'
 Command $note 101
 Wait-Until { [NotesTest]::Find($app.Id,'DesktopNotes').Count -eq 2 } 'multiple independent notes'
 if ($Screenshot) {
  [NotesTest]::SendMessage($note,0x8002,[IntPtr]1,[IntPtr]::Zero)|Out-Null
  $previousDpi=[NotesTest]::SetThreadDpiAwarenessContext([IntPtr](-4))
  [NotesTest+Rect]$r=New-Object NotesTest+Rect;[NotesTest]::GetWindowRect($note,[ref]$r)|Out-Null
  $bitmap=New-Object Drawing.Bitmap ($r.right-$r.left),($r.bottom-$r.top)
  $graphics=[Drawing.Graphics]::FromImage($bitmap);$dc=$graphics.GetHdc()
  $captured=[NotesTest]::PrintWindow($note,$dc,0);$graphics.ReleaseHdc($dc)
  if(!$captured){throw 'FAIL capture'};$bitmap.Save($Screenshot,[Drawing.Imaging.ImageFormat]::Png);$graphics.Dispose();$bitmap.Dispose();[NotesTest]::SetThreadDpiAwarenessContext($previousDpi)|Out-Null;[NotesTest]::SendMessage($note,0x8002,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null;'PASS window capture'
 }
 Exit-App
 Launch
 $restored=@([NotesTest]::Find($app.Id,'DesktopNotes') | Where-Object { [NotesTest]::Text([NotesTest]::GetDlgItem($_,110)) -eq $content })
 if($restored.Count -ne 1){throw 'FAIL restart restore'};$note=$restored[0];'PASS text restored after restart'
 # Inject a real write failure without changing any production data.
 $tmp=$file+'.tmp';New-Item -ItemType Directory -Path $tmp | Out-Null
 Command $note 102
 if(![NotesTest]::Text([NotesTest]::GetDlgItem($note,130)).Contains('保存失败')){throw 'FAIL write failure visible'}
 Command $hostWindow 901
 Remove-Item -LiteralPath $tmp
 Command $note 102
 'PASS failed write reported and recovered'
 Exit-App
 # Primary is deliberately damaged; a previous valid version must be recovered.
 [IO.File]::WriteAllBytes($file,[byte[]](1,2,3))
 Launch
 $restored=@([NotesTest]::Find($app.Id,'DesktopNotes') | Where-Object { [NotesTest]::Text([NotesTest]::GetDlgItem($_,110)) -eq $content })
 if($restored.Count -ne 1){throw 'FAIL backup recovery'};$note=$restored[0]
 if((Get-ChildItem $DataRoot -Filter '*.corrupt-*').Count -lt 1){throw 'FAIL damaged file preserved'}
 'PASS backup recovery and damaged original preserved'
 Exit-App
 # Due time is part of the saved format; exercise the actual timer while hidden.
 $raw=[IO.File]::ReadAllBytes($file);$due=[DateTimeOffset]::UtcNow.ToUnixTimeSeconds()+4
 [Array]::Copy([BitConverter]::GetBytes([long]$due),0,$raw,28,8);[IO.File]::WriteAllBytes($file,$raw)
 Launch
 $note=@([NotesTest]::Find($app.Id,'DesktopNotes') | Where-Object { [NotesTest]::Text([NotesTest]::GetDlgItem($_,110)) -eq $content })[0]
 [NotesTest]::SendMessage($note,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
 Wait-Until { [NotesTest]::Text([NotesTest]::GetDlgItem($note,130)).Contains('提醒已到时') } 'real reminder timer fires while hidden'
 if(![NotesTest]::IsWindowVisible($note)){throw 'FAIL reminder reveals note'}
 if([BitConverter]::ToInt64([IO.File]::ReadAllBytes($file),28) -ne 0){throw 'FAIL reminder consumed once'}
 'PASS reminder reveals note and persists consumed state'
 Exit-App
 # Preserve compatibility with the first version's 36-byte header.
 $raw=[IO.File]::ReadAllBytes($file);$legacy=New-Object byte[] ($raw.Length-20)
 [Array]::Copy($raw,0,$legacy,0,36);[Array]::Copy($raw,56,$legacy,36,$raw.Length-56)
 [Array]::Copy([BitConverter]::GetBytes([int]0x4E4F5431),0,$legacy,0,4);[IO.File]::WriteAllBytes($file,$legacy)
 Launch
 if(@([NotesTest]::Find($app.Id,'DesktopNotes') | Where-Object { [NotesTest]::Text([NotesTest]::GetDlgItem($_,110)) -eq $content }).Count -ne 1){throw 'FAIL legacy read'}
 Exit-App
 $raw=[IO.File]::ReadAllBytes($file)
 if([BitConverter]::ToInt32($raw,0) -ne 0x4E4F5433 -or [Text.Encoding]::Unicode.GetString($raw,56,$raw.Length-56) -ne $content){throw 'FAIL legacy upgrade'}
 'PASS original version data migrated without text loss'
 'ALL INTEGRATION CHECKS PASSED'
} finally {
 if($script:app -and !$script:app.HasExited){Command $script:hostWindow 903}
}
