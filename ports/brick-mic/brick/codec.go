package main

import "encoding/binary"

// Independent IMA ADPCM frames: every frame includes the predictor and index.
// A missing Bluetooth packet therefore cannot corrupt subsequent audio frames.
var steps = [...]int{7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767}
var indices = [...]int{-1, -1, -1, -1, 2, 4, 6, 8}

func clamp(v, lo, hi int) int {
	if v < lo {
		return lo
	}
	if v > hi {
		return hi
	}
	return v
}
func encodeADPCM(pcm []byte) []byte {
	n := len(pcm) / 2
	if n == 0 {
		return nil
	}
	out := make([]byte, 5+n/2)
	binary.LittleEndian.PutUint16(out, uint16(n))
	pred := int(int16(binary.LittleEndian.Uint16(pcm)))
	binary.LittleEndian.PutUint16(out[2:], uint16(int16(pred)))
	index := 0
	for i := 1; i < n; i++ {
		diff := int(int16(binary.LittleEndian.Uint16(pcm[i*2:]))) - pred
		code := 0
		if diff < 0 {
			code = 8
			diff = -diff
		}
		step := steps[index]
		delta := step >> 3
		if diff >= step {
			code |= 4
			diff -= step
			delta += step
		}
		if diff >= step>>1 {
			code |= 2
			diff -= step >> 1
			delta += step >> 1
		}
		if diff >= step>>2 {
			code |= 1
			delta += step >> 2
		}
		if code&8 != 0 {
			pred -= delta
		} else {
			pred += delta
		}
		pred = clamp(pred, -32768, 32767)
		index = clamp(index+indices[code&7], 0, 88)
		shift := uint(((i - 1) % 2) * 4)
		out[5+(i-1)/2] |= byte(code) << shift
	}
	return out
}
