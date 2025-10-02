from dataclasses import dataclass
from typing import Optional


# -------------------------
# PSG (AY-3-8910) helpers
# -------------------------

def psg_mode_from_reg7(vvc: int, ch: int) -> int:
    """
    Register 7 bit mapping:
      bits 0-2: tone disable for ch 0/1/2
      bits 3-5: noise disable for ch 0/1/2
    Mode:
      3 = tone+noise, 2 = noise only, 1 = tone only, 0 = neither
    """
    tone_mask = 1 << ch
    noise_mask = 1 << (ch + 3)
    tone_enabled = (vvc & tone_mask) == 0
    noise_enabled = (vvc & noise_mask) == 0
    if tone_enabled and noise_enabled:
        return 3
    if noise_enabled and not tone_enabled:
        return 2
    if tone_enabled and not noise_enabled:
        return 1
    return 0


def psg_freq_from_regs(fa: int, fb: int) -> int:
    """
    12-bit frequency register (fa low 8-bit, fb high 4-bit in AY).
    Return register value (divider).
    """
    return (fb << 8) | (fa & 0xFF)


def psg_tone_hz_from_reg(reg16: int) -> float:
    """
    AY tone frequency ≈ 1_789_772.5 / (16 * reg16). If reg16=0 => highest tone.
    """
    if reg16 == 0:
        return 111860.78125
    return 1789772.5 / (16.0 * reg16)


# -------------------------
# SCC (K051649) helpers
# -------------------------

@dataclass
class SCCAddress:
    # Logical absolute address used for convenience
    abs_addr: int
    ch: Optional[int]  # 0..3 (basic SCC) or 4 (SCC+), or None
    kind: str  # "wtb", "f1", "f2", "vol", "en", "unknown"
    wtb_index: Optional[int] = None  # 0..31 offset within wavetable if applicable


def scc_abs_addr_from_port(pp: int, aa: int) -> int:
    """
    Mapping:
      port 0 -> 0x9800 + aa
      port 1 -> 0x9880 + aa
      port 2 -> 0x988A + aa
      port 3 -> 0x988F + aa
    """
    if pp == 0:
        return 0x9800 + aa
    elif pp == 1:
        return 0x9880 + aa
    elif pp == 2:
        return 0x988A + aa
    elif pp == 3:
        return 0x988F + aa
    else:
        return (pp << 8) | aa


def scc_decode_abs_addr(abs_addr: int) -> SCCAddress:
    """
    Classify logical address into channel/kind.
    """
    # Wavetable regions (32 bytes per channel)
    if 0x9800 <= abs_addr <= 0x981F:
        return SCCAddress(abs_addr, ch=0, kind="wtb", wtb_index=abs_addr - 0x9800)
    if 0x9820 <= abs_addr <= 0x983F:
        return SCCAddress(abs_addr, ch=1, kind="wtb", wtb_index=abs_addr - 0x9820)
    if 0x9840 <= abs_addr <= 0x985F:
        return SCCAddress(abs_addr, ch=2, kind="wtb", wtb_index=abs_addr - 0x9840)
    if 0x9860 <= abs_addr <= 0x987F:
        return SCCAddress(abs_addr, ch=3, kind="wtb", wtb_index=abs_addr - 0x9860)

    # Freq1/Freq2 pairs
    if 0x9880 <= abs_addr <= 0x9887:
        idx = abs_addr - 0x9880
        ch = idx // 2
        kind = "f1" if (idx % 2) == 0 else "f2"
        return SCCAddress(abs_addr, ch=ch, kind=kind)

    # Volume
    if 0x988A <= abs_addr <= 0x988D:
        ch = abs_addr - 0x988A
        return SCCAddress(abs_addr, ch=ch, kind="vol")

    # Enable (bitfield)
    if abs_addr == 0x988F:
        return SCCAddress(abs_addr, ch=None, kind="en")

    return SCCAddress(abs_addr, ch=None, kind="unknown")