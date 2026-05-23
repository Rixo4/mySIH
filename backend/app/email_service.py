from __future__ import annotations

import os
import smtplib
from email.message import EmailMessage


def _is_developer_mode() -> bool:
    return os.getenv("SPP_DEVELOPER_MODE", "0") == "1"


def send_email(to: str, subject: str, body: str) -> None:
    host = os.getenv("SMTP_HOST")
    port_raw = os.getenv("SMTP_PORT")
    from_email = os.getenv("SMTP_FROM_EMAIL")

    if _is_developer_mode() and not host:
        print(f"[email][dev] to={to} subject={subject}\n{body}")
        return

    username = os.getenv("SMTP_USERNAME")
    password = (os.getenv("SMTP_PASSWORD") or "").replace(" ", "")
    use_tls = os.getenv("SMTP_USE_TLS", "true").lower() == "true"

    if not host or not port_raw or not from_email:
        raise RuntimeError("SMTP_HOST, SMTP_PORT, and SMTP_FROM_EMAIL must be set for email delivery")

    port = int(port_raw)
    message = EmailMessage()
    message["From"] = from_email
    message["To"] = to
    message["Subject"] = subject
    message.set_content(body)

    with smtplib.SMTP(host, port, timeout=30) as client:
        if use_tls:
            client.starttls()
        if username:
            try:
                client.login(username, password)
            except smtplib.SMTPAuthenticationError as exc:
                raise RuntimeError("SMTP authentication failed. Check SMTP_USERNAME and SMTP_PASSWORD (use the Gmail app password without spaces).") from exc
        client.send_message(message)
