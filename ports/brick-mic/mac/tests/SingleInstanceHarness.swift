import Foundation
import Darwin

@main
struct SingleInstanceHarness {
    static let files = FileManager.default

    static func expect(_ condition: @autoclosure () throws -> Bool, _ message: String) throws {
        if !(try condition()) { throw TestFailure(message: message) }
    }

    struct TestFailure: Swift.Error, CustomStringConvertible {
        let message: String
        var description: String { message }
    }

    static func waitFor(_ message: String, timeout: TimeInterval = 8, _ condition: () -> Bool) throws {
        let deadline = Date().addingTimeInterval(timeout)
        while !condition() {
            if Date() >= deadline { throw TestFailure(message: "Timeout: \(message)") }
            Thread.sleep(forTimeInterval: 0.01)
        }
    }

    static func write(_ text: String, _ url: URL) throws {
        try Data(text.utf8).write(to: url, options: .atomic)
    }

    static func launch(lock: URL, result: URL, release: URL, barrier: URL? = nil) throws -> Process {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: CommandLine.arguments[0]).standardizedFileURL
        process.arguments = ["--child", lock.path, result.path, release.path, barrier?.path ?? ""]
        try process.run()
        return process
    }

    static func stop(_ process: Process) {
        if process.isRunning { Darwin.kill(process.processIdentifier, SIGKILL) }
        process.waitUntilExit()
    }

    static func child() throws {
        let lock = SingleInstanceLock(url: URL(fileURLWithPath: CommandLine.arguments[2]))
        let result = URL(fileURLWithPath: CommandLine.arguments[3])
        let release = URL(fileURLWithPath: CommandLine.arguments[4])
        let barrier = CommandLine.arguments[5]
        if !barrier.isEmpty { try waitFor("race barrier", { files.fileExists(atPath: barrier) }) }
        let acquired = try lock.acquire()
        try write("\(acquired ? "owner" : "busy") \(getpid()) \(lock.ownerPID ?? 0)", result)
        if acquired {
            try waitFor("owner release", timeout: 20, { files.fileExists(atPath: release.path) })
            lock.release()
        }
    }

    static func tests() throws {
        let fixture = files.temporaryDirectory.appendingPathComponent("brick-mic-single-instance-\(UUID().uuidString)", isDirectory: true)
        try files.createDirectory(at: fixture, withIntermediateDirectories: true)
        defer { try? files.removeItem(at: fixture) }
        var children: [Process] = []
        defer { for child in children { stop(child) } }
        let lockURL = fixture.appendingPathComponent("Application Support/Brick Mic/instance.lock")

        // An old PID never blocks acquisition. Only the live kernel lock does.
        try files.createDirectory(at: lockURL.deletingLastPathComponent(), withIntermediateDirectories: true)
        try write("999999\n", lockURL)
        let stale = SingleInstanceLock(url: lockURL)
        try expect(try stale.acquire(), "A stale PID file blocked acquisition")
        try expect(stale.ownerPID == getpid(), "An acquired lock did not record the current PID")
        try expect(try stale.acquire(), "Repeated acquire lost the owned lock")
        let inodeBefore = try files.attributesOfItem(atPath: lockURL.path)[.systemFileNumber] as! NSNumber
        stale.release()
        try expect(files.fileExists(atPath: lockURL.path), "release removed the stable lock file")
        let inodeAfter = try files.attributesOfItem(atPath: lockURL.path)[.systemFileNumber] as! NSNumber
        try expect(inodeBefore == inodeAfter, "release changed the lock inode")

        // Two real processes cannot both enter, even if their PID files are present.
        let result1 = fixture.appendingPathComponent("graceful-result")
        let release1 = fixture.appendingPathComponent("graceful-release")
        let owner1 = try launch(lock: lockURL, result: result1, release: release1)
        children.append(owner1)
        try waitFor("first process owns lock", { files.fileExists(atPath: result1.path) })
        try expect(try String(contentsOf: result1, encoding: .utf8).hasPrefix("owner "), "First child could not own the lock")
        let contender = SingleInstanceLock(url: lockURL)
        try expect(!(try contender.acquire()), "A second process acquired a live lock")
        try expect(contender.ownerPID == owner1.processIdentifier, "Busy owner hint did not identify the holder")
        try write("release", release1)
        owner1.waitUntilExit()
        try expect(owner1.terminationStatus == 0, "Graceful child failed")
        try expect(try contender.acquire(), "A graceful exit did not release the lock")
        contender.release()

        // SIGKILL cannot strand the lock; there is deliberately no PID cleanup requirement.
        let crashResult = fixture.appendingPathComponent("crash-result")
        let crashOwner = try launch(lock: lockURL, result: crashResult, release: fixture.appendingPathComponent("never-release"))
        children.append(crashOwner)
        try waitFor("crash fixture owns lock", { files.fileExists(atPath: crashResult.path) })
        try expect(try String(contentsOf: crashResult, encoding: .utf8).hasPrefix("owner "), "Crash fixture could not own lock")
        Darwin.kill(crashOwner.processIdentifier, SIGKILL)
        crashOwner.waitUntilExit()
        let afterCrash = SingleInstanceLock(url: lockURL)
        try expect(try afterCrash.acquire(), "A killed process stranded the lock")
        afterCrash.release()

        // Simultaneous starts produce exactly one owner. Hold the winner until all contenders reply.
        let barrier = fixture.appendingPathComponent("race-barrier")
        let raceRelease = fixture.appendingPathComponent("race-release")
        let resultURLs = (0..<8).map { fixture.appendingPathComponent("race-result-\($0)") }
        for result in resultURLs {
            children.append(try launch(lock: lockURL, result: result, release: raceRelease, barrier: barrier))
        }
        try write("start", barrier)
        try waitFor("all race results", { resultURLs.allSatisfy { files.fileExists(atPath: $0.path) } })
        let results = try resultURLs.map { try String(contentsOf: $0, encoding: .utf8) }
        try expect(results.filter { $0.hasPrefix("owner ") }.count == 1, "Concurrent launch admitted more than one owner")
        try expect(results.filter { $0.hasPrefix("busy ") }.count == 7, "Concurrent launch did not reject all other instances")
        try write("release", raceRelease)
        for child in children { child.waitUntilExit() }
        try expect(children.suffix(8).allSatisfy { $0.terminationStatus == 0 }, "A race child failed")

        // A symlink must not cause unrelated user files to be truncated or locked.
        let marker = fixture.appendingPathComponent("keep.txt")
        let symlink = fixture.appendingPathComponent("symlink.lock")
        try write("keep me", marker)
        try files.createSymbolicLink(at: symlink, withDestinationURL: marker)
        do {
            _ = try SingleInstanceLock(url: symlink).acquire()
            throw TestFailure(message: "A symlink lock path was accepted")
        } catch is SingleInstanceLock.LockError {}
        try expect(try String(contentsOf: marker, encoding: .utf8) == "keep me", "A rejected symlink modified its target")

        let hardlink = fixture.appendingPathComponent("hardlink.lock")
        try files.linkItem(at: marker, to: hardlink)
        do {
            _ = try SingleInstanceLock(url: hardlink).acquire()
            throw TestFailure(message: "A hardlink lock path was accepted")
        } catch is SingleInstanceLock.LockError {}
        try expect(try String(contentsOf: marker, encoding: .utf8) == "keep me", "A rejected hardlink modified its target")

        let directory = fixture.appendingPathComponent("directory.lock", isDirectory: true)
        try files.createDirectory(at: directory, withIntermediateDirectories: true)
        do {
            _ = try SingleInstanceLock(url: directory).acquire()
            throw TestFailure(message: "A directory lock path was accepted")
        } catch is SingleInstanceLock.LockError {}

        print("PASS: cross-process exclusion, 8-process race, graceful/crash release, stale PID recovery, stable inode, and unsafe-file rejection")
    }

    static func main() {
        do {
            if CommandLine.arguments.count > 1 && CommandLine.arguments[1] == "--child" { try child() }
            else { try tests() }
        } catch {
            fputs("FAIL: \(error)\n", stderr)
            exit(1)
        }
    }
}
