from __future__ import annotations

from datetime import datetime
from html import escape
from urllib.parse import quote
import time

import streamlit as st

from app import (
    SensorReading,
    WeatherReading,
    load_model,
    fetch_firestore_sensor_reading,
    fetch_weather_reading,
    _parse_console_url,
    firebase_api_key,
    classify_environment,
    describe_weather_code,
    can_open_stream,
    iter_video_analysis,
    frame_to_data_uri,
    format_temperature,
    format_humidity,
    format_smoke,
)

st.set_page_config(page_title="Giám sát cháy và môi trường", layout="wide")

st.html("""
<style>
.main .block-container { padding-top: 1.6rem; padding-bottom: 2rem; }
.section-title { font-size: 1.05rem; font-weight: 800; color: #ffffff; margin: 0.25rem 0 0.75rem; }
.sensor-card { min-height: 128px; background: #111827; border: 1px solid #1f2937; border-radius: 10px; padding: 18px; box-shadow: 0 1px 3px rgba(0,0,0,0.4); }
.sensor-card__top { display: flex; align-items: center; justify-content: space-between; gap: 12px; margin-bottom: 14px; }
.sensor-card__label { color: #9ca3af; font-size: 0.9rem; font-weight: 700; }
.sensor-card__icon { width: 42px; height: 42px; display: inline-flex; align-items: center; justify-content: center; border-radius: 8px; background: #1f2937; border: 1px solid #374151; }
.sensor-card__icon svg { width: 24px; height: 24px; stroke-width: 2.1; }
.sensor-card__value { color: #ffffff; font-size: 2rem; line-height: 1.1; font-weight: 800; letter-spacing: 0; }
.sensor-card__sub { color: #d1d5db; font-size: 0.85rem; margin-top: 8px; }
.sensor-card--temperature .sensor-card__icon svg { color: #f87171; }
.sensor-card--humidity .sensor-card__icon svg { color: #38bdf8; }
.sensor-card--smoke .sensor-card__icon svg { color: #9ca3af; }
.sensor-card--status .sensor-card__icon svg { color: #4ade80; }
.sensor-card--offline .sensor-card__icon svg { color: #f87171; }
.sensor-card--weather .sensor-card__icon svg { color: #fbbf24; }
.sensor-card--wind .sensor-card__icon svg { color: #9ca3af; }
.monitor-shell { width: 100%; max-width: 980px; margin: 0 auto; }
.monitor-frame { position: relative; width: 100%; aspect-ratio: 16/9; background: #020617; border: 1px solid #1e293b; border-radius: 8px; overflow: hidden; display: flex; align-items: center; justify-content: center; box-shadow: 0 10px 28px rgba(0,0,0,0.4); }
.monitor-frame img { display: block; width: 100%; height: 100%; object-fit: contain; background: #020617; }
.monitor-placeholder { width: 100%; height: 100%; display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 10px; padding: 24px; color: #e2e8f0; text-align: center; }
.monitor-placeholder svg { width: 58px; height: 58px; color: #ef4444; stroke-width: 1.8; }
.monitor-placeholder__title { color: #ffffff; font-size: 1.15rem; font-weight: 800; }
.monitor-placeholder__text { max-width: 520px; color: #94a3b8; font-size: 0.92rem; line-height: 1.5; }
.monitor-badge { position: absolute; left: 14px; top: 14px; max-width: calc(100% - 28px); padding: 8px 12px; border-radius: 8px; color: #ffffff; font-size: 0.88rem; font-weight: 800; line-height: 1.25; background: rgba(22,163,74,0.92); box-shadow: 0 8px 22px rgba(0,0,0,0.3); }
.monitor-frame--warning .monitor-badge { background: rgba(217,119,6,0.94); }
.monitor-frame--danger { border-color: #dc2626; }
.monitor-frame--danger .monitor-badge { background: rgba(220,38,38,0.95); }
.updateline { color: #d1d5db; font-size: 0.82rem; margin-top: 0.3rem; }
</style>
""")

# ── icons ──

