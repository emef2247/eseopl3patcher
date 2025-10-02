from dataclasses import dataclass, field, asdict
from typing import Any, Dict, List, Optional


@dataclass
class Event:
    # Absolute time
    time_s: float
    samples: int

    # Chip and channel
    chip: str  # "PSG" | "SCC"
    channel: Optional[int]  # 0-based channel index when known

    # Raw write
    kind: str  # e.g., "reg-write", "wtb-write"
    address: Optional[int]  # logical/absolute register address if applicable
    value: Optional[int]

    # Decoded extras (chip-specific derived fields)
    extras: Dict[str, Any] = field(default_factory=dict)


@dataclass
class Note:
    chip: str
    channel: int
    t_on: float
    t_off: float
    pitch_hz: Optional[float] = None
    volume: Optional[int] = None
    cause: str = ""  # e.g., "vol-rise", "enable", "freq-change"
    meta: Dict[str, Any] = field(default_factory=dict)

    @property
    def duration_s(self) -> float:
        return max(0.0, self.t_off - self.t_on)


@dataclass
class IR:
    header: Dict[str, Any]
    events: List[Event] = field(default_factory=list)
    notes: List[Note] = field(default_factory=list)

    def to_dict(self) -> Dict[str, Any]:
        return {
            "header": self.header,
            "events": [asdict(e) for e in self.events],
            "notes": [
                {
                    **asdict(n),
                    "duration_s": n.duration_s,
                }
                for n in self.notes
            ],
        }