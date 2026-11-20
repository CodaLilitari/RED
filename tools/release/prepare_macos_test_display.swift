// Select a native display mode on the disposable hosted macOS runner.
// Core Graphics reports logical dimensions separately from backing pixel sizes.
import CoreGraphics
import Foundation

let display = CGMainDisplayID()
guard let current = CGDisplayCopyDisplayMode(display),
      let modes = CGDisplayCopyAllDisplayModes(display, nil) as? [CGDisplayMode] else {
    throw NSError(domain: "RED test display", code: 1,
                  userInfo: [NSLocalizedDescriptionKey: "Cannot query the Cocoa test display"])
}

func description(_ mode: CGDisplayMode) -> String {
    "\(mode.width)x\(mode.height) points (\(mode.pixelWidth)x\(mode.pixelHeight) pixels)"
}

print("Current display: \(description(current))")
print("Advertised display modes:", Set(modes.map(description)).sorted().joined(separator: ", "))
// Prefer the desktop size used by the X11 gate. Two complete dock panels
// also fit in 1920x1080; keep larger desktops already supplied by the runner.
if current.width < 1920 || current.height < 1080 {
    let fitting = modes.filter { $0.width >= 1920 && $0.height >= 1080 }
    let target = fitting.first { $0.width == 2560 && $0.height == 1440 }
        ?? fitting.min { $0.width * $0.height < $1.width * $1.height }
    if let target {
        var configuration: CGDisplayConfigRef?
        guard CGBeginDisplayConfiguration(&configuration) == .success,
              let configuration else {
            throw NSError(domain: "RED test display", code: 2)
        }
        let result = CGConfigureDisplayWithDisplayMode(configuration, display, target, nil)
        if result != .success {
            CGCancelDisplayConfiguration(configuration)
            throw NSError(domain: "RED test display", code: Int(result.rawValue))
        }
        guard CGCompleteDisplayConfiguration(configuration, .forSession) == .success,
              let actual = CGDisplayCopyDisplayMode(display),
              actual.width == target.width, actual.height == target.height else {
            throw NSError(domain: "RED test display", code: 3,
                          userInfo: [NSLocalizedDescriptionKey: "Native display mode readback failed"])
        }
        print("Selected display: \(description(actual))")
    } else {
        print("No larger native mode fits both editor panels. Running the full suite on the reported desktop.")
    }
}
