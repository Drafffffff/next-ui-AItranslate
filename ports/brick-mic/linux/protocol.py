"""The same bounded BLE wire format and independent IMA frames as the Mac client."""
import struct

SERVICE = 'ba1c0000-7e89-4c31-a2d0-4f923cb1a100'
AUDIO = 'ba1c0001-7e89-4c31-a2d0-4f923cb1a100'
CONTROL = 'ba1c0002-7e89-4c31-a2d0-4f923cb1a100'
STEPS = (7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767)
INDICES = (-1,-1,-1,-1,2,4,6,8)

def decode(data):
    if len(data)<5: raise ValueError('short ADPCM frame')
    n,pred,index = struct.unpack_from('<HhB',data)
    if not 0<n<=320 or len(data)!=5+n//2 or index>88: raise ValueError('invalid ADPCM frame')
    samples=[pred]
    for i in range(1,n):
        code=(data[5+(i-1)//2] >> ((i-1)%2*4)) & 15
        step=STEPS[index]; delta=step>>3
        if code&4: delta+=step
        if code&2: delta+=step>>1
        if code&1: delta+=step>>2
        pred=max(-32768,min(32767,pred+(-delta if code&8 else delta)))
        index=max(0,min(88,index+INDICES[code&7])); samples.append(pred)
    return struct.pack('<'+'h'*n,*samples)

class Assembler:
    def __init__(self): self.reset()
    def reset(self): self.key=None; self.total=0; self.data=bytearray()
    def append(self,packet):
        if len(packet)<=9: raise ValueError('short fragment')
        kind,sid,frame,offset,size=struct.unpack_from('<BHHHH',packet)
        if not 0<size<=16384 or offset+len(packet)-9>size: raise ValueError('invalid fragment size')
        key=(kind,sid,frame)
        if offset==0: self.key=key; self.total=size; self.data=bytearray()
        if self.key!=key or len(self.data)!=offset or self.total!=size:
            self.reset(); raise ValueError('out of order fragment')
        self.data.extend(packet[9:])
        if len(self.data)==size:
            result=(*key,bytes(self.data));self.reset();return result
        return None
