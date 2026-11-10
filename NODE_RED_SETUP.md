# Bảng điều khiển cảnh báo camera qua MQTT

ESP32 gửi cảnh báo được xác thực bằng AES-256-GCM đến HiveMQ qua TLS.
Dữ liệu cảnh báo đã mã hóa gồm nhãn phát hiện, độ tin cậy, thời gian, trạng
thái pin và ảnh JPEG được mã hóa Base64. Node-RED giải mã dữ liệu và hiển thị
tối đa mười cảnh báo gần nhất.

## Cấu hình thiết bị

1. Sao chép `include/app_secrets.example.h` thành `include/app_secrets.h`.
2. Điền tên mạng Wi-Fi 2,4 GHz và mật khẩu vào tệp vừa sao chép.
3. Tạo khóa ngẫu nhiên 32 byte dưới dạng 64 ký tự thập lục phân, ví dụ:
   `python -c "import secrets; print(secrets.token_hex(32))"`
4. Điền khóa vào `SMARTCAM_AES_KEY_HEX`. Giữ tệp này ở máy cá nhân; Git đã
   được cấu hình bỏ qua tệp. Dùng cùng khóa cho biến môi trường Node-RED bên
   dưới.
5. Biên dịch và nạp chương trình. Cửa sổ Serial sẽ hiển thị địa chỉ IP của
   camera và chủ đề MQTT cảnh báo.

Không sử dụng khóa ví dụ làm khóa thật và không đăng khóa AES lên luồng,
kho mã nguồn hoặc tin nhắn MQTT công khai.

## Cấu hình Node-RED

1. Cài gói `node-red-dashboard` nếu chưa có bảng điều khiển Dashboard cổ điển.
2. Trong `settings.js` của Node-RED, bật các mô-đun ngoài cho nút Function:
   `functionExternalModules: true,`
3. Đặt biến môi trường `SMARTCAM_AES_KEY` của tiến trình Node-RED thành khóa
   hex 64 ký tự giống khóa trong `include/app_secrets.h`, sau đó khởi động lại
   Node-RED.
4. Nhập `node-red-flow.json` và triển khai. Mở bảng điều khiển tại
   `http://localhost:1880/ui`.

Trong dashboard, nhập IP của camera (in trong Serial Monitor) vào ô “Xem
camera trực tiếp”, theo dạng `http://192.168.1.25`. Luồng chỉ hiển thị khi
camera đang bật và thiết bị xem cùng mạng Wi-Fi.

Luồng đăng ký nhận tin từ `smartcam/+/alerts` trên HiveMQ công cộng và xác thực
thẻ GCM trước khi hiển thị ảnh cùng thông tin cảnh báo. Firmware chụp và suy
luận liên tục khi hoạt động. Để xem trực tiếp, dùng điện thoại hoặc máy tính
cùng mạng Wi-Fi, mở `http://<địa-chỉ-IP-của-camera>/` (địa chỉ được in trong
Serial Monitor). Trang camera lấy luồng hình ảnh từ `/stream`.

Trường pin được để là `N/A` trong dữ liệu vì thiết bị hiện dùng nguồn USB và
chưa có mạch đo điện áp pin.

Nếu cảnh báo không đến Node-RED, kiểm tra Serial Monitor để biết firmware đã
đồng bộ giờ mạng, đã cấu hình cùng khóa AES trong `app_secrets.h` và đã báo
“Đã gửi cảnh báo mã hóa”. Cảnh báo bị lỗi gửi sẽ được thử lại; sau khi gửi
thành công, các cảnh báo tiếp theo được giới hạn bởi thời gian chờ 10 giây.

## Ghi chú về ESP-NN

Cấu hình PlatformIO bật các nhân ESP-NN của Edge Impulse cho ESP32. Siêu dữ
liệu mô hình hiện tại cho biết mô hình chưa lượng tử hóa và sử dụng số thực;
các nhân tích chập tối ưu của ESP-NN chủ yếu tăng tốc những phép toán lượng
tử hóa được hỗ trợ. Để đánh giá mức tăng tốc thực tế, hãy xuất mô hình int8
tương thích ESP-NN từ Edge Impulse rồi kiểm tra độ chính xác bằng ảnh chụp
thực tế từ camera.
