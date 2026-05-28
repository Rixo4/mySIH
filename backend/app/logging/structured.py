from __future__ import annotations

import json
import logging
import sys
from datetime import datetime, timezone
from typing import Any

_STANDARD_RECORD_ATTRS = {
    "args",
    "asctime",
    "created",
    "exc_info",
    "exc_text",
    "filename",
    "funcName",
    "levelname",
    "levelno",
    "lineno",
    "module",
    "msecs",
    "message",
    "msg",
    "name",
    "pathname",
    "process",
    "processName",
    "relativeCreated",
    "stack_info",
    "thread",
    "threadName",
}


class StructuredFormatter(logging.Formatter):
    def format(self, record: logging.LogRecord) -> str:
        payload: dict[str, Any] = {
            "timestamp": datetime.now(timezone.utc).isoformat(),
            "level": record.levelname,
            "logger": record.name,
            "message": record.getMessage(),
        }

        for key, value in record.__dict__.items():
            if key in _STANDARD_RECORD_ATTRS or key.startswith("_"):
                continue
            if key not in payload:
                payload[key] = value

        if record.exc_info:
            payload["exception"] = self.formatException(record.exc_info)

        return json.dumps(payload, default=str, ensure_ascii=True)


def configure_logging(level: str = "INFO") -> None:
    root_logger = logging.getLogger()
    root_logger.setLevel(level.upper())

    if root_logger.handlers:
        for handler in root_logger.handlers:
            handler.setFormatter(StructuredFormatter())
        return

    handler = logging.StreamHandler(sys.stdout)
    handler.setFormatter(StructuredFormatter())
    root_logger.addHandler(handler)


def get_logger(name: str) -> logging.Logger:
    return logging.getLogger(name)
