from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime
from functools import cache
from html import escape
from pathlib import Path
from typing import Any, Generator
from urllib.error import HTTPError, URLError
from urllib.parse import quote, unquote, urlencode, urlsplit, urlunsplit
from urllib.request import Request, urlopen
import base64
import json
import re
import time

import cv2
import streamlit as st
from ultralytics import YOLO


MODEL_PATH = Path(__file__).with_name("best (3).pt")
FIREBASE_CONFIG_PATH = Path(__file__).with_name("firebaseConfig.js")
DEFAULT_SENSOR_URL = "http://192.168.1.200"
DEFAULT_FIREBASE_SENSOR_PATH = "devices/4845788"
DEFAULT_CAMERA_STREAM_URL = "http://192.168.1.200"
DEFAULT_WEATHER_NAME = "Hà Nội"
DEFAULT_WEATHER_LATITUDE = 21.0278
DEFAULT_WEATHER_LONGITUDE = 105.8342
DEFAULT_WEATHER_TIMEOUT = 3.0
DEFAULT_CONFIDENCE = 0.3
DEFAULT_FRAME_WIDTH = 960
DEFAULT_STREAM_ANALYSIS_SECONDS = 60
DEFAULT_AI_ALERT_SEND_INTERVAL_SECONDS = 8.0
ESP32_STREAM_PATH = "/stream"
DEFAULT_AI_TARGET_FPS = 5
DEFAULT_AI_CPU_THREADS = 2


@dataclass(frozen=True)
class SensorReading:
    temperature: float | None
    humidity: float | None
    smoke: float | None
    raw: dict[str, Any]
    updated_at: datetime


@dataclass(frozen=True)
class WeatherReading:
    temperature: float | None
    humidity: float | None
    apparent_temperature: float | None
    wind_speed: float | None
    weather_code: int | None
    updated_at: datetime


st.set_page_config(
    page_title="Giám sát cháy và môi trường",
    page_icon="",
    layout="wide",
)


st.html(
    """
    <style>
    .main .block-container {
        padding-top: 1.6rem;
        padding-bottom: 2rem;
    }

    .section-title {
        font-size: 1.05rem;
        font-weight: 800;
        margin: 0.25rem 0 0.75rem;
    }

    .device-line {
        font-size: 0.92rem;
        margin-top: -0.3rem;
        margin-bottom: 1rem;
    }

    .sensor-card {
        min-height: 136px;
        border-radius: 10px;
        padding: 18px 18px 16px;
        box-shadow: 0 1px 3px rgba(0,0,0,0.08);
    }

    .sensor-card__top {
        display: flex;
        align-items: center;
        justify-content: space-between;
        gap: 12px;
        margin-bottom: 14px;
    }

    .sensor-card__label {
        font-size: 0.92rem;
        font-weight: 700;
    }

    .sensor-card__icon {
        width: 42px;
        height: 42px;
        display: inline-flex;
        align-items: center;
        justify-content: center;
        border-radius: 8px;
        border: 1px solid;
    }

    .sensor-card__icon svg {
        width: 24px;
        height: 24px;
        stroke-width: 2.1;
    }

    .sensor-card__value {
        font-size: 2rem;
        line-height: 1.1;
        font-weight: 800;
        letter-spacing: -0.02em;
    }

    .sensor-card__sub {
        font-size: 0.85rem;
        margin-top: 8px;
    }

    /* LIGHT MODE (default) */
    .section-title { color: #0f172a; }
    .device-line { color: #475569; }
    .sensor-card { background: #ffffff; border: 1px solid #e2e8f0; }
    .sensor-card__label { color: #334155; }
    .sensor-card__value { color: #0f172a; }
    .sensor-card__sub { color: #64748b; }
    .sensor-card__icon { background: #f8fafc; border-color: #e2e8f0; }
    .sensor-card--temperature .sensor-card__icon svg { color: #dc2626; }
    .sensor-card--humidity .sensor-card__icon svg { color: #0284c7; }
    .sensor-card--smoke .sensor-card__icon svg { color: #6b7280; }
    .sensor-card--status .sensor-card__icon svg { color: #16a34a; }
    .sensor-card--offline .sensor-card__icon svg { color: #dc2626; }
    .sensor-card--weather .sensor-card__icon svg { color: #d97706; }
    .sensor-card--wind .sensor-card__icon svg { color: #475569; }
    .camera-caption { color: #64748b; font-size: 0.88rem; margin-top: 0.55rem; text-align: center; }

    /* DARK MODE — works with Streamlit's data-theme="dark" */
    :root[data-theme="dark"] .section-title,
    [data-theme="dark"] .section-title { color: #e2e8f0; }
    :root[data-theme="dark"] .device-line,
    [data-theme="dark"] .device-line { color: #94a3b8; }
    :root[data-theme="dark"] .sensor-card,
    [data-theme="dark"] .sensor-card { background: #1e293b; border-color: #334155; }
    :root[data-theme="dark"] .sensor-card__label,
    [data-theme="dark"] .sensor-card__label { color: #e2e8f0; }
    :root[data-theme="dark"] .sensor-card__value,
    [data-theme="dark"] .sensor-card__value { color: #f8fafc; }
    :root[data-theme="dark"] .sensor-card__sub,
    [data-theme="dark"] .sensor-card__sub { color: #94a3b8; }
    :root[data-theme="dark"] .sensor-card__icon,
    [data-theme="dark"] .sensor-card__icon { background: #0f172a; border-color: #334155; }
    :root[data-theme="dark"] .sensor-card--temperature .sensor-card__icon svg,
    [data-theme="dark"] .sensor-card--temperature .sensor-card__icon svg { color: #f87171; }
    :root[data-theme="dark"] .sensor-card--humidity .sensor-card__icon svg,
    [data-theme="dark"] .sensor-card--humidity .sensor-card__icon svg { color: #38bdf8; }
    :root[data-theme="dark"] .sensor-card--smoke .sensor-card__icon svg,
    [data-theme="dark"] .sensor-card--smoke .sensor-card__icon svg { color: #9ca3af; }
    :root[data-theme="dark"] .sensor-card--status .sensor-card__icon svg,
    [data-theme="dark"] .sensor-card--status .sensor-card__icon svg { color: #4ade80; }
    :root[data-theme="dark"] .sensor-card--offline .sensor-card__icon svg,
    [data-theme="dark"] .sensor-card--offline .sensor-card__icon svg { color: #f87171; }
    :root[data-theme="dark"] .sensor-card--weather .sensor-card__icon svg,
    [data-theme="dark"] .sensor-card--weather .sensor-card__icon svg { color: #fbbf24; }
    :root[data-theme="dark"] .sensor-card--wind .sensor-card__icon svg,
    [data-theme="dark"] .sensor-card--wind .sensor-card__icon svg { color: #94a3b8; }
    :root[data-theme="dark"] .camera-caption,
    [data-theme="dark"] .camera-caption { color: #94a3b8; }

    /* DARK MODE — also catch prefers-color-scheme for OS-level */
    @media (prefers-color-scheme: dark) {
        .section-title { color: #e2e8f0; }
        .device-line { color: #94a3b8; }
        .sensor-card { background: #1e293b; border-color: #334155; box-shadow: 0 1px 3px rgba(0,0,0,0.3); }
        .sensor-card__label { color: #e2e8f0; }
        .sensor-card__value { color: #f8fafc; }
        .sensor-card__sub { color: #94a3b8; }
        .sensor-card__icon { background: #0f172a; border-color: #334155; }
        .sensor-card--temperature .sensor-card__icon svg { color: #f87171; }
        .sensor-card--humidity .sensor-card__icon svg { color: #38bdf8; }
        .sensor-card--smoke .sensor-card__icon svg { color: #9ca3af; }
        .sensor-card--status .sensor-card__icon svg { color: #4ade80; }
        .sensor-card--offline .sensor-card__icon svg { color: #f87171; }
        .sensor-card--weather .sensor-card__icon svg { color: #fbbf24; }
        .sensor-card--wind .sensor-card__icon svg { color: #94a3b8; }
        .camera-caption { color: #94a3b8; }
    }

    .monitor-shell {
        width: 100%;
        max-width: 980px;
        margin: 0 auto;
    }

    .monitor-frame {
        position: relative;
        width: 100%;
        aspect-ratio: 16 / 9;
        background: #020617;
        border: 1px solid #1e293b;
        border-radius: 8px;
        overflow: hidden;
        display: flex;
        align-items: center;
        justify-content: center;
        box-shadow: 0 10px 28px rgba(15, 23, 42, 0.14);
    }

    .monitor-frame img {
        display: block;
        width: 100%;
        height: 100%;
        object-fit: contain;
        background: #020617;
    }

    .monitor-placeholder {
        width: 100%;
        height: 100%;
        display: flex;
        flex-direction: column;
        align-items: center;
        justify-content: center;
        gap: 10px;
        padding: 24px;
        color: #e2e8f0;
        text-align: center;
    }

    .monitor-placeholder svg {
        width: 58px;
        height: 58px;
        color: #ef4444;
        stroke-width: 1.8;
    }

    .monitor-placeholder__title {
        color: #ffffff;
        font-size: 1.15rem;
        font-weight: 800;
    }

    .monitor-placeholder__text {
        max-width: 520px;
        color: #94a3b8;
        font-size: 0.92rem;
        line-height: 1.5;
    }

    .monitor-badge {
        position: absolute;
        left: 14px;
        top: 14px;
        max-width: calc(100% - 28px);
        padding: 8px 12px;
        border-radius: 8px;
        color: #ffffff;
        font-size: 0.88rem;
        font-weight: 800;
        line-height: 1.25;
        background: rgba(22, 163, 74, 0.92);
        box-shadow: 0 8px 22px rgba(15, 23, 42, 0.22);
    }

    .monitor-frame--warning .monitor-badge {
        background: rgba(217, 119, 6, 0.94);
    }

    .monitor-frame--danger {
        border-color: #dc2626;
    }

    .monitor-frame--danger .monitor-badge {
        background: rgba(220, 38, 38, 0.95);
    }
    </style>
    """
)


