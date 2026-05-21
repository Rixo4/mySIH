from __future__ import annotations

import hashlib
import os
import re
from datetime import datetime, timedelta, timezone

from fastapi import APIRouter, Depends, HTTPException, Request, Response
from jose import JWTError, jwt
from passlib.context import CryptContext
from pydantic import BaseModel, EmailStr, Field, validator
from sqlalchemy import select
from sqlalchemy.orm import Session

from .config import get_settings
from .database import get_db
from .models import AuditLog, Company, RefreshToken, User
from .ratelimit import get_rate_limiter
from .utils import utc_now

limiter = get_rate_limiter()

settings = get_settings()

pwd_context = CryptContext(schemes=["argon2", "bcrypt"], deprecated="auto")

router = APIRouter(prefix="/auth")


def get_secret_key() -> str:
    key = os.getenv("SECRET_KEY")
    if not key:
        raise RuntimeError("SECRET_KEY not set in environment")
    return key


def hash_token(token: str) -> str:
    return hashlib.sha256(token.encode("utf-8")).hexdigest()


def verify_password(plain: str, hashed: str) -> bool:
    return pwd_context.verify(plain, hashed)


def get_password_hash(password: str) -> str:
    return pwd_context.hash(password)


def create_access_token(data: dict, expires_minutes: int = 15) -> str:
    to_encode = data.copy()
    expire = datetime.now(timezone.utc) + timedelta(minutes=expires_minutes)
    to_encode.update({"exp": expire, "type": "access"})
    return jwt.encode(to_encode, get_secret_key(), algorithm="HS256")


def create_refresh_token(data: dict, expires_days: int = 7) -> str:
    to_encode = data.copy()
    expire = datetime.now(timezone.utc) + timedelta(days=expires_days)
    to_encode.update({"exp": expire, "type": "refresh"})
    return jwt.encode(to_encode, get_secret_key(), algorithm="HS256")


class SignupRequest(BaseModel):
    full_name: str = Field(..., min_length=1)
    email: EmailStr
    password: str
    company_name: str | None = None

    @validator("password")
    def password_strength(cls, v: str) -> str:
        if len(v) < 12:
            raise ValueError("Password must be at least 12 characters")
        if not any(c.islower() for c in v):
            raise ValueError("Password must include a lowercase letter")
        if not any(c.isupper() for c in v):
            raise ValueError("Password must include an uppercase letter")
        if not any(c.isdigit() for c in v):
            raise ValueError("Password must include a number")
        if not any(not c.isalnum() for c in v):
            raise ValueError("Password must include a special character")
        common = {"password", "12345678", "qwerty", "letmein", "password123!", "admin123!", "welcome123!"}
        if v.lower() in common:
            raise ValueError("Password too weak")
        return v


class LoginRequest(BaseModel):
    email: EmailStr
    password: str


class TokenResponse(BaseModel):
    access_token: str
    token_type: str = "bearer"


class MeResponse(BaseModel):
    id: int
    full_name: str
    email: str
    role: str
    company_id: int | None
    company_name: str | None


def audit(db: Session, user_id: int | None, company_id: int | None, action: str, request: Request | None = None) -> None:
    ip = None
    ua = None
    if request is not None:
        ip = request.client.host if request.client else None
        ua = request.headers.get("user-agent")
    entry = AuditLog(user_id=user_id, company_id=company_id, action=action, ip_address=ip, user_agent=ua, created_at=utc_now())
    db.add(entry)
    db.commit()


@router.post("/signup", status_code=201)
def signup(payload: SignupRequest, request: Request, db: Session = Depends(get_db)) -> dict:
    client_ip = request.headers.get("x-forwarded-for", request.client.host if request.client else "unknown")
    signup_key = f"signup:ip:{client_ip}"
    cnt = limiter.incr_with_expire(signup_key, window=3600)
    if cnt > 5:
        raise HTTPException(status_code=429, detail="Rate limit exceeded")

    stmt = select(User).where(User.email == payload.email)
    existing = db.execute(stmt).scalar_one_or_none()
    if existing:
        raise HTTPException(status_code=400, detail="Invalid request")

    company = None
    if payload.company_name:
        stmtc = select(Company).where(Company.name == payload.company_name)
        company = db.execute(stmtc).scalar_one_or_none()
        if company is None:
            company = Company(name=payload.company_name, created_at=utc_now())
            db.add(company)
            db.commit()
            db.refresh(company)

    pwd_hash = get_password_hash(payload.password)
    now = utc_now()
    user = User(full_name=payload.full_name, email=payload.email, password_hash=pwd_hash, role="researcher", company_id=(company.id if company else None), is_active=True, created_at=now, updated_at=now)
    db.add(user)
    db.commit()
    db.refresh(user)
    audit(db, user.id, user.company_id, "signup", request)
    return {"id": user.id, "email": user.email}


