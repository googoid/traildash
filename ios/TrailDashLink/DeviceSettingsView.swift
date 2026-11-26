import SwiftUI

/// TrailDash's own settings, live over Bluetooth. Every change applies on the device at
/// once (no Save button, like iOS Settings) and is stored in config.txt on its SD card.
struct DeviceSettingsView: View {
    @EnvironmentObject private var link: LinkManager

    var body: some View {
        content
            .navigationTitle("Device Settings")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                if link.configSaving {
                    ToolbarItem(placement: .topBarTrailing) { ProgressView() }
                }
            }
            .alert("TrailDash", isPresented: errorShown) {
                Button("OK", role: .cancel) {}
            } message: {
                Text(link.configError ?? "")
            }
            // fresh status (sensor, SD card) each time the screen opens
            .task { await link.reloadConfig() }
    }

    @ViewBuilder
    private var content: some View {
        switch link.configState {
        case .unavailable:
            ContentUnavailableView {
                Label("Not Connected", systemImage: "antenna.radiowaves.left.and.right.slash")
            } description: {
                Text("Settings appear here as soon as TrailDash connects. Turn on the ignition and keep the phone nearby.")
            }
        case .loading:
            ProgressView("Reading settings…")
                .frame(maxWidth: .infinity, maxHeight: .infinity)
        case .unsupported:
            ContentUnavailableView {
                Label("Firmware Update Needed", systemImage: "arrow.down.circle")
            } description: {
                Text("This TrailDash firmware can't be configured from the app. Flash the latest firmware, or edit config.txt on its SD card.")
            }
        case .ready:
            SettingsForm()
        }
    }

    private var errorShown: Binding<Bool> {
        Binding(get: { link.configError != nil }, set: { if !$0 { link.configError = nil } })
    }
}

// MARK: - Form

private struct SettingsForm: View {
    @EnvironmentObject private var link: LinkManager
    private typealias Key = DeviceConfig.Key

    private var config: DeviceConfig { link.config }

    var body: some View {
        Form {
            if !config.hasCard {
                Section {
                    Label {
                        Text("No SD card in TrailDash. Changes work now but are lost when it powers off.")
                    } icon: {
                        Image(systemName: "sdcard").foregroundStyle(.orange)
                    }
                    .font(.subheadline)
                }
            }

            if config.has(Key.rollWarn) {
                Section {
                    AngleZonesRow(title: "Roll", systemImage: "car.rear",
                                  warn: number(Key.rollWarn, 20), danger: number(Key.rollDanger, 30))
                    AngleZonesRow(title: "Pitch", systemImage: "car.side",
                                  warn: number(Key.pitchWarn, 25), danger: number(Key.pitchDanger, 35))
                    if config.has(Key.buzzer) {
                        Toggle(isOn: bool(Key.buzzer)) {
                            Label("Alarm Sound", systemImage: "speaker.wave.2")
                        }
                    }
                } header: {
                    Text("Tilt Warnings")
                } footer: {
                    Text("The gauge turns amber at the warning angle, and red at the danger angle, where the alarm sounds.")
                }
            }

            if config.has(Key.backlight) {
                Section("Display") {
                    BrightnessRow(value: number(Key.backlight, 85))
                }
            }

            if config.has(Key.redline) {
                Section {
                    Stepper(value: number(Key.redline, 5000), in: 2000...6000, step: 250) {
                        HStack {
                            Label("Red Zone", systemImage: "gauge.with.dots.needle.67percent")
                            Spacer()
                            Text("\(Int(config.number(Key.redline) ?? 0)) rpm")
                                .monospacedDigit()
                                .foregroundStyle(.secondary)
                        }
                        .accessibilityElement(children: .combine)
                    }
                } header: {
                    Text("Rev Gauge")
                }
            }

            if config.has(Key.speedCorrection) {
                SpeedCorrectionSection(percent: config.number(Key.speedCorrection) ?? 0)
            }

            if config.has(Key.log) {
                DriveLogSection(enabled: bool(Key.log), interval: number(Key.logInterval, 1))
            }

            if config.has(Key.remoteForward) {
                SensorSection(forward: string(Key.remoteForward))
            }

            if config.has(Key.obdName) {
                OBDSection(name: config.string(Key.obdName), probe: config.has(Key.obdProbe) ? bool(Key.obdProbe) : nil)
            }

            ActionsSection()
        }
        .refreshable { await link.reloadConfig() }
    }