# --- Session state for real-time updates ---
if "sensor_cache" not in st.session_state:
    st.session_state.sensor_cache = None
    st.session_state.sensor_cache_error = None
if "weather_cache" not in st.session_state:
    st.session_state.weather_cache = None
    st.session_state.weather_cache_error = None
    st.session_state.weather_cache_time = 0.0
if "force_update" not in st.session_state:
    st.session_state.force_update = False


TEMPERATURE_ICON = """
<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round">
  <path d="M14 14.76V5a4 4 0 0 0-8 0v9.76a6 6 0 1 0 8 0Z"/>
  <path d="M10 9v7"/>
</svg>
"""

HUMIDITY_ICON = """
<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round">
  <path d="M12 2.5 6.8 8.3a7.2 7.2 0 1 0 10.4 0L12 2.5Z"/>
  <path d="M8.5 14.5a3.5 3.5 0 0 0 7 0"/>
</svg>
"""

STATUS_ICON = """
<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round">
  <path d="M5 13a10 10 0 0 1 14 0"/>
  <path d="M8.5 16.5a5 5 0 0 1 7 0"/>
  <path d="M12 20h.01"/>
  <path d="M3 9a14 14 0 0 1 18 0"/>
</svg>
"""

OFFLINE_ICON = """
<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round">
  <path d="m2 2 20 20"/>
  <path d="M8.5 16.5a5 5 0 0 1 7 0"/>
  <path d="M12 20h.01"/>
  <path d="M5 13a10 10 0 0 1 5.2-2.7"/>
  <path d="M13.8 10.3A10 10 0 0 1 19 13"/>
  <path d="M3 9a14 14 0 0 1 3.8-2.4"/>
  <path d="M10.6 5.2A14 14 0 0 1 21 9"/>
</svg>
"""

WEATHER_ICON = """
<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round">
  <circle cx="12" cy="12" r="4"/>
  <path d="M12 2v2"/>
  <path d="M12 20v2"/>
  <path d="m4.93 4.93 1.41 1.41"/>
  <path d="m17.66 17.66 1.41 1.41"/>
  <path d="M2 12h2"/>
  <path d="M20 12h2"/>
  <path d="m6.34 17.66-1.41 1.41"/>
  <path d="m19.07 4.93-1.41 1.41"/>
</svg>
"""

