# Giữ đúng thời điểm ho khi mất kết nối

Ví dụ (giờ Việt Nam): node đã nhận giờ chuẩn trước 10:00; bắt đầu nhận diện
cơn ho lúc 10:00, mất BLE và gửi bù lúc 10:05. Pi lưu event_ts=10:00,
received_ts=10:05. Mốc ho là lúc thuật toán xác nhận bắt đầu cơn ho, có
độ trễ do cửa sổ âm thanh và xử lý AI; không cam kết chính xác mili giây.

## Luồng đồng bộ

Pi ghi UTC uint32 little-endian vào Time Sync sau khi đăng ký thông báo.
Relay giữ UTC bằng bộ đếm 64-bit và gửi giờ hiện tại tới node. Node xác nhận
ghi rồi mới gửi hàng đợi. Relay chuyển nguyên gói, không sửa timestamp.
Pi kiểm tra đọc lại đồng hồ peer và làm mới mỗi giờ.

Khi peer là relay, đọc lại chỉ xác minh relay; status bit7 xác nhận node
đã ACK. Bit6 cho biết relay có UTC. Relay dùng đồng hồ đang tiếp tục chạy
để đồng bộ lại node ngay cả khi Pi vắng mặt.

UUID Time Sync có tiền tố b5e00004, cùng hậu tố với service BreathSense.
Firmware node và relay đều cần cập nhật; gateway cũng cần gửi UTC.

## Giới hạn

- Sau reset/mất nguồn phải đồng bộ ít nhất một lần. Cơn ho trước đó giữ
  giờ unknown, không tự gán giờ kết nối. Đồng hồ và hàng đợi nằm trong RAM.
- Node và relay mỗi nơi đệm tối đa 64 sự kiện; đầy có thể mất sự kiện.
- Pi phải có giờ hệ thống đúng. Kiểm tra khoảng năm chỉ từ chối giờ rõ ràng
  sai, không chứng minh đồng hồ Pi chính xác. Nên bật đồng bộ NTP trên Pi.
- Dao động đồng hồ và độ trễ BLE vẫn gây sai số; chưa xác nhận độ chính xác
  trên phần cứng. Không cộng UTC+7 vào timestamp; chỉ đổi múi giờ khi hiển thị.
- Gói chưa đồng bộ có event_ts=null, received_ts riêng, timestamp_source
  là unknown_unsynchronized. Dashboard phải hiển thị “Chưa xác định giờ ho”.
- Dữ liệu lịch sử cũ không thể phục hồi giờ ho nếu chỉ lưu giờ nhận trước đây.

## Cập nhật Pi

gateway-relay-time-sync.patch là bản vá cộng dồn trên commit
9d83c69c41ecdbe42a0e60ab40fd54dbae188beb, thay cho patch relay cũ.
Không áp dụng cả hai patch lên cùng bản gốc. Nếu Pi có sửa riêng, đối chiếu
và ghép thay đổi. Dừng bộ nhận/backend và sao lưu SQLite trước khi triển khai.

    git apply --check /path/to/gateway-relay-time-sync.patch
    git apply /path/to/gateway-relay-time-sync.patch
    python -m unittest discover -s tests -v

Bộ nhận BGM220 giữ lệnh chạy trong readme. Để thử với Bluetooth trực tiếp:

    python tools/pi_ble_receiver.py --name BSRelay_01

Hoặc dùng công cụ kiểm tra relay, chủ động cho phép ghi UTC của máy thử:

    python relay/tools/inspect_relay.py --sync-time --seconds 60

Không chạy hai bộ nhận tranh cùng một kết nối.

## Kiểm thử thiết bị còn cần thực hiện

1. Nạp đúng firmware node/relay; cập nhật bộ nhận và backend Pi.
2. Chờ log node “BLE: time synchronized” và relay “source time synchronized”.
3. Ngắt chặng node–relay, tạo cơn ho và ghi giờ thực tế; giữ nguồn node.
4. Nối lại sau 5 phút. event_ts phải là giờ ho, received_ts là giờ nhận.
5. Lặp lại khi chỉ ngắt Pi–relay để kiểm tra hàng đợi relay.
6. Reset node khi chưa có nguồn giờ: cơn ho phải báo unknown.

Build ARM và kiểm thử mô phỏng đã qua trên máy. Chưa nạp bản cập nhật này
hay xác nhận end-to-end trên hai kit và Pi. Không dùng validation.json của
bản cũ làm bằng chứng kiểm thử phần cứng cho bản mới.
