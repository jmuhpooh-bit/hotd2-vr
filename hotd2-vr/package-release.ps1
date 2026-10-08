<#
  Builds the Quest app and the PC version and puts what goes on the Releases page in dist\:

    hotd2-vr-quest.apk     the Quest app (Quest 3, 3S, 2, Pro)
    hotd2-vr-pcvr.zip      the PC version with VR on (flycast.exe, emu.cfg, readme, licence)

  Neither holds anything of the game: the player brings it, and the app makes the agent's
  hands from it on the spot.

  .\package-release.ps1               both
  .\package-release.ps1 -SkipBuild    package what was built last
#>
param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $repo 'dist'
$cmd = Join-Path $env:SystemRoot 'System32\cmd.exe'

if (-not $SkipBuild) {
	& $cmd /c (Join-Path $PSScriptRoot 'build-quest.cmd')
	if ($LASTEXITCODE -ne 0) { throw 'build-quest.cmd failed' }
	& $cmd /c (Join-Path $PSScriptRoot 'build-win.cmd') Release
	if ($LASTEXITCODE -ne 0) { throw 'build-win.cmd failed' }
}
New-Item -ItemType Directory -Force $dist | Out-Null

# the Quest app
$apk = Join-Path $repo 'shell\android-studio\flycast\build\intermediates\apk\vr\flycast-vr.apk'
Copy-Item $apk (Join-Path $dist 'hotd2-vr-quest.apk') -Force

# the PC version: VR on, OpenGL (the only renderer VR draws with)
$stage = Join-Path $dist 'hotd2-vr-pcvr'
if (Test-Path $stage) { Get-ChildItem $stage -Recurse -File | ForEach-Object { $_.Delete() } }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item (Join-Path $repo 'build-win\flycast.exe') $stage
Copy-Item (Join-Path $repo 'LICENSE') (Join-Path $stage 'LICENSE.txt')
Set-Content -Path (Join-Path $stage 'emu.cfg') -Encoding ascii -Value @'
[config]
pvr.rend = 0
rend.NativeDepthInterpolation = yes
vr.Xr = yes
'@
Set-Content -Path (Join-Path $stage 'README.txt') -Encoding utf8 -Value @'
The House of the Dead 2 VR, PC version
https://github.com/mikermak/hotd2-vr

1. Connect your VR headset: start SteamVR, Quest Link / Air Link or Virtual Desktop, so it
   is the active OpenXR runtime.
2. Start flycast.exe. In its window, add the folder with your own copy of the game (the
   European or US version: a .chd, .gdi, .cue or .cdi) and start it. It plays in the headset.
3. The first time, the app makes the agent's hands and pistol from your game: it plays
   itself to the game over scene, fast, behind a panel that says so. Once only; B skips it.

Without a headset the game plays in the window. Settings are in emu.cfg (vr.Xr = yes is VR).

No game is included: you need your own copy. Unofficial fan project, not affiliated with
Sega. Flycast and this fork are free software under the GPL (LICENSE.txt); the source is at
the address above.
'@
$zip = Join-Path $dist 'hotd2-vr-pcvr.zip'
if (Test-Path $zip) { (Get-Item $zip).Delete() }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip
Get-ChildItem $dist -File | Select-Object Name, @{ n = 'MB'; e = { [math]::Round($_.Length / 1MB, 1) } }