WIND_ICON = """
<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round">
  <path d="M3 8h10a3 3 0 1 0-3-3"/>
  <path d="M3 12h15a3 3 0 1 1-3 3"/>
  <path d="M3 16h8"/>
</svg>
"""

SMOKE_ICON = """
<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round">
  <path d="M8 2c0 2-2 4-2 6a4 4 0 0 0 8 0c0-2-2-4-2-6"/>
  <path d="M16 6c0 1.5-1 3-1 4.5a3 3 0 0 0 6 0c0-1.5-1-3-1-4.5"/>
  <path d="M2 18h20"/>
  <path d="M4 22h16"/>
</svg>
"""


SENSOR_TEMPERATURE_KEYS = (
    "temperature",
    "temperaturevalue",
    "temperature_c",
    "temp",
    "temp_c",
    "air_temperature",
    "nhiet_do",
    "nhietdo",
    "t",
)
SENSOR_HUMIDITY_KEYS = (
    "humidity",
    "humidityvalue",
    "humidity_percent",
    "hum",
    "air_humidity",
    "do_am",
    "doam",
    "h",
)
SENSOR_SMOKE_KEYS = (
    "smoke",
    "smokevalue",
    "smoke_percent",
    "khoi",
    "gas",
    "gasvalue",
    "mq2",
)
SENSOR_VALUE_KEYS = SENSOR_TEMPERATURE_KEYS + SENSOR_HUMIDITY_KEYS + SENSOR_SMOKE_KEYS
SENSOR_TIMESTAMP_KEYS = (
    "updated_at",
    "updatedAt",
    "timestamp",
    "time",
    "created_at",
    "createdAt",
    "ts",
)


def configure_ai_runtime(cpu_threads: int = DEFAULT_AI_CPU_THREADS) -> None:
    cv2.setNumThreads(1)
    try:
        import torch
    except Exception:
        return

    safe_cpu_threads = max(1, int(cpu_threads))
    try:
        torch.set_num_threads(safe_cpu_threads)
    except Exception:
        pass
    try:
        torch.set_num_interop_threads(1)
    except RuntimeError:
        pass
    except Exception:
        pass


@st.cache_resource
def load_model() -> YOLO:
    configure_ai_runtime()
    if not MODEL_PATH.exists():
        raise FileNotFoundError(f"Không tìm thấy model: {MODEL_PATH}")
    return YOLO(str(MODEL_PATH))


def _as_float(value: Any) -> float | None:
    if value is None:
        return None
    if isinstance(value, (int, float)):
        return float(value)
    if isinstance(value, str):
        normalized = value.strip().replace(",", ".")
        if not normalized:
            return None
        try:
            return float(normalized)
        except ValueError:
            return None
    return None


def _parse_firebase_config(config_path: Path = FIREBASE_CONFIG_PATH) -> dict[str, str]:
    if not config_path.exists():
        return {}

    content = config_path.read_text(encoding="utf-8")
    config: dict[str, str] = {}
    for key, value in re.findall(r"(\w+)\s*:\s*[\"']([^\"']+)[\"']", content):
        config[key] = value
    return config


def _firebase_database_url(firebase_config: dict[str, str]) -> str:
    database_url = firebase_config.get("databaseURL", "").strip()
    if database_url:
        return database_url.rstrip("/")

    project_id = firebase_config.get("projectId", "").strip()
    if not project_id:
        return ""
    return f"https://{project_id}-default-rtdb.firebaseio.com"


def _has_sensor_value(payload: dict[str, Any]) -> bool:
    lookup = {str(key).lower(): value for key, value in payload.items()}
    for key in SENSOR_VALUE_KEYS:
        raw = lookup.get(key.lower())
        if _as_float(raw) is not None:
            return True
        if isinstance(raw, dict):
            for nested_key in ("value", "val", "reading", "data"):
                if _as_float(raw.get(nested_key)) is not None:
                    return True
    return False


def _timestamp_score(payload: dict[str, Any]) -> float:
    lookup = {str(key): value for key, value in payload.items()}
    for key in SENSOR_TIMESTAMP_KEYS:
        value = lookup.get(key)
        number_value = _as_float(value)
        if number_value is not None:
            return number_value
        if isinstance(value, str):
            try:
                return datetime.fromisoformat(value.replace("Z", "+00:00")).timestamp()
            except ValueError:
                continue
    return 0.0


def _select_latest_sensor_payload(payload: Any) -> Any:
    if not isinstance(payload, dict):
        return payload
    if _has_sensor_value(payload):
        return payload

    candidate_items = [
        (key, value)
        for key, value in payload.items()
        if isinstance(value, dict)
    ]
    if not candidate_items:
        return payload

    scored_items = [
        (_timestamp_score(value), str(key), value)
        for key, value in candidate_items
    ]
    scored_items.sort(key=lambda item: (item[0], item[1]))
    return scored_items[-1][2]


def _safe_json_loads(body: str, source_name: str) -> Any:
    try:
        return json.loads(body)
    except json.JSONDecodeError:
        if "<html" in body.lower() or "<!doctype" in body.lower():
            raise ValueError(
                f"{source_name} đang trả về trang web (HTML) thay vì dữ liệu JSON. "
                "Có vẻ bạn đã copy sai đường dẫn hoặc nhầm link trang quản lý Firebase."
            )
        raise ValueError(f"{source_name} trả về dữ liệu không hợp lệ. Mong đợi JSON.")


def _normalize_payload(payload: Any) -> dict[str, Any]:
    if isinstance(payload, list) and payload:
        payload = payload[0]
    if not isinstance(payload, dict):
        raise ValueError("Dữ liệu thiết bị phải là JSON object.")

    for nested_key in ("data", "sensor", "sensors", "environment"):
        nested = payload.get(nested_key)
        if isinstance(nested, dict):
            return _select_latest_sensor_payload(nested)
    return _select_latest_sensor_payload(payload)


