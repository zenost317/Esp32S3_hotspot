import streamlit as st
import cv2
import tempfile
import numpy as np
from ultralytics import YOLO

# ---------------------------------------------------------
# 1. CẤU HÌNH TRANG VÀ TẢI MÔ HÌNH
# ---------------------------------------------------------
st.set_page_config(page_title="Hệ Thống Nhận Diện Cháy", page_icon="", layout="wide")

@st.cache_resource
def load_model():
    # Khởi tạo mô hình YOLO. 
    # Đảm bảo file 'yolov8_perfect_model.pt' nằm cùng thư mục với file app.py này
    model = YOLO('yolov8_perfect_model.pt')
    return model

model = load_model()

# ---------------------------------------------------------
# 2. HÀM XỬ LÝ VIDEO VÀ LOGIC PHÁT HIỆN
# ---------------------------------------------------------
def process_video(video_path):
    cap = cv2.VideoCapture(video_path)
    
    # Khung hiển thị video trên giao diện Streamlit
    stframe = st.empty()
    
    # Bảng trạng thái hiển thị text
    status_text = st.empty()

    while cap.isOpened():
        ret, frame = cap.read()
        if not ret:
            st.success("Đã xử lý xong video!")
            break

        # Resize frame để tăng tốc độ xử lý nếu video quá nặng (tùy chọn)
        frame = cv2.resize(frame, (800, int(800 * frame.shape[0] / frame.shape[1])))

        # 1. Chạy YOLO dự đoán trên khung hình
        results = model(frame, conf=0.3, verbose=False) # conf=0.3 để lọc bớt nhiễu
        
        # Lấy danh sách các đối tượng phát hiện được trong frame này
        detected_names = []
        for r in results:
            for box in r.boxes:
                class_id = int(box.cls[0])
                class_name = model.names[class_id]
                detected_names.append(class_name)

        # 2. Logic phân tích ngữ cảnh (Động thái tự thích ứng với số lượng class)
        has_fire = 'fire' in detected_names
        has_smoke = 'smoke' in detected_names
        has_person = 'person' in detected_names

        # Mặc định là an toàn
        color = (0, 255, 0) # Xanh lá
        alert_msg = "Bình thường"

        if has_fire or has_smoke:
            if not has_person:
                # Có lửa/khói nhưng KHÔNG CÓ NGƯỜI -> Nguy hiểm cao
                color = (0, 0, 255) # Đỏ (BGR trong OpenCV)
                alert_msg = "PHÁT HIỆN CHÁY KHÔNG KIỂM SOÁT!"
            else:
                # Có lửa/khói VÀ CÓ NGƯỜI -> Có thể đang nấu ăn / hút thuốc
                # Tương lai: Đây là chỗ bạn gọi mô hình ViT để kiểm tra chéo
                color = (0, 165, 255) # Cam
                alert_msg = "Chú ý có lửa/khói và người"

        # 3. Vẽ bounding box và text cảnh báo lên khung hình
        # YOLO tự động vẽ box qua hàm plot()
        annotated_frame = results[0].plot() 
        
        # Ghi đè text cảnh báo lên góc trên cùng của video
        cv2.putText(annotated_frame, alert_msg, (20, 40), 
                    cv2.FONT_HERSHEY_SIMPLEX, 0.8, color, 2, cv2.LINE_AA)

        # 4. Hiển thị lên giao diện Streamlit
        # OpenCV dùng hệ màu BGR, Streamlit dùng RGB nên cần chuyển đổi
        annotated_frame = cv2.cvtColor(annotated_frame, cv2.COLOR_BGR2RGB)
        stframe.image(annotated_frame, channels="RGB", use_container_width=True)
        
        # Cập nhật trạng thái bằng text tĩnh ở ngoài video cho dễ nhìn
        if color == (0, 0, 255):
            status_text.error(alert_msg)
        elif color == (0, 165, 255):
            status_text.warning(alert_msg)
        else:
            status_text.success(alert_msg)

    cap.release()

# ---------------------------------------------------------
# 3. THIẾT KẾ GIAO DIỆN CHÍNH
# ---------------------------------------------------------
st.title(" Hệ Thống Cảnh Báo Hoả Hoạn")
st.markdown("Hệ thống phát hiện Lửa, Khói và Con người để giảm thiểu báo động giả.")

# In ra các lớp mà mô hình hiện tại đang hỗ trợ
st.sidebar.markdown("Thông tin mô hình")
st.sidebar.info(f"Các đối tượng nhận diện được:\n{', '.join(model.names.values())}")

uploaded_file = st.sidebar.file_uploader("Tải lên video thử nghiệm (mp4, avi)", type=['mp4', 'avi', 'mov'])

if uploaded_file is not None:
    # Streamlit cần đọc file từ đường dẫn vật lý, nên ta lưu file upload vào thư mục tạm
    tfile = tempfile.NamedTemporaryFile(delete=False, suffix='.mp4')
    tfile.write(uploaded_file.read())
    
    st.sidebar.markdown("---")
    if st.sidebar.button("BẮT ĐẦU PHÂN TÍCH"):
        st.markdown("Kết Quả Nhận Diện Trực Tiếp")
        process_video(tfile.name)
else:
    st.info("Vui lòng tải lên một đoạn video ở thanh công cụ bên trái để bắt đầu.")