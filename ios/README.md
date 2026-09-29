# Mini Printer for iOS

A native SwiftUI client for the ESP32 Mini Printer. It discovers the printer with CoreBluetooth, shows paper/battery/print-head status, and prints a photo or a built-in test page through the repository's [MP v1 protocol](../docs/protocol.md).

## Requirements

- Xcode 16 or newer and iOS 17 or newer
- A physical iPhone for Bluetooth printing (the simulator is for UI and unit tests)
- [XcodeGen](https://github.com/yonaskolb/XcodeGen) to regenerate the checked-in Xcode project

```sh
cd ios
xcodegen -s project.yml
open MiniPrinter.xcodeproj
```

The target's bundle ID and development team are set in `project.yml`. Select your own team in Xcode when signing for a physical device.

## Layout

```text
MiniPrinter/Sources/
  App/        SwiftUI entry point
  Features/   Printer screen and observable presentation state
  Bluetooth/  CoreBluetooth transport and GATT write acknowledgements
  Protocol/   MP frame and status encoding/decoding
  Printing/   384-dot monochrome raster generation
MiniPrinter/Resources/  App icon
MiniPrinterTests/       Protocol and raster tests
project.yml             XcodeGen project definition
```

The interface uses SwiftUI controls and the system Photos picker. The transport sends acknowledged GATT writes in batches of eight raster lines, matching the Python test client. No third-party runtime packages are required.

Run tests with:

```sh
xcodebuild -project MiniPrinter.xcodeproj -scheme MiniPrinter \
  -destination 'platform=iOS Simulator,name=iPhone Air' CODE_SIGNING_ALLOWED=NO test
```
