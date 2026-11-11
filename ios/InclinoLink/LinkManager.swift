import Foundation
import CoreBluetooth
import CoreLocation

/// Streams GPS speed + course to the inclinometer over BLE, also while the phone is locked.
///
/// Location services run ONLY while the inclinometer is connected:
///  - `bluetooth-central` background mode + state restoration: iOS keeps a pending connect
///    to the remembered device. When the car powers the inclinometer up, iOS wakes (or
///    relaunches) the app in the background and the connection completes.
///  - On connect, GPS starts (`location` background mode keeps the app running). Starting
///    it from the background needs "Always" permission. On disconnect, GPS stops and the
///    app is allowed to suspend — no location indicator while you're not in the car.
///  - Optional (`relaunchAfterSwipe`): significant-change monitoring, which also brings the
///    app back after the user swiped it away (iOS does not relaunch a swiped-away app for
///    Bluetooth alone). This keeps a location indicator on permanently.
/// Deliberately NOT using CLBackgroundActivitySession: iOS keeps such a session (and the blue
/// location pill) alive after the app is closed. Plain background updates end with the app.
/// The manager is created at process launch (`shared`), not by the UI, because a background
/// relaunch doesn't build any SwiftUI views.
final class LinkManager: NSObject, ObservableObject {
    static let shared = LinkManager()

    enum BLEState: String {
        case off = "Bluetooth off"
        case unauthorized = "Bluetooth not allowed"
        case searching = "Searching…"
        case connecting = "Connecting…"
        case connected = "Connected"
    }

    @Published private(set) var bleState: BLEState = .off
    @Published private(set) var deviceName: String?
    @Published private(set) var lastLocation: CLLocation?
    @Published private(set) var locationAuth: CLAuthorizationStatus = .notDetermined
    @Published private(set) var packetsSent = 0
    @Published var enabled: Bool {
        didSet {
            UserDefaults.standard.set(enabled, forKey: Keys.enabled)
            enabled ? start() : stop()
        }
    }
    @Published var relaunchAfterSwipe: Bool {
        didSet {
            UserDefaults.standard.set(relaunchAfterSwipe, forKey: Keys.relaunch)
            updateRelaunchMonitoring()
        }
    }

    private enum Keys {
        static let enabled = "enabled"
        static let relaunch = "relaunchAfterSwipe"
        static let peripheral = "peripheralID"
        static let restore = "InclinoLink.central"
    }

    private var central: CBCentralManager!
    private let location = CLLocationManager()
    private var peripheral: CBPeripheral?
    private var navChar: CBCharacteristic?

    private var savedPeripheralID: UUID? {
        get { UserDefaults.standard.string(forKey: Keys.peripheral).flatMap(UUID.init) }
        set { UserDefaults.standard.set(newValue?.uuidString, forKey: Keys.peripheral) }
    }

    var hasRememberedDevice: Bool { savedPeripheralID != nil }

    override init() {
        enabled = UserDefaults.standard.object(forKey: Keys.enabled) as? Bool ?? true
        relaunchAfterSwipe = UserDefaults.standard.bool(forKey: Keys.relaunch)
        super.init()
        location.delegate = self
        location.activityType = .automotiveNavigation
        location.desiredAccuracy = kCLLocationAccuracyBestForNavigation
        location.distanceFilter = kCLDistanceFilterNone
        location.pausesLocationUpdatesAutomatically = false
        location.showsBackgroundLocationIndicator = true
        locationAuth = location.authorizationStatus
        central = CBCentralManager(delegate: self, queue: nil,
                                   options: [CBCentralManagerOptionRestoreIdentifierKey: Keys.restore])
        location.stopUpdatingLocation()   // GPS starts only once the inclinometer is connected
        if enabled { start() }
    }

    // MARK: control

    /// Ask for location permission if it hasn't been decided yet. iOS silently drops the
    /// request while another system alert (e.g. the Bluetooth prompt) is showing, so this is
    /// retried after Bluetooth settles and whenever the app becomes active.
    func requestLocationIfNeeded() {
        guard enabled else { return }
        switch location.authorizationStatus {
        case .notDetermined:
            location.requestWhenInUseAuthorization()
        case .authorizedWhenInUse:
            // iOS shows the "Change to Always Allow" prompt once; after that only Settings can change it.
            location.requestAlwaysAuthorization()
        default:
            break
        }
    }

    private func start() {
        requestLocationIfNeeded()
        updateRelaunchMonitoring()
        connectOrScan()
        if navChar != nil { startLocation() }
    }

    private var locationAllowed: Bool {
        let auth = location.authorizationStatus
        return auth == .authorizedWhenInUse || auth == .authorizedAlways
    }

    /// GPS on — only called while the inclinometer is connected.
    private func startLocation() {
        guard enabled, locationAllowed else { return }
        location.allowsBackgroundLocationUpdates = true
        location.startUpdatingLocation()
    }

    /// GPS off — the app may now be suspended; the pending BLE connect wakes it again.
    private func stopLocation() {
        location.stopUpdatingLocation()
        location.allowsBackgroundLocationUpdates = false
        lastLocation = nil
    }

