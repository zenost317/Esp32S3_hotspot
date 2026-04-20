from __future__ import annotations

from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path
from typing import Generator, Iterable
import time

import cv2
import yt_dlp
from ultralytics import YOLO

MODEL_PATH = Path(__file__).with_name("yolov8_perfect_model.pt")
DEFAULT_CONFIDENCE = 0.3
DEFAULT_FRAME_WIDTH = 640


@dataclass(frozen=True)
class AlertState:
    level: str
    message: str


@lru_cache(maxsize=1)
def load_model(model_path: str | None = None) -> YOLO:
    resolved_path = Path(model_path) if model_path else MODEL_PATH
    if not resolved_path.exists():
        raise FileNotFoundError(f"Model file not found: {resolved_path}")
    return YOLO(str(resolved_path))


def get_model_labels() -> list[str]:
    names = load_model().names
    if isinstance(names, dict):
        return [str(name) for _, name in sorted(names.items())]
    return [str(name) for name in names]


def get_youtube_stream_url(youtube_url: str) -> str | None:
    ydl_opts = {
        "format": "best[ext=mp4][height<=720]/best[ext=mp4]/best",
        "quiet": True,
        "noplaylist": True,
    }

    try:
        with yt_dlp.YoutubeDL(ydl_opts) as ydl:
            info_dict = ydl.extract_info(youtube_url, download=False)
            return info_dict.get("url")
    except Exception:
        return None


def build_alert_state(detected_names: Iterable[str]) -> AlertState:
    detected_set = {name.lower() for name in detected_names}
    has_fire = "fire" in detected_set
    has_smoke = "smoke" in detected_set
    has_person = "person" in detected_set

    if has_fire or has_smoke:
        if has_person:
            return AlertState(
                level="warning",
                message="Co dau hieu lua/khoi va co nguoi trong khung hinh",
            )
        return AlertState(
            level="danger",
            message="Canh bao chay: phat hien lua/khoi khong co nguoi kiem soat",
        )

    return AlertState(
        level="normal",
        message="Binh thuong - khong phat hien nguy co chay",
    )


def _resize_frame(frame, target_width: int):
    if frame is None or frame.shape[1] <= target_width:
        return frame
    scale = target_width / frame.shape[1]
    target_height = max(1, int(frame.shape[0] * scale))
    return cv2.resize(frame, (target_width, target_height))


def iter_video_analysis(
    video_source: str,
    confidence: float = DEFAULT_CONFIDENCE,
    frame_width: int = DEFAULT_FRAME_WIDTH,
    sync_to_fps: bool = True,
) -> Generator[dict, None, None]:
    model = load_model()
    cap = cv2.VideoCapture(video_source)

    if not cap.isOpened():
        raise RuntimeError(f"Cannot open video source: {video_source}")

    fps = cap.get(cv2.CAP_PROP_FPS)
    target_frame_time = 1.0 / fps if fps and fps > 0 else 1.0 / 30.0

    try:
        while True:
            started_at = time.time()
            ok, frame = cap.read()
            if not ok:
                break

            frame = _resize_frame(frame, frame_width)
            results = model.predict(frame, conf=confidence, verbose=False)

            detected_names: list[str] = []
            detections: list[dict] = []

            for result in results:
                for box in result.boxes:
                    class_id = int(box.cls[0])
                    class_name = str(model.names[class_id])
                    score = float(box.conf[0])
                    detected_names.append(class_name)
                    detections.append(
                        {
                            "label": class_name,
                            "confidence": round(score, 3),
                        }
                    )

            alert_state = build_alert_state(detected_names)
            annotated_frame = results[0].plot()
            frame_rgb = cv2.cvtColor(annotated_frame, cv2.COLOR_BGR2RGB)

            yield {
                "frame": frame_rgb,
                "alert_level": alert_state.level,
                "alert_message": alert_state.message,
                "detections": detections,
                "detected_names": detected_names,
            }

            if sync_to_fps:
                processing_time = time.time() - started_at
                if processing_time < target_frame_time:
                    time.sleep(target_frame_time - processing_time)
    finally:
        cap.release()
