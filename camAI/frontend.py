from __future__ import annotations

from pathlib import Path
import tempfile

import streamlit as st

from backend import get_model_labels, get_youtube_stream_url, iter_video_analysis

DEFAULT_YOUTUBE_URL = "https://www.youtube.com/watch?v=tT4Ofx1E4Jw"

st.set_page_config(
    page_title="He thong canh bao chay",
    page_icon="",
    layout="wide",
)

st.title("He thong canh bao chay")

with st.sidebar:
    st.header("Nguon video")
    uploaded_file = st.file_uploader(
        "Tai len video",
        type=["mp4", "avi", "mov"],
    )
    youtube_url = st.text_input("Hoac dan link YouTube", value=DEFAULT_YOUTUBE_URL)

    st.header("Thong so")
    confidence = st.slider("Nguong tin cay", 0.1, 0.9, 0.3, 0.05)
    frame_width = st.slider("Do rong frame xu ly", 320, 1280, 640, 32)

    st.header("Model")
    st.info(", ".join(get_model_labels()))

    start_clicked = st.button("Bat dau phan tich", use_container_width=True)


def _show_status(container, level: str, message: str) -> None:
    if level == "danger":
        container.error(message)
    elif level == "warning":
        container.warning(message)
    else:
        container.success(message)


if not start_clicked:
    st.info("Chon video hoac YouTube, sau do bam 'Bat dau phan tich'.")
else:
    source_path: str | None = None
    temp_file_path: Path | None = None

    try:
        if uploaded_file is not None:
            with tempfile.NamedTemporaryFile(delete=False, suffix=".mp4") as tmp_file:
                tmp_file.write(uploaded_file.read())
                temp_file_path = Path(tmp_file.name)
                source_path = str(temp_file_path)
        elif youtube_url.strip():
            with st.spinner("Dang lay luong video YouTube..."):
                source_path = get_youtube_stream_url(youtube_url.strip())
            if not source_path:
                st.error("Khong lay duoc luong video tu YouTube.")
        else:
            st.warning("Ban can tai video len hoac nhap link YouTube.")

        if source_path:
            status_placeholder = st.empty()
            frame_placeholder = st.empty()
            detail_placeholder = st.empty()

            for result in iter_video_analysis(
                source_path,
                confidence=confidence,
                frame_width=frame_width,
            ):
                frame_placeholder.image(
                    result["frame"],
                    channels="RGB",
                    use_container_width=True,
                )
                _show_status(
                    status_placeholder,
                    result["alert_level"],
                    result["alert_message"],
                )

                detections = result["detections"]
                if detections:
                    summary = ", ".join(
                        f'{item["label"]} ({item["confidence"]:.2f})'
                        for item in detections
                    )
                    detail_placeholder.caption(f"Phat hien: {summary}")
                else:
                    detail_placeholder.caption("Không có đối tượng nào")

            st.success("Da xu ly xong video.")
    except Exception as exc:
        st.error(f"Loi xu ly: {exc}")
    finally:
        if temp_file_path and temp_file_path.exists():
            temp_file_path.unlink(missing_ok=True)
