# Hộp thuốc thông minh nhắc uống thuốc Nhóm: 08
Họ tên: Lê Doãn Minh.
Mã sinh viên: K23DTCN292.
Lớp: D23TXCN06-K.
Phiên bản Arduino-ESP32/board và thư viện đang sử dụng, nếu xác định được từ cấu hình thực tế.

Đồ án học phần **Xây dựng các hệ thống nhúng (INT1461)**, thực hiện trên nền tảng mô phỏng Wokwi. Hệ thống dùng ESP32 để nhắc lịch uống thuốc theo ba ngăn, hiển thị thời gian/trạng thái trên LCD và ghi nhận thao tác người dùng.

## Liên kết dự án

- **Mô phỏng Wokwi:** https://wokwi.com/projects/477970164539571201
- **Repository GitHub:**https://github.com/Minhled/Nhom08-INT1461-Hop-Thuoc-Thong-Minh

## Chức năng chính

- Đọc thời gian từ module RTC DS1307.
- Hiển thị thời gian, lịch nhắc và trạng thái trên LCD1602 giao tiếp I²C.
- Nhắc ba ngăn thuốc theo lịch mặc định 08:00, 13:00 và 20:00.
- Sử dụng LED và buzzer để báo nhắc.
- Nút **Đã uống** để xác nhận liều; nút **Tắt tiếng** để tắt âm báo.
- Lưu tối đa 20 bản ghi lịch sử trong RAM.
- Khởi chạy web server trên ESP32 và kết nối Wi-Fi mô phỏng `Wokwi-GUEST`.

> **Giới hạn đã biết:** lịch sử trong RAM sẽ mất khi reset/dừng mô phỏng. Việc truy cập web server từ trình duyệt bên ngoài Wokwi chưa được xác minh trong mọi môi trường. Kết quả kiểm thử cần được mô tả đúng theo log/ảnh thực tế; không suy diễn từ việc chương trình biên dịch hoặc server khởi động.

## Phần cứng và chân kết nối

| Thành phần | Kết nối ESP32 |
|---|---|
| LCD1602 I²C SDA | GPIO21 |
| LCD1602 I²C SCL | GPIO22 |
| RTC DS1307 SDA/SCL | Bus I²C chung: GPIO21/GPIO22 |
| LED ngăn A | GPIO18 (qua điện trở 220 Ω) |
| LED ngăn B | GPIO19 (qua điện trở 220 Ω) |
| LED ngăn C | GPIO23 (qua điện trở 220 Ω) |
| Buzzer | GPIO32 và GND |
| Nút xác nhận đã uống | GPIO27 và GND (`INPUT_PULLUP`) |
| Nút tắt tiếng/lịch sử | GPIO26 và GND (`INPUT_PULLUP`) |

Xem sơ đồ Wokwi trong [`wokwi/diagram.json`](wokwi/diagram.json).

## Cấu trúc repository

```text
INT1461-Hop-Thuoc-Thong-Minh/
├── README.md
├── .gitignore
├── src/
│   ├── sketch.ino
│   └── libraries.txt
├── wokwi/
│   └── diagram.json
└── docs/
    └── test-evidence/
        └── README.md
```

## Mở và chạy mô phỏng

1. Mở link Wokwi ở trên hoặc tạo project ESP32 mới tại [Wokwi](https://wokwi.com/).
2. Nạp nội dung `wokwi/diagram.json` vào sơ đồ mạch và nội dung `src/sketch.ino` vào trình soạn thảo mã nguồn (nếu tạo project mới).
3. Thêm các thư viện được liệt kê trong `src/libraries.txt` nếu Wokwi chưa tự nhận.
4. Chạy **Start Simulation** và mở Serial Monitor ở tốc độ 115200 baud.
5. Quan sát LCD, log Serial, đèn/buzzer và thao tác hai nút nhấn.

## Lịch nhắc mặc định

| Ngăn | Giờ nhắc |
|---|---:|
| A | 08:00 |
| B | 13:00 |
| C | 20:00 |

Muốn thay đổi lịch, chỉnh mảng `schedule[]` trong `src/sketch.ino`.

## Kiểm thử và minh chứng

Đặt ảnh chụp màn hình và log kiểm thử thực tế vào `docs/test-evidence/`. Ghi rõ mã ca kiểm thử, lần chạy và kết quả quan sát được. Không ghi một chuỗi thao tác liên tiếp thành nhiều lần thử độc lập nếu chưa thực sự chạy độc lập.

Một số lần mô phỏng đã ghi nhận cảnh báo bỏ lỡ liều xuất hiện sớm hơn 60 giây theo thời gian hiển thị trong log. Vì vậy, **không kết luận yêu cầu timeout 60 giây đã đạt**; cần kiểm tra lại hành vi này nếu tiếp tục phát triển mã nguồn.

## Thư viện

- RTClib (Adafruit): https://github.com/adafruit/RTClib
- LiquidCrystal I2C (thư viện được khai báo trong `src/libraries.txt`)

## Phạm vi

Đây là mô hình học tập chạy trên trình mô phỏng, không phải thiết bị y tế đã được chứng nhận và không thay thế hướng dẫn dùng thuốc của nhân viên y tế.
