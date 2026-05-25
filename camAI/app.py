from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime
from functools import cache
from pathlib import Path
from typing import Any, Generator
from urllib.error import HTTPError, URLError
from urllib.parse import quote
from urllib.request import Request, urlopen
import base64
import json
import re
import time

import cv2
from ultralytics import YOLO


MODEL_PATH = Path(__file__).with_name("yolov8_perfect_model.pt")
FIREBASE_CONFIG_PATH = Path(__file__).with_name("firebaseConfig.js")
DEFAULT_WEATHER_NAME = "Hà Nội"
DEFAULT_WEATHER_LATITUDE = 21.0278
DEFAULT_WEATHER_LONGITUDE = 105.8342
DEFAULT_WEATHER_TIMEOUT = 3.0
DEFAULT_CONFIDENCE = 0.3
DEFAULT_FRAME_WIDTH = 960
DEFAULT_STREAM_ANALYSIS_SECONDS = 60


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


SENSOR_TEMPERATURE_KEYS = (
    "temperature", "temperature_c", "temperaturevalue",
    "temp", "temp_c", "air_temperature",
    "nhiet_do", "nhietdo", "t",
)
SENSOR_HUMIDITY_KEYS = (
    "humidity", "humidity_percent", "humidityvalue",
    "hum", "air_humidity", "do_am", "doam", "h",
)
SENSOR_SMOKE_KEYS = (
    "smoke", "smoke_percent", "smokevalue",
    "khoi", "gas", "gasvalue", "mq2",
)
SENSOR_VALUE_KEYS = SENSOR_TEMPERATURE_KEYS + SENSOR_HUMIDITY_KEYS + SENSOR_SMOKE_KEYS
SENSOR_TIMESTAMP_KEYS = (
    "updated_at", "updatedAt", "timestamp",
    "time", "created_at", "createdAt", "ts",
)


# ---------- helpers ----------

@cache
def load_model() -> YOLO:
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


def _extract_value(raw_value: Any) -> float | None:
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


def _safe_json_loads(body: str, source_name: str) -> Any:
    try:
        return json.loads(body)
    except json.JSONDecodeError:
        if "<html" in body.lower() or "<!doctype" in body.lower():
            raise ValueError(
                f"{source_name} đang trả về trang web (HTML). "
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
        (key, value) for key, value in payload.items() if isinstance(value, dict)
    ]
    if not candidate_items:
        return payload
    scored_items = [
        (_timestamp_score(value), str(key), value) for key, value in candidate_items
    ]
    scored_items.sort(key=lambda item: (item[0], item[1]))
    return scored_items[-1][2]


def _build_sensor_reading(payload: Any) -> SensorReading:
    normalized = _normalize_payload(payload)
    return SensorReading(
        temperature=_first_number(normalized, SENSOR_TEMPERATURE_KEYS),
        humidity=_first_number(normalized, SENSOR_HUMIDITY_KEYS),
        smoke=_first_number(normalized, SENSOR_SMOKE_KEYS),
        raw=normalized,
        updated_at=datetime.now(),
    )


def _decode_firestore_value(value: Any) -> Any:
    if not isinstance(value, dict):
        return value
    if "doubleValue" in value:
        return _as_float(value["doubleValue"])
    if "integerValue" in value:
        return _as_float(value["integerValue"])
    if "stringValue" in value:
        return value["stringValue"]
    if "booleanValue" in value:
        return value["booleanValue"]
    if "timestampValue" in value:
        return value["timestampValue"]
    if "nullValue" in value:
        return None
    if "mapValue" in value:
        fields = value["mapValue"].get("fields", {})
        return {k: _decode_firestore_value(v) for k, v in fields.items()}
    if "arrayValue" in value:
        values = value["arrayValue"].get("values", [])
        return [_decode_firestore_value(item) for item in values]
    return value


def _decode_firestore_fields(fields: dict[str, Any]) -> dict[str, Any]:
    return {key: _decode_firestore_value(value) for key, value in fields.items()}


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
        path = encoded_path.replace("~2F", "/").replace("~2f", "/").replace("%2F", "/").replace("%2f", "/")
        return project_id, path.strip("/")
    return None, None


def _parse_firebase_config(config_path: Path = FIREBASE_CONFIG_PATH) -> dict[str, str]:
    if not config_path.exists():
        return {}
    content = config_path.read_text(encoding="utf-8")
    return {k: v for k, v in re.findall(r"(\w+)\s*:\s*[\"']([^\"']+)[\"']", content)}


