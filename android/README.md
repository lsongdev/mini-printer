# Mini Printer for Android

Native Android client for the printer's BLE `MP` protocol. The single screen
follows the iOS app: discover and connect to a printer, inspect its live status,
choose a photo, set density, and print a photo or a short test page.

## Project layout

| Path | Responsibility |
| --- | --- |
| `app/src/main/java/com/embeded/miniprinter/MainActivity.kt` | Bluetooth permission and system photo picker launchers |
| `PrinterViewModel.kt` | Screen state, print jobs, and lifecycle ownership |
| `ui/PrinterScreen.kt` | Material 3 Compose screen |
| `bluetooth/BlePrinterTransport.kt` | Android BLE scanning, GATT connection, status notifications, and acknowledged writes |
| `protocol/PrinterProtocol.kt` | MP v1 frames and status decoding |
| `printing/PrintRasterizer.kt` | 384-dot photo dithering and test page raster |

The app uses Android's Bluetooth APIs and Photo Picker. It does not require
storage permission or a third-party Bluetooth stack. Android 12 and newer ask
for Nearby devices permission; Android 9–11 ask for location permission to scan
for BLE devices. Print transport and protocol match the iOS client and
[`../docs/protocol.md`](../docs/protocol.md).

## Build

Use Android Studio with JDK 17 or newer and an installed Android SDK. The
Gradle wrapper uses Gradle 9.6, Android Gradle Plugin 9.4, and the stable
Compose BOM `2026.09.00`. The project compiles against API 37 and targets API
36; Gradle can install the required SDK platform after its license is accepted.

```sh
cd android
./gradlew :app:assembleDebug
```

The APK is written to `app/build/outputs/apk/debug/`. BLE discovery and
printing require a physical Android device and printer; an emulator can only
exercise the screen layout.