TEMP_ICON = """<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round"><path d="M14 14.76V5a4 4 0 0 0-8 0v9.76a6 6 0 1 0 8 0Z"/><path d="M10 9v7"/></svg>"""
HUM_ICON = """<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round"><path d="M12 2.5 6.8 8.3a7.2 7.2 0 1 0 10.4 0L12 2.5Z"/><path d="M8.5 14.5a3.5 3.5 0 0 0 7 0"/></svg>"""
SMOKE_ICON = """<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round"><path d="M8 2c0 2-2 4-2 6a4 4 0 0 0 8 0c0-2-2-4-2-6"/><path d="M16 6c0 1.5-1 3-1 4.5a3 3 0 0 0 6 0c0-1.5-1-3-1-4.5"/><path d="M2 18h20"/><path d="M4 22h16"/></svg>"""
STATUS_ICON = """<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round"><path d="M5 13a10 10 0 0 1 14 0"/><path d="M8.5 16.5a5 5 0 0 1 7 0"/><path d="M12 20h.01"/><path d="M3 9a14 14 0 0 1 18 0"/></svg>"""
OFFLINE_ICON = """<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round"><path d="m2 2 20 20"/><path d="M8.5 16.5a5 5 0 0 1 7 0"/><path d="M12 20h.01"/><path d="M5 13a10 10 0 0 1 5.2-2.7"/><path d="M13.8 10.3A10 10 0 0 1 19 13"/><path d="M3 9a14 14 0 0 1 3.8-2.4"/><path d="M10.6 5.2A14 14 0 0 1 21 9"/></svg>"""
WEATHER_ICON = """<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="4"/><path d="M12 2v2"/><path d="M12 20v2"/><path d="m4.93 4.93 1.41 1.41"/><path d="m17.66 17.66 1.41 1.41"/><path d="M2 12h2"/><path d="M20 12h2"/><path d="m6.34 17.66-1.41 1.41"/><path d="m19.07 4.93-1.41 1.41"/></svg>"""
WIND_ICON = """<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round"><path d="M3 8h10a3 3 0 1 0-3-3"/><path d="M3 12h15a3 3 0 1 1-3 3"/><path d="M3 16h8"/></svg>"""

def render_sensor_card(label, value, subtitle, icon_svg, variant):
    st.markdown(f"""<div class="sensor-card sensor-card--{variant}"><div class="sensor-card__top"><div class="sensor-card__label">{escape(label)}</div><div class="sensor-card__icon">{icon_svg}</div></div><div class="sensor-card__value">{escape(value)}</div><div class="sensor-card__sub">{escape(subtitle)}</div></div>""", unsafe_allow_html=True)

def show_status(level, message):
    if level == "danger": st.error(message)
    elif level == "warning": st.warning(message)
    else: st.success(message)

def render_offline_monitor(title, message):
    st.html(f"""<div class="monitor-shell"><div class="monitor-frame monitor-frame--danger"><div class="monitor-placeholder">{OFFLINE_ICON}<div class="monitor-placeholder__title">{escape(title)}</div><div class="monitor-placeholder__text">{escape(message)}</div></div></div></div>""")

# ── session state ──

for key in ("sensor_cache", "sensor_cache_error", "weather_cache", "weather_cache_error", "weather_cache_time", "force_update"):
    if key not in st.session_state:
        st.session_state[key] = 0.0 if key == "weather_cache_time" else (None if key in ("sensor_cache","sensor_cache_error","weather_cache","weather_cache_error") else False)

# ── sidebar ──

