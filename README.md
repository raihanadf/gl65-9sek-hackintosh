### MSI GL65 9SEK Hackintosh
[![macOS](https://img.shields.io/badge/macOS_Sequoia-15.8.1-brightgreen.svg)](https://www.apple.com/macos/sequoia)
[![OpenCore](https://img.shields.io/badge/OpenCore-1.0.9-blue.svg)](https://github.com/acidanthera/OpenCorePkg)
[![DualBoot](https://img.shields.io/badge/DualBoot-8A2BE2.svg)]()

OpenCore EFI generated with [1401](https://github.com/nullmoth/1401), plus a patched [NVMeFix](NVMeFix) that makes the factory **Samsung PM981** SSD work.

### Hardware
| Component      | Info                                          |
|----------------|-----------------------------------------------|
| **CPU**        | Intel Core i7-9750H                           |
| **Storage**    | Samsung PM981 512GB (MZVLB512HAJQ), shared with Windows |
| **Screen**     | 15.6" 1920x1080 144Hz                         |
| **iGPU**       | Intel UHD Graphics 630                        |
| **dGPU**       | Nvidia RTX 2060 (NullMoth driver, Metal 3)    |
| **Audio**      | Realtek ALC269                                |
| **Ethernet**   | Realtek RTL8111                               |
| **WiFi / BT**  | Intel Wi-Fi 6E AX210                          |
| **SMBIOS**     | MacBookPro16,1                                |

### Status
**Everything works!** PM981 with TRIM, Metal 3 graphics, RTX 2060 with HDMI output (NullMoth driver), sleep/wake, Wi-Fi, Bluetooth and AirPods, audio, USB.

**Note:** the laptop is muxless, so the internal panel always runs on the Intel GPU and the RTX 2060 drives the HDMI and USB-C outputs. The EFI in this repo ships with the RTX powered off (`-wegnoegpu`, `SSDT-dGPU-Off`); see [PROGRESS.md](PROGRESS.md) for the NullMoth setup.

### Progress
The full status, progress log, crash-report workflow and setup notes are in [PROGRESS.md](PROGRESS.md).
