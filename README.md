# MSI GL65 9SEK Hackintosh (macOS Sequoia)

OpenCore EFI for the MSI GL65 9SEK, generated with [1401](https://github.com/nullmoth/1401) and extended to make the factory **Samsung PM981 (MZVLB512HAJQ)** NVMe SSD usable under macOS Sequoia.

## Hardware

| Part | Model |
|------|-------|
| CPU | Intel Core i7-9750H (Coffee Lake-H) |
| iGPU / dGPU | Intel UHD 630 / NVIDIA RTX 2060 (NullMoth driver via 1401) |
| SSD | Samsung PM981 MZVLB512HAJQ-00000, firmware EXA7301Q (shared with Windows) |
| Wi-Fi / BT | Intel AX210 (Wi-Fi 6E), itlwm + IntelBluetoothFirmware |
| Ethernet | Realtek RTL8111 |
| SMBIOS | MacBookPro16,1 |

## Status

**macOS Sequoia 15.8.1 runs on the GL65 9SEK and its factory PM981 SSD. Daily use works.**

| Area | State |
|------|-------|
| SSD (PM981) | Works: ~2.0 GB/s write, ~3.3 GB/s read, survives sleep/wake. TRIM is still off (not tested yet). |
| Intel UHD 630 | Accelerated (Metal 3), internal 1080p 144 Hz panel with backlight. |
| RTX 2060 | Powered off in macOS (`SSDT-dGPU-Off`); the GPU LED is white. |
| Sleep / wake | Works (`SSDT-PTSWAK` with `_PTS`/`_WAK` renames, hibernation off). |
| Wi-Fi | itlwm with HeliPort for the menu-bar icon (macOS's own Wi-Fi icon hidden). |
| Audio | ALC269, `layout-id` 66. After a forced power-off, do a full shutdown so the codec resets. |
| USB | Custom `UTBMap.kext` (12 ports, including port 9 for the SteelSeries KLC keyboard lighting). |
| Boot | OpenCore on the SSD's own EFI partition (`disk0s4`), registered in the BIOS (`LauncherOption = Full`). Windows keeps its own ESP. |

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

## If an install crashes

Boot the 1401 entry, open Terminal and upload the state. `nvram: Error getting variable ... data was not found` only means that no crash report is stored.

```
{ echo "== panic"; nvram -x aapl,panic-info; echo "== ioreg"; ioreg -r -c IONVMeController -l -w0 | grep -E '"apst"|"quirks"|"early-mask"|"watchdog"'; } > /tmp/state.txt
```

Decode the crash report on another Mac with `python3 tools/decode_panic.py state.txt`.

## Next steps

1. Test TRIM again (`nvme=512` and `SetApfsTrimTimeout`), with a backup first.
2. Optional: try AirportItlwm for the native Wi-Fi menu (on Sequoia it needs an OCLP root patch).

## Post-install notes

- `pmset -a hibernatemode 0 standby 0 autopoweroff 0 powernap 0 tcpkeepalive 0 proximitywake 0`
- HeliPort starts at login from `~/Library/LaunchAgents/local.heliport.plist`.
- Hide macOS's own Wi-Fi icon: `defaults write com.apple.controlcenter "NSStatusItem Visible WiFi" -bool false`.
- To make Macintosh HD the default entry, press Ctrl+Enter on it in the OpenCore picker.