@router.post("/login")
def login(payload: LoginRequest, request: Request, response: Response, db: Session = Depends(get_db)) -> TokenResponse:
    client_ip = request.headers.get("x-forwarded-for", request.client.host if request.client else "unknown")

    ip_locked = limiter.is_locked(f"ip:{client_ip}")
    email_locked = limiter.is_locked(f"email:{payload.email}")
    if ip_locked or email_locked:
        raise HTTPException(status_code=401, detail="Invalid credentials")

    stmt = select(User).where(User.email == payload.email)
    user = db.execute(stmt).scalar_one_or_none()
    if user is None or not verify_password(payload.password, user.password_hash):
        ip_key = f"failed:ip:{client_ip}"
        email_key = f"failed:email:{payload.email}"
        ip_count = limiter.incr_with_expire(ip_key, window=15 * 60)
        email_count = limiter.incr_with_expire(email_key, window=15 * 60)
        if ip_count > 5:
            limiter.set_lock(f"ip:{client_ip}", seconds=15 * 60)
        if email_count > 5:
            limiter.set_lock(f"email:{payload.email}", seconds=15 * 60)
        audit(db, user.id if user else None, user.company_id if user else None, "login_failure", request)
        raise HTTPException(status_code=401, detail="Invalid credentials")

    if not user.is_active:
        audit(db, user.id, user.company_id, "login_blocked", request)
        raise HTTPException(status_code=401, detail="Invalid credentials")

    claims = {"sub": str(user.id), "email": user.email, "role": user.role, "company_id": user.company_id}
    access = create_access_token(claims, expires_minutes=int(os.getenv("ACCESS_TOKEN_EXPIRE_MINUTES", "15")))
    refresh = create_refresh_token(claims, expires_days=int(os.getenv("REFRESH_TOKEN_EXPIRE_DAYS", "7")))

    rt = RefreshToken(user_id=user.id, token_hash=hash_token(refresh), expires_at=utc_now() + timedelta(days=int(os.getenv("REFRESH_TOKEN_EXPIRE_DAYS", "7"))), revoked_at=None, created_at=utc_now())
    db.add(rt)
    db.commit()

    # reset failed counters on successful login
    limiter.delete(f"failed:ip:{client_ip}")
    limiter.delete(f"failed:email:{user.email}")

    audit(db, user.id, user.company_id, "login_success", request)

    cookie_secure = os.getenv("AUTH_COOKIE_SECURE", "false").lower() == "true"
    samesite = os.getenv("AUTH_COOKIE_SAMESITE", "lax")
    response.set_cookie("spp_refresh_token", refresh, httponly=True, secure=cookie_secure, samesite=samesite, max_age=7 * 24 * 3600)

    return TokenResponse(access_token=access)


def _verify_refresh_token(db: Session, token: str) -> User | None:
    try:
        payload = jwt.decode(token, get_secret_key(), algorithms=["HS256"])  # type: ignore
    except JWTError:
        return None
    if payload.get("type") != "refresh":
        return None
    user_id = int(payload.get("sub"))
    stmt = select(User).where(User.id == user_id)
    user = db.execute(stmt).scalar_one_or_none()
    if user is None:
        return None
    stmt2 = select(RefreshToken).where(RefreshToken.user_id == user.id, RefreshToken.token_hash == hash_token(token))
    found = db.execute(stmt2).scalar_one_or_none()
    if found is None:
        return None
    return user


