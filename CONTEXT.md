# Working context

Notes for picking this project up again, by a person or an agent. PROGRESS.md covers what each fix does (README.md is the short overview). This file covers how the machine is set up today and how to work on it.

## Current state (2026-10-16)

- macOS Sequoia 15.8.1 (24H32) on the factory Samsung PM981 (MZVLB512HAJQ-00000, firmware EXA7301Q), sharing the disk with Windows.
- OpenCore 1.0.9 (DEBUG build from 1401) runs from the SSD's own EFI partition `disk0s4` (label `BOOT`, 1 GB, FAT32). The Windows ESP is `disk0s1`. The USB stick (`1401`) holds a copy of the laptop's EFI from 2026-10-16, before TRIM was enabled (MacBookPro16,1, `nvme=512`, TRIM off), as a rescue stick. Its older EFI is in `EFI.old-20261016` on the stick.
- Working: SSD (writes capped at 128 KB, TRIM on), Intel UHD 630 acceleration with the 144 Hz panel, sleep/wake, Wi-Fi (itlwm + HeliPort), Bluetooth, AirPods audio (Music app streams through the "GL65 Multi" output, see below), speakers and microphone, USB map, boot menu with Macintosh HD first. The RTX 2060 is powered off, so the HDMI and USB-C display outputs do not work.
- TRIM (2026-10-16): `nvme=512` removed and `SetApfsTrimTimeout = -1`. Boot trim: `disk1 40448586 blocks trimmed in 5243 extents` in 0.82 s. `tools/trimstress.sh` (copy on the laptop at `~/gl65-test/`) (10 × 2 GB write, delete, unmount, mount on a scratch APFS volume) passed both with and without APFS trim, with no NVMe errors. Config backups on `disk0s4`: `config.plist.bak-133005` (before removing `nvme=512`) and `config.plist.bak-133324` (before `SetApfsTrimTimeout = -1`).
- Known gaps: no external display (its ports are wired to the RTX).

## NVIDIA RTX 2060 (tried, removed 2026-10-16)

- Current state: RTX off, as before the attempt: `-wegnoegpu`, `SSDT-dGPU-Off.aml` enabled, no `IONDRVSupport` block, the original `SSDT-PTSWAK.aml`, `pmset -a sleep 1`. The driver was removed with `sudo ~/nullmoth/pkgroot/uninstall.sh /Library/NullMoth/backup-20261016-141459.4okLov`. After the reboot: no `com.nullmoth` kexts loaded, no NVIDIA device on the PCI bus, Intel Metal 3. The live config differs from `config.plist.bak-135659` on `disk0s4` only in `Misc > Debug > SysReport` (off).
- Hardware: RTX 2060 Mobile TU106M `10DE:1F11`. ACPI is Optimus without a mux (`MXDS`/`MXMX` absent), and the GOP is on Intel. The internal panel is Intel-only; HDMI and USB-C belong to the RTX.
- NullMoth driver 2.0.4 worked as far as an HDMI desktop at 2560×1440. Findings, in case it is tried again:
  - BAR1 maxes out at 256 MB (`sizes supported mask 0x1c0`), so the driver waits 100 s before bring-up. Zero-copy flip is refused, and every frame for the HDMI screen is copied by the CPU (`NVDisplayPipe::performTransaction`).
  - With the 100 s settle, the driver's 60 s headless check fires first (`debug.nvaccel_headless: 1`). WindowServer then stays on Intel for both screens, and the RTX only scans out copied frames: clean, but about 47–64 fps presented on a 144 Hz monitor.
  - With the RTX composited (`nvrmsettle=3000`, or `-nvaccelnoheadless`), the HDMI monitor showed drawing artifacts at the Apple logo and a black band on the lock screen.
  - A patched NVAccel (memcpy and double-buffered scanout) stopped tearing but was laggier.
  - Metal compute: about 3.9–4.2 TFLOPS fp32 (vec4 FMA) on the RTX. The port stubs `nv_acpi_get_powersource`, and the open SDK has no clock readout.
  - Sleep with the dGPU on and no driver froze on wake (GPU LED orange).

## Repo copy versus the machine

- `EFI/` here matches the laptop's `disk0s4`, except for scrubbed identifiers:
  - `PlatformInfo > Generic` (serial, MLB, UUID, ROM)
  - the Wi-Fi network in `itlwm.kext` `WiFiConfig`
  - the NVMe EUI and Windows ESP UUID in the `Misc > Entries` Windows entry, which is disabled as a template
- The laptop's Windows ESP (`disk0s1`) has `EFI/Microsoft/Boot/.contentVisibility` containing `Disabled`. That hides the scanned Windows entry, so the custom entry shows up after macOS.

## Editing the laptop's OpenCore config remotely

The laptop accepts SSH key login from the main Mac (Remote Login is on). Steps:

1. `sudo diskutil mount disk0s4`
2. Copy `/Volumes/BOOT/EFI/OC/config.plist` to the Mac and edit it there with `plistlib`. Don't use PlistBuddy for `<data>` values: it stores hex text instead of bytes.
3. Run `plutil -lint` and `ocvalidate` (OpenCorePkg 1.0.8 Utilities is close enough to 1.0.9).
4. Back up the old file next to it (`config.plist.bak-HHMMSS`), write the new one, and compare hashes.
5. Unmount, then `sudo shutdown -r now`.

`tools/remote_cfg.sh pull|push <file>` (run with `GL65=user@laptop-ip`) does steps 1, 2 and 4.

Gotchas:
- In zsh on the laptop, `log` is a builtin. Use `/usr/bin/log show ...`.
- `strings`, `otool` and other developer tools are not installed on the laptop. Calling them opens an "install developer tools" dialog. Do binary analysis on the main Mac instead.
- `DisableVariableWrite = true` means macOS cannot write NVRAM:
  - `sudo nvram` and Startup Disk do not stick. Set NVRAM through OpenCore `NVRAM > Add` and list the key in `NVRAM > Delete`.
  - Kernel panic reports are not saved to NVRAM (`aapl,panic-info`) while it is on.
- After a forced power-off, audio can disappear (ALC269 codec stuck). A full shutdown and power-on fixes it; a warm restart does not.
- Sleep needs `SSDT-PTSWAK` (RTX on before the BIOS sleep path, off again after wake) and `pmset -a hibernatemode 0 standby 0 autopoweroff 0 powernap 0 tcpkeepalive 0 proximitywake 0`.
- A temporary passwordless sudo rule exists at `/etc/sudoers.d/gl65-setup` for remote work. Remove it when done (`sudo rm /etc/sudoers.d/gl65-setup`).

## Crash reports

Crash reports can only be saved while `DisableVariableWrite` is off, which the installer needs on.

- Read the report in Recovery or macOS with `nvram -x aapl,panic-info > state.txt`.
- On another Mac, decode it with `python3 tools/decode_panic.py state.txt`.
- During the install it was always `nvme: Command timeout. Write`. The 128 KB write cap fixed it.

## Building the kexts

- NVMeFix fork (`NVMeFix/`): put `Lilu.kext` (the DEBUG release, which contains the SDK headers) and acidanthera `MacKernelSDK` in the folder, then run `xcodebuild -configuration Release`.
- BlueToolFixup:
  1. Get the BrcmPatchRAM source at hexxyan/BlueToolFixup-FixA2DP `d8bd62a`.
  2. Apply `BlueToolFixup/tighten-a2dp-pattern-15.8.1.diff`.
  3. Add Lilu and MacKernelSDK.
  4. Run `xcodebuild -project BrcmPatchRAM.xcodeproj -target BlueToolFixup -configuration Release`.
- Keep 1401's IntelBluetoothFirmware and IntelBTPatcher 2.5.0 builds. The public 2.4.0 releases leave the AX210 at firmware v0 on Sequoia.

## Music app streams stuck at 0:00 on AirPods (fixed with a workaround)

Status on 2026-10-16: fixed by sending the output through a Multi-Output device. See "Fix" below. HLS/FairPlay 4 streaming itself cannot work on this hardware.

### Fix

The laptop already has a persistent Multi-Output named "GL65 Multi" (UID `gl65.multi.1089`, created by `mkmulti` and kept by coreaudiod across reboots). Select "GL65 Multi" as the sound output while using the AirPods. To make a new one: Audio MIDI Setup → + → Create Multi-Output Device → tick only the AirPods. Music then sees a non-Bluetooth device and uses its plain streaming engine (`readytoplay:Y (downloadItemIsPlayable)`) instead of HLS. Verified after the final reboot with a never-played subscription track ("Pocket (montreal)", 12 → 16 s, output `gl65.multi.1089`).

In Music's AirPlay picker, keep "Raihan's MacBook Pro" (the computer) selected. That follows the system output. Picking the AirPods there, or as the system output, goes back to Bluetooth and HLS, and streams stall.

Auto-switch: `tools/airpods_multi.c` (binary at `~/gl65-tools/airpods_multi`, LaunchAgent `~/Library/LaunchAgents/com.gl65.airpods-multi.plist`, log `~/gl65-tools/airpods_multi.log`) watches the default output and moves any Bluetooth default to "GL65 Multi". Built-in and USB outputs are left alone. To stop it: `launchctl bootout gui/$(id -u)/com.gl65.airpods-multi`.

