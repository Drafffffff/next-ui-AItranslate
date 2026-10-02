import Foundation

/// Holds PCM locally until it contains enough audio to justify an ASR request.
/// Input is little-endian, mono PCM16 at 16 kHz, with complete samples per chunk.
struct ShortClipGate {
    enum Error: Swift.Error, Equatable {
        case invalidPCMByteCount(Int)
    }

    private let minimumBytes: Int
    private var pending = Data()
    private(set) var isOpen = false

    /// Retained audio is always below the threshold; opened gates retain none.
    var retainedSampleCount: Int { pending.count / 2 }

    init(minimumSamples: Int = 8_000) {
        precondition(minimumSamples > 0 && minimumSamples <= Int.max / 2)
        minimumBytes = minimumSamples * 2
    }

    /// Returns the complete buffered prefix once, then each subsequent chunk.
    /// A short recording never returns data. Invalid chunks leave state unchanged.
    mutating func append(_ pcm: Data) throws -> Data? {
        guard pcm.count.isMultiple(of: 2) else {
            throw Error.invalidPCMByteCount(pcm.count)
        }
        guard !pcm.isEmpty else { return nil }
        guard !isOpen else { return pcm }

        // Compare against remaining bytes rather than adding chunk sizes. Besides
        // avoiding overflow, this never retains an oversized chunk in pending.
        if pcm.count < minimumBytes - pending.count {
            pending.append(pcm)
            return nil
        }

        let output = pending + pcm
        pending = Data()
        isOpen = true
        return output
    }

    mutating func reset() {
        pending = Data()
        isOpen = false
    }
}
