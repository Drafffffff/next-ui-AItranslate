import Foundation
import Darwin

// Darwin's Swift module exposes the struct named flock, hiding the C function of
// the same name. Link the standard C entry point explicitly.
@_silgen_name("flock")
private func kernelFlock(_ descriptor: Int32, _ operation: Int32) -> Int32

/// A process-scoped lock on a stable file. The PID is advisory; only flock decides ownership.
/// Do not remove this file, including when releasing the lock: every instance must use one inode.
final class SingleInstanceLock {
    enum LockError: Swift.Error, CustomStringConvertible {
        case system(operation: String, code: Int32)
        case unsafeFile

        var description: String {
            switch self {
            case let .system(operation, code):
                return "\(operation): \(String(cString: strerror(code)))"
            case .unsafeFile:
                return "The instance lock must be a regular file owned by the current user, without additional hard links."
            }
        }
    }

    let url: URL
    private let mutex = NSLock()
    private var descriptor: Int32 = -1
    private var recordedOwner: pid_t?

    /// After a busy result this is only a best-effort hint, never proof of a live owner.
    var ownerPID: pid_t? {
        mutex.lock()
        defer { mutex.unlock() }
        return recordedOwner
    }

    init(url: URL) {
        self.url = url
    }

    /// Returns false when another process owns the lock. All other failures throw.
    /// Calling this again on an already-owned lock is safe and returns true.
    func acquire() throws -> Bool {
        mutex.lock()
        defer { mutex.unlock() }
        if descriptor >= 0 { return true }
        recordedOwner = nil
        guard url.isFileURL else { throw LockError.unsafeFile }

        try FileManager.default.createDirectory(
            at: url.deletingLastPathComponent(),
            withIntermediateDirectories: true,
            attributes: [.posixPermissions: 0o700]
        )
        let fd = url.path.withCString {
            Darwin.open($0, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, mode_t(0o600))
        }
        guard fd >= 0 else { throw systemError("open instance lock") }
        var keepDescriptor = false
        defer { if !keepDescriptor { Darwin.close(fd) } }

        var metadata = stat()
        guard Darwin.fstat(fd, &metadata) == 0 else { throw systemError("inspect instance lock") }
        guard metadata.st_mode & mode_t(S_IFMT) == mode_t(S_IFREG),
              metadata.st_uid == getuid(),
              metadata.st_nlink == 1 else { throw LockError.unsafeFile }
        guard Darwin.fchmod(fd, mode_t(0o600)) == 0 else { throw systemError("protect instance lock") }

        var result: Int32
        repeat { result = kernelFlock(fd, LOCK_EX | LOCK_NB) } while result != 0 && errno == EINTR
        if result != 0 {
            let code = errno
            if code == EWOULDBLOCK || code == EAGAIN {
                recordedOwner = readOwner(fd)
                return false
            }
            throw LockError.system(operation: "acquire instance lock", code: code)
        }

        // If this write fails, closing fd releases the acquired lock before returning an error.
        try writeOwner(fd, pid: getpid())
        descriptor = fd
        recordedOwner = getpid()
        keepDescriptor = true
        return true
    }

    func release() {
        mutex.lock()
        defer { mutex.unlock() }
        if descriptor >= 0 {
            // Closing the last descriptor releases flock, including on process exit or crash.
            Darwin.close(descriptor)
            descriptor = -1
        }
        recordedOwner = nil
    }

    deinit { release() }

    private func systemError(_ operation: String) -> LockError {
        .system(operation: operation, code: errno)
    }

    private func writeOwner(_ fd: Int32, pid: pid_t) throws {
        guard Darwin.ftruncate(fd, 0) == 0 else { throw systemError("truncate instance lock") }
        let bytes = Array("\(pid)\n".utf8)
        try bytes.withUnsafeBytes { buffer in
            var written = 0
            while written < buffer.count {
                let count = Darwin.pwrite(fd, buffer.baseAddress!.advanced(by: written), buffer.count - written, off_t(written))
                if count < 0 {
                    if errno == EINTR { continue }
                    throw systemError("write instance lock owner")
                }
                guard count > 0 else { throw LockError.system(operation: "write instance lock owner", code: EIO) }
                written += count
            }
        }
    }

    private func readOwner(_ fd: Int32) -> pid_t? {
        var bytes = [UInt8](repeating: 0, count: 32)
        let count = bytes.withUnsafeMutableBytes { Darwin.pread(fd, $0.baseAddress!, $0.count, 0) }
        guard count > 0,
              let text = String(bytes: bytes.prefix(count), encoding: .utf8),
              let pid = pid_t(text.trimmingCharacters(in: .whitespacesAndNewlines)),
              pid > 0 else { return nil }
        return pid
    }
}