with st.sidebar:
    st.header("Nguồn dữ liệu")
    default_url = "https://console.firebase.google.com/u/0/project/firealarm-8587f/firestore/databases/-default-/data/~2Fdevices~2F4845788"
    firebase_url = st.text_input("URL Firebase Console", value=default_url)
    parsed_project, parsed_path = _parse_console_url(firebase_url)
    if parsed_project and parsed_path:
        st.info(f"Đọc từ: {parsed_project}/{parsed_path}")
    else:
        st.warning("URL không hợp lệ.")
    st.divider()
    st.header("Camera")
    camera_url = st.text_input("URL luồng ESP32-CAM", value="http://192.168.4.1:81/stream")
    show_live = st.toggle("Hiển thị camera trực tiếp", value=True)
    start_ai = st.button("Bắt đầu nhận diện cháy", use_container_width=True, type="primary")
    st.divider()
    st.header("Model")
    try:
        labels = list(load_model().names.values())
        st.caption(f"Nhãn: {', '.join(labels)}")
    except Exception as e:
        st.warning(str(e))
    refresh_btn = st.button("Cập nhật thông số", use_container_width=True)
    if refresh_btn:
        st.session_state.force_update = True
        st.rerun()

# ── main ──

project = parsed_project or "firealarm-8587f"
path = parsed_path or "devices/4845788"
refresh_interval = 1
auto_refresh = True

st.title("Giám sát cháy và môi trường")

# ── fetch sensor ──

sensor_reading = None
sensor_error = None
try:
    sensor_reading = fetch_firestore_sensor_reading(project, path, 3.0, firebase_api_key)
    st.session_state.sensor_cache = sensor_reading
    st.session_state.sensor_cache_error = None
except Exception as exc:
    sensor_error = str(exc)
if sensor_reading is None:
    sensor_reading = st.session_state.sensor_cache
    if sensor_error is None:
        sensor_error = st.session_state.sensor_cache_error

# ── fetch weather ──

weather_reading = None
weather_error = None
now = time.time()
if st.session_state.weather_cache is None or st.session_state.force_update or (now - st.session_state.weather_cache_time) > 300:
    try:
        weather_reading = fetch_weather_reading(21.0278, 105.8342, 3.0)
        st.session_state.weather_cache = weather_reading
        st.session_state.weather_cache_error = None
        st.session_state.weather_cache_time = now
    except Exception as exc:
        weather_error = str(exc)
if weather_reading is None:
    weather_reading = st.session_state.weather_cache
    if weather_error is None:
        weather_error = st.session_state.weather_cache_error
st.session_state.force_update = False

# ── sensor cards ──

st.markdown('<div class="section-title">Thông số từ thiết bị</div>', unsafe_allow_html=True)

col1, col2, col3, col4 = st.columns(4)
if sensor_reading:
    env_level, env_msg = classify_environment(sensor_reading)
    updated_at = sensor_reading.updated_at.strftime("%H:%M:%S")
    with col1:
        render_sensor_card("Nhiệt độ không khí", format_temperature(sensor_reading.temperature), "Dữ liệu đo từ cảm biến nhiệt độ.", TEMP_ICON, "temperature")
    with col2:
        render_sensor_card("Độ ẩm không khí", format_humidity(sensor_reading.humidity), "Độ ẩm tương đối trong khu vực giám sát.", HUM_ICON, "humidity")
    with col3:
        render_sensor_card("Nồng độ khói", format_smoke(sensor_reading.smoke), "Dữ liệu đo từ cảm biến khói.", SMOKE_ICON, "smoke")
    with col4:
        render_sensor_card("Trạng thái thiết bị", "Đã kết nối", f"Cập nhật lúc {updated_at}.", STATUS_ICON, "status")
    show_status(env_level, env_msg)
else:
    with col1:
        render_sensor_card("Nhiệt độ không khí", "--", "Chưa có dữ liệu.", TEMP_ICON, "temperature")
    with col2:
        render_sensor_card("Độ ẩm không khí", "--", "Chưa có dữ liệu.", HUM_ICON, "humidity")
    with col3:
        render_sensor_card("Nồng độ khói", "--", "Chưa có dữ liệu.", SMOKE_ICON, "smoke")
    with col4:
        render_sensor_card("Trạng thái thiết bị", "Mất kết nối", "Không có tín hiệu.", OFFLINE_ICON, "offline")
    show_status("warning", sensor_error or "Chưa có dữ liệu cảm biến.")

# ── weather cards ──

st.divider()
st.markdown('<div class="section-title">Thời tiết khu vực</div>', unsafe_allow_html=True)

