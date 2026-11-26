import SwiftUI

@main
struct TrailDashLinkApp: App {
    // Created at process start — also on background relaunches (BLE restore,
    // significant location change), where no UI is built.
    @StateObject private var link = LinkManager.shared

    init() {
        _ = LinkManager.shared
        #if DEBUG
        if ProcessInfo.processInfo.arguments.contains("-demoDevice") { LinkManager.shared.loadDemoDevice() }
        #endif
    }

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(link)
                .preferredColorScheme(.dark)
        }
    }
}
