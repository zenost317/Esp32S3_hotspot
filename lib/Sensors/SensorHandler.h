#pragma once
#include <Wire.h>

// Khởi tạo AHT10/AHT20 trên bus I2C đã cho.
// Trả về true nếu tìm thấy cảm biến.
bool sensorInit(TwoWire& wire);

// Đọc nhiệt độ (°C) và độ ẩm tương đối (%).
// Trả về true nếu thành công.
bool sensorRead(float& tempC, float& humiPct);