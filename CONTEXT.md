# Working context

Notes for picking this project up again, by a person or an agent. The README covers what each fix does. This file covers how the machine is set up today and how to work on it.

## Current state (2026-10-16)

- macOS Sequoia 15.8.1 (24H32) on the factory Samsung PM981 (MZVLB512HAJQ-00000, firmware EXA7301Q), sharing the disk with Windows.
- OpenCore 1.0.9 (DEBUG build from 1401) runs from the SSD's own EFI partition `disk0s4` (label `BOOT`, 1 GB, FAT32). The Windows ESP is `disk0s1`. The USB stick is no longer needed; keep it as a rescue stick, but note it has an older config.
- Working: SSD (writes capped at 128 KB, TRIM off), Intel UHD 630 acceleration with the 144 Hz panel, RTX 2060 powered off, sleep/wake, Wi-Fi (itlwm + HeliPort), Bluetooth, AirPods audio (Safari, system sounds), speakers and microphone, USB map, boot menu with Macintosh HD first.
- Known gaps:
  - Apple Music streams in the Music app stay at 0:00 on AirPods. Local files and Safari work. A fix (per-app spatial mode Off) was applied but has not been tested. See "Open investigation" below.
  - TRIM has not been tested again since the install.

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
- Sleep needs `SSDT-PTSWAK` (RTX on before the BIOS sleep path) and `pmset -a hibernatemode 0 standby 0 autopoweroff 0 powernap 0 tcpkeepalive 0 proximitywake 0`.
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

## Open investigation: Music app streams stuck at 0:00 on AirPods

Status on 2026-10-16: a fix was applied, but it has not been tested yet.

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
- Working theory: the head-tracked spatial path never comes up on this hardware, so the audio queue never starts.

### Tried without success

- Dolby Atmos off (`preferredDolbyAtmosPlaySetting=30`)
- a Multi-Output device
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

### Applied on the laptop (untested)

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

### Next steps

1. Connect the AirPods, quit Music completely, reopen it and play a stream.
2. Check that `bluetoothd` now logs `Get Spatial Mode with Bundle ID: com.apple.Music=>0,0` (or `global=>0,0`):
   `/usr/bin/log show --last 5m --style compact --predicate 'process == "bluetoothd" AND eventMessage CONTAINS[c] "spatial mode"'`
3. If the stream still stalls, compare these lines after `fpfs_AddNewTrack` between an AirPods stream and a built-in stream: the `com.apple.coreaudio:SpatialMgr` "Spatial info for binding" dump and `setPlayState`.
   - Note: a USB play at 11:16:46 that was used for comparison earlier was a local file, not a stream. Redo the comparison with a real stream.
4. Mode 0 might mean something other than Off. If so, try mode 2 or 3 with head tracking off.
   - Control Center's Spatial Audio menu (Off / Fixed / Head Tracked) writes through `setSpatialModeWithBundleID`. Picking Off there while Music plays and then reading `SpatialModeDict` back shows the real encoding.

### Log tips

- `log show` on the laptop is slow (minutes). Narrow it with `--predicate "process == \"Music\""` and `--last`, then grep for the time.
- The laptop clock jumped during testing: entries dated `2026-10-15 21:1x` are from the 2026-10-16 morning boot. `--start` with a wrong date returns nothing.

## Next

1. Finish the Music/AirPods investigation above.
2. TRIM test: back up first, remove `nvme=512` and test, then set `SetApfsTrimTimeout` back to `-1` and test again. Use the same 10× 2 GB write and unmount stress loop.
3. Remove the temporary sudo rule and change the laptop password.
4. Optional: try AirportItlwm with an OCLP root patch for native Wi-Fi.
