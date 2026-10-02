import Foundation

/// Only the companion's explicitly exported, local desktop inventory can grant
/// desktop provenance. Hooks, directory names and cached control-state do not.
/// This store performs no UI access, RPC, network activity or process launch.
final class DesktopTaskMetadataStore {
    struct Entry: Equatable {
        let id: String
        let title: String
        let status: String
        let updated: Double
    }

    enum Failure: String {
        case missingSnapshot, invalidSchema, invalidSource, invalidSnapshot
        case tooLarge, io, cancelled
    }

    struct LoadResult {
        let entries: [Entry]
        let generatedAt: Double?
        let ageSeconds: Double?
        /// Age is informational: a few hours do not revoke an explicitly known ID.
        let stale: Bool
        let failure: Failure?

        fileprivate func aged(at now: Double) -> LoadResult {
            let age = generatedAt.map { max(0, now - $0) }
            return LoadResult(entries: entries, generatedAt: generatedAt,
                              ageSeconds: age, stale: age.map { $0 > 900 } ?? false,
                              failure: failure)
        }
    }

    typealias RefreshResult = LoadResult
    static let filename = "desktop-task-metadata.json"
    static let maximumBytes = 1_048_576
    static let maximumEntries = 512

    private struct Snapshot: Decodable {
        let schema_version: Int
        let source: String
        let host_id: String
        let generated_at: Double
        let threads: [Row]
    }

    private struct Row: Decodable {
        let id: String
        let title: String
        let status: String
        let updated: Double
    }

    private let file: URL
    private let queue = DispatchQueue(label: "com.nextui.brickmic.desktop-metadata", qos: .utility)
    private let lock = NSLock()
    private var generation: UInt64 = 0
    // Cache belongs exclusively to queue. Monotonic time bounds the reuse window.
    private var cache: LoadResult?
    private var cachedAt: TimeInterval = 0

    init(directory: URL) { file = directory.appendingPathComponent(Self.filename) }

    private static func failed(_ failure: Failure) -> LoadResult {
        LoadResult(entries: [], generatedAt: nil, ageSeconds: nil, stale: false, failure: failure)
    }

    private static func validLabel(_ text: String, maximumBytes: Int) -> Bool {
        !text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty &&
        text.utf8.count <= maximumBytes &&
        text.rangeOfCharacter(from: .controlCharacters) == nil
    }

    /// A bounded synchronous read for callers already on a background queue.
    /// Atomic replacement keeps an opened file descriptor on one complete version.
    func loadSnapshot() -> LoadResult {
        do {
            let attributes = try FileManager.default.attributesOfItem(atPath: file.path)
            guard attributes[.type] as? FileAttributeType == .typeRegular else { return Self.failed(.invalidSnapshot) }
            if let size = attributes[.size] as? NSNumber, size.intValue > Self.maximumBytes { return Self.failed(.tooLarge) }
            let handle = try FileHandle(forReadingFrom: file)
            defer { try? handle.close() }
            // Check the actual opened version too; a concurrent replacement can
            // change size between attributesOfItem and opening the descriptor.
            let data = try handle.read(upToCount: Self.maximumBytes + 1) ?? Data()
            guard data.count <= Self.maximumBytes else { return Self.failed(.tooLarge) }
            let snapshot: Snapshot
            do { snapshot = try JSONDecoder().decode(Snapshot.self, from: data) }
            catch { return Self.failed(.invalidSnapshot) }
            guard snapshot.schema_version == 1 else { return Self.failed(.invalidSchema) }
            guard snapshot.source == "codex-app/list_threads", snapshot.host_id == "local" else { return Self.failed(.invalidSource) }
            let now = Date().timeIntervalSince1970
            guard snapshot.generated_at.isFinite, snapshot.generated_at > 0,
                  snapshot.generated_at <= now + 300,
                  snapshot.threads.count <= Self.maximumEntries else { return Self.failed(.invalidSnapshot) }
            var ids = Set<String>(), entries: [Entry] = []
            for row in snapshot.threads {
                guard let uuid = UUID(uuidString: row.id),
                      Self.validLabel(row.title, maximumBytes: 2048), row.title.count <= 512,
                      Self.validLabel(row.status, maximumBytes: 64),
                      row.updated.isFinite, row.updated >= 0,
                      row.updated <= snapshot.generated_at + 300 else { return Self.failed(.invalidSnapshot) }
                let id = uuid.uuidString.lowercased()
                guard ids.insert(id).inserted else { return Self.failed(.invalidSnapshot) }
                entries.append(Entry(id: id, title: row.title, status: row.status, updated: row.updated))
            }
            return LoadResult(entries: entries, generatedAt: snapshot.generated_at,
                              ageSeconds: nil, stale: false, failure: nil).aged(at: now)
        } catch {
            let error = error as NSError
            let absent = error.domain == NSCocoaErrorDomain &&
                [NSFileNoSuchFileError, NSFileReadNoSuchFileError].contains(error.code)
            return Self.failed(absent ? .missingSnapshot : .io)
        }
    }

    /// Reuses a successfully validated snapshot for at most 30 seconds. Explicit
    /// list refreshes use force:true to see a new atomic export immediately.
    /// All disk access runs on queue and every callback is delivered on main.
    func refresh(force: Bool = false, completion: @escaping (RefreshResult) -> Void) {
        lock.lock(); let request = generation; lock.unlock()
        queue.async { [weak self] in
            guard let self = self else {
                DispatchQueue.main.async { completion(Self.failed(.cancelled)) }
                return
            }
            self.lock.lock(); let current = self.generation == request; self.lock.unlock()
            guard current else {
                DispatchQueue.main.async { completion(Self.failed(.cancelled)) }
                return
            }
            let uptime = ProcessInfo.processInfo.systemUptime
            let result: LoadResult
            if !force, let cached = self.cache, uptime - self.cachedAt < 30 {
                result = cached.aged(at: Date().timeIntervalSince1970)
            } else {
                result = self.loadSnapshot()
                // Do not cache absence/corruption for 30 seconds: a new valid
                // export should be usable as soon as it appears.
                if result.failure == nil { self.cache = result; self.cachedAt = uptime }
                else { self.cache = nil }
            }
            DispatchQueue.main.async { [weak self] in
                guard let self = self else { completion(Self.failed(.cancelled)); return }
                self.lock.lock(); let current = self.generation == request; self.lock.unlock()
                completion(current ? result : Self.failed(.cancelled))
            }
        }
    }

    /// Pending work cannot update a stopped or replaced control session.
    func cancel() { lock.lock(); generation &+= 1; lock.unlock() }
}
