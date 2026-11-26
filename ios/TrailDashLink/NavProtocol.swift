import CoreBluetooth
import CoreLocation

/// Wire format shared with the TrailDash firmware — keep in sync with src/ble.h.
enum NavProtocol {
    static let service = CBUUID(string: "CCCF9685-6127-47E1-9884-2E68EF1D44A3")
    static let characteristic = CBUUID(string: "1CCA3ED6-DCF2-4A66-A435-AB5016547E5A")
    /// Settings: read = "key=value" lines (status + settings), write = lines to change. See DeviceConfig.
    static let config = CBUUID(string: "1219E800-95D4-4A08-9572-1D76DCB4B1FF")
    /// Actions: write one byte (DeviceCommand).
    static let control = CBUUID(string: "CA5380E5-BE5F-4B00-8875-DB58900DAEAC")

    static let flagSpeed: UInt8 = 0x01
    static let flagCourse: UInt8 = 0x02

    /// Packet v1, little-endian, 10 bytes:
    /// u8 version, u8 flags, u16 speed (0.01 m/s), u16 speedAcc (0.01 m/s, 0xFFFF unknown),
    /// u16 course (0.01°), u16 courseAcc (0.01°, 0xFFFF unknown)
    static func packet(for loc: CLLocation) -> Data {
        var flags: UInt8 = 0
        if loc.speed >= 0 { flags |= flagSpeed }
        if loc.course >= 0 { flags |= flagCourse }

        func u16(_ v: Double, scale: Double) -> UInt16 {
            UInt16(clamping: Int((v * scale).rounded()))
        }
        let speed = loc.speed >= 0 ? u16(loc.speed, scale: 100) : 0
        let speedAcc = loc.speedAccuracy >= 0 ? min(u16(loc.speedAccuracy, scale: 100), 0xFFFE) : 0xFFFF
        let course = loc.course >= 0 ? u16(loc.course.truncatingRemainder(dividingBy: 360), scale: 100) : 0
        let courseAcc = loc.courseAccuracy >= 0 ? min(u16(loc.courseAccuracy, scale: 100), 0xFFFE) : 0xFFFF

        var d = Data([1, flags])
        for v in [speed, speedAcc, course, courseAcc] {
            d.append(UInt8(v & 0xFF))
            d.append(UInt8(v >> 8))
        }
        return d
    }
}
