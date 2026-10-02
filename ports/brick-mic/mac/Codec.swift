import Foundation

enum MicCodecError: Error { case invalidFrame }
enum MicCodec {
    static let steps = [7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767]
    static let indices = [-1,-1,-1,-1,2,4,6,8]
    static func u16(_ b: [UInt8], _ offset: Int) -> Int { Int(b[offset]) | Int(b[offset+1]) << 8 }
    static func decode(_ data: Data) throws -> Data {
        let b = [UInt8](data)
        guard b.count >= 5 else { throw MicCodecError.invalidFrame }
        let n = u16(b, 0)
        guard n > 0, n <= 320, b.count == 5 + n/2, b[4] <= 88 else { throw MicCodecError.invalidFrame }
        var predictor = Int(Int16(bitPattern: UInt16(u16(b,2))))
        var index = Int(b[4]); var out = Data(capacity: n*2)
        func append(_ x: Int) { let v = UInt16(bitPattern: Int16(x)); out.append(UInt8(v & 255)); out.append(UInt8(v >> 8)) }
        append(predictor)
        if n > 1 { for i in 1..<n {
            let code = Int((b[5+(i-1)/2] >> (((i-1)%2)*4)) & 15)
            let step = steps[index]; var delta = step >> 3
            if code & 4 != 0 { delta += step }; if code & 2 != 0 { delta += step >> 1 }; if code & 1 != 0 { delta += step >> 2 }
            predictor = max(-32768,min(32767,predictor + (code & 8 == 0 ? delta : -delta)))
            index = max(0,min(88,index+indices[code & 7])); append(predictor)
        } }
        return out
    }
}

// Reassemble bounded messages; each fragment identifies its session and frame.
// Reject missing/out-of-order fragments instead of silently emitting corrupt PCM.
final class MicAssembler {
    private var key = ""
    private var bytes = Data()
    private var total = 0
    func reset() { key = ""; bytes.removeAll(); total = 0 }
    func append(_ data: Data) throws -> (kind: UInt8, session: Int, frame: Int, data: Data)? {
        let b = [UInt8](data)
        guard b.count > 9 else { throw MicCodecError.invalidFrame }
        let sid = MicCodec.u16(b,1), frame = MicCodec.u16(b,3), offset = MicCodec.u16(b,5), size = MicCodec.u16(b,7)
        let nextKey = "\(b[0]):\(sid):\(frame)"
        guard size > 0, size <= 16384, offset+b.count-9 <= size else { throw MicCodecError.invalidFrame }
        if offset == 0 { key=nextKey;bytes=Data();total=size }
        guard key == nextKey, offset == bytes.count, total == size else { reset(); throw MicCodecError.invalidFrame }
        bytes.append(contentsOf: b[9...])
        if bytes.count == total { let completed=bytes;reset();return (b[0],sid,frame,completed) }
        return nil
    }
}
