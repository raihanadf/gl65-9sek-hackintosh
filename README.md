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

Work in progress. The macOS install is in its second stage.

- Wi-Fi in Recovery works through itlwm with a network saved in `itlwm.kext/Contents/Info.plist` (`WiFiConfig`). There is no HeliPort in Recovery.
- PM981: unmount and format freezes are fixed. A stress test (10 × 2 GB write, delete, unmount/remount) passes.
- The installer used to panic after about 30 minutes with `Command timeout. Write` (see `docs/panics/`). That happened twice, with APST on and with APST off.
- With the early mask, the completion watchdog and `nand-io-timeoutms=180000`, the first install stage finishes and reboots into "macOS Installer". ioreg confirmed `early-mask = Yes`, `watchdog = Yes` and `apst = No`.
- Current issue: in the second install stage, the verbose text scrolls and then the screen goes black with the display off. It looks like the graphics handover, not the SSD. If it persists, add `-wegnoegpu` for the rest of the install.

### Progress log

| Date | Change | Result |
|------|--------|--------|
| 2026-10-15 | itlwm `WiFiConfig` | Recovery has internet, and the download works |
| 2026-10-15 | Early interrupt mask (Catalina order) | Unmount and format no longer freeze, and the stress test passes |
| 2026-10-15 | — | Install panic: `Command timeout. Write` at about 30 min |
| 2026-10-16 | APST off (`ps-max-latency-us = 0`) | Erase froze because of the upstream `initActivePM` heap overflow (fixed) |
| 2026-10-16 | APST off, host power management skipped | Install panic: same write timeout at about 27 min |
| 2026-10-16 | Completion watchdog and `nand-io-timeoutms=180000` | First install stage completes; the second stage shows a black screen |

## If an install crashes

Boot the 1401 entry, open Terminal and upload the state. `nvram: Error getting variable ... data was not found` only means that no crash report is stored.

```
{ echo "== panic"; nvram -x aapl,panic-info; echo "== ioreg"; ioreg -r -c IONVMeController -l -w0 | grep -E '"apst"|"quirks"|"early-mask"|"watchdog"'; } > /tmp/state.txt
```

Decode the crash report on another Mac with `python3 tools/decode_panic.py state.txt`.

## Next steps if the write timeout comes back

1. Add NVRAM diagnostics to the watchdog: when the completion queue makes no progress for 10+ seconds, save the queue state and controller status to an NVRAM variable, which survives the panic.
2. Put a SystemRescue partition in the free space on the USB stick. Use `nvme error-log`, `smart-log` and `fw-log`, and run `fio` with the installer's write pattern on the macOS partition only. This shows whether the firmware or macOS is at fault.
3. Write a 5-minute reproduction script (`ditto` of `/System`, parallel large `dd` writes, `sync`) so each fix doesn't need a 30-minute install.

## Before you use this EFI

- `PlatformInfo > Generic` is scrubbed. Generate your own `SystemSerialNumber`, `MLB`, `SystemUUID` and `ROM` (for example with GenSMBIOS) for MacBookPro16,1.
- Put your own Wi-Fi network in `EFI/OC/Kexts/itlwm.kext/Contents/Info.plist` under `WiFiConfig`.

## Why the PM981 fails on modern macOS

NVMeFix 1.0.2 fixed PM981 timeouts on Catalina by forcing `IONVMeController::ANS2MSIWorkaround`. That flag makes Apple's interrupt filter mask the interrupt *before* it checks the completion queues, and unmask through `HandleSpuriousInterrupt` when nothing is pending.

Apple removed the flag and that code path in macOS 11. Sequoia's `FilterInterruptRequest` checks the queues first and masks afterwards. Upstream NVMeFix still looks for the old instruction, does not find it, and silently skips the fix (`Ignoring ANS2 workaround patch on newer system`).

This was verified by disassembling `IONVMeFamily` from the Recovery images of 10.15.7, 12.5.1, 15.4.1 and 15.6.1.

## Changes in the NVMeFix fork (`NVMeFix/`)

Based on acidanthera/NVMeFix 1.1.4 (BSD licence, see `NVMeFix/LICENSE.txt`).

- **Early interrupt mask (Catalina order restored).** Apple's code is never patched. For a plain `IONVMeController`, the fork swaps the `filterAction` pointer of the controller's own `IOFilterInterruptEventSource` at `+0x68`. It only does so when that pointer still equals Apple's `FilterInterruptRequest` and the source's owner is the controller.
  - Every offset is read from Apple's code at boot.
  - The completion queue check must match byte for byte, otherwise nothing changes.
  - The status shows in ioreg as `early-mask`.
- **Completion watchdog.** Every 250 ms, on the controller's workloop, it checks the completion queues and signals Apple's handler if a completion is waiting without an interrupt. The status shows in ioreg as `watchdog`.
- **APST off on request.** With `ps-max-latency-us = 0`, APST is written off explicitly (like Linux `nvme_configure_apst`), and host power management is skipped so the drive stays at full power.
- **Upstream bug fix.** `PM::initActivePM` allocated `IOPMPowerState[entry.nstates]` while `nstates` was still 0, then wrote past the buffer. This is a kernel heap overflow that only happens when APST is not used.

### Building

```
cd NVMeFix
# put Lilu.kext (DEBUG release, contains the SDK headers) and MacKernelSDK in this folder first
xcodebuild -configuration Release
```

## OpenCore changes on top of 1401's output

| Setting | Value | Why |
|---------|-------|-----|
| `boot-args` | `+ nvme=512` | Apple's IONVMeFamily flag that disables Unmap (TRIM) |
| `boot-args` | `+ nand-io-timeoutms=180000` | 3-minute NVMe command timeout instead of 35 s (read by `GetDefaultRequestTimeout`) |
| `NVRAM > Delete` | `boot-args` | so changes to `boot-args` take effect |
| `Kernel > Quirks > SetApfsTrimTimeout` | `0` | no APFS trim at mount |
| `DeviceProperties` `PciRoot(0x0)/Pci(0x1D,0x0)/Pci(0x0,0x0)` | `ps-max-latency-us = 0` (8 bytes) | APST off |

## Verification tools (`tools/`)

- `kcsyms.py`, `kcdump.py`, `dis.py`: list symbols, dump bytes and disassemble a kext inside a fileset kernel collection (`BootKernelExtensions.kc`).
- `verify2.py`: replays the fork's offset detection against a real `IONVMeFamily`.
- `diff_filter.py`: differential test of Apple's real `FilterInterruptRequest` (Unicorn emulation) against `mirror/pending.c`.
- `e2e3.py`: end-to-end emulation of the compiled kext together with Apple's real functions, recording NVMe register writes.
- `decode_panic.py`: decodes `nvram -x aapl,panic-info` output (7-bit packed) into readable text.

## Crash reports (`docs/panics/`)

Both install crashes show `3rd party NVMe controller. Command timeout. Write ... CSTS=0x1` on macOS 24G90 (15.6.1), at about 30 minutes into the install.