wc1, wc2, wc3 = st.columns(3)
if weather_reading:
    wt = weather_reading.updated_at.strftime("%H:%M:%S")
    with wc1:
        render_sensor_card("Thời tiết Hà Nội", describe_weather_code(weather_reading.weather_code), f"Cập nhật lúc {wt}.", WEATHER_ICON, "weather")
    with wc2:
        render_sensor_card("Nhiệt độ ngoài trời", format_temperature(weather_reading.temperature), f"Cảm giác như {format_temperature(weather_reading.apparent_temperature)}.", TEMP_ICON, "temperature")
    with wc3:
        ws = f"{weather_reading.wind_speed:.1f} km/h" if weather_reading.wind_speed is not None else "--"
        render_sensor_card("Gió và độ ẩm", ws, f"Độ ẩm ngoài trời {format_humidity(weather_reading.humidity)}.", WIND_ICON, "wind")
else:
    with wc1:
        render_sensor_card("Thời tiết Hà Nội", "Không có dữ liệu", "Không truy cập được dịch vụ thời tiết.", WEATHER_ICON, "offline")
    with wc2:
        render_sensor_card("Nhiệt độ ngoài trời", "--", "Dữ liệu độc lập với cảm biến.", TEMP_ICON, "temperature")
    with wc3:
        render_sensor_card("Gió và độ ẩm", "--", "Kiểm tra Internet.", WIND_ICON, "wind")

# ── last update timestamps ──

sensor_time = st.session_state.sensor_cache.updated_at.strftime("%H:%M:%S") if st.session_state.sensor_cache else "---"
weather_time = st.session_state.weather_cache.updated_at.strftime("%H:%M:%S") if st.session_state.weather_cache else "---"
st.markdown(f'<div class="updateline">Cảm biến cập nhật lúc {sensor_time}  ·  Thời tiết cập nhật lúc {weather_time}</div>', unsafe_allow_html=True)

# ── camera ──

st.divider()
st.markdown('<div class="section-title">Camera trực tiếp</div>', unsafe_allow_html=True)

if show_live and not start_ai:
    safe_url = escape(camera_url.strip(), quote=True)
    ok, _ = can_open_stream(camera_url.strip())
    if ok:
        st.html(f"""<div class="monitor-shell"><div class="monitor-frame"><img src="{safe_url}" alt="Live" /></div></div>""")
    else:
        render_offline_monitor("Không có tín hiệu", "Chưa có thiết bị để kết nối hoặc camera chưa phát luồng trực tiếp.")
elif start_ai:
    st.info("Đang dùng luồng ESP32-CAM để phân tích. Kết quả bên dưới.")
else:
    st.info("Bật 'Hiển thị camera trực tiếp' trong sidebar để xem luồng ESP32-CAM.")

# ── AI analysis ──

st.divider()
st.markdown('<div class="section-title">Phân tích hình ảnh</div>', unsafe_allow_html=True)

if start_ai:
    if not camera_url.strip():
        st.warning("Chưa có URL camera.")
    else:
        f_ph = st.empty()
        s_ph = st.empty()
        d_ph = st.empty()
        for result in iter_video_analysis(camera_url.strip(), 0.3, 960, 60):
            if result["type"] == "error":
                render_offline_monitor("Không có tín hiệu", result["message"])
                break
            if result["type"] == "done":
                st.success(result["message"])
                break
            image_uri = frame_to_data_uri(result["frame"])
            level = result["level"]
            message = result["message"]
            detections = result["detections"]
            f_ph.html(f"""<div class="monitor-shell"><div class="monitor-frame monitor-frame--{level}"><img src="{image_uri}" alt="AI" /><div class="monitor-badge">{escape(message)}</div></div></div>""")
            with s_ph.container():
                show_status(level, message)
            d_ph.caption("Phát hiện: " + ", ".join(detections) if detections else "Không có đối tượng nào.")
else:
    st.info("Bấm 'Bắt đầu nhận diện cháy' trong sidebar để phân tích.")

# ── auto-refresh ──

if auto_refresh and not start_ai:
    time.sleep(refresh_interval)
    st.rerun()