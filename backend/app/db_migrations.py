from __future__ import annotations

from sqlalchemy import inspect, text
from sqlalchemy.engine import Engine


def ensure_auth_schema(engine: Engine) -> None:
    inspector = inspect(engine)
    if not inspector.has_table("users"):
        return

    columns = {column["name"] for column in inspector.get_columns("users")}
    statements: list[str] = []

    if "is_email_verified" not in columns:
        statements.append("ALTER TABLE users ADD COLUMN is_email_verified BOOLEAN NOT NULL DEFAULT 0")
    if "email_verified_at" not in columns:
        statements.append("ALTER TABLE users ADD COLUMN email_verified_at DATETIME")

    if not statements:
        return

    with engine.begin() as connection:
        for statement in statements:
            connection.execute(text(statement))
