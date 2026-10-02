import Foundation

// run-short-input-message-test.sh inserts the production message-path methods
// verbatim at the markers below. All external dependencies here are local fakes.
typealias AXUIElement = Int

struct ASRMetrics {
    var connectionMS = 0.0
    var uploadTailMS = 0.0
    var serverFinalMS = 0.0
}

final class BailianASR {
    static var instances: [BailianASR] = []
    var onMetrics: ((ASRMetrics) -> Void)?
    var onFinal: ((String, Double) -> Void)?
    var onError: ((String) -> Void)?
    var starts = 0
    var ends = 0
    var cancellations = 0
    var chunks: [Data] = []

    init() { Self.instances.append(self) }
    func start(key: String, endpoint: String, model: String) { starts += 1 }
    func append(_ data: Data) { chunks.append(data) }
    func end() { ends += 1 }
    func cancel() { cancellations += 1 }
}

final class BrickBluetooth {
    var onDisconnect: (() -> Void)?
    var writes: [[String: Any]] = []
    var results: [(session: Int, text: String)] = []
    func write(_ value: [String: Any]) { writes.append(value) }
    func result(session: Int, text: String) { results.append((session, text)) }
}

final class MicControl {
    var inputMode = "ordinary"
    var disconnections = 0
    var preparationCancellations = 0
    var lastInputFailure: String?
    var preparedPID: pid_t?
    func prepare(_ completion: @escaping (pid_t?) -> Void) { completion(preparedPID) }
    func cancelPreparation() { preparationCancellations += 1 }
    func disconnected() { disconnections += 1 }
    func message(_ data: Data) {}
}

enum RemoteEditor {
    static var preparedField: AXUIElement?
    static var preparedLookups = 0
    static func preparedFocused(_ pid: pid_t) -> AXUIElement? {
        preparedLookups += 1
        return preparedField
    }
    static func focused(_ pid: pid_t) -> AXUIElement? {
        preconditionFailure("Offline message fixtures must never target a real input field")
    }
}

final class FixtureField { var stringValue = "" }
final class FixtureTranscript {
    var string = ""
    func scrollRangeToVisible(_ range: NSRange) {}
}
final class FixtureMeter { var doubleValue = 0.0 }
enum FixtureToggleState { case on, off }
final class FixtureButton {
    var state = FixtureToggleState.off
    var isEnabled = true
}

final class AppDelegate {
    let ble = BrickBluetooth()
    let control = MicControl()
    var asr: BailianASR?
    var shortClipGate = ShortClipGate()
    let transcript = FixtureTranscript()
    let detail = FixtureField()
    let meter = FixtureMeter()
    let key = FixtureField()
    let endpoint = FixtureField()
    let model = FixtureField()
    let automatic = FixtureButton()
    let remoteControl = FixtureButton()
    var connected = true
    var inserting = false
    var insertionID = 0
    var currentSession = 0
    var frames = 0
    var samples = 0
    var broken = false
    var startAt = Date()
    var lastPCMAt = Date()
    var stopAt: Date?
    var asrMetrics: ASRMetrics?
    var releaseDelayMS = 0.0
    var targetPID: pid_t?
    var targetField: AXUIElement?
    var audioFile: FileHandle?
    var probeOut: String?
    var probeOnly = false
    var statuses: [String] = []
    var recordingStates: [Bool] = []
    var inserted: [String] = []

    func setStatus(_ text: String) { statuses.append(text) }
    func setRecording(_ value: Bool) { recordingStates.append(value) }
    func updateResult() {}
    func insert(_ text: String) { inserted.append(text) }

    func installDisconnectCallback() {
        // INJECT_PRODUCTION_DISCONNECT_CALLBACK
    }

    // INJECT_PRODUCTION_MESSAGE_METHODS
}

@main enum ShortInputMessageHarness {
    static func main() throws {
        try shortEndStaysLocal()
        try exactThresholdFlushAndNormalEnd()
        try cancelAndReplacementIsolateSessions()
        try disconnectDiscardsAudio()
        try transportIntegrityFailuresStayLocal()
        focusFailurePreservesRecognizedText()
        changedComposerPreservesRecognizedText()
        print("PASS: actual AppDelegate message path keeps short input local, opens ASR once, preserves PCM, isolates cancel/disconnect/replacement, rejects invalid transport")
    }

    static func fixture() -> AppDelegate {
        BailianASR.instances.removeAll()
        RemoteEditor.preparedField = nil
        RemoteEditor.preparedLookups = 0
        let app = AppDelegate()
        app.transcript.string = "Previous recognized result"
        app.installDisconnectCallback()
        return app
    }