def _firebase_database_url(firebase_config: dict[str, str]) -> str:
    database_url = firebase_config.get("databaseURL", "").strip()
    if database_url:
        return database_url.rstrip("/")
    project_id = firebase_config.get("projectId", "").strip()
    if not project_id:
        return ""
    return f"https://{project_id}-default-rtdb.firebaseio.com"


firebase_config = _parse_firebase_config()
firebase_api_key = firebase_config.get("apiKey", "")


# ---------- data fetching ----------

def fetch_firestore_sensor_reading(
    project_id: str,
    path: str,
    timeout: float,
    api_key: str = "",
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
    if api_key:
        sep = "&" if "?" in firestore_url else "?"
        firestore_url += f"{sep}key={quote(api_key, safe='')}"

    request = Request(firestore_url, headers={"Accept": "application/json"})
    try:
        with urlopen(request, timeout=timeout) as response:
            body = response.read().decode("utf-8")
            payload = _safe_json_loads(body, "Firestore")
    except HTTPError as exc:
        if exc.code == 403:
            raise PermissionError(
                "Truy cập Firestore bị từ chối (403). "
                "Hãy đảm bảo Firestore Rules cho phép đọc, hoặc API key có quyền truy cập Firestore."
            )
        if exc.code == 404:
            raise ValueError(f"Không tìm thấy dữ liệu tại '{normalized_path}' (404).")
        raise

    document = _select_firestore_document(payload)
    decoded = _decode_firestore_fields(document.get("fields", {}))
    return _build_sensor_reading(decoded)


def fetch_weather_reading(
    latitude: float,
    longitude: float,
    timeout: float,
) -> WeatherReading:
    weather_url = (
        "https://api.open-meteo.com/v1/forecast"
        f"?latitude={latitude:.4f}&longitude={longitude:.4f}"
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


def can_open_stream(camera_url: str, timeout: float = 1.5) -> tuple[bool, str | None]:
    try:
        request = Request(camera_url, headers={"Cache-Control": "no-cache"})
        with urlopen(request, timeout=timeout) as response:
            response.read(1)
        return True, None
    except (HTTPError, URLError, TimeoutError, OSError) as exc:
        return False, str(exc)


# ---------- classification ----------

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


# ---------- YOLO processing ----------

def resize_frame(frame, frame_width: int):
    if frame is None or frame.shape[1] <= frame_width:
        return frame
    scale = frame_width / frame.shape[1]
    return cv2.resize(frame, (frame_width, int(frame.shape[0] * scale)))


def frame_to_data_uri(frame_rgb) -> str:
    frame_bgr = cv2.cvtColor(frame_rgb, cv2.COLOR_RGB2BGR)
    ok, buffer = cv2.imencode(".jpg", frame_bgr, [int(cv2.IMWRITE_JPEG_QUALITY), 85])
    if not ok:
        raise RuntimeError("Không thể mã hóa khung hình.")
    return f"data:image/jpeg;base64,{base64.b64encode(buffer).decode('ascii')}"


def iter_video_analysis(
    video_source: str,
    confidence: float = DEFAULT_CONFIDENCE,
    frame_width: int = DEFAULT_FRAME_WIDTH,
    max_seconds: int | None = None,
) -> Generator[dict, None, None]:
    model = load_model()
    cap = cv2.VideoCapture(video_source)
    if not cap.isOpened():
        yield {"type": "error", "message": "Không thể mở luồng video."}
        return

    started_at = time.time()
    try:
        while cap.isOpened():
            if max_seconds is not None and time.time() - started_at >= max_seconds:
                yield {"type": "done", "message": "Đã dừng phân tích theo thời lượng."}
                break
            ok, frame = cap.read()
            if not ok:
                yield {"type": "done", "message": "Đã xử lý xong nguồn video."}
                break
            frame = resize_frame(frame, frame_width)
            results = model.predict(frame, conf=confidence, verbose=False)
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
            yield {
                "type": "frame",
                "frame": annotated_frame,
                "level": level,
                "message": message,
                "detections": detections,
            }
    finally:
        cap.release()


# ---------- format helpers ----------

def format_temperature(value: float | None) -> str:
    return "--" if value is None else f"{value:.1f} °C"


def format_humidity(value: float | None) -> str:
    return "--" if value is None else f"{value:.1f}%"


def format_smoke(value: float | None) -> str:
    return "--" if value is None else f"{value:.1f}%"