def _extract_value(raw_value: Any) -> float | None:
    """Extract a numeric value from either a plain number or a nested map like {value: 28.3}."""
    direct = _as_float(raw_value)
    if direct is not None:
        return direct
    if isinstance(raw_value, dict):
        for nested_key in ("value", "val", "reading", "data"):
            nested = _as_float(raw_value.get(nested_key))
            if nested is not None:
                return nested
    return None


def _first_number(payload: dict[str, Any], keys: tuple[str, ...]) -> float | None:
    lookup = {str(key).lower(): value for key, value in payload.items()}
    for key in keys:
        value = _extract_value(lookup.get(key))
        if value is not None:
            return value
    return None


def _build_sensor_reading(payload: Any) -> SensorReading:
    normalized_payload = _normalize_payload(payload)
    temperature = _first_number(normalized_payload, SENSOR_TEMPERATURE_KEYS)
    humidity = _first_number(normalized_payload, SENSOR_HUMIDITY_KEYS)
    smoke = _first_number(normalized_payload, SENSOR_SMOKE_KEYS)

    return SensorReading(
        temperature=temperature,
        humidity=humidity,
        smoke=smoke,
        raw=normalized_payload,
        updated_at=datetime.now(),
    )


def _firebase_realtime_url(database_url: str, path: str) -> str:
    normalized_base = database_url.strip().rstrip("/")
    normalized_path = path.strip().strip("/")
    if not normalized_base:
        raise ValueError("Chưa có Realtime Database URL trong Firebase config.")
    if not normalized_path:
        raise ValueError("Chưa có đường dẫn dữ liệu Firebase.")
    return f"{normalized_base}/{quote(normalized_path, safe='/')}.json"


def fetch_realtime_database_sensor_reading(
    database_url: str,
    path: str,
    timeout: float,
) -> SensorReading:
    console_project, console_path = _parse_console_url(database_url)
    if not console_project:
        console_project, console_path = _parse_console_url(path)
        
    if console_project and console_path:
        return fetch_firestore_sensor_reading(console_project, console_path, timeout)

    request = Request(
        _firebase_realtime_url(database_url, path),
        headers={
            "Accept": "application/json",
            "Cache-Control": "no-cache",
        },
    )
    try:
        with urlopen(request, timeout=timeout) as response:
            body = response.read().decode("utf-8")
            payload = _safe_json_loads(body, "Realtime Database")
    except HTTPError as exc:
        if exc.code == 403:
             raise PermissionError(
                "Truy cập Realtime Database bị từ chối (403). Vui lòng kiểm tra Rules."
            )
        raise
    return _build_sensor_reading(payload)


def _decode_firestore_value(value: Any) -> Any:
    if not isinstance(value, dict):
        return value

    if "doubleValue" in value:
        return _as_float(value.get("doubleValue"))
    if "integerValue" in value:
        return _as_float(value.get("integerValue"))
    if "stringValue" in value:
        return value.get("stringValue")
    if "booleanValue" in value:
        return value.get("booleanValue")
    if "timestampValue" in value:
        return value.get("timestampValue")
    if "nullValue" in value:
        return None
    if "mapValue" in value:
        fields = value.get("mapValue", {}).get("fields", {})
        return {
            field_key: _decode_firestore_value(field_value)
            for field_key, field_value in fields.items()
        }
    if "arrayValue" in value:
        values = value.get("arrayValue", {}).get("values", [])
        return [_decode_firestore_value(item) for item in values]
    return value


def _decode_firestore_fields(fields: dict[str, Any]) -> dict[str, Any]:
    return {
        key: _decode_firestore_value(value)
        for key, value in fields.items()
    }


def _select_firestore_document(payload: dict[str, Any]) -> dict[str, Any]:
    if "fields" in payload:
        return payload

    documents = payload.get("documents")
    if isinstance(documents, list) and documents:
        return sorted(
            documents,
            key=lambda item: (
                str(item.get("updateTime", "")),
                str(item.get("createTime", "")),
                str(item.get("name", "")),
            ),
        )[-1]

    raise ValueError("Không tìm thấy document Firestore ở đường dẫn đã nhập.")


def _parse_console_url(url: str) -> tuple[str | None, str | None]:
    if "console.firebase.google.com" not in url:
        return None, None
    match = re.search(r"/project/([^/]+)/firestore/(?:data|databases/[^/]+/data)/(.+)$", url)
    if match:
        project_id = match.group(1)
        encoded_path = match.group(2)
        # Handle both standard URL encoding (%2F) and Firebase Console encoding (~2F)
        path = encoded_path.replace("~2F", "/").replace("~2f", "/").replace("%2F", "/").replace("%2f", "/")
        return project_id, path.strip("/")
    return None, None


def fetch_firestore_sensor_reading(
    project_id: str,
    path: str,
    timeout: float,
) -> SensorReading:
    normalized_project_id = project_id.strip()
    normalized_path = path.strip().strip("/")

    console_project, console_path = _parse_console_url(project_id)
    if not console_project:
        console_project, console_path = _parse_console_url(path)

    if console_path:
        if console_project:
            normalized_project_id = console_project
        normalized_path = console_path

    if not normalized_project_id:
        raise ValueError("Chưa có projectId trong Firebase config.")
    if not normalized_path:
        raise ValueError("Chưa có đường dẫn dữ liệu Firestore.")

    firestore_url = (
        "https://firestore.googleapis.com/v1/projects/"
        f"{quote(normalized_project_id, safe='')}/databases/(default)/documents/"
        f"{quote(normalized_path, safe='/')}"
    )
    request = Request(firestore_url, headers={"Accept": "application/json"})
    try:
        with urlopen(request, timeout=timeout) as response:
            body = response.read().decode("utf-8")
            payload = _safe_json_loads(body, "Firestore")
    except HTTPError as exc:
        if exc.code == 403:
            raise PermissionError(
                "Truy cập bị từ chối (403). Vui lòng vào Firebase Console -> Firestore Database -> Rules và thêm 'allow read: if true;' để cho phép đọc dữ liệu."
            )
        if exc.code == 404:
            raise ValueError(f"Không tìm thấy dữ liệu tại '{normalized_path}' (404).")
        raise

    document = _select_firestore_document(payload)
    decoded = _decode_firestore_fields(document.get("fields", {}))
    return _build_sensor_reading(decoded)


