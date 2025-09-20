#!/usr/bin/env python3
import sys, struct

ALLOWED_SINGLE = set(range(0x70,0x80)) | {0x66,0x62,0x63}
SIMPLE_1PARAM = {0x50,0x51,0x5E,0x5F}
SIMPLE_2PARAM = {0xA0}
FOUR_PARAM    = {0xD2}
WAIT_0x61     = 0x61

def read_le16(b,off):
    return b[off] | (b[off+1]<<8)

def main(path):
    with open(path,'rb') as f:
        data = f.read()
    if len(data) < 0x40 or data[0:4] != b'Vgm ':
        print("Not a VGM.")
        return 1
    data_off = struct.unpack_from("<I", data, 0x34)[0]
    if data_off == 0:
        start = 0x40
    else:
        start = 0x34 + data_off
    i = start
    errors=[]
    while i < len(data):
        cmd = data[i]
        if cmd == 0x66:
            break
        if cmd in ALLOWED_SINGLE:
            i+=1; continue
        if cmd == WAIT_0x61:
            if i+2 >= len(data): errors.append((i,"trunc wait"))
            i+=3; continue
        if cmd in SIMPLE_1PARAM:
            if i+2 >= len(data): errors.append((i,"trunc simple1"))
            i+=3; continue
        if cmd in SIMPLE_2PARAM:
            if i+2 >= len(data): errors.append((i,"trunc AY8910"))
            i+=3; continue
        if cmd in FOUR_PARAM:
            if i+3 >= len(data): errors.append((i,"trunc K051649"))
            i+=4; continue
        # Unknown → treat as error
        if cmd == 0x00:
            errors.append((i,"illegal stray 0x00"))
            i+=1
            continue
        errors.append((i,f"unknown 0x{cmd:02X}"))
        i+=1
    if errors:
        print("Validation FAILED:")
        for off,msg in errors[:200]:
            print(f"  0x{off:06X}: {msg}")
        return 2
    print("Validation OK.")
    return 0

if __name__=="__main__":
    if len(sys.argv)<2:
        print("Usage: vgm_validate.py <file.vgm>")
        sys.exit(1)
    sys.exit(main(sys.argv[1]))