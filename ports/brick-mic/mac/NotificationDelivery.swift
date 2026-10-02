import Foundation
import CryptoKit

/// Receipt state is separate from whether a task's reply is still unread.
struct PendingNotification: Codable, Equatable {
    let id: String
    let task: String
    let kind: String
}

struct NotificationBatch: Equatable {
    let event: String
    let events: [String]
    let kind: String
    var count: Int { events.count }
}

/// Persists only alerts that have not received a transport receipt. The in-flight
/// map is deliberately local to this process: an ACK cannot acknowledge a packet
/// that this instance did not send.
struct NotificationDelivery: Codable {
    static let maximumPending = 100
    private static let maximumInFlight = 256
    private(set) var pending: [PendingNotification]
    private var inFlight: [String: Set<String>] = [:]
    private var inFlightOrder: [String] = []

    init(pending: [PendingNotification] = []) {
        var seen = Set<String>()
        // Keep the newest bounded entries, including when a cache contains
        // duplicate or malformed records.
        self.pending = Array(pending.reversed().filter {
            Self.valid($0) && seen.insert($0.id).inserted
        }.prefix(Self.maximumPending).reversed())
    }

    private enum CodingKeys: String, CodingKey { case pending }

    init(from decoder: Decoder) throws {
        let values = try decoder.container(keyedBy: CodingKeys.self)
        self.init(pending: try values.decodeIfPresent([PendingNotification].self, forKey: .pending) ?? [])
    }

    func encode(to encoder: Encoder) throws {
        var values = encoder.container(keyedBy: CodingKeys.self)
        try values.encode(pending, forKey: .pending)
    }

    /// The hook source ID is stable across reconnects; no connection token or
    /// wall-clock timestamp is part of this transport event ID.
    @discardableResult
    mutating func enqueue(sourceID: String, task: String, kind: String) -> String? {
        guard !sourceID.isEmpty, sourceID.utf8.count <= 4_096 else { return nil }
        let notification = PendingNotification(id: Self.eventID(sourceID), task: task, kind: kind)
        guard Self.valid(notification), !pending.contains(where: { $0.id == notification.id }) else { return nil }
        pending.append(notification)
        if pending.count > Self.maximumPending { pending.removeFirst(pending.count - Self.maximumPending) }
        pruneInFlight()
        return notification.id
    }

    /// Construct one alert packet for the requested pending entries. Retrying
    /// the same entries after a reconnect yields the same packet ID.
    mutating func batch(ids: [String]? = nil) -> NotificationBatch? {
        let selection = ids.map(Set.init)
        let covered = pending.filter { selection?.contains($0.id) ?? true }
        guard !covered.isEmpty else { return nil }
        let events = covered.map(\.id).sorted()
        let event = events.count == 1 ? events[0] : "catchup:" + Self.digest(events.joined(separator: "\n"))
        let kind = covered.contains(where: { $0.kind == "waiting" }) ? "waiting"
            : covered.contains(where: { $0.kind == "failed" }) ? "failed" : "completed"
        inFlight[event] = Set(events)
        inFlightOrder.removeAll { $0 == event }
        inFlightOrder.append(event)
        if inFlightOrder.count > Self.maximumInFlight {
            let expired = Array(inFlightOrder.prefix(inFlightOrder.count - Self.maximumInFlight))
            inFlightOrder.removeFirst(expired.count)
            for key in expired { inFlight.removeValue(forKey: key) }
        }
        return NotificationBatch(event: event, events: events, kind: kind)
    }

    /// Accept receipts only for an actual packet sent by this process and clear
    /// precisely the entries it covered, never newer offline arrivals.
    @discardableResult
    mutating func acknowledge(event: String) -> Bool {
        guard let covered = inFlight.removeValue(forKey: event) else { return false }
        inFlightOrder.removeAll { $0 == event }
        pending.removeAll { covered.contains($0.id) }
        pruneInFlight()
        return true
    }

    /// Reading a task or resolving an approval may make an unsent alert obsolete.
    /// Nil filters match all entries, so remove() explicitly clears the queue.
    mutating func remove(task: String? = nil, kind: String? = nil) {
        pending.removeAll { (task == nil || $0.task == task) && (kind == nil || $0.kind == kind) }
        pruneInFlight()
    }

    private mutating func pruneInFlight() {
        let current = Set(pending.map(\.id))
        let expired = inFlight.compactMap { $0.value.isDisjoint(with: current) ? $0.key : nil }
        for key in expired { inFlight.removeValue(forKey: key) }
        inFlightOrder.removeAll { inFlight[$0] == nil }
    }

    static func eventID(_ source: String) -> String { "hook:" + digest(source) }

    private static func digest(_ value: String) -> String {
        SHA256.hash(data: Data(value.utf8)).map { String(format: "%02x", $0) }.joined()
    }

    private static func valid(_ notification: PendingNotification) -> Bool {
        guard ["waiting", "failed", "completed"].contains(notification.kind),
              !notification.task.isEmpty, notification.task.utf8.count <= 256,
              !notification.task.unicodeScalars.contains(where: CharacterSet.controlCharacters.contains),
              notification.id.hasPrefix("hook:"), notification.id.utf8.count == 69 else { return false }
        return notification.id.dropFirst(5).utf8.allSatisfy { (48...57).contains($0) || (97...102).contains($0) }
    }
}
