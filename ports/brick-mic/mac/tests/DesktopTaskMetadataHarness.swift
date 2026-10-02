import Foundation

@main
enum DesktopTaskMetadataHarness {
    private static let id = "01a0f5f7-3a70-7383-ae3d-ba44a3897b5a"
    private static let otherID = "01a0f771-3eb0-7250-b096-42a986b2551f"
    private static var assertions = 0

    private static func expect(_ value: @autoclosure () -> Bool, _ label: String) {
        precondition(value(), label); assertions += 1
    }

    private static func row(id: String = id, title: String = "完整桌面任务标题") -> [String: Any] {
        ["id": id, "title": title, "status": "idle", "updated": Date().timeIntervalSince1970 - 10]
    }

    private static func snapshot(_ rows: [[String: Any]] = [row()], age: Double = 0) -> [String: Any] {
        ["schema_version": 1, "source": "codex-app/list_threads", "host_id": "local",
         "generated_at": Date().timeIntervalSince1970 - age, "threads": rows]
    }

    private static func write(_ object: Any, directory: URL) {
        let data = try! JSONSerialization.data(withJSONObject: object)
        try! data.write(to: directory.appendingPathComponent(DesktopTaskMetadataStore.filename), options: .atomic)
    }

    private static func refresh(_ store: DesktopTaskMetadataStore, force: Bool = false) -> DesktopTaskMetadataStore.RefreshResult {
        var result: DesktopTaskMetadataStore.RefreshResult?
        var returned = false
        store.refresh(force: force) {
            expect(Thread.isMainThread, "refresh callback must use the main queue")
            expect(returned, "refresh must never block or synchronously call its completion")
            result = $0
        }
        returned = true
        let deadline = Date().addingTimeInterval(3)
        while result == nil, Date() < deadline { RunLoop.main.run(until: Date().addingTimeInterval(0.005)) }
        expect(result != nil, "bounded asynchronous read must complete")
        return result!
    }

    static func main() throws {
        let directory = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
        let store = DesktopTaskMetadataStore(directory: directory)
        expect(store.loadSnapshot().failure == .missingSnapshot, "missing owned inventory is explicit")

        write(snapshot(), directory: directory)
        let valid = store.loadSnapshot()
        expect(valid.failure == nil && valid.entries.count == 1, "explicit local inventory loads")
        expect(valid.entries.first?.id == id && valid.entries.first?.title == "完整桌面任务标题", "canonical identity and exact title survive")
        expect(valid.generatedAt != nil && valid.ageSeconds != nil && !valid.stale, "freshness is returned")

        var uppercase = row(); uppercase["id"] = id.uppercased()
        write(snapshot([uppercase]), directory: directory)
        expect(store.loadSnapshot().entries.first?.id == id, "UUID comparisons use canonical lowercase")

        var invalid = snapshot(); invalid["schema_version"] = 2
        write(invalid, directory: directory)
        expect(store.loadSnapshot().failure == .invalidSchema, "unknown schema cannot grant desktop provenance")
        for (key, value) in [("source", "hook"), ("host_id", "remote"), ("source", "app-server/thread-list")] {
            var invalid = snapshot(); invalid[key] = value; write(invalid, directory: directory)
            expect(store.loadSnapshot().failure == .invalidSource, "only the official local export is accepted")
        }

        let invalidRows: [[String: Any]] = [
            row(id: "not-a-thread-id"), row(title: "  "), row(title: String(repeating: "中", count: 513)),
            row(title: "任务\n标题"), ["id": id, "title": "任务", "status": String(repeating: "x", count: 65), "updated": 0],
            ["id": id, "title": "任务", "status": "idle", "updated": -1],
            ["id": id, "title": "任务", "status": "idle", "updated": Date().timeIntervalSince1970 + 1000]
        ]
        for value in invalidRows {
            write(snapshot([value]), directory: directory)
            expect(store.loadSnapshot().failure == .invalidSnapshot, "malformed ID, label or timestamp is rejected")
        }
        write(snapshot([row(), uppercase]), directory: directory)
        expect(store.loadSnapshot().failure == .invalidSnapshot, "duplicate canonical UUIDs are ambiguous")
        invalid = snapshot(); invalid["generated_at"] = Date().timeIntervalSince1970 + 1000
        write(invalid, directory: directory)
        expect(store.loadSnapshot().failure == .invalidSnapshot, "future export is rejected")
        invalid["generated_at"] = 0; write(invalid, directory: directory)
        expect(store.loadSnapshot().failure == .invalidSnapshot, "missing generation time cannot look fresh")
        write([row()], directory: directory)
        expect(store.loadSnapshot().failure == .invalidSnapshot, "legacy unversioned task array is not authoritative")

        let tooMany = (0...DesktopTaskMetadataStore.maximumEntries).map { _ in row(id: UUID().uuidString) }
        write(snapshot(tooMany), directory: directory)
        expect(store.loadSnapshot().failure == .invalidSnapshot, "entry count is bounded")
        try Data(repeating: 65, count: DesktopTaskMetadataStore.maximumBytes + 1)
            .write(to: directory.appendingPathComponent(DesktopTaskMetadataStore.filename), options: .atomic)
        expect(store.loadSnapshot().failure == .tooLarge, "oversized files are rejected before decoding")

        var oldRow = row(); oldRow["updated"] = Date().timeIntervalSince1970 - 172_900
        write(snapshot([oldRow], age: 172_800), directory: directory)
        let old = store.loadSnapshot()
        expect(old.stale && old.entries.count == 1 && old.failure == nil, "age does not revoke explicitly known desktop identity")

        var withIgnoredFields = snapshot()
        withIgnoredFields["preview"] = "never exposed"
        withIgnoredFields["desktop_ids"] = [otherID]
        withIgnoredFields["hook_tasks"] = [row(id: otherID)]
        write(withIgnoredFields, directory: directory)
        expect(store.loadSnapshot().entries.map { $0.id } == [id], "extra cached or hook IDs cannot gain provenance")

        write(snapshot([row(title: "第一版")]), directory: directory)
        expect(refresh(store).entries.first?.title == "第一版", "first async refresh reads the owned snapshot")
        write(snapshot([row(title: "新版完整标题")]), directory: directory)
        expect(refresh(store).entries.first?.title == "第一版", "successful cache is bounded and reused")
        expect(refresh(store, force: true).entries.first?.title == "新版完整标题", "explicit force sees atomic replacement immediately")

        var cancelled: DesktopTaskMetadataStore.LoadResult?
        store.refresh(force: true) { cancelled = $0 }
        store.cancel()
        let deadline = Date().addingTimeInterval(3)
        while cancelled == nil, Date() < deadline { RunLoop.main.run(until: Date().addingTimeInterval(0.005)) }
        expect(cancelled?.failure == .cancelled && cancelled?.entries.isEmpty == true, "cancelled callback cannot apply stale metadata")
        expect(refresh(store, force: true).failure == nil, "new requests remain possible after cancelling old work")

        let missingDirectory = directory.appendingPathComponent("appears-later", isDirectory: true)
        try FileManager.default.createDirectory(at: missingDirectory, withIntermediateDirectories: true)
        let appears = DesktopTaskMetadataStore(directory: missingDirectory)
        expect(refresh(appears).failure == .missingSnapshot, "failed read is explicit")
        write(snapshot(), directory: missingDirectory)
        expect(refresh(appears).entries.count == 1, "absence is not cached against a newly exported file")
        print("Desktop metadata: \(assertions) offline assertions passed")
    }
}