Limit: the macOS volume keys do not control a Multi-Output device. Use Music's own volume slider.

### Symptom

- Apple Music streams in Music.app stay at 0:00 when the output is AirPods Pro (74:3F:8E:8B:E5:DE, product 0x2027).
- These all work:
  - streams on the built-in output and on a USB DAC (C-Media)
  - local files in Music, mono and stereo, on the AirPods
  - music.apple.com in Safari on the AirPods

### What the logs show

- Pressing play sets the rate to 1, and the HLS segment pump downloads data at about 24 Mbit/s.
- The FairPlay lease is granted, and the track gets `fpfs_AddNewTrack ... media type soun`.
- After that there is never an `AVAudioSession setPlayState Started` for the AirPods route, so the audio queue never starts.
- When an item is removed, `fpfs_StopPlayingItem ... CoreMediaErrorDomain Code=-15697` appears. It only shows on removal, so it is not the cause.
- Music calls `SetSpatialPlaybackMode: AVCFPlayerItemSetVariantPreferences ... 0x4` and sets `AudioSpatializationAllowed` on the item.
- CoreAudio reports the route as "untracked Bluetooth headphones" with spatialAudioSources `['binh','mlti']`.
- `bluetoothd` logs:
  - `SensorTracking stopTrackingForSession: sensor tracking is not supported on this platform`
  - `StackManager Get Spatial Mode with Bundle ID: global=>1,1` (mode 1, head tracking on)
- Earlier sessions also logged:
  - `AudioObjectGetPropertyData: no object with given ID 0`
  - `Assertion failure: err == (-42588)`
- Old working theory (disproven, see "Root cause"): the head-tracked spatial path never comes up on this hardware, so the audio queue never starts.

### Tried without success

- Dolby Atmos off (`preferredDolbyAtmosPlaySetting=30`)
- a Multi-Output device (the early test probably used a cached or local file; the 2026-10-16 retest with a fresh stream works, see "Fix")
- `SpatialSoundProfileAllowed=0` in `/Library/Preferences/com.apple.Bluetooth.plist` (backup `.bak-spatial`)
- turning off Bluetooth on the iPhone and on the other Mac
- `GlobalSpatialMode = SpatialModeNever` in root's `com.apple.BTServer`: `bluetoothd` still reported `global=>1,1` after a restart

### How bluetoothd stores the spatial mode

These findings are from disassembling the 15.8.1 `bluetoothd` on the main Mac with `objdump --macho -d`.

- Preference domain: `com.apple.BTServer`. `bluetoothd` runs as root, so this is root's preferences.
- Per-app key: `SpatialModeDict`, a dictionary of `bundleID -> { kCBMsgArgParamSpatialMode: uint, kCBMsgArgParamSpatialHeadTracking: bool }`. The entry `global` is the fallback.
- Getter at `0x1004a73ea`: it looks up the bundle ID in the cached dictionary. When the bundle ID is missing, it returns mode 1 with head tracking on (FaceTime has its own case). The cache lives at `self+0x1d8`.
- Loader at `0x1004a6856`: it reads `SpatialModeDict`. When the dictionary is empty, it falls back to `GlobalSpatialMode` (`0x1004a6206`) to build the `global` entry.
- Values of the `GlobalSpatialMode` string: `SpatialModeNever` = 0, `SpatialModeAutomatic` = 1, `SpatialModeAlways` = 2, `SpatialModeMultiChannelOnly` = 3. When the key is unset, it maps to 2.

### Applied on the laptop (tested, no effect)

```sh
sudo defaults write com.apple.BTServer GlobalSpatialMode -string SpatialModeNever
sudo defaults write com.apple.BTServer SpatialModeDict -dict \
  global "<dict><key>kCBMsgArgParamSpatialMode</key><integer>0</integer><key>kCBMsgArgParamSpatialHeadTracking</key><false/></dict>" \
  com.apple.Music "<dict><key>kCBMsgArgParamSpatialMode</key><integer>0</integer><key>kCBMsgArgParamSpatialHeadTracking</key><false/></dict>"
sudo killall bluetoothd
```

`bluetoothd` restarted cleanly. The AirPods were disconnected at that point.

Undo:

```sh
sudo defaults delete com.apple.BTServer SpatialModeDict
sudo defaults delete com.apple.BTServer GlobalSpatialMode
sudo killall bluetoothd
```

### Root cause (found 2026-10-16)

