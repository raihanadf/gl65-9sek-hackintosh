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

**macOS Sequoia 15.8.1 (24H32) installs and boots on the factory PM981.**

- The install completes, and the system boots to Setup Assistant from the PM981.
- Wi-Fi works through itlwm with a network saved in `itlwm.kext/Contents/Info.plist` (`WiFiConfig`), in Recovery and in macOS.
- Graphics: there is no acceleration yet. With accelerated Intel graphics the internal panel goes black (tried `0x3EA50009` and `0x3E9B0000`), so `-igfxvesa` is set for now.
- The RTX 2060 is hidden with `-wegnoegpu`, but it is not powered off (the GPU LED stays orange).
- OpenCore only lives on the USB stick. It is not installed on the SSD yet.
- TRIM is off (`nvme=512`, `SetApfsTrimTimeout=0`). It has not been tested again since the install.

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

## If an install crashes

Boot the 1401 entry, open Terminal and upload the state. `nvram: Error getting variable ... data was not found` only means that no crash report is stored.

```
{ echo "== panic"; nvram -x aapl,panic-info; echo "== ioreg"; ioreg -r -c IONVMeController -l -w0 | grep -E '"apst"|"quirks"|"early-mask"|"watchdog"'; } > /tmp/state.txt
```

Decode the crash report on another Mac with `python3 tools/decode_panic.py state.txt`.

## Next steps

1. Fix accelerated Intel graphics on the internal panel, then drop `-igfxvesa`.
2. Install OpenCore on the SSD's EFI partition next to the Windows Boot Manager, and add a boot entry.
3. Power the RTX 2060 off (SSDT-dGPU-Off, `\_SB.PCI0.PEG0.PEGP`).
4. Map the USB ports (USBToolBox), and check audio (`layout-id`).
5. Test TRIM again, then `SetApfsTrimTimeout` and `nvme=512`, with a backup first.
6. Test sleep and wake.