@router.post("/refresh")
def refresh(request: Request, response: Response, db: Session = Depends(get_db)) -> TokenResponse:
    token = request.cookies.get("spp_refresh_token") or request.headers.get("x-refresh-token")
    if not token:
        raise HTTPException(status_code=401, detail="Invalid credentials")
    user = _verify_refresh_token(db, token)
    if user is None:
        raise HTTPException(status_code=401, detail="Invalid credentials")

    claims = {"sub": str(user.id), "email": user.email, "role": user.role, "company_id": user.company_id}
    access = create_access_token(claims, expires_minutes=int(os.getenv("ACCESS_TOKEN_EXPIRE_MINUTES", "15")))
    # revoke any existing active refresh tokens for the user before rotating
    existing_tokens = db.execute(select(RefreshToken).where(RefreshToken.user_id == user.id, RefreshToken.revoked_at.is_(None))).scalars().all()
    for token_row in existing_tokens:
        token_row.revoked_at = utc_now()
        db.add(token_row)

    new_refresh = create_refresh_token(claims, expires_days=int(os.getenv("REFRESH_TOKEN_EXPIRE_DAYS", "7")))
    rt = RefreshToken(user_id=user.id, token_hash=hash_token(new_refresh), expires_at=utc_now() + timedelta(days=int(os.getenv("REFRESH_TOKEN_EXPIRE_DAYS", "7"))), revoked_at=None, created_at=utc_now())
    db.add(rt)
    db.commit()

    cookie_secure = os.getenv("AUTH_COOKIE_SECURE", "false").lower() == "true"
    samesite = os.getenv("AUTH_COOKIE_SAMESITE", "lax")
    response.set_cookie("spp_refresh_token", new_refresh, httponly=True, secure=cookie_secure, samesite=samesite, max_age=7 * 24 * 3600)
    audit(db, user.id, user.company_id, "refresh_token", request)
    return TokenResponse(access_token=access)


@router.post("/logout")
def logout(request: Request, response: Response, db: Session = Depends(get_db)) -> dict:
    token = request.cookies.get("spp_refresh_token") or request.headers.get("x-refresh-token")
    if token:
        # revoke matching refresh token records
        token_h = hash_token(token)
        stmt = select(RefreshToken).where(RefreshToken.token_hash == token_h)
        rec = db.execute(stmt).scalar_one_or_none()
        if rec:
            rec.revoked_at = utc_now()
            db.add(rec)
            db.commit()
            audit(db, rec.user_id, None, "logout", request)

    response.delete_cookie("spp_refresh_token")
    return {"detail": "ok"}


def get_user_from_access_token(db: Session, token: str) -> User | None:
    try:
        payload = jwt.decode(token, get_secret_key(), algorithms=["HS256"])  # type: ignore
    except JWTError:
        return None
    if payload.get("type") != "access":
        return None
    user_id = int(payload.get("sub"))
    stmt = select(User).where(User.id == user_id)
    return db.execute(stmt).scalar_one_or_none()


def require_user(request: Request, db: Session = Depends(get_db)) -> User:
    auth = request.headers.get("authorization")
    if not auth or not auth.lower().startswith("bearer "):
        audit(db, None, None, "failed_authentication", request)
        raise HTTPException(status_code=401, detail="Not authenticated")
    token = auth.split(" ", 1)[1]
    user = get_user_from_access_token(db, token)
    if user is None or not user.is_active:
        audit(db, None, None, "failed_authentication", request)
        raise HTTPException(status_code=401, detail="Not authenticated")
    return user


def require_roles(*allowed_roles: str):
    def _dependency(request: Request, db: Session = Depends(get_db), user: User = Depends(require_user)) -> User:
        if user.role not in allowed_roles:
            audit(db, user.id, user.company_id, "failed_authorization", request)
            raise HTTPException(status_code=403, detail="Forbidden")
        return user

    return _dependency


@router.get("/me", response_model=MeResponse)
def me(user: User = Depends(require_user), db: Session = Depends(get_db)) -> MeResponse:
    company_name = None
    if user.company_id:
        stmt = select(Company).where(Company.id == user.company_id)
        comp = db.execute(stmt).scalar_one_or_none()
        company_name = comp.name if comp else None
    return MeResponse(id=user.id, full_name=user.full_name, email=user.email, role=user.role, company_id=user.company_id, company_name=company_name)


@router.post("/request-password-reset")
def request_password_reset(payload: dict, request: Request, db: Session = Depends(get_db)) -> dict:
    client_ip = request.headers.get("x-forwarded-for", request.client.host if request.client else "unknown")
    pr_key = f"pwdreset:ip:{client_ip}"
    cnt = limiter.incr_with_expire(pr_key, window=3600)
    if cnt > 3:
        return {"detail": "ok"}

    return {"detail": "ok"}


@router.post("/reset-password")
def reset_password(payload: dict, request: Request, db: Session = Depends(get_db)) -> dict:
    # payload should contain token and new_password; implement later
    return {"detail": "ok"}