- The spatial-off setting took effect (`bluetoothd`: `global=>0,0`, "Spatial Audio Not Active"), but the stream still stalled at 0:00.
- Music has two engines. ITPlayer plays a plain download/stream (log `readytoplay:Y (downloadItemIsPlayable)`) and works on every output. CMPlayer plays HLS with FairPlay Streaming.
- With AirPods as the output, Music logs `play> select> forcing CMPlayer for HLS` (bit `0x200` of the flags passed to the selector at `0x10162acb0` in the 15.8.1 x86_64 Music binary). On the built-in output it picks ITPlayer.
- The HLS item downloads its first segment (HTTP 206), then `keyboss ckb_processRequestViaNextHandler ... handler 4` (the AVContentKeySession handler) is never answered. No key-server request goes out, and `fairplayd`, `lskdd` and `adid` log nothing. Music loads AppleGVA at exactly that moment.
- Not AirPods-specific. Forcing lossless (`losslessEnabled=1`, `preferredStreamPlaybackAudioQuality=40`, which logs `select CMPlayer for lossless`) on the built-in output stalls at 0:00 the same way.
- HLS assets that are already cached with an offline key do play through CMPlayer, on the built-in output and on the AirPods. Only the online key fetch fails.
- Earlier "works on built-in/USB" comparisons used cached or local files. Check for `cached file` or `downloadItemIsPlayable` in the log before treating a play as a stream.
- Debug logging for `com.apple.Music` is off by default. `sudo log config --subsystem com.apple.Music --mode "level:debug"` (not persistent across reboots) shows the `cks` key flow: `SendCKSCreateKeyRequest` is never followed by `MakeKeyRequestDataCallback`, so `AVCFContentKeyRequest_MakeKeyRequestData` never returns, and the stream's own `DidProvideKeyRequest` is never even called.
- At that moment `lskdd` (CoreLSKD, FairPlay 4) logs `remote_device_copy_unique_of_type: device not found`, and AppleGVA is consulted. Per WhateverGreen's DRM chart, FairPlay 4 has no software path with an iGPU present, and this laptop is iGPU-only (`-wegnoegpu`; the RTX 2060 is unsupported). The HaC-Mini (iGPU hackintosh) issue #970 shows the same thing: lossless HLS songs skip.
- Downloads use a different key path: persistable keys through CoreFP/adid. They need Music > Account > Authorizations > Authorize This Computer (done on 2026-10-16; before that, downloads failed with "Your computer is not authorised").
- With a Bluetooth output (`lad> ... deviceType:B`), Music switches to CMPlayer and streams HLS. With a wired or aggregate output it uses ITPlayer and a plain stream.

### Tried without success (DRM side)

- `unfairgva=2` in boot-args.
- SMBIOS `iMac19,1` (no T2, same serial/MLB/ROM): `lskdd` still looked for the remote device and streams still stalled. Reverted to `MacBookPro16,1`.

### Test helpers

On the laptop in `~/gl65-test/` (not in the repo), all built on the main Mac with `xcrun --sdk macosx clang -arch x86_64 ... -framework CoreAudio -framework CoreFoundation`:
- `cap.sh <label> "<track name part>"` logs a fresh play to `cap_<label>.log`.
- `setout ["name part"]` lists or sets the default output.
- `mkmulti "<name part>"` creates a Multi-Output wrapping that output and sets it as the default. Run it inside the GUI session: `sudo launchctl asuser $(id -u) sudo -u raihan ~/gl65-test/mkmulti AirPods &`. Over plain SSH it fails with `'nope'`, and the device it creates that way does not survive. The GUI-session one did survive a reboot.

Check for `cached file` or `downloadItemIsPlayable` in the log before treating a play as a stream.

### Log tips

- `log show` on the laptop is slow (minutes). Narrow it with `--predicate "process == \"Music\""` and `--last`, then grep for the time.
- The laptop clock jumped during testing: entries dated `2026-10-15 21:1x` are from the 2026-10-16 morning boot. `--start` with a wrong date returns nothing.

## Next

1. Optional: clean up the no-op spatial-off setting (undo commands above).
2. Watch for NVMe timeouts with TRIM on. On 2026-10-16 one boot panicked about 6 minutes in with an NVMe command timeout (`IONVMeController::GetNumPolledRequests` from `IOTimerEventSource::timeoutSignaled`; no nullmoth kext in the backtrace). A full power-off and power-on booted cleanly, with boot TRIM taking 1.1 s. If it happens again, boot from the 1401 stick (or Windows) and restore `config.plist.bak-133324` or `config.plist.bak-133005` on `disk0s4` (TRIM off).
3. Remove the temporary sudo rule.
4. Optional: try AirportItlwm with an OCLP root patch for native Wi-Fi.
