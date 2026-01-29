# QR Station

**QR Station** là một dự án mã nguồn mở sử dụng ESP32-C3 và màn hình TFT để hiển thị mã QR thanh toán ngân hàng một cách chuyên nghiệp và tiện lợi. Thiết bị hỗ trợ hiển thị mã QR tĩnh và động, tích hợp thông báo giao dịch qua giao thức MQTT

![QR Station](./device.jpg)

## Tính năng chính

- **Hiển thị VietQR**: Hỗ trợ tối đa 3 tài khoản ngân hàng
- **QR Động**: Tạo mã QR kèm số tiền và nội dung chuyển khoản qua Extension
- **Thông báo giao dịch**: Nhận và hiển thị thông báo chuyển khoản tức thì (dùng API của [Pay2S](https://pay2s.vn/?aff=1073))
- **Web Dashboard**: Giao diện cấu hình WiFi, tài khoản ngân hàng, MQTT và cập nhật Firmware trực tiếp trên trình duyệt
- **Browser Extension**: Tích hợp nút "Đẩy mã QR" ngay trên trang KiotViet để gửi dữ liệu sang thiết bị

## Phần cứng

1. **Board điều khiển**: [ESP32-C3 Super Mini](https://s.shopee.vn/3fxmxrFitH)
2. **Màn hình**: [ST7735S TFT kèm 4 nút bấm](https://s.shopee.vn/6KyY8v4lM3)
3. **Mạch sạc**: [Mạch sạc mini Type C](https://s.shopee.vn/30i6B1hlRx) (tùy chọn)
4. **Pin lipo**: [Pin lipo 3.7V](https://s.shopee.vn/3fxmyJZHNL) (tùy chọn)

## Hướng dẫn cài đặt

### 1. Nạp Code cho ESP32
- Mở file `qr-station.ino` bằng Arduino IDE
- Cài đặt các thư viện cần thiết:
    - `Adafruit GFX Library`
    - `Adafruit ST7735 and ST7789 Library`
    - `ArduinoJson`
    - `PubSubClient`
    - `qrcode_st7735` (Thư viện tạo QR cho màn hình ST7735)
- Chọn board `XIAO ESP32C3` (hoặc Generic ESP32-C3) và nạp code

### 2. Cấu hình thiết bị
- Dựng server bằng folder `server/` nếu muốn nhận thông báo giao dịch
- Bấm nhanh 2 lần nút K4 để tắt/mở màn hình
- Đè nút K4 trong 3s để vào/thoát chế độ WiFi config
- Kết nối WiFi: `QR Station` (Mật khẩu: `88888888`)
- Truy cập IP trên màn hình (mặc định `192.168.4.1`) để vào trang Web Dashboard
- Thông tin đăng nhập mặc định: `admin` / `admin`
- Thiết lập tài khoản ngân hàng để tạo QR tĩnh
- Thiết lập kết nối WiFi & MQTT server để nhận thông báo giao dịch
- Đè nút K1 trong 3s để vào/thoát chế độ xem IP của thiết bị
- Đè nút K2 trong 3s để khởi động lại thiết bị
- Đè nút K3 trong 3s để reset thiết bị về trạng thái trống

### 3. Server Webhook
- Thư mục `server/` chứa mã nguồn Node.js để nhận webhook từ các dịch vụ ngân hàng và gửi sang MQTT.
- Đổi tên `.env.example` thành `.env` và cấu hình các thông số cần thiết
- Chạy lệnh: `npm install` và `node index.js`
- Lấy webhook token ở [Pay2S](https://pay2s.vn/?aff=1073)

### 4. Browser Extension
- Load thư mục `extension/` vào Chrome/Edge qua chế độ Developer Mode
- Tìm kiếm thiết bị, thiết lập MQTT để nhận thông báo giao dịch
- Tích hợp vào KiotViet để đẩy QR lên thiết bị

## Đóng góp
Rất hoan nghênh mọi sự đóng góp! Nếu có cải tiến mới, hãy Fork dự án và gửi Pull Request

---
Phát triển bởi [mhqb365.com](https://mhqb365.com)