    // Bindings read from the device config and write changes straight back to it.
    private func number(_ key: String, _ fallback: Double) -> Binding<Double> {
        Binding(get: { link.config.number(key) ?? fallback },
                set: { link.setConfig(key, DeviceConfig.format($0)) })
    }

    private func bool(_ key: String) -> Binding<Bool> {
        Binding(get: { link.config.bool(key) }, set: { link.setConfig(key, DeviceConfig.format($0)) })
    }

    private func string(_ key: String) -> Binding<String> {
        Binding(get: { link.config.string(key) }, set: { link.setConfig(key, $0) })
    }
}

// MARK: - Tilt warnings

/// Warning and danger angle of one axis, with a bar that shows the resulting colour zones.
private struct AngleZonesRow: View {
    let title: String
    let systemImage: String
    @Binding var warn: Double
    @Binding var danger: Double

    private static let maxAngle = 60.0

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Label(title, systemImage: systemImage).font(.headline)
            ZoneBar(warn: warn, danger: danger, max: Self.maxAngle)
            // the ranges keep danger above warning, as the device requires
            Stepper(value: $warn, in: 1...max(1, danger - 1)) {
                zoneLabel("Warning", angle: warn, color: .orange)
            }
            Stepper(value: $danger, in: min(Self.maxAngle, warn + 1)...Self.maxAngle) {
                zoneLabel("Danger", angle: danger, color: .red)
            }
        }
        .padding(.vertical, 4)
    }

    private func zoneLabel(_ name: String, angle: Double, color: Color) -> some View {
        HStack {
            Circle().fill(color).frame(width: 8, height: 8)
            Text(name)
            Spacer()
            Text("\(Int(angle))°").monospacedDigit().foregroundStyle(.secondary)
        }
        .accessibilityElement(children: .combine)
    }
}

private struct ZoneBar: View {
    let warn: Double
    let danger: Double
    let max: Double

    var body: some View {
        GeometryReader { geo in
            let w = geo.size.width
            HStack(spacing: 2) {
                Rectangle().fill(.green.opacity(0.7)).frame(width: w * warn / max)
                Rectangle().fill(.orange).frame(width: w * (danger - warn) / max)
                Rectangle().fill(.red)
            }
            .clipShape(Capsule())
        }
        .frame(height: 6)
        .animation(.snappy, value: warn)
        .animation(.snappy, value: danger)
        .accessibilityHidden(true)
    }
}

// MARK: - Display

private struct BrightnessRow: View {
    @Binding var value: Double

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            LabeledContent("Brightness") {
                Text("\(Int(value)) %").monospacedDigit()
            }
            // the screen follows the slider live while dragging
            Slider(value: $value, in: 5...100, step: 1) {
                Text("Brightness")
            } minimumValueLabel: {
                Image(systemName: "sun.min")
            } maximumValueLabel: {
                Image(systemName: "sun.max")
            }
        }
    }
}

// MARK: - Drive log

private struct DriveLogSection: View {
    @Binding var enabled: Bool
    @Binding var interval: Double

    private static let intervals: [Double] = [0.2, 0.5, 1, 2, 5, 10, 30, 60]

    var body: some View {
        Section {
            Toggle(isOn: $enabled) {
                Label("Record Drive Log", systemImage: "list.bullet.rectangle")
            }
            if enabled {
                Picker("Interval", selection: $interval) {
                    // a value set in config.txt by hand stays selectable
                    ForEach(options, id: \.self) { Text(label($0)).tag($0) }
                }
            }
        } header: {
            Text("Drive Log")
        } footer: {
            Text("One CSV file per drive on the SD card: angles, speed, heading and engine values.")
        }
    }

    private var options: [Double] {
        Self.intervals.contains(interval) ? Self.intervals : (Self.intervals + [interval]).sorted()
    }

    private func label(_ s: Double) -> String {
        s < 1 ? "\(Int((s * 1000).rounded())) ms" : s == 1 ? "1 second" : "\(DeviceConfig.format(s)) seconds"
    }
}

// MARK: - Remote sensor

private struct SensorSection: View {
    @EnvironmentObject private var link: LinkManager
    @Binding var forward: String
    @State private var changed = false

    private static let axes = ["+x", "-x", "+y", "-y", "+z", "-z"]

