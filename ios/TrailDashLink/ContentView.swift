import SwiftUI
import CoreLocation

struct ContentView: View {
    @EnvironmentObject private var link: LinkManager
    @Environment(\.scenePhase) private var scenePhase
    #if DEBUG
    @State private var showSettings = ProcessInfo.processInfo.arguments.contains("-demoDevice")
    #else
    @State private var showSettings = false
    #endif

    var body: some View {
        NavigationStack {
            List {
                Section {
                    speedHero
                        .frame(maxWidth: .infinity)
                        .listRowBackground(Color.clear)
                }

                Section("TrailDash") {
                    row("Bluetooth", link.bleState.rawValue, color: bleColor)
                    if let name = link.deviceName { row("Device", name) }
                    row("Packets sent", "\(link.packetsSent)")
                    NavigationLink {
                        DeviceSettingsView()
                    } label: {
                        Label("Device Settings", systemImage: "slider.horizontal.3")
                    }
                    if link.hasRememberedDevice {
                        Button("Forget device", role: .destructive) { link.forgetDevice() }
                    }
                }

                Section {
                    row("Permission", authText, color: authOK ? .secondary : .orange)
                    if let loc = link.lastLocation {
                        row("Accuracy", String(format: "± %.0f m", loc.horizontalAccuracy))
                        if loc.speedAccuracy >= 0 {
                            row("Speed accuracy", String(format: "± %.1f km/h", loc.speedAccuracy * 3.6))
                        }
                    }
                    if link.locationAuth == .notDetermined {
                        Button("Allow location") { link.requestLocationIfNeeded() }
                    }
                    if link.locationAuth == .authorizedWhenInUse {
                        Text("Set Location to **Always** — needed to start GPS when TrailDash connects while the phone is locked.")
                            .font(.footnote)
                            .foregroundStyle(.orange)
                    }
                    if link.locationAuth != .authorizedAlways && link.locationAuth != .notDetermined {
                        Button("Open Settings") {
                            if let url = URL(string: UIApplication.openSettingsURLString) {
                                UIApplication.shared.open(url)
                            }
                        }
                    }
                } header: {
                    Text("GPS")
                } footer: {
                    Text("GPS runs only while TrailDash is connected — also with the phone locked. When the car powers up, iOS wakes the app and it reconnects by itself.")
                }

                Section {
                    Toggle("Send speed to TrailDash", isOn: $link.enabled)
                    Toggle("Restart after swipe-away", isOn: $link.relaunchAfterSwipe)
                } footer: {
                    Text("iOS won't reopen a swiped-away app for Bluetooth. This option lets it reopen after you drive a few hundred metres, using low-power location — the location arrow then stays on all the time.")
                }
            }
            .navigationTitle("TrailDash Link")
            .navigationDestination(isPresented: $showSettings) { DeviceSettingsView() }
            .onChange(of: scenePhase) { _, phase in
                if phase == .active { link.requestLocationIfNeeded() }
            }
        }
    }

    private var speedHero: some View {
        let loc = link.lastLocation
        let speed = loc.flatMap { $0.speed >= 0 ? $0.speed * 3.6 : nil }
        let course = loc.flatMap { $0.course >= 0 ? $0.course : nil }
        return VStack(spacing: 4) {
            Text(speed.map { String(format: "%.0f", $0) } ?? "--")
                .font(.system(size: 96, weight: .bold, design: .rounded))
                .monospacedDigit()
            Text("km/h").font(.headline).foregroundStyle(.secondary)
            Text(course.map { String(format: "%@  %03.0f°", compass($0), $0) } ?? "heading --")
                .font(.title3.monospacedDigit())
                .foregroundStyle(.secondary)
                .padding(.top, 6)
        }
        .padding(.vertical, 12)
    }

    private func row(_ title: String, _ value: String, color: Color = .secondary) -> some View {
        HStack {
            Text(title)
            Spacer()
            Text(value).foregroundStyle(color)
        }
    }

    private var bleColor: Color {
        switch link.bleState {
        case .connected: .green
        case .searching, .connecting: .secondary
        default: .orange
        }
    }

    private var authOK: Bool {
        link.locationAuth == .authorizedWhenInUse || link.locationAuth == .authorizedAlways
    }

    private var authText: String {
        switch link.locationAuth {
        case .authorizedAlways: "Always"
        case .authorizedWhenInUse: "While using"
        case .denied: "Denied"
        case .restricted: "Restricted"
        default: "Not asked"
        }
    }

    private func compass(_ deg: Double) -> String {
        ["N", "NE", "E", "SE", "S", "SW", "W", "NW"][Int((deg + 22.5) / 45) % 8]
    }
}
