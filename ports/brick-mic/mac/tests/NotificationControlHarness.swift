import Foundation

// This file is concatenated after the unchanged production Control.swift, so
// fixture helpers can exercise its private hook boundary without exposing a
// production testing API. The BLE double only captures output; no GUI editing,
// accessibility queries, real BLE connection, or production cache is used.
final class BrickBluetooth {
    var controlsAllowed = true
    var controlToken = "fixture-connection-1"
    var onControl: (([String: Any]) -> Void)?
    func control(_ object: [String: Any]) {
        guard !controlToken.isEmpty else { return }
        onControl?(object)
    }
}

extension MicControl {
    fileprivate func fixtureSelectForReading(_ id:String) {
        desktopIDs=Set(tasks.map{$0.id});mode="codex";targetID=id;lastID=id;publish()
    }
    fileprivate var fixturePending: [PendingNotification] { notifications.pending }
    fileprivate var fixtureUnread: [[String: String]] { unread }
    fileprivate var fixtureApprovals: Int { approvals.requests.count }
    fileprivate func fixtureFocusPending(_ value: Bool) { focusInFlight = value }
    fileprivate func fixtureExpireApprovals() {
        approvals.expire(now: Date().timeIntervalSince1970 + 91)
    }
    fileprivate func fixtureApprovalPeerDisconnected() {
        approvals.cancel { $0.owner == "fixture-socket" }
    }
    fileprivate func fixtureHook(_ name: String, task: String, turn: String,
                                 request: String? = nil,
                                 reply: (([String: Any]) -> Void)? = nil) {
        var object: [String: Any] = [
            "token": hookToken, "session_id": task, "turn_id": turn,
            "hook_event_name": name, "cwd": "/tmp/notification-fixture",
            "at": 1_800_000_000.0,
        ]
        if name == "Stop" { object["last_assistant_message"] = "Synthetic fixture reply" }
        if let request = request {
            object["request_id"] = request
            object["permission"] = ["tool": "Bash", "summary": "printf fixture", "allowAvailable": true]
        }
        receiveHook(object, socketID: "fixture-socket", approvalReply: reply)
    }
}

private final class NotificationFixture {
    static let taskA = "00000000-0000-4000-8000-000000000001"
    static let taskB = "00000000-0000-4000-8000-000000000002"
    let directory: URL
    let ble: BrickBluetooth
    let control: MicControl
    var packets: [[String: Any]] = []
    var alerts: [[String: Any]] { packets.filter { $0["op"] as? String == "notify" } }

    init(base: URL, name: String) throws {
        directory = base.appendingPathComponent(name, isDirectory: true)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        ble = BrickBluetooth()
        control = MicControl(ble, directory: directory)
        ble.onControl = { [weak self] object in self?.packets.append(object) }
    }
    deinit { control.stop() }
    func resetPackets() { packets.removeAll() }
    func stopped(_ task: String = taskA, turn: String) {
        control.fixtureHook("Stop", task: task, turn: turn)
    }
    func ack(_ event: String, token: String? = nil) throws {
        try command("notifyAck", event: event, token: token)
    }
    func command(_ op: String, event: String? = nil, token: String? = nil) throws {
        var message: [String: Any] = ["token": token ?? ble.controlToken, "op": op]
        if let event = event { message["event"] = event }
        control.message(try JSONSerialization.data(withJSONObject: message))
    }
    func cache() throws -> [String: Any] {
        let data = try Data(contentsOf: directory.appendingPathComponent("control-state.json"))
        return try JSONSerialization.jsonObject(with: data) as! [String: Any]
    }
    func alertID(_ index: Int = 0) -> String { alerts[index]["event"] as! String }
}

@main enum NotificationControlHarness {
    static func main() throws {
        guard CommandLine.arguments.count == 2 else { exit(2) }
        let base = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
        try legacyUnreadDoesNotReplay(base)
        try receiptClearsTransportOnly(base)
        try offlineHooksProduceOneCatchup(base)
        try receiptsRequireCurrentConnection(base)
        try oldBatchCannotConsumeFutureHooks(base)
        try cancelledApprovalDoesNotReplay(base)
        try closedApprovalWithoutBLEDisconnectDoesNotReplay(base)
        try readingPersistsAnEmptyQueue(base)
        try taskBadgesAndRevisionScopedReading(base)
        try batteryTelemetryWhileRecording(base)
        print("PASS: real Control hook/ACK routing, legacy migration, reconnect silence, offline catch-up, token isolation, precise batch receipts, approval disconnect/expiry/socket cancellation, read persistence")
    }

