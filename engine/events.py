from dataclasses import dataclass
from datetime import datetime


@dataclass
class FileEvent:
    event_type: str
    path: str
    timestamp: datetime
    old_path: str = None