    private func updateRelaunchMonitoring() {
        if enabled, relaunchAfterSwipe, locationAllowed,
           CLLocationManager.significantLocationChangeMonitoringAvailable() {
            location.startMonitoringSignificantLocationChanges()
        } else {
            location.stopMonitoringSignificantLocationChanges()
        }
    }

    private func stop() {
        stopLocation()
        updateRelaunchMonitoring()
        if central.isScanning { central.stopScan() }
        if let p = peripheral { central.cancelPeripheralConnection(p) }
        navChar = nil
        if central.state == .poweredOn { bleState = .searching }
    }

    /// Forget the paired device and look for any inclinometer again.
    func forgetDevice() {
        if let p = peripheral { central.cancelPeripheralConnection(p) }
        peripheral = nil
        navChar = nil
        deviceName = nil
        savedPeripheralID = nil
        connectOrScan()
    }

    private func connectOrScan() {
        guard enabled, central.state == .poweredOn else { return }
        if let p = peripheral, p.state == .connected || p.state == .connecting { return }

        // Already connected to iOS (e.g. by another app instance before restoration)?
        if let p = central.retrieveConnectedPeripherals(withServices: [NavProtocol.service]).first {
            connect(p)
            return
        }
        // Remembered device: a pending connect never times out, so iOS reconnects
        // whenever the inclinometer comes into range — even in the background.
        if let id = savedPeripheralID, let p = central.retrievePeripherals(withIdentifiers: [id]).first {
            connect(p)
            return
        }
        bleState = .searching
        central.scanForPeripherals(withServices: [NavProtocol.service])
    }

    private func connect(_ p: CBPeripheral) {
        peripheral = p
        p.delegate = self
        deviceName = p.name ?? deviceName
        bleState = p.state == .connected ? .connected : .connecting
        if p.state == .connected {
            p.discoverServices([NavProtocol.service])
        } else {
            central.connect(p)
        }
    }

    private func send(_ loc: CLLocation) {
        guard let p = peripheral, let c = navChar, p.state == .connected else { return }
        // CoreLocation can hand out a cached fix first; never forward stale data.
        guard -loc.timestamp.timeIntervalSinceNow < 2 else { return }
        guard p.canSendWriteWithoutResponse else { return }
        p.writeValue(NavProtocol.packet(for: loc), for: c, type: .withoutResponse)
        packetsSent += 1
    }
}

// MARK: - CoreBluetooth

extension LinkManager: CBCentralManagerDelegate, CBPeripheralDelegate {
    func centralManager(_ central: CBCentralManager, willRestoreState dict: [String: Any]) {
        if let p = (dict[CBCentralManagerRestoredStatePeripheralsKey] as? [CBPeripheral])?.first {
            peripheral = p
            p.delegate = self
        }
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        // The Bluetooth prompt (if any) is answered by now — safe to ask for location.
        requestLocationIfNeeded()
        switch central.state {
        case .poweredOn:
            if let p = peripheral { connect(p) } else { connectOrScan() }
        case .unauthorized:
            bleState = .unauthorized
        default:
            bleState = .off
            navChar = nil
            stopLocation()
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover p: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        central.stopScan()
        savedPeripheralID = p.identifier
        deviceName = advertisementData[CBAdvertisementDataLocalNameKey] as? String ?? p.name
        connect(p)
    }

    func centralManager(_ central: CBCentralManager, didConnect p: CBPeripheral) {
        bleState = .connected
        deviceName = p.name ?? deviceName
        p.discoverServices([NavProtocol.service])
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral p: CBPeripheral, error: Error?) {
        navChar = nil
        stopLocation()
        guard enabled, p == peripheral else { return }
        bleState = .connecting
        central.connect(p)   // pending: completes when the device is back
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect p: CBPeripheral, error: Error?) {
        guard enabled, p == peripheral else { return }
        bleState = .connecting
        central.connect(p)
    }

    func peripheral(_ p: CBPeripheral, didDiscoverServices error: Error?) {
        guard let s = p.services?.first(where: { $0.uuid == NavProtocol.service }) else { return }
        p.discoverCharacteristics([NavProtocol.characteristic], for: s)
    }

    func peripheral(_ p: CBPeripheral, didDiscoverCharacteristicsFor s: CBService, error: Error?) {
        navChar = s.characteristics?.first(where: { $0.uuid == NavProtocol.characteristic })
        if navChar != nil { startLocation() }
    }
}

// MARK: - CoreLocation

extension LinkManager: CLLocationManagerDelegate {
    func locationManagerDidChangeAuthorization(_ manager: CLLocationManager) {
        locationAuth = manager.authorizationStatus
        guard enabled else { return }
        if navChar != nil { startLocation() }
        updateRelaunchMonitoring()
        if locationAuth == .authorizedWhenInUse { requestLocationIfNeeded() }   // offer the Always upgrade
    }

    func locationManager(_ manager: CLLocationManager, didUpdateLocations locations: [CLLocation]) {
        guard let loc = locations.last else { return }
        lastLocation = loc
        send(loc)
    }

    func locationManager(_ manager: CLLocationManager, didFailWithError error: Error) {
        // kCLErrorLocationUnknown is transient; CoreLocation keeps trying.
    }
}