    private static func batteryTelemetryWhileRecording(_ base:URL) throws {
        let fixture=try NotificationFixture(base:base,name:"battery-menu")
        fixture.control.busy={true}
        var readings:[(Int?,Bool)]=[]
        fixture.control.onBatteryLevel={readings.append(($0,$1))}
        for level in [0,100,101] {
            let packet:[String:Any]=["token":fixture.ble.controlToken,"op":"battery","battery":["percent":level,"charging":true]]
            fixture.control.message(try JSONSerialization.data(withJSONObject:packet))
        }
        fixture.control.message(try JSONSerialization.data(withJSONObject:["token":fixture.ble.controlToken,"op":"battery","battery":["percent":NSNull(),"charging":false]]))
        fixture.control.message(try JSONSerialization.data(withJSONObject:["token":"old-connection","op":"battery","battery":["percent":88,"charging":false]]))
        precondition(readings.count==4 && readings[0].0==0 && readings[1].0==100 && readings[2].0==nil && readings[3].0==nil && !readings[3].1)
        print("PASS: menu battery telemetry updates during recording; invalid/unknown readings stay unknown, stale connections ignored")
    }

    private static func taskBadgesAndRevisionScopedReading(_ base:URL) throws {
        let fixture=try NotificationFixture(base:base,name:"task-badges")
        for n in 1...7 {fixture.stopped(turn:"badge-\(n)")}
        fixture.stopped(NotificationFixture.taskB,turn:"other-task")
        fixture.control.fixtureSelectForReading(NotificationFixture.taskA)
        func state()->[String:Any] {fixture.packets.last(where:{$0["op"] as? String=="state"})!["state"] as! [String:Any]}
        let page=state()["tasks"] as! [[String:Any]]
        precondition(page.first(where:{$0["id"] as? String==NotificationFixture.taskA})?["unread"] as? Int==7,"Badges use all unread events, not the latest five")
        let first=(state()["reply"] as! [String:Any])["revision"] as! String
        fixture.stopped(turn:"badge-new-turn")
        let second=(state()["reply"] as! [String:Any])["revision"] as! String
        precondition(first != second,"Identical reply text in a new turn must refresh")
        func seen(_ revision:String) throws {
            fixture.control.message(try JSONSerialization.data(withJSONObject:["token":fixture.ble.controlToken,"op":"reply","arg":"seen","revision":revision]))
        }
        try seen(first)
        precondition(fixture.control.fixtureUnread.count==9,"A late read ACK cannot consume a newer reply")
        try seen(second)
        precondition(fixture.control.fixtureUnread.count==1 && fixture.control.fixtureUnread[0]["task"]==NotificationFixture.taskB,"Reading clears only this task")
        let updated=state()["tasks"] as! [[String:Any]]
        precondition(updated.first(where:{$0["id"] as? String==NotificationFixture.taskA})?["unread"] as? Int==0,"Ordinary publishes also refresh task badges")
        print("PASS: complete task unread counts, new reply revisions, stale read rejection and task-scoped live badge updates")
    }

    private static func legacyUnreadDoesNotReplay(_ base: URL) throws {
        let fixture = try NotificationFixture(base: base, name: "legacy")
        let source = NotificationFixture.taskA + ":legacy-turn:Stop"
        let legacy: [String: Any] = [
            "tasks": [["id": NotificationFixture.taskA, "title": "Legacy fixture", "status": "completed", "updated": 1.0]],
            "unread": [["id": source, "task": NotificationFixture.taskA, "title": "Legacy fixture", "kind": "completed"]],
            "delivered": [source], "replies": [:], "mode": "ordinary",
        ]
        let file = fixture.directory.appendingPathComponent("control-state.json")
        try JSONSerialization.data(withJSONObject: legacy).write(to: file)
        fixture.control.start()
        fixture.control.connected()
        fixture.ble.controlToken = "fixture-connection-2"
        fixture.control.connected()
        precondition(fixture.alerts.isEmpty, "Legacy unread replies must not create transport alerts")
        precondition(fixture.control.fixtureUnread.count == 1 && fixture.control.fixturePending.isEmpty)
        let migratedCache = try fixture.cache()
        precondition((migratedCache["pendingNotifications"] as? [[String: Any]])?.isEmpty == true)
    }