def fetch_sensor_reading(url: str, timeout: float) -> SensorReading:
    normalized_url = url.strip()
    console_project, console_path = _parse_console_url(normalized_url)
    if console_project and console_path:
        return fetch_firestore_sensor_reading(console_project, console_path, timeout)

    request = Request(
        normalized_url,
        headers={
            "Accept": "application/json",
            "Cache-Control": "no-cache",
        },
    )
    with urlopen(request, timeout=timeout) as response:
        body = response.read().decode("utf-8")

    payload = _safe_json_loads(body, "URL ESP32")
    return _build_sensor_reading(payload)


def fetch_weather_reading(
    latitude: float,
    longitude: float,
    timeout: float,
) -> WeatherReading:
    weather_url = (
        "https://api.open-meteo.com/v1/forecast"
        f"?latitude={latitude:.4f}"
        f"&longitude={longitude:.4f}"
        "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m"
        "&timezone=auto"
    )
    request = Request(weather_url, headers={"Accept": "application/json"})
    with urlopen(request, timeout=timeout) as response:
        payload = json.loads(response.read().decode("utf-8"))

    current = payload.get("current", {})
    return WeatherReading(
        temperature=_as_float(current.get("temperature_2m")),
        humidity=_as_float(current.get("relative_humidity_2m")),
        apparent_temperature=_as_float(current.get("apparent_temperature")),
        wind_speed=_as_float(current.get("wind_speed_10m")),
        weather_code=int(current["weather_code"]) if current.get("weather_code") is not None else None,
        updated_at=datetime.now(),
    )


def describe_weather_code(code: int | None) -> str:
    if code is None:
        return "Chưa xác định"
    if code == 0:
        return "Trời quang"
    if code in (1, 2, 3):
        return "Có mây"
    if code in (45, 48):
        return "Sương mù"
    if code in (51, 53, 55, 56, 57):
        return "Mưa phùn"
    if code in (61, 63, 65, 66, 67, 80, 81, 82):
        return "Có mưa"
    if code in (71, 73, 75, 77, 85, 86):
        return "Có tuyết"
    if code in (95, 96, 99):
        return "Dông"
    return "Thời tiết biến động"


def classify_environment(reading: SensorReading) -> tuple[str, str]:
    temperature = reading.temperature
    humidity = reading.humidity

    if temperature is None and humidity is None:
        return "warning", "Đã nhận JSON nhưng chưa thấy trường nhiệt độ hoặc độ ẩm."

    if temperature is not None and temperature >= 45:
        return "danger", "Nhiệt độ cao, cần kiểm tra khu vực ngay."

    if temperature is not None and temperature >= 35:
        return "warning", "Nhiệt độ đang cao hơn ngưỡng bình thường."

    if humidity is not None and (humidity < 30 or humidity > 85):
        return "warning", "Độ ẩm nằm ngoài vùng khuyến nghị."

    smoke = reading.smoke
    if smoke is not None and smoke >= 60:
        return "danger", "Nồng độ khói cao, cần kiểm tra khu vực ngay."
    if smoke is not None and smoke >= 30:
        return "warning", "Nồng độ khói đang cao hơn bình thường."

    return "normal", "Môi trường đang trong ngưỡng ổn định."


def show_status(level: str, message: str) -> None:
    if level == "danger":
        st.error(message)
    elif level == "warning":
        st.warning(message)
    else:
        st.success(message)


def format_temperature(value: float | None) -> str:
    return "--" if value is None else f"{value:.1f} °C"


def format_humidity(value: float | None) -> str:
    return "--" if value is None else f"{value:.1f}%"


def format_smoke(value: float | None) -> str:
    return "--" if value is None else f"{value:.1f}%"


def render_sensor_card(
    label: str,
    value: str,
    subtitle: str,
    icon_svg: str,
    variant: str,
) -> None:
    st.markdown(
        f"""
        <div class="sensor-card sensor-card--{variant}">
            <div class="sensor-card__top">
                <div class="sensor-card__label">{escape(label)}</div>
                <div class="sensor-card__icon">{icon_svg}</div>
            </div>
            <div class="sensor-card__value">{escape(value)}</div>
            <div class="sensor-card__sub">{escape(subtitle)}</div>
        </div>
        """,
        unsafe_allow_html=True,
    )


def build_alert_state(detected_names: list[str]) -> tuple[str, str]:
    detected_set = {name.lower() for name in detected_names}
    has_fire = "fire" in detected_set
    has_smoke = "smoke" in detected_set
    has_person = "person" in detected_set

    if has_fire or has_smoke:
        if has_person:
            return "warning", "Chú ý: có lửa/khói và có người trong khung hình."
        return "danger", "Cảnh báo cháy: phát hiện lửa/khói không có người kiểm soát."

    return "normal", "Bình thường - không phát hiện nguy cơ cháy."


def normalize_camera_stream_url(camera_url: str) -> str:
    """Use the real MJPEG endpoint when the user enters only the ESP32 base URL."""
    cleaned_url = camera_url.strip()
    parts = urlsplit(cleaned_url)
    if not parts.scheme or not parts.netloc:
        return cleaned_url

    normalized_path = parts.path.rstrip("/")
    if normalized_path in ("", "/"):
        return urlunsplit((parts.scheme, parts.netloc, ESP32_STREAM_PATH, "", ""))
    return cleaned_url


def _esp32_base_url_from_camera_url(camera_url: str) -> str:
    parts = urlsplit(camera_url.strip())
    if not parts.scheme or not parts.netloc:
        return camera_url.strip().rstrip("/")
    return urlunsplit((parts.scheme, parts.netloc, "", "", "")).rstrip("/")


def frame_to_jpeg_bytes(frame_rgb, quality: int = 85) -> bytes:
    frame_bgr = cv2.cvtColor(frame_rgb, cv2.COLOR_RGB2BGR)
    ok, buffer = cv2.imencode(".jpg", frame_bgr, [int(cv2.IMWRITE_JPEG_QUALITY), quality])
    if not ok:
        raise RuntimeError("Không thể mã hóa khung hình JPEG.")
    return buffer.tobytes()


