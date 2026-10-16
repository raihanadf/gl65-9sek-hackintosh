# Progress and notes

Full status, progress log and setup notes for the MSI GL65 9SEK hackintosh. The short overview is in [README.md](README.md).

## Hardware

| Part | Model |
|------|-------|
| CPU | Intel Core i7-9750H (Coffee Lake-H) |
| iGPU / dGPU | Intel UHD 630 / NVIDIA RTX 2060 (NullMoth driver 2.0.4, or powered off) |
| SSD | Samsung PM981 MZVLB512HAJQ-00000, firmware EXA7301Q (shared with Windows) |
| Wi-Fi / BT | Intel AX210 (Wi-Fi 6E), itlwm + IntelBluetoothFirmware |
| Ethernet | Realtek RTL8111 |
| SMBIOS | MacBookPro16,1 |

## Status

**macOS Sequoia 15.8.1 runs on the GL65 9SEK and its factory PM981 SSD. Daily use works.**

| Area | State |
|------|-------|
| SSD (PM981) | Works: ~2.0 GB/s write, ~3.3 GB/s read, survives sleep/wake. TRIM on (`nvme=512` removed, `SetApfsTrimTimeout = -1`). |
| Intel UHD 630 | Accelerated (Metal 3), internal 1080p 144 Hz panel with backlight. |
| RTX 2060 | Works with the NullMoth driver 2.0.4: Metal 3, drives the HDMI output (tested with a 2560×1440 monitor, extended desktop). Muxless Optimus: the internal panel stays on the Intel GPU. The EFI in this repo ships with the RTX powered off (`SSDT-dGPU-Off`, `-wegnoegpu`); the NullMoth setup and its trade-offs are in the progress log and CONTEXT.md. |
| Sleep / wake | Works (`SSDT-PTSWAK` with `_PTS`/`_WAK` renames, hibernation off). |
| Wi-Fi | itlwm with HeliPort for the menu-bar icon (macOS's own Wi-Fi icon hidden). |
| Bluetooth | Intel AX210 BT works. AirPods audio works (A2DP patch, see below). Music app streams need a Multi-Output device wrapping the AirPods, because the HLS/FairPlay 4 path needs a T2 or AMD GPU (see CONTEXT.md). |
| Audio | ALC269, `layout-id` 66. After a forced power-off, do a full shutdown so the codec resets. |
| USB | Custom `UTBMap.kext` (12 ports, including port 9 for the SteelSeries KLC keyboard lighting). |
| Boot | OpenCore on the SSD's own EFI partition (`disk0s4`), registered in the BIOS (`LauncherOption = Full`). Windows keeps its own ESP. Macintosh HD is listed first (see Boot menu order). |

### Progress log

| Date | Change | Result |
|------|--------|--------|
| 2026-10-15 | itlwm `WiFiConfig` | Recovery has internet, and the download works |
| 2026-10-15 | Early interrupt mask (Catalina order) | Unmount and format no longer freeze, and the stress test passes |
| 2026-10-15 | — | Install panic: `Command timeout. Write` at about 30 min |
| 2026-10-16 | APST off (`ps-max-latency-us = 0`) | Erase froze because of the upstream `initActivePM` heap overflow (fixed) |
| 2026-10-16 | APST off, host power management skipped | Install panic: same write timeout at about 27 min |
| 2026-10-16 | Completion watchdog and `nand-io-timeoutms=180000` | First install stage completes |
| 2026-10-16 | — | Final stage (15.8.1) panics with the same write timeout, even with a 3-minute timeout (firmware stall) |
| 2026-10-16 | 128 KB write cap (`nvme-max-write-bytes`) and `DisableVariableWrite` | **Install completes** |
| 2026-10-16 | `-wegnoegpu`, MythB iGPU properties | First boot reaches the desktop stage, but the panel is black (Caps Lock works) |
| 2026-10-16 | `-igfxvesa` | **macOS boots with a picture** (no acceleration) |
| 2026-10-16 | `enable-backlight-registers-fix` instead of the alternative fix | **Accelerated graphics and backlight work**; `-igfxvesa` removed |
| 2026-10-16 | OpenCore on `disk0s4`, `LauncherOption = Full` | Boots without the USB stick |
| 2026-10-16 | `UTBMap.kext` | All USB devices mapped, including the USB 3 ports |
| 2026-10-16 | `SSDT-dGPU-Off` (`\\_SB.PCI0.PEG0.PEGP._OFF` from the OptTabl SSDT) | RTX 2060 powered off |
| 2026-10-16 | Sleep without PTSWAK | Hangs with a static power LED; the watchdog now pauses for sleep |
| 2026-10-16 | `SSDT-PTSWAK`, `_PTS`/`_WAK` renames, `pmset` hibernatemode 0 | **Sleep and wake work** |
| 2026-10-16 | Official IntelBluetoothFirmware/IntelBTPatcher 2.4.0 | Bluetooth dead on the AX210 (firmware v0), reverted to 1401's 2.5.0 builds |
| 2026-10-16 | `bluetoothHostControllerSwitchBehavior=always` | Safari audio on AirPods |
| 2026-10-16 | `nvme=512` removed | `TRIM Support: Yes`, 10× 2 GB write/delete/unmount stress passes, no NVMe errors |
| 2026-10-16 | dGPU on (`-wegnoegpu` removed, `SSDT-dGPU-Off` off), no driver | RTX 2060 (10DE:1F11, class 0300) visible; wake from sleep freezes with the GPU LED orange |
| 2026-10-16 | `SSDT-PTSWAK` powers the dGPU on/off only when `SSDT-dGPU-Off` is loaded | Sleep hooks no longer turn off a GPU the driver owns |
| 2026-10-16 | NullMoth NVIDIA driver 2.0.4 (`install.sh`), `Kernel > Block` IONDRVSupport | **External HDMI display works on the RTX 2060**: 4 kexts load, GPU comes up after the 100 s small-BAR settle, H24G30Q 2560×1440 online, windows usable. Same result on a second boot |
| 2026-10-16 | NullMoth driver removed (`uninstall.sh`), back to `-wegnoegpu` + `SSDT-dGPU-Off` + the original PTSWAK, sleep on | The external screen was laggy. When the RTX composited it, the HDMI monitor showed artifacts and a black band at the lock screen; when Intel composited it, the RTX only displayed CPU-copied frames. Not worth keeping |

## If an install crashes

Boot the 1401 entry, open Terminal and upload the state. `nvram: Error getting variable ... data was not found` only means that no crash report is stored.

```
{ echo "== panic"; nvram -x aapl,panic-info; echo "== ioreg"; ioreg -r -c IONVMeController -l -w0 | grep -E '"apst"|"quirks"|"early-mask"|"watchdog"'; } > /tmp/state.txt
```

Decode the crash report on another Mac with `python3 tools/decode_panic.py state.txt`.

## Next steps

1. Optional: try AirportItlwm for the native Wi-Fi menu (on Sequoia it needs an OCLP root patch).

## Bluetooth audio (AirPods)

Apple's `bluetoothd` sends `HCI_Read_Clock` while bringing up A2DP for W1/H1/H2 headphones. The Intel controller does not answer it, so `bluetoothd` logs `Failed to read BT clock` and aborts the audio pipeline: the AirPods are connected but there is no sound ([IntelBluetoothFirmware#462](https://github.com/OpenIntelWireless/IntelBluetoothFirmware/issues/462)).

- `EFI/OC/Kexts/BlueToolFixup.kext` is built from [acidanthera/BrcmPatchRAM#38](https://github.com/acidanthera/BrcmPatchRAM/pull/38) (hexxyan), with one change: `BlueToolFixup/tighten-a2dp-pattern-15.8.1.diff`.
  - The PR's pattern also matched an unrelated `.bin` resource-load check in the 15.8.1 `bluetoothd`.
  - The diff adds the preceding `lea rdi, [rbp-0x11]; call`, so only the clock gate is patched.
- It is enabled with the boot-arg `-btlfxa2dpcheck`.
- `bluetoothHostControllerSwitchBehavior=always` is set through OpenCore `NVRAM > Add` as raw bytes, because `DisableVariableWrite` blocks `nvram` from macOS.

## Boot menu order

OpenCore lists entries in disk order, so the Windows ESP came first. The scanned Windows entry is hidden with a `.contentVisibility` file (content `Disabled`) in the Windows ESP at `EFI/Microsoft/Boot/`. Windows is then added again in `Misc > Entries`, and custom entries are listed after scanned ones. The repo copy of that entry is a disabled template: fill in your NVMe EUI and Windows ESP partition UUID (`diskutil info disk0s1`).

## Post-install notes

- `pmset -a hibernatemode 0 standby 0 autopoweroff 0 powernap 0 tcpkeepalive 0 proximitywake 0`
- HeliPort starts at login from `~/Library/LaunchAgents/local.heliport.plist`.
- Hide macOS's own Wi-Fi icon: `defaults write com.apple.controlcenter "NSStatusItem Visible WiFi" -bool false`.
- To make Macintosh HD the default entry, press Ctrl+Enter on it in the OpenCore picker.