    // Build valid IMA ADPCM payloads for the real production MicCodec decoder.
    static func frame(_ samples: Int = 320, seed: Int = 0) -> Data {
        precondition(samples > 0 && samples <= 320)
        let initial = UInt16(truncatingIfNeeded: seed * 131)
        var data = Data([UInt8(samples & 255), UInt8(samples >> 8),
                         UInt8(initial & 255), UInt8(initial >> 8), 0])
        data.append(Data(repeating: UInt8(seed & 0x33), count: samples / 2))
        return data
    }

    @discardableResult static func feed(_ app: AppDelegate, sid: Int,
                                       counts: [Int], start: Int = 0) throws -> Data {
        var expected = Data()
        for (offset, samples) in counts.enumerated() {
            let sequence = start + offset
            let encoded = frame(samples, seed: sequence + 1)
            expected.append(try MicCodec.decode(encoded))
            app.message(2, sid, sequence, encoded)
        }
        return expected
    }

    static func end(_ app: AppDelegate, sid: Int, frames: Int? = nil, samples: Int? = nil) throws {
        let payload = try JSONSerialization.data(withJSONObject: [
            "frames": frames ?? app.frames, "samples": samples ?? app.samples
        ])
        app.message(3, sid, 0, payload)
    }

    static func shortEndStaysLocal() throws {
        let app = fixture()
        app.message(1, 11, 0, Data())
        try feed(app, sid: 11, counts: Array(repeating: 320, count: 24) + [304])
        precondition(app.samples == 7_984 && !app.shortClipGate.isOpen)
        precondition(BailianASR.instances.isEmpty && app.asr == nil)
        precondition(app.transcript.string == "Previous recognized result")
        try end(app, sid: 11)
        precondition(BailianASR.instances.isEmpty)
        precondition(app.currentSession == 0 && !app.shortClipGate.isOpen)
        precondition(app.shortClipGate.retainedSampleCount == 0)
        precondition(app.ble.results.count == 1 && app.ble.results[0].session == 11 && app.ble.results[0].text.isEmpty)
        precondition(!app.ble.writes.contains { $0["op"] as? String == "error" })
        precondition(app.transcript.string == "Previous recognized result" && app.inserted.isEmpty)
        precondition(app.recordingStates.last == false)
        precondition(app.control.preparationCancellations == 1)

        let empty = fixture()
        empty.message(1, 12, 0, Data())
        try end(empty, sid: 12)
        precondition(BailianASR.instances.isEmpty && empty.ble.results.count == 1)
    }

    static func focusFailurePreservesRecognizedText() {
        let app = fixture()
        app.control.inputMode = "codex"
        app.currentSession = 73
        app.control.lastInputFailure = "已打开任务，输入框未取得焦点 · 文字已保留"
        let text = "保留这段中文口述，不要发到其他输入框。"
        app.complete(73, text, 0)
        precondition(app.transcript.string == text && app.ble.results.last?.text == text)
        precondition(app.inserted.isEmpty && !app.inserting)
        precondition(app.targetPID == nil && app.targetField == nil)
        precondition(app.statuses.last == app.control.lastInputFailure)
        app.abort("已取消")
        precondition(app.control.preparationCancellations == 1)
    }

    static func changedComposerPreservesRecognizedText() {
        let app = fixture()
        app.control.inputMode = "codex"
        app.control.preparedPID = 42
        app.currentSession = 74
        let text = "焦点切换时保留口述，不能填进搜索框。"
        app.complete(74, text, 0)
        precondition(RemoteEditor.preparedLookups == 1)
        precondition(app.transcript.string == text && app.ble.results.last?.text == text)
        precondition(app.inserted.isEmpty && !app.inserting)
        precondition(app.targetPID == nil && app.targetField == nil)
        precondition(app.statuses.last == "输入框焦点已变化 · 文字已保留")
    }

    static func exactThresholdFlushAndNormalEnd() throws {
        let app = fixture()
        app.message(1, 21, 0, Data())
        let prefix = try feed(app, sid: 21, counts: Array(repeating: 320, count: 24))
        precondition(BailianASR.instances.isEmpty)
        let boundary = try feed(app, sid: 21, counts: [320], start: 24)
        precondition(app.samples == 8_000 && BailianASR.instances.count == 1)
        let recognizer = BailianASR.instances[0]
        precondition(recognizer.starts == 1 && recognizer.chunks == [prefix + boundary])
        precondition(app.transcript.string.isEmpty)
        let ongoing = try feed(app, sid: 21, counts: [320, 160], start: 25)
        precondition(BailianASR.instances.count == 1 && recognizer.chunks.count == 3)
        precondition(recognizer.chunks.reduce(Data(), +) == prefix + boundary + ongoing)
        try end(app, sid: 21)
        precondition(recognizer.ends == 1 && recognizer.cancellations == 0)
        precondition(app.currentSession == 21 && app.ble.results.isEmpty)
        recognizer.onFinal?("Fixture final", 0)
        precondition(app.currentSession == 0 && app.asr == nil && !app.shortClipGate.isOpen)
        precondition(app.transcript.string == "Fixture final" && app.ble.results.last?.text == "Fixture final")
        precondition(app.inserted.isEmpty)
    }