def send_alert_to_esp32(
    esp32_base_url: str,
    detected_names: list[str],
    annotated_frame_rgb=None,
    detections: list[str] | None = None,
    timeout: float = 2.0,
) -> bool:
    """Gửi kết quả phân tích AI (khói/lửa) và ảnh đã nhận diện về ESP32 qua HTTP POST."""
    detected_set = {name.lower() for name in detected_names}
    has_fire = "fire" in detected_set
    has_smoke = "smoke" in detected_set

    if not has_fire and not has_smoke:
        alert_type = "none"
    elif has_fire and has_smoke:
        alert_type = "fire_and_smoke"
    elif has_fire:
        alert_type = "fire"
    else:
        alert_type = "smoke"

    image_payload = b""
    if annotated_frame_rgb is not None:
        image_payload = frame_to_jpeg_bytes(annotated_frame_rgb)

    query = urlencode({
        "alert_type": alert_type,
        "has_fire": int(has_fire),
        "has_smoke": int(has_smoke),
        "detections": "; ".join(detections or [])[:240],
        "captured_at": datetime.now().strftime("%Y%m%d_%H%M%S"),
    })

    alert_url = _esp32_base_url_from_camera_url(esp32_base_url) + "/ai_alert?" + query
    request = Request(
        alert_url,
        data=image_payload,
        headers={"Content-Type": "image/jpeg" if image_payload else "application/octet-stream"},
        method="POST",
    )
    try:
        with urlopen(request, timeout=timeout) as response:
            return response.status == 200
    except Exception:
        return False


def resize_frame(frame, frame_width: int):
    if frame is None or frame.shape[1] <= frame_width:
        return frame
    scale = frame_width / frame.shape[1]
    return cv2.resize(frame, (frame_width, int(frame.shape[0] * scale)))


def frame_to_data_uri(frame_rgb) -> str:
    encoded = base64.b64encode(frame_to_jpeg_bytes(frame_rgb)).decode("ascii")
    return f"data:image/jpeg;base64,{encoded}"


def render_processed_frame(frame_rgb, level: str, message: str) -> str:
    image_uri = frame_to_data_uri(frame_rgb)
    safe_level = escape(level)
    safe_message = escape(message)
    return f"""
        <div class="monitor-shell">
            <div class="monitor-frame monitor-frame--{safe_level}">
                <img src="{image_uri}" alt="Khung hình đã được YOLOv8 xử lý" />
                <div class="monitor-badge">{safe_message}</div>
            </div>
        </div>
    """


def render_offline_monitor(title: str, message: str) -> None:
    st.html(
        f"""
        <div class="monitor-shell">
            <div class="monitor-frame monitor-frame--danger">
                <div class="monitor-placeholder">
                    {OFFLINE_ICON}
                    <div class="monitor-placeholder__title">{escape(title)}</div>
                    <div class="monitor-placeholder__text">{escape(message)}</div>
                </div>
            </div>
        </div>
        """
    )


def can_open_stream(camera_url: str, timeout: float = 1.5) -> tuple[bool, str | None]:
    try:
        stream_url = normalize_camera_stream_url(camera_url)
        request = Request(stream_url, headers={"Cache-Control": "no-cache"})
        with urlopen(request, timeout=timeout) as response:
            response.read(1)
        return True, None
    except (HTTPError, URLError, TimeoutError, OSError) as exc:
        return False, str(exc)


def process_video_source(
    video_source: str,
    confidence: float,
    frame_width: int,
    max_seconds: int | None = None,
    send_ai_alerts: bool = False,
    target_fps: float = DEFAULT_AI_TARGET_FPS,
) -> None:
    stream_url = normalize_camera_stream_url(video_source)
    configure_ai_runtime()

    try:
        model = load_model()
    except Exception as exc:
        st.error(f"Không tải được model YOLOv8: {exc}")
        return

    cap = cv2.VideoCapture(stream_url)
    cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)

    if not cap.isOpened():
        render_offline_monitor(
            "Không có tín hiệu",
            f"Không mở được luồng camera tại {stream_url}. Hãy kiểm tra ESP32 có endpoint /stream.",
        )
        return

    frame_placeholder = st.empty()
    status_placeholder = st.empty()
    detail_placeholder = st.empty()
    started_at = time.time()
    last_alert_sent_at = 0.0
    failed_reads = 0
    max_failed_reads = 30
    target_frame_interval = 1.0 / max(1.0, float(target_fps))

    try:
        while cap.isOpened():
            frame_started_at = time.time()
            if max_seconds is not None and time.time() - started_at >= max_seconds:
                st.info("Đã dừng phân tích theo thời lượng đã đặt.")
                break

            ok, frame = cap.read()
            if not ok:
                failed_reads += 1
                if failed_reads >= max_failed_reads:
                    st.warning("Luồng camera bị ngắt hoặc không đọc được frame liên tiếp. Đã dừng nhận diện.")
                    break
                time.sleep(0.1)
                continue
            failed_reads = 0

            frame = resize_frame(frame, frame_width)
            try:
                results = model.predict(frame, conf=confidence, verbose=False)
            except Exception as exc:
                st.error(f"Lỗi khi YOLOv8 xử lý khung hình: {exc}")
                break

            detected_names: list[str] = []
            detections: list[str] = []
            for result in results:
                for box in result.boxes:
                    class_id = int(box.cls[0])
                    class_name = str(model.names[class_id])
                    score = float(box.conf[0])
                    detected_names.append(class_name)
                    detections.append(f"{class_name} ({score:.2f})")

            level, message = build_alert_state(detected_names)
            annotated_frame = results[0].plot()
            annotated_frame = cv2.cvtColor(annotated_frame, cv2.COLOR_BGR2RGB)

            detected_set = {name.lower() for name in detected_names}
            should_send_alert = bool({"fire", "smoke"} & detected_set)
            esp32_alert_sent: bool | None = None
            now = time.time()
            if send_ai_alerts and should_send_alert and now - last_alert_sent_at >= DEFAULT_AI_ALERT_SEND_INTERVAL_SECONDS:
                esp32_alert_sent = send_alert_to_esp32(
                    video_source,
                    detected_names,
                    annotated_frame,
                    detections,
                )
                last_alert_sent_at = now

            frame_placeholder.html(render_processed_frame(annotated_frame, level, message))

            with status_placeholder.container():
                show_status(level, message)

            if detections:
                esp32_status = ""
                if esp32_alert_sent is True:
                    esp32_status = " | ESP32: đã nhận cảnh báo"
                elif esp32_alert_sent is False:
                    esp32_status = " | ESP32: chưa nhận được cảnh báo"
                detail_placeholder.caption("Phát hiện: " + ", ".join(detections) + esp32_status)
            else:
                detail_placeholder.caption("Không có đối tượng nào trong khung hình.")

            processing_time = time.time() - frame_started_at
            if processing_time < target_frame_interval:
                time.sleep(target_frame_interval - processing_time)
    finally:
        cap.release()


