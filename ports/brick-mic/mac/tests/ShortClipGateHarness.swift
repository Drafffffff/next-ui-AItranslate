import Foundation

@main enum ShortClipGateHarness {
    static func main() throws {
        try thresholdAndContinuity()
        try irregularChunksAndReset()
        try invalidChunksDoNotMutate()
        try oversizedChunkIsNotRetained()
        print("PASS: short audio stays local, threshold flush is lossless, chunk/reset isolation, invalid PCM rejection, bounded retention")
    }

    private static func check(_ condition: @autoclosure () throws -> Bool) rethrows {
        let verified = try condition()
        precondition(verified)
    }

    private static func pcm(_ samples: Int, seed: UInt8 = 0) -> Data {
        Data((0..<(samples * 2)).map { UInt8(truncatingIfNeeded: $0) &+ seed })
    }

    private static func thresholdAndContinuity() throws {
        var gate = ShortClipGate()
        let short = pcm(7_984) // 0.499 s at 16 kHz.
        try check(gate.append(short) == nil)
        precondition(!gate.isOpen && gate.retainedSampleCount == 7_984)
        try check(gate.append(Data()) == nil)

        let boundary = pcm(16, seed: 17)
        let flushed = try gate.append(boundary)
        precondition(flushed == short + boundary)
        precondition(gate.isOpen && gate.retainedSampleCount == 0)

        let next = pcm(320, seed: 47) // Typical 20 ms chunk, not the old prefix.
        try check(gate.append(next) == next)
        precondition(gate.retainedSampleCount == 0)
    }

    private static func irregularChunksAndReset() throws {
        var gate = ShortClipGate()
        var source = Data()
        var emitted = Data()
        // Complete samples need not line up with a 20 ms frame or the threshold.
        let sampleCounts = [1, 319, 641, 7_000, 3, 317, 2_048]
        for (index, samples) in sampleCounts.enumerated() {
            let chunk = pcm(samples, seed: UInt8(index))
            source.append(chunk)
            if let output = try gate.append(chunk) { emitted.append(output) }
            precondition(gate.retainedSampleCount < 8_000)
        }
        precondition(gate.isOpen && emitted == source)

        gate.reset()
        precondition(!gate.isOpen && gate.retainedSampleCount == 0)
        let discarded = pcm(4_000, seed: 91)
        try check(gate.append(discarded) == nil)
        gate.reset()
        let newSession = pcm(8_000, seed: 101)
        try check(gate.append(newSession) == newSession)
        precondition(gate.retainedSampleCount == 0)
    }

    private static func invalidChunksDoNotMutate() throws {
        var gate = ShortClipGate(minimumSamples: 2)
        let valid = Data([0x01, 0x02])
        try check(gate.append(valid) == nil)
        do {
            _ = try gate.append(Data([0x03]))
            preconditionFailure("An incomplete PCM16 sample must be rejected")
        } catch let error as ShortClipGate.Error {
            precondition(error == .invalidPCMByteCount(1))
        }
        precondition(!gate.isOpen && gate.retainedSampleCount == 1)
        try check(gate.append(Data([0x04, 0x05])) == Data([0x01, 0x02, 0x04, 0x05]))
        do {
            _ = try gate.append(Data([0x06, 0x07, 0x08]))
            preconditionFailure("Opened gates must still validate PCM")
        } catch let error as ShortClipGate.Error {
            precondition(error == .invalidPCMByteCount(3))
        }
        precondition(gate.isOpen && gate.retainedSampleCount == 0)
        try check(gate.append(Data([0x09, 0x0a])) == Data([0x09, 0x0a]))
    }

    private static func oversizedChunkIsNotRetained() throws {
        var gate = ShortClipGate()
        let prefix = pcm(7_999, seed: 21)
        try check(gate.append(prefix) == nil)
        let large = Data(repeating: 0x55, count: 4 * 1_024 * 1_024)
        let flushed = try gate.append(large)
        precondition(flushed?.count == prefix.count + large.count)
        precondition(flushed?.prefix(prefix.count) == prefix)
        precondition(flushed?.suffix(large.count) == large)
        precondition(gate.isOpen && gate.retainedSampleCount == 0)
        try check(gate.append(large) == large)
        precondition(gate.retainedSampleCount == 0)
        gate.reset()
        precondition(!gate.isOpen && gate.retainedSampleCount == 0)
    }
}