    static func cancelAndReplacementIsolateSessions() throws {
        let app = fixture()
        app.message(1, 31, 0, Data())
        try feed(app, sid: 31, counts: Array(repeating: 320, count: 20))
        app.message(4, 31, 0, Data())
        precondition(BailianASR.instances.isEmpty && app.currentSession == 0)
        precondition(app.shortClipGate.retainedSampleCount == 0 && app.ble.results.isEmpty)
        app.message(1, 32, 0, Data())
        try feed(app, sid: 32, counts: Array(repeating: 320, count: 5))
        try end(app, sid: 32)
        precondition(BailianASR.instances.isEmpty && app.ble.results.last?.session == 32)

        app.message(1, 33, 0, Data())
        try feed(app, sid: 33, counts: Array(repeating: 320, count: 25))
        let active = BailianASR.instances[0]
        app.message(4, 33, 0, Data())
        precondition(active.cancellations == 1 && app.asr == nil)
        app.message(1, 34, 0, Data())
        try feed(app, sid: 34, counts: Array(repeating: 320, count: 23))
        // A new begin replaces another partial prefix without forwarding it.
        app.message(1, 35, 0, Data())
        let newPCM = try feed(app, sid: 35, counts: Array(repeating: 320, count: 25))
        precondition(BailianASR.instances.count == 2)
        precondition(BailianASR.instances[1].chunks == [newPCM])
        // A delayed result from the earlier session cannot finish the new one.
        active.onFinal?("Stale result", 0)
        precondition(app.currentSession == 35 && app.ble.results.count == 1)
        app.message(4, 35, 0, Data())
    }

    static func disconnectDiscardsAudio() throws {
        let app = fixture()
        app.message(1, 41, 0, Data())
        try feed(app, sid: 41, counts: Array(repeating: 320, count: 24))
        app.ble.onDisconnect?()
        precondition(!app.connected && app.control.disconnections == 1)
        precondition(app.currentSession == 0 && app.shortClipGate.retainedSampleCount == 0)
        precondition(BailianASR.instances.isEmpty && app.ble.results.isEmpty)
        precondition(app.transcript.string == "Previous recognized result")
        app.message(1, 42, 0, Data())
        try feed(app, sid: 42, counts: Array(repeating: 320, count: 25))
        let recognizer = BailianASR.instances[0]
        app.ble.onDisconnect?()
        precondition(recognizer.cancellations == 1 && app.asr == nil && app.currentSession == 0)
        precondition(app.shortClipGate.retainedSampleCount == 0)
    }

    static func transportIntegrityFailuresStayLocal() throws {
        let app = fixture()
        app.message(1, 51, 0, Data())
        app.message(2, 52, 0, frame())
        try end(app, sid: 52, frames: 0, samples: 0)
        precondition(app.currentSession == 51 && app.frames == 0 && BailianASR.instances.isEmpty)
        app.message(2, 51, 1, frame())
        precondition(app.currentSession == 0 && app.broken && BailianASR.instances.isEmpty)
        precondition(app.ble.writes.last?["op"] as? String == "error")

        app.message(1, 53, 0, Data())
        try feed(app, sid: 53, counts: Array(repeating: 320, count: 24))
        try end(app, sid: 53, frames: app.frames + 1)
        precondition(app.currentSession == 0 && BailianASR.instances.isEmpty && app.ble.results.isEmpty)
        precondition(app.shortClipGate.retainedSampleCount == 0)

        app.message(1, 54, 0, Data())
        app.message(2, 54, 0, Data([1, 2, 3]))
        precondition(app.currentSession == 0 && app.broken && BailianASR.instances.isEmpty)

        app.message(1, 55, 0, Data())
        try feed(app, sid: 55, counts: Array(repeating: 320, count: 25))
        let active = BailianASR.instances[0]
        try end(app, sid: 55, samples: app.samples - 1)
        precondition(active.ends == 0 && active.cancellations == 1 && app.currentSession == 0)
        precondition(app.ble.results.isEmpty && app.shortClipGate.retainedSampleCount == 0)
    }
}