def render_live_camera(camera_url: str) -> None:
    stream_url = normalize_camera_stream_url(camera_url)
    safe_url = escape(stream_url, quote=True)
    can_connect, _ = can_open_stream(stream_url)
    if not can_connect:
        render_offline_monitor(
            "Không có tín hiệu",
            "Chưa có thiết bị để kết nối hoặc camera chưa phát luồng trực tiếp.",
        )
        return

    st.html(
        f"""
        <div class="monitor-shell">
            <div class="monitor-frame">
                <img src="{safe_url}" alt="Luồng trực tiếp ESP32-CAM" />
            </div>
        </div>
        <div class="camera-caption">
            Đang hiển thị luồng MJPEG trực tiếp từ ESP32-CAM. Trình duyệt cần truy cập được cùng mạng với camera.
        </div>
        """
    )


firebase_config = _parse_firebase_config()
firebase_project_id = firebase_config.get("projectId", "")
firebase_default_database_url = _firebase_database_url(firebase_config)


with st.sidebar:
    st.header("Thiết bị môi trường")
    st.info(f"Dữ liệu đang được đồng bộ trực tiếp từ {DEFAULT_FIREBASE_SENSOR_PATH}")
    
    sensor_source = "Firebase"
    firebase_service = "Firestore"
    firebase_project_id = "firealarm-8587f"
    firebase_sensor_path = DEFAULT_FIREBASE_SENSOR_PATH
    sensor_timeout = 3.0
    auto_refresh = False
    refresh_interval = 5

    refresh_clicked = st.button("Cập nhật thông số", use_container_width=True)
    if refresh_clicked:
        st.session_state.force_update = True
        st.rerun()

    st.divider()
    st.header("Camera ESP32-CAM")
    camera_stream_url = st.text_input(
        "URL luồng trực tiếp",
        value=DEFAULT_CAMERA_STREAM_URL,
    )
    show_live_camera = st.toggle("Hiển thị camera trực tiếp", value=True)
    send_ai_alerts = st.toggle("Gửi cảnh báo AI về ESP32", value=False)
    start_clicked = st.button("Bắt đầu nhận diện cháy", use_container_width=True)
    confidence = DEFAULT_CONFIDENCE
    frame_width = DEFAULT_FRAME_WIDTH
    stream_analysis_seconds = DEFAULT_STREAM_ANALYSIS_SECONDS
    target_fps = st.slider("FPS nhận diện AI", 1, 15, DEFAULT_AI_TARGET_FPS, 1)

    st.divider()
    st.header("Model")
    show_model_info = st.checkbox("Hiển thị lớp nhận diện", value=False)
    if show_model_info:
        try:
            labels = load_model().names
            if isinstance(labels, dict):
                st.caption(", ".join(str(value) for value in labels.values()))
            else:
                st.caption(", ".join(str(value) for value in labels))
        except Exception as exc:
            st.warning(str(exc))
    else:
        st.caption("Model sẽ được tải khi bắt đầu phân tích video.")


st.title("Giám sát cháy và môi trường")
st.markdown(
    '<div class="device-line">Theo dõi nhiệt độ không khí, độ ẩm, thời tiết khu vực và cảnh báo lửa/khói từ ESP32-CAM.</div>',
    unsafe_allow_html=True,
)

if auto_refresh:
    st.caption(f"Tự động cập nhật mỗi {refresh_interval}s — dữ liệu cảm biến và thời tiết được làm mới theo chu kỳ.")

st.markdown('<div class="section-title">Thông số từ thiết bị</div>', unsafe_allow_html=True)

sensor_reading: SensorReading | None = None
sensor_error: str | None = None

try:
    if sensor_source == "Firebase":
        if firebase_service == "Realtime Database":
            sensor_reading = fetch_realtime_database_sensor_reading(
                firebase_database_url,
                firebase_sensor_path,
                sensor_timeout,
            )
        else:
            sensor_reading = fetch_firestore_sensor_reading(
                firebase_project_id,
                firebase_sensor_path,
                sensor_timeout,
            )
    else:
        sensor_reading = fetch_sensor_reading(sensor_url.strip(), sensor_timeout)
    st.session_state.sensor_cache = sensor_reading
    st.session_state.sensor_cache_error = None
except (HTTPError, URLError, TimeoutError, json.JSONDecodeError, ValueError, OSError, PermissionError) as exc:
    sensor_error = str(exc)
    st.session_state.sensor_cache_error = sensor_error

if sensor_reading is None:
    sensor_reading = st.session_state.sensor_cache
    if sensor_error is None:
        sensor_error = st.session_state.sensor_cache_error

metric_col_1, metric_col_2, metric_col_3, metric_col_4 = st.columns(4)

if sensor_reading:
    level, message = classify_environment(sensor_reading)
    updated_at = sensor_reading.updated_at.strftime("%H:%M:%S")

    with metric_col_1:
        render_sensor_card(
            "Nhiệt độ không khí",
            format_temperature(sensor_reading.temperature),
            "Dữ liệu đo từ cảm biến nhiệt độ.",
            TEMPERATURE_ICON,
            "temperature",
        )
    with metric_col_2:
        render_sensor_card(
            "Độ ẩm không khí",
            format_humidity(sensor_reading.humidity),
            "Độ ẩm tương đối trong khu vực giám sát.",
            HUMIDITY_ICON,
            "humidity",
        )
    with metric_col_3:
        render_sensor_card(
            "Nồng độ khói",
            format_smoke(sensor_reading.smoke),
            "Dữ liệu đo từ cảm biến khói.",
            SMOKE_ICON,
            "smoke",
        )
    with metric_col_4:
        render_sensor_card(
            "Trạng thái thiết bị",
            "Đã kết nối",
            f"Cập nhật lúc {updated_at}.",
            STATUS_ICON,
            "status",
        )
    show_status(level, message)

    with st.expander("Dữ liệu JSON nhận từ thiết bị"):
        st.json(sensor_reading.raw)
