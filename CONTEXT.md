# Working context

Notes for picking this project up again, by a person or an agent. The README covers what each fix does. This file covers how the machine is set up today and how to work on it.

## Current state (2026-10-16)

- macOS Sequoia 15.8.1 (24H32) on the factory Samsung PM981 (MZVLB512HAJQ-00000, firmware EXA7301Q), sharing the disk with Windows.
- OpenCore 1.0.9 (DEBUG build from 1401) runs from the SSD's own EFI partition `disk0s4` (label `BOOT`, 1 GB, FAT32). The Windows ESP is `disk0s1`. The USB stick is no longer needed; keep it as a rescue stick, but note it has an older config.
- Working: SSD (writes capped at 128 KB, TRIM off), Intel UHD 630 acceleration with the 144 Hz panel, RTX 2060 powered off, sleep/wake, Wi-Fi (itlwm + HeliPort), Bluetooth, AirPods audio (Safari, system sounds), speakers and microphone, USB map, boot menu with Macintosh HD first.
- Known gaps:
  - The Music app stalls on AirPods because it requires a spatial audio device, and `bluetoothd` says sensor tracking is not supported. Use music.apple.com in Safari instead.
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

## Next

1. TRIM test: back up first, remove `nvme=512` and test, then set `SetApfsTrimTimeout` back to `-1` and test again. Use the same 10× 2 GB write and unmount stress loop.
2. Remove the temporary sudo rule and change the laptop password.
3. Optional: get the Music app working with AirPods, or try AirportItlwm with an OCLP root patch for native Wi-Fi.
