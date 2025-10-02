import io
import struct
from typing import Dict, Generator, Tuple


class VGMParser:
    """
    Minimal VGM parser:
      - Reads header (little endian)
      - Iterates commands and yields (cmd, payload_bytes, abs_samples)
      - Handles waits (0x61/0x62/0x63/0x70..0x7F)
      - Handles AY8910 (0xA0), YM2413 (0x51), and K051649/SCC (0xD2)
      - Skips data blocks (0x67)
    """

    def __init__(self, data: bytes):
        self._bio = io.BytesIO(data)
        self.header: Dict[str, int] = {}
        self.sample_rate = 44100  # default; header 'rate' may override
        self.data_offset = 0
        self.total_samples = 0

    def _read_u8(self) -> int:
        b = self._bio.read(1)
        if not b:
            raise EOFError
        return b[0]

    def _read_u16_le(self) -> int:
        b = self._bio.read(2)
        if len(b) < 2:
            raise EOFError
        return struct.unpack("<H", b)[0]

    def _read_u32_le(self) -> int:
        b = self._bio.read(4)
        if len(b) < 4:
            raise EOFError
        return struct.unpack("<I", b)[0]

    def parse_header(self) -> Dict[str, int]:
        bio = self._bio
        bio.seek(0)
        ident = bio.read(4)
        if ident != b"Vgm ":
            raise ValueError("Not a VGM file (missing 'Vgm ' magic)")

        eof_offset = self._read_u32_le()
        version = self._read_u32_le()
        sn76489_clock = self._read_u32_le()
        ym2413_clock = self._read_u32_le()
        gd3_offset = self._read_u32_le()
        total_samples = self._read_u32_le()
        loop_offset = self._read_u32_le()
        loop_samples = self._read_u32_le()

        rate = self._read_u32_le()
        if rate:
            self.sample_rate = rate

        # Skip to data offset region
        ym2612_clock = self._read_u32_le()
        ym2151_clock = self._read_u32_le()
        vgm_data_offset = self._read_u32_le()

        # VGM data offset is relative to 0x34; if zero, data starts at 0x40.
        if vgm_data_offset == 0:
            self.data_offset = 0x40
        else:
            self.data_offset = 0x34 + vgm_data_offset

        self.total_samples = total_samples

        self.header = {
            "version_bcd": version,
            "sample_rate": self.sample_rate,
            "eof_offset": eof_offset,
            "total_samples": total_samples,
            "loop_offset": loop_offset,
            "loop_samples": loop_samples,
            "data_offset": self.data_offset,
            "sn76489_clock": sn76489_clock,
            "ym2413_clock": ym2413_clock,
            "gd3_offset": gd3_offset,
            "ym2612_clock": ym2612_clock,
            "ym2151_clock": ym2151_clock,
        }
        return self.header

    def iter_commands(self) -> Generator[Tuple[int, bytes, int], None, None]:
        """
        Yields (cmd, payload, abs_samples).
        """
        if not self.header:
            self.parse_header()

        self._bio.seek(self.data_offset)
        abs_samples = 0

        while True:
            b = self._bio.read(1)
            if not b:
                break
            cmd = b[0]

            # Waits
            if cmd == 0x61:
                n = self._read_u16_le()
                abs_samples += n
                yield cmd, struct.pack("<H", n), abs_samples
                continue
            elif cmd == 0x62:
                abs_samples += 735
                yield cmd, b"", abs_samples
                continue
            elif cmd == 0x63:
                abs_samples += 882
                yield cmd, b"", abs_samples
                continue
            elif 0x70 <= cmd <= 0x7F:
                n = (cmd & 0x0F) + 1
                abs_samples += n
                yield cmd, b"", abs_samples
                continue

            # End of sound data
            if cmd == 0x66:
                yield cmd, b"", abs_samples
                break

            # Data block: 0x67 0x66 tt len(4) data...
            if cmd == 0x67:
                type_check = self._read_u8()
                if type_check != 0x66:
                    block_type = type_check
                    data_len = self._read_u32_le()
                else:
                    block_type = self._read_u8()  # tt
                    data_len = self._read_u32_le()
                # Skip data
                self._bio.seek(data_len, io.SEEK_CUR)
                yield cmd, b"", abs_samples
                continue

            # YM2413 write: 0x51 aa dd
            if cmd == 0x51:
                aa = self._read_u8()
                dd = self._read_u8()
                yield cmd, bytes([aa, dd]), abs_samples
                continue

            # AY8910 PSG write: 0xA0 aa dd
            if cmd == 0xA0:
                aa = self._read_u8()
                dd = self._read_u8()
                yield cmd, bytes([aa, dd]), abs_samples
                continue

            # K051649 (SCC1): 0xD2 pp aa dd
            if cmd == 0xD2:
                pp = self._read_u8()
                aa = self._read_u8()
                dd = self._read_u8()
                yield cmd, bytes([pp, aa, dd]), abs_samples
                continue

            # Unknown/unsupported command
            yield cmd, b"", abs_samples