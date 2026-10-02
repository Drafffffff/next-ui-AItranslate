import Foundation

@main enum NotificationDeliveryHarness {
    static func main() throws {
        try receiptsDoNotMarkRepliesRead()
        reconnectIdentityAndOfflineArrivals()
        try pendingSurvivesRestart()
        resolvedApprovalsAndReadTasks()
        try boundedAndValidatedState()
        print("PASS: notification receipts, stable reconnect IDs, precise ACK coverage, restart persistence, approval pruning, bounded state")
    }

    private static func add(_ ledger: inout NotificationDelivery, _ source: String, task: String = "task-a", kind: String = "completed") -> String {
        guard let id = ledger.enqueue(sourceID: source, task: task, kind: kind) else {
            preconditionFailure("Expected a valid new notification")
        }
        return id
    }

    private static func receiptsDoNotMarkRepliesRead() throws {
        var ledger = NotificationDelivery()
        let id = add(&ledger, "task-a:turn-1:Stop")
        let unread = ["reply-1"] // Caller-owned reading state remains independent.
        precondition(!ledger.acknowledge(event: id)) // Not sent yet.
        let packet = ledger.batch()!
        precondition(packet.event == id && packet.events == [id] && packet.kind == "completed")
        precondition(ledger.acknowledge(event: packet.event))
        precondition(ledger.pending.isEmpty && unread == ["reply-1"])
        precondition(!ledger.acknowledge(event: packet.event))
        precondition(ledger.batch() == nil) // Reconnect does not alert an unread old reply.
    }

    private static func reconnectIdentityAndOfflineArrivals() {
        var first = NotificationDelivery()
        let one = add(&first, "event-1")
        let two = add(&first, "event-2", kind: "failed")
        let oldBatch = first.batch()!
        precondition(oldBatch.events == [one, two].sorted() && oldBatch.kind == "failed")
        precondition(first.batch() == oldBatch)

        var reconnected = NotificationDelivery(pending: first.pending.reversed())
        precondition(reconnected.batch() == oldBatch) // Connection and ordering independent.
        let future = add(&first, "event-3", kind: "waiting")
        let newerBatch = first.batch()!
        precondition(newerBatch.event != oldBatch.event && newerBatch.kind == "waiting")
        precondition(newerBatch.count == 3)
        precondition(first.acknowledge(event: oldBatch.event))
        precondition(first.pending.map(\.id) == [future]) // Old receipt cannot clear a later event.
        precondition(!first.acknowledge(event: "catchup:forged"))
        precondition(first.pending.map(\.id) == [future])
        precondition(first.acknowledge(event: newerBatch.event))
        precondition(first.pending.isEmpty)

        var subset = NotificationDelivery()
        let a = add(&subset, "subset-a")
        let b = add(&subset, "subset-b")
        let sent = subset.batch(ids: [a, a, "unknown"])!
        precondition(sent.events == [a] && sent.count == 1)
        precondition(subset.acknowledge(event: sent.event))
        precondition(subset.pending.map(\.id) == [b])
        precondition(subset.batch(ids: []) == nil)
    }

    private static func pendingSurvivesRestart() throws {
        var ledger = NotificationDelivery()
        let id = add(&ledger, "restart-event")
        let sent = ledger.batch()!
        let data = try JSONEncoder().encode(ledger)
        let object = try JSONSerialization.jsonObject(with: data) as! [String: Any]
        precondition(Set(object.keys) == ["pending"])
        var restored = try JSONDecoder().decode(NotificationDelivery.self, from: data)
        precondition(restored.pending == ledger.pending)
        precondition(!restored.acknowledge(event: sent.event)) // No persisted in-flight authority.
        precondition(restored.batch() == sent)
        precondition(restored.acknowledge(event: id) && restored.pending.isEmpty)
        let empty = try JSONDecoder().decode(NotificationDelivery.self, from: Data("{}".utf8))
        precondition(empty.pending.isEmpty) // Missing receipt state never imports unread history.
    }

    private static func resolvedApprovalsAndReadTasks() {
        var ledger = NotificationDelivery()
        _ = add(&ledger, "approval-a", kind: "waiting")
        let complete = add(&ledger, "completion-a")
        let other = add(&ledger, "approval-b", task: "task-b", kind: "waiting")
        ledger.remove(task: "task-a", kind: "waiting")
        precondition(Set(ledger.pending.map(\.id)) == [complete, other])
        ledger.remove(task: "task-a")
        precondition(ledger.pending.map(\.id) == [other])
        let packet = ledger.batch()!
        ledger.remove(kind: "waiting")
        precondition(ledger.pending.isEmpty && !ledger.acknowledge(event: packet.event))
        _ = add(&ledger, "last")
        ledger.remove()
        precondition(ledger.pending.isEmpty)
    }

    private static func boundedAndValidatedState() throws {
        var ledger = NotificationDelivery()
        precondition(ledger.enqueue(sourceID: "", task: "task-a", kind: "completed") == nil)
        precondition(ledger.enqueue(sourceID: String(repeating: "x", count: 4_097), task: "task-a", kind: "completed") == nil)
        precondition(ledger.enqueue(sourceID: "bad-kind", task: "task-a", kind: "running") == nil)
        precondition(ledger.enqueue(sourceID: "empty-task", task: "", kind: "completed") == nil)
        precondition(ledger.enqueue(sourceID: "bad-task", task: "task\n", kind: "completed") == nil)
        let first = add(&ledger, "source-0")
        precondition(ledger.enqueue(sourceID: "source-0", task: "task-a", kind: "completed") == nil)
        for index in 1...120 { _ = add(&ledger, "source-\(index)") }
        precondition(ledger.pending.count == 100 && !ledger.pending.contains(where: { $0.id == first }))
        let valid = ledger.pending
        let invalid = [
            PendingNotification(id: "reconnect:token", task: "task-a", kind: "completed"),
            PendingNotification(id: valid[0].id, task: "task-a", kind: "made-up"),
            PendingNotification(id: "hook:" + String(repeating: "g", count: 64), task: "task-a", kind: "completed"),
            PendingNotification(id: valid[0].id, task: String(repeating: "x", count: 257), kind: "completed")
        ]
        var restored = NotificationDelivery(pending: valid + invalid + [valid.last!])
        precondition(restored.pending == valid)
        let packet = restored.batch()!
        let wire: [String: Any] = ["op": "notify", "event": packet.event, "events": packet.events, "kind": packet.kind]
        let wireData = try JSONSerialization.data(withJSONObject: wire)
        precondition(wireData.count < 8_000)
        precondition(packet.events.count == 100 && Set(packet.events).count == 100)
    }
}
