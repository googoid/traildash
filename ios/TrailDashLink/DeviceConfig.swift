import Foundation

/// TrailDash's settings as read over BLE — keep in sync with src/ble.h (CFG_*) and
/// src/settings.cpp. The wire format is the device's config.txt syntax: "key=value" lines.
///
/// Settings are kept as text, keyed like config.txt, so the app shows only what the connected
/// firmware reports (older or newer firmware simply has fewer or more keys).
struct DeviceConfig: Equatable {
    private(set) var values: [String: String] = [:]

    init() {}

    init(data: Data) {
        for line in String(decoding: data, as: UTF8.self).split(whereSeparator: \.isNewline) {
            guard let eq = line.firstIndex(of: "=") else { continue }
            let key = line[..<eq].trimmingCharacters(in: .whitespaces)
            let value = line[line.index(after: eq)...].trimmingCharacters(in: .whitespaces)
            if !key.isEmpty { values[key] = value }
        }
    }

    var isEmpty: Bool { values.isEmpty }
    func has(_ key: String) -> Bool { values[key] != nil }

    func number(_ key: String) -> Double? { values[key].flatMap(Double.init) }
    func bool(_ key: String) -> Bool { values[key].flatMap(Double.init).map { $0 != 0 } ?? false }
    func string(_ key: String) -> String { values[key] ?? "" }

    mutating func set(_ key: String, _ value: String) { values[key] = value }

    // Device status (read-only)
    var protocolVersion: Int { Int(values[Key.version] ?? "") ?? 0 }
    var hasCard: Bool { bool(Key.card) }
    var sensorLive: Bool { bool(Key.sensor) }
    var levelSet: Bool { bool(Key.level) }
    var leveling: Bool { bool(Key.leveling) }

    enum Key {
        // status
        static let version = "v", card = "card", sensor = "sensor", level = "level", leveling = "leveling"
        // settings (config.txt names)
        static let rollWarn = "roll_warn", rollDanger = "roll_danger"
        static let pitchWarn = "pitch_warn", pitchDanger = "pitch_danger"
        static let buzzer = "buzzer", backlight = "backlight"
        static let log = "log", logInterval = "log_interval"
        static let obdName = "obd_name", obdProbe = "obd_probe"
        static let remoteForward = "remote_forward", redline = "redline"
        static let speedCorrection = "speed_correction"
    }

    /// Value text the firmware parses (atof): whole numbers without a decimal point.
    static func format(_ v: Double) -> String {
        v == v.rounded() ? String(Int(v)) : String(format: "%g", v)
    }
    static func format(_ b: Bool) -> String { b ? "1" : "0" }

    /// "key=value" lines for a write. Characters the device's parser treats specially
    /// ('#' starts a comment, line breaks end the value) are removed.
    static func encode(_ changes: [String: String]) -> Data {
        let text = changes.sorted { $0.key < $1.key }.map { key, value in
            let clean = value.filter { $0 != "#" && !$0.isNewline }
            return "\(key)=\(clean)\n"
        }.joined()
        return Data(text.utf8)
    }
}

/// One-byte actions for the control characteristic (src/ble.h CMD_*).
enum DeviceCommand: UInt8 {
    case setLevel = 1
    case resetPeaks = 2
    case resetTrip = 3
    case resetTripB = 4
}

enum DeviceLinkError: LocalizedError {
    case notConnected, busy, unsupported

    var errorDescription: String? {
        switch self {
        case .notConnected: "TrailDash isn't connected."
        case .busy: "TrailDash is still busy with the last request."
        case .unsupported: "The TrailDash firmware is too old for this."
        }
    }
}
