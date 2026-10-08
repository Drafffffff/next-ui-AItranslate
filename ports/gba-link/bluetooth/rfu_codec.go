package main

import (
	"bytes"
	"encoding/binary"
	"fmt"
)

// v5 encodes only complete RFU wire packets. Full integer widths are retained
// as canonical varints; only trailing zero padding is omitted. Decoding restores
// the exact frontend byte stream. Everything else uses the existing tail codec.
func encodeRFUChunk(data []byte) ([]byte, error) {
	generic, err := encodeBridgeChunk(data)
	if err != nil {
		return nil, err
	}
	fallback := append([]byte{0}, generic...)
	if len(data) < 20 {
		return fallback, nil
	}
	outer := binary.BigEndian.Uint32(data)
	body := binary.BigEndian.Uint32(data[4:])
	if int(body) != len(data)-8 || (outer != 1 && outer != 4 && outer != 5) {
		return fallback, nil
	}
	offset := 8
	var exchange uint64
	if outer != 1 {
		if len(data) < 28 {
			return fallback, nil
		}
		exchange = binary.BigEndian.Uint64(data[8:])
		offset = 16
	}
	if !bytes.Equal(data[offset:offset+4], []byte("RFU1")) {
		return fallback, nil
	}
	kind := binary.BigEndian.Uint32(data[offset+4:])
	info := binary.BigEndian.Uint32(data[offset+8:])
	if kind > 7 || (outer == 4 && kind != 5) || (outer == 5 && kind != 7) {
		return fallback, nil
	}
	encoded := []byte{1}
	encoded = binary.AppendUvarint(encoded, uint64(outer))
	encoded = binary.AppendUvarint(encoded, uint64(body))
	if outer != 1 {
		encoded = binary.AppendUvarint(encoded, exchange)
	}
	encoded = binary.AppendUvarint(encoded, uint64(kind))
	encoded = binary.AppendUvarint(encoded, uint64(info))
	end := len(data)
	for end > offset+12 && data[end-1] == 0 {
		end--
	}
	encoded = append(encoded, data[offset+12:end]...)
	if len(encoded) >= len(fallback) {
		return fallback, nil
	}
	return encoded, nil
}

func decodeRFUChunk(encoded []byte) ([]byte, error) {
	if len(encoded) == 0 {
		return nil, fmt.Errorf("empty RFU chunk")
	}
	if encoded[0] == 0 {
		return decodeBridgeChunk(encoded[1:])
	}
	if encoded[0] != 1 {
		return nil, fmt.Errorf("unknown RFU chunk encoding")
	}
	cursor := 1
	read := func() (uint64, error) {
		value, n := binary.Uvarint(encoded[cursor:])
		if n <= 0 || len(binary.AppendUvarint(nil, value)) != n {
			return 0, fmt.Errorf("invalid RFU varint")
		}
		cursor += n
		return value, nil
	}
	outer, err := read()
	if err != nil {
		return nil, err
	}
	if outer != 1 && outer != 4 && outer != 5 {
		return nil, fmt.Errorf("invalid RFU outer type")
	}
	body, err := read()
	if err != nil {
		return nil, err
	}
	offset := 8
	var exchange uint64
	if outer != 1 {
		exchange, err = read()
		if err != nil {
			return nil, err
		}
		offset = 16
	}
	if body+8 < uint64(offset+12) || body > bridgeChunkLimit-8 {
		return nil, fmt.Errorf("invalid RFU body length")
	}
	kind, err := read()
	if err != nil {
		return nil, err
	}
	info, err := read()
	if err != nil {
		return nil, err
	}
	if kind > 7 || info > 0xffffffff || (outer == 4 && kind != 5) || (outer == 5 && kind != 7) {
		return nil, fmt.Errorf("invalid RFU fields")
	}
	length := int(body) + 8
	if len(encoded)-cursor > length-offset-12 {
		return nil, fmt.Errorf("RFU payload exceeds body")
	}
	data := make([]byte, length)
	binary.BigEndian.PutUint32(data, uint32(outer))
	binary.BigEndian.PutUint32(data[4:], uint32(body))
	if outer != 1 {
		binary.BigEndian.PutUint64(data[8:], exchange)
	}
	copy(data[offset:], "RFU1")
	binary.BigEndian.PutUint32(data[offset+4:], uint32(kind))
	binary.BigEndian.PutUint32(data[offset+8:], uint32(info))
	copy(data[offset+12:], encoded[cursor:])
	return data, nil
}

func encodeTransportChunk(data []byte) ([]byte, error) {
	if bleTransportVersion == "5" {
		return encodeRFUChunk(data)
	}
	return encodeBridgeChunk(data)
}
func decodeTransportChunk(data []byte) ([]byte, error) {
	if bleTransportVersion == "5" {
		return decodeRFUChunk(data)
	}
	return decodeBridgeChunk(data)
}
