from __future__ import annotations
from typing import List, Tuple

def channel_from_reg(reg: int) -> int:
    if 0x20 <= reg <= 0x28:
        return reg - 0x20
    if 0x10 <= reg <= 0x18:
        return reg - 0x10
    if 0x30 <= reg <= 0x38:
        return reg - 0x30
    return -1

def type_from_reg(reg: int) -> str:
    if 0x20 <= reg <= 0x28:
        return "fL"
    if 0x10 <= reg <= 0x18:
        return "fHBK"
    if 0x30 <= reg <= 0x38:
        return "iv"
    if reg == 0x0E:
        return "rhy"
    return f"reg{reg:02X}"

def derive_fm_params(regs: List[int], ch: int) -> Tuple[int, int, int, int]:
    if not (0 <= ch <= 8):
        return 0, 0, 0, 0
    fnum_l = regs[0x20 + ch] & 0xFF
    fnum_h = regs[0x10 + ch] & 0xFF
    fnum = ((fnum_h & 0x03) << 8) | fnum_l
    block = (fnum_h >> 2) & 0x07
    key_on = (fnum_h >> 5) & 0x01
    return key_on, block, fnum, fnum_l

def derive_inst_vol(regs: List[int], ch: int) -> Tuple[int, int]:
    if not (0 <= ch <= 8):
        return 0, 0
    iv = regs[0x30 + ch] & 0xFF
    inst = (iv >> 4) & 0x0F
    vol = iv & 0x0F
    return inst, vol

def derive_rhythm(regs: List[int]) -> Tuple[int, int]:
    r = regs[0x0E] & 0xFF
    rhythm_mode = (r >> 5) & 0x01
    rhythm_bits = r & 0x1F
    return rhythm_mode, rhythm_bits