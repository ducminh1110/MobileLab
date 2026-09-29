// swift-tools-version: 6.0
import PackageDescription

// The logic of the macOS app lives in IOSLabDashboardCore: models that match the backend API, the API client,
// the event-stream client, reducers, navigator trees, log classifier and formatters. It has no SwiftUI, AppKit
// or Combine dependency, so it builds and is unit tested on Linux with `swift test`.
//
// The Xcode project (IOSLabDashboard.xcodeproj) compiles these same files straight into the app target; the
// SwiftUI layer in IOSLabDashboard/ only draws what Core computes.
let package = Package(
    name: "IOSLabDashboardCore",
    platforms: [.macOS(.v13)],
    products: [
        .library(name: "IOSLabDashboardCore", targets: ["IOSLabDashboardCore"])
    ],
    targets: [
        .target(name: "IOSLabDashboardCore"),
        .testTarget(
            name: "IOSLabDashboardCoreTests",
            dependencies: ["IOSLabDashboardCore"],
            resources: [.copy("Fixtures")]
        )
    ]
)