    private static func receiptClearsTransportOnly(_ base: URL) throws {
        let fixture = try NotificationFixture(base: base, name: "receipt")
        fixture.stopped(turn: "turn-1")
        precondition(fixture.alerts.count == 1 && fixture.control.fixturePending.count == 1)
        precondition(fixture.alerts[0]["silent"] as? Bool == false, "Live arrivals must still buzz")
        let event = fixture.alertID()
        try fixture.ack(event)
        precondition(fixture.control.fixturePending.isEmpty && fixture.control.fixtureUnread.count == 1,
                     "Transport receipt must not mark a reply read")
        let cache = try fixture.cache()
        precondition((cache["pendingNotifications"] as? [[String: Any]])?.isEmpty == true)
        precondition((cache["unread"] as? [[String: String]])?.count == 1)
        fixture.resetPackets()
        fixture.control.connected()
        fixture.control.disconnected()
        fixture.ble.controlToken = "fixture-connection-2"
        fixture.control.connected()
        fixture.stopped(turn: "turn-1")
        precondition(fixture.alerts.isEmpty, "Acknowledged or duplicate hooks must not alert on reconnect")

        let restoredBLE = BrickBluetooth()
        var restoredAlerts = 0
        restoredBLE.onControl = { if $0["op"] as? String == "notify" { restoredAlerts += 1 } }
        let restored = MicControl(restoredBLE, directory: fixture.directory)
        restored.start(); defer { restored.stop() }
        restored.connected()
        precondition(restoredAlerts == 0 && restored.fixtureUnread.count == 1 && restored.fixturePending.isEmpty)
    }

    private static func offlineHooksProduceOneCatchup(_ base: URL) throws {
        let fixture = try NotificationFixture(base: base, name: "offline")
        fixture.ble.controlToken = ""
        fixture.stopped(turn: "offline-1")
        fixture.stopped(NotificationFixture.taskB, turn: "offline-2")
        precondition(fixture.alerts.isEmpty && fixture.control.fixturePending.count == 2)
        let offlineCache = try fixture.cache()
        precondition((offlineCache["pendingNotifications"] as? [[String: Any]])?.count == 2)
        fixture.ble.controlToken = "fixture-online"
        fixture.control.connected()
        precondition(fixture.alerts.count == 1, "Multiple offline hooks should produce one catch-up packet")
        let alert = fixture.alerts[0]
        precondition(alert["silent"] as? Bool == true, "Opening the app must synchronize without buzzing")
        precondition((alert["events"] as? [String])?.count == 2)
        precondition((alert["events"] as? [String])?.sorted() == fixture.control.fixturePending.map(\.id).sorted())
        try fixture.ack(fixture.alertID())
        fixture.resetPackets()
        fixture.control.connected()
        precondition(fixture.alerts.isEmpty && fixture.control.fixtureUnread.count == 2)
    }

    private static func receiptsRequireCurrentConnection(_ base: URL) throws {
        let fixture = try NotificationFixture(base: base, name: "invalid-receipts")
        fixture.stopped(turn: "receipt-1")
        let event = fixture.alertID()
        try fixture.ack(event, token: "old-connection")
        try fixture.ack("hook:unknown")
        fixture.ble.controlsAllowed = false
        try fixture.ack(event)
        fixture.ble.controlsAllowed = true
        fixture.ble.controlToken = ""
        try fixture.ack(event)
        fixture.ble.controlToken = "fixture-connection-1"
        let malformed: [String: Any] = ["token": fixture.ble.controlToken, "op": "notifyAck", "event": 23]
        fixture.control.message(try JSONSerialization.data(withJSONObject: malformed))
        precondition(fixture.control.fixturePending.count == 1, "Invalid ACK must not consume pending events")
        fixture.control.busy = { true }
        fixture.control.fixtureFocusPending(true)
        try fixture.ack(event)
        precondition(fixture.control.fixturePending.isEmpty, "Valid receipts are accepted while recording or focusing")
    }