    var body: some View {
        Section {
            Picker(selection: Binding(get: { forward }, set: { forward = $0; changed = true })) {
                ForEach(Self.axes, id: \.self) { Text(label($0)).tag($0) }
            } label: {
                Label("Forward Axis", systemImage: "arrow.up.forward.circle")
            }
            if changed && !link.config.leveling {
                Label("Set level again so the new axis is used.", systemImage: "exclamationmark.triangle.fill")
                    .font(.subheadline)
                    .foregroundStyle(.orange)
            }
        } header: {
            Text("Remote Sensor")
        } footer: {
            Text("The axis of the sensor board that points to the front of the car. \(link.config.sensorLive ? "The sensor is connected." : "The sensor is not connected.")")
        }
    }

    private func label(_ axis: String) -> String {
        let sign = axis.hasPrefix("-") ? "−" : "+"
        let name = axis.dropFirst().uppercased()
        return axis == "+x" ? "\(sign)\(name) (board arrow)" : "\(sign)\(name)"
    }
}

// MARK: - Speed correction

/// Car (OBD) speed correction in %, typed in: the speed check screen on the device shows the value
/// to enter ("SET SPEED CORRECTION +8.7 %").
private struct SpeedCorrectionSection: View {
    @EnvironmentObject private var link: LinkManager
    let percent: Double

    @State private var draft = ""
    @FocusState private var editing: Bool
    static let range = -20.0...20.0

    var body: some View {
        Section {
            LabeledContent {
                HStack(spacing: 4) {
                    TextField("0", text: $draft)
                        .multilineTextAlignment(.trailing)
                        .keyboardType(.numbersAndPunctuation)   // has the minus sign, unlike the decimal pad
                        .submitLabel(.done)
                        .focused($editing)
                        .onSubmit(commit)
                        .monospacedDigit()
                    Text("%").foregroundStyle(.secondary)
                }
            } label: {
                Label("Speed Correction", systemImage: "speedometer")
            }
        } header: {
            Text("Car Speed")
        } footer: {
            Text("Corrects the speed read from the car (wrong tyre size, speedometer error): +8 % makes it 8 % higher. Drive a few km above 20 km/h with the phone connected, then enter the value TrailDash's speed check screen suggests.")
        }
        .onAppear { draft = Self.text(percent) }
        .onChange(of: percent) { _, new in if !editing { draft = Self.text(new) } }
        .onChange(of: editing) { _, isEditing in if !isEditing { commit() } }
    }

    private static func text(_ v: Double) -> String { v == 0 ? "" : String(format: "%+.1f", v) }

    private func commit() {
        // accept "8.7", "+8,7", "-3" (comma or point, whatever the keyboard gave)
        let t = draft.trimmingCharacters(in: .whitespaces).replacingOccurrences(of: ",", with: ".")
        guard let v = t.isEmpty ? 0 : Double(t.replacingOccurrences(of: "+", with: "")) else {
            draft = Self.text(percent)   // not a number: show the device's value again
            return
        }
        let clamped = (min(max(v, Self.range.lowerBound), Self.range.upperBound) * 10).rounded() / 10
        draft = Self.text(clamped)
        if clamped != percent { link.setConfig(DeviceConfig.Key.speedCorrection, DeviceConfig.format(clamped)) }
    }
}

// MARK: - OBD

private struct OBDSection: View {
    @EnvironmentObject private var link: LinkManager
    let name: String
    let probe: Binding<Bool>?

    @State private var draft = ""
    @FocusState private var editing: Bool

    var body: some View {
        Section {
            LabeledContent {
                TextField("Automatic", text: $draft)
                    .multilineTextAlignment(.trailing)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
                    .submitLabel(.done)
                    .focused($editing)
                    .onSubmit(commit)
            } label: {
                Label("Adapter Name", systemImage: "car.badge.gearshape")
            }
            if let probe {
                Toggle("Log Maker Data", isOn: probe)
            }
        } header: {
            Text("OBD Adapter")
        } footer: {
            Text("Part of the adapter's Bluetooth name, only needed if it isn't found automatically. Logging maker data (probe.txt) takes effect the next time TrailDash starts and slows the engine values a little.")
        }
        .onAppear { draft = name }
        .onChange(of: name) { _, new in if !editing { draft = new } }
        .onChange(of: editing) { _, isEditing in if !isEditing { commit() } }
    }

    private func commit() {
        // the device stores at most 23 characters, lower case
        let clean = String(draft.trimmingCharacters(in: .whitespaces).lowercased()
            .filter { $0 != "#" && $0 != "=" && !$0.isNewline }.prefix(23))
        draft = clean
        if clean != name { link.setConfig(DeviceConfig.Key.obdName, clean) }
    }
}

// MARK: - Actions

private struct ActionsSection: View {
    @EnvironmentObject private var link: LinkManager