else:
    with metric_col_1:
        render_sensor_card(
            "Nhiệt độ không khí",
            "--",
            "Chưa có dữ liệu nhiệt độ.",
            TEMPERATURE_ICON,
            "temperature",
        )
    with metric_col_2:
        render_sensor_card(
            "Độ ẩm không khí",
            "--",
            "Chưa có dữ liệu độ ẩm.",
            HUMIDITY_ICON,
            "humidity",
        )
    with metric_col_3:
        render_sensor_card(
            "Nồng độ khói",
            "--",
            "Chưa có dữ liệu khói.",
            SMOKE_ICON,
            "smoke",
        )
    with metric_col_4:
        render_sensor_card(
            "Trạng thái thiết bị",
            "Mất kết nối",
            "Không có tín hiệu mạng từ ESP32.",
            OFFLINE_ICON,
            "offline",
        )
    if sensor_error:
        st.info(f"Chưa có dữ liệu cảm biến để hiển thị: {sensor_error}")
    else:
        st.info("Chưa có thiết bị để kết nối.")

st.divider()
st.markdown('<div class="section-title">Thời tiết khu vực</div>', unsafe_allow_html=True)

weather_reading: WeatherReading | None = None
weather_error: str | None = None

current_time = time.time()
should_fetch_weather = (
    st.session_state.weather_cache is None
    or st.session_state.force_update
    or (current_time - st.session_state.weather_cache_time) > 300
)

if should_fetch_weather:
    try:
        weather_reading = fetch_weather_reading(
            DEFAULT_WEATHER_LATITUDE,
            DEFAULT_WEATHER_LONGITUDE,
            DEFAULT_WEATHER_TIMEOUT,
        )
        st.session_state.weather_cache = weather_reading
        st.session_state.weather_cache_error = None
        st.session_state.weather_cache_time = current_time
    except (HTTPError, URLError, TimeoutError, json.JSONDecodeError, ValueError, OSError) as exc:
        weather_error = str(exc)

if weather_reading is None:
    weather_reading = st.session_state.weather_cache
    if weather_error is None:
        weather_error = st.session_state.weather_cache_error

st.session_state.force_update = False

weather_col_1, weather_col_2, weather_col_3 = st.columns(3)

if weather_reading:
    weather_time = weather_reading.updated_at.strftime("%H:%M:%S")
    with weather_col_1:
        render_sensor_card(
            f"Thời tiết {DEFAULT_WEATHER_NAME}",
            describe_weather_code(weather_reading.weather_code),
            f"Cập nhật lúc {weather_time}.",
            WEATHER_ICON,
            "weather",
        )
    with weather_col_2:
        render_sensor_card(
            "Nhiệt độ ngoài trời",
            format_temperature(weather_reading.temperature),
            f"Cảm giác như {format_temperature(weather_reading.apparent_temperature)}.",
            TEMPERATURE_ICON,
            "temperature",
        )
    with weather_col_3:
        render_sensor_card(
            "Gió và độ ẩm",
            f"{weather_reading.wind_speed:.1f} km/h" if weather_reading.wind_speed is not None else "--",
            f"Độ ẩm ngoài trời {format_humidity(weather_reading.humidity)}.",
            WIND_ICON,
            "wind",
        )
else:
    with weather_col_1:
        render_sensor_card(
            f"Thời tiết {DEFAULT_WEATHER_NAME}",
            "Không có dữ liệu",
            "Không truy cập được dịch vụ thời tiết.",
            OFFLINE_ICON,
            "offline",
        )
    with weather_col_2:
        render_sensor_card(
            "Nhiệt độ ngoài trời",
            "--",
            "Dữ liệu này độc lập với cảm biến ESP32.",
            TEMPERATURE_ICON,
            "temperature",
        )
    with weather_col_3:
        render_sensor_card(
            "Gió và độ ẩm",
            "--",
            "Kiểm tra Internet hoặc dịch vụ thời tiết.",
            WIND_ICON,
            "wind",
        )
    if weather_error:
        st.info("Chưa có dữ liệu thời tiết để hiển thị.")

# Show last-updated timestamps
sensor_time = (
    st.session_state.sensor_cache.updated_at.strftime("%H:%M:%S")
    if st.session_state.sensor_cache
    else "---"
)
weather_time = (
    st.session_state.weather_cache.updated_at.strftime("%H:%M:%S")
    if st.session_state.weather_cache
    else "---"
)
st.caption(f"Cảm biến cập nhật lúc {sensor_time}  ·  Thời tiết cập nhật lúc {weather_time}")

st.divider()
st.markdown('<div class="section-title">Camera trực tiếp</div>', unsafe_allow_html=True)

is_live_ai_running = start_clicked

if show_live_camera and not is_live_ai_running:
    render_live_camera(camera_stream_url)
elif is_live_ai_running:
    st.info("Đang dùng luồng ESP32-CAM cho. Khung xử lý sẽ hiển thị ở phần bên dưới.")
else:
    st.info("Bật 'Hiển thị camera trực tiếp' trong sidebar để xem luồng ESP32-CAM.")

st.divider()
st.markdown('<div class="section-title">Phân tích hình ảnh và cảnh báo cháy</div>', unsafe_allow_html=True)

if start_clicked:
    if not camera_stream_url.strip():
        st.warning("Chưa có thiết bị để kết nối.")
    else:
        process_video_source(
            camera_stream_url.strip(),
            confidence,
            frame_width,
            max_seconds=stream_analysis_seconds,
            send_ai_alerts=send_ai_alerts,
            target_fps=target_fps,
        )