    private static func oldBatchCannotConsumeFutureHooks(_ base: URL) throws {
        let fixture = try NotificationFixture(base: base, name: "batch-coverage")
        fixture.ble.controlToken = ""
        fixture.stopped(turn: "old-1")
        fixture.stopped(turn: "old-2")
        fixture.ble.controlToken = "connection-old"
        fixture.control.connected()
        let oldEvent = fixture.alertID()
        fixture.control.disconnected()
        fixture.ble.controlToken = ""
        fixture.stopped(turn: "future-3")
        let future = fixture.control.fixturePending.last!.id
        fixture.resetPackets()
        fixture.ble.controlToken = "connection-new"
        fixture.control.connected()
        precondition(fixture.alerts.count == 1 && fixture.alertID() != oldEvent)
        let newEvent = fixture.alertID()
        try fixture.ack(oldEvent, token: "connection-old")
        precondition(fixture.control.fixturePending.count == 3)
        try fixture.ack(oldEvent)
        precondition(fixture.control.fixturePending.map(\.id) == [future],
                     "A receipt clears only the hooks its actual packet covered")
        try fixture.ack(newEvent)
        precondition(fixture.control.fixturePending.isEmpty && fixture.control.fixtureUnread.count == 3)
    }

    private static func cancelledApprovalDoesNotReplay(_ base: URL) throws {
        let fixture = try NotificationFixture(base: base, name: "cancelled-approval")
        var responses: [[String: Any]] = []
        fixture.control.fixtureHook("PermissionRequest", task: NotificationFixture.taskA, turn: "approval-turn",
                                    request: "00000000-0000-4000-8000-000000000003",
                                    reply: { responses.append($0) })
        precondition(fixture.alerts.count == 1 && fixture.control.fixtureApprovals == 1)
        precondition(fixture.control.fixturePending.first?.kind == "waiting")
        fixture.control.disconnected()
        precondition(responses.count == 1 && responses[0].isEmpty && fixture.control.fixtureApprovals == 0)
        precondition(!fixture.control.fixturePending.contains { $0.kind == "waiting" },
                     "Disconnected approvals must not replay a now-unanswerable alert")
        fixture.resetPackets()
        fixture.ble.controlToken = "after-disconnect"
        fixture.control.connected()
        precondition(fixture.alerts.isEmpty)
        let cancelledCache = try fixture.cache()
        precondition((cancelledCache["pendingNotifications"] as? [[String: Any]])?.isEmpty == true)
    }

    private static func readingPersistsAnEmptyQueue(_ base: URL) throws {
        let fixture = try NotificationFixture(base: base, name: "read")
        fixture.stopped(turn: "read-1")
        fixture.stopped(NotificationFixture.taskB, turn: "read-2")
        precondition(fixture.control.fixturePending.count == 2 && fixture.control.fixtureUnread.count == 2)
        try fixture.command("read")
        precondition(fixture.control.fixturePending.isEmpty && fixture.control.fixtureUnread.isEmpty)
        let cache = try fixture.cache()
        precondition((cache["pendingNotifications"] as? [[String: Any]])?.isEmpty == true)
        precondition((cache["unread"] as? [[String: String]])?.isEmpty == true)
        fixture.resetPackets()
        fixture.control.disconnected()
        fixture.ble.controlToken = "read-reconnect"
        fixture.control.connected()
        precondition(fixture.alerts.isEmpty)
    }

    private static func closedApprovalWithoutBLEDisconnectDoesNotReplay(_ base: URL) throws {
        for outcome in ["expiry", "socket-cancel"] {
            let fixture = try NotificationFixture(base: base, name: "approval-" + outcome)
            var responses: [[String: Any]] = []
            fixture.control.fixtureHook("PermissionRequest", task: NotificationFixture.taskA, turn: outcome,
                                        request: "00000000-0000-4000-8000-000000000004",
                                        reply: { responses.append($0) })
            precondition(fixture.control.fixtureApprovals == 1 && fixture.alerts.count == 1)
            precondition(fixture.control.fixturePending.first?.kind == "waiting")
            if outcome == "expiry" { fixture.control.fixtureExpireApprovals() }
            else { fixture.control.fixtureApprovalPeerDisconnected() }
            precondition(responses.count == 1 && responses[0].isEmpty && fixture.control.fixtureApprovals == 0)
            precondition(fixture.control.fixturePending.isEmpty,
                         "An expired or closed hook socket must remove its unanswerable transport alert")
            precondition(fixture.control.fixtureUnread.count == 1,
                         "Cancellation is not an explicit approval decision or a read receipt")
            let cache = try fixture.cache()
            precondition((cache["pendingNotifications"] as? [[String: Any]])?.isEmpty == true)
            fixture.resetPackets()
            // Deliberately keep BLE connected: pruning must be caused by the
            // real approval finish callback, not disconnected() housekeeping.
            fixture.control.connected()
            precondition(fixture.alerts.isEmpty, "A closed approval must not pulse on the next state resync")
        }
    }
}