    private enum Confirm: Identifiable {
        case level, peaks, trip, tripB
        var id: Self { self }
    }
    private enum LevelPhase { case idle, leveling, done }

    @State private var confirm: Confirm?
    @State private var levelPhase = LevelPhase.idle
    @State private var successes = 0   // drives the success haptic

    var body: some View {
        Section {
            Button { confirm = .level } label: {
                HStack {
                    Label("Set Level", systemImage: "level")
                    Spacer()
                    levelStatus
                }
            }
            .disabled(levelPhase == .leveling || !link.config.sensorLive)

            Button(role: .destructive) { confirm = .peaks } label: {
                Label("Reset Peak Angles", systemImage: "arrow.counterclockwise")
            }
            Button(role: .destructive) { confirm = .trip } label: {
                Label("Reset Trip and Max Speed", systemImage: "road.lanes")
            }
            Button(role: .destructive) { confirm = .tripB } label: {
                Label("Reset Trip B and Average Fuel", systemImage: "fuelpump")
            }
        } header: {
            Text("Calibrate and Reset")
        } footer: {
            if !link.config.sensorLive {
                Text("Set Level needs the remote sensor connected.")
            } else if !link.config.levelSet {
                Text("Level isn't set yet: park on flat ground and tap Set Level.")
            }
        }
        .confirmationDialog(title, isPresented: Binding(get: { confirm != nil }, set: { if !$0 { confirm = nil } }),
                            titleVisibility: .visible, presenting: confirm) { what in
            switch what {
            case .level: Button("Set Level") { setLevel() }
            case .peaks: Button("Reset Peaks", role: .destructive) { run(.resetPeaks) }
            case .trip: Button("Reset Trip", role: .destructive) { run(.resetTrip) }
            case .tripB: Button("Reset Trip B", role: .destructive) { run(.resetTripB) }
            }
        } message: { what in
            switch what {
            case .level: Text("Park on flat, level ground. The car must stay still for a few seconds.")
            case .peaks: Text("The highest roll and pitch angles are cleared.")
            case .trip: Text("Trip distance and max speed start again from zero. Trip B is kept.")
            case .tripB: Text("Trip B and the average fuel use start again from zero.")
            }
        }
        .sensoryFeedback(.success, trigger: successes)
    }

    private var title: String {
        switch confirm {
        case .level: "Set level now?"
        case .peaks: "Reset peak angles?"
        case .trip: "Reset trip?"
        case .tripB: "Reset trip B?"
        case nil: ""
        }
    }

    @ViewBuilder
    private var levelStatus: some View {
        switch levelPhase {
        case .leveling:
            HStack(spacing: 6) {
                ProgressView()
                Text("Keep still…").foregroundStyle(.secondary)
            }
        case .done:
            Label("Done", systemImage: "checkmark.circle.fill")
                .labelStyle(.iconOnly)
                .foregroundStyle(.green)
                .transition(.scale.combined(with: .opacity))
        case .idle:
            EmptyView()
        }
    }

    private func run(_ command: DeviceCommand) {
        Task {
            do {
                try await link.perform(command)
                successes += 1
            } catch {
                link.configError = error.localizedDescription
            }
        }
    }

    /// Start levelling, then follow it through the device status until it's finished
    /// (it averages the sensor for about a second).
    private func setLevel() {
        Task {
            do {
                withAnimation { levelPhase = .leveling }
                try await link.perform(.setLevel)
                try await Task.sleep(for: .milliseconds(300))
                await link.reloadConfig()
                guard link.config.leveling else {
                    // the device didn't start: the sensor dropped out in the meantime
                    throw LevelError.noSensor
                }
                let deadline = Date.now.addingTimeInterval(10)
                while link.config.leveling && Date.now < deadline {
                    try await Task.sleep(for: .milliseconds(400))
                    await link.reloadConfig()
                }
                withAnimation { levelPhase = .done }
                successes += 1
                try await Task.sleep(for: .seconds(3))
                withAnimation { levelPhase = .idle }
            } catch {
                withAnimation { levelPhase = .idle }
                if !(error is CancellationError) { link.configError = error.localizedDescription }
            }
        }
    }

    private enum LevelError: LocalizedError {
        case noSensor
        var errorDescription: String? { "Level wasn't set: the remote sensor isn't sending data." }
    }
}

#if DEBUG
#Preview {
    LinkManager.shared.loadDemoDevice()
    return NavigationStack { DeviceSettingsView() }
        .environmentObject(LinkManager.shared)
        .preferredColorScheme(.dark)
}
#endif
