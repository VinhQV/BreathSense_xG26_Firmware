# BreathSense BLE Relay — xG26 Explorer Kit

Firmware riêng cho **BRD2709A / EFR32MG26B510F3200IM48**.
Không nạp firmware này lên kit BreathSense BRD2608A.

```text
BreathSense BRD2608A (mic + AI + cảm biến)
    -- BLE --> BRD2709A relay (BSRelay_01)
    -- BLE --> BGM220 NCP + Raspberry Pi
    --> backend SQLite / dashboard hiện có
```

Relay là BLE Central đối với BreathSense và BLE Peripheral đối với Pi.
Đây là cầu nối dữ liệu ứng dụng một chặng; không phải bộ khuếch đại RF,
Bluetooth Mesh hay cơ chế tự chuyển tuyến trực tiếp/relay.
Silicon Labs hỗ trợ đồng thời hai vai trò:
https://docs.silabs.com/bluetooth/3.1/bluetooth-general-connections/central-and-peripheral-roles

## Đã làm

- Cập nhật đồng bộ giờ: Pi ghi UTC vào relay; relay giữ đồng hồ qua mất kết nối
  và gửi giờ hiện tại xuống node khi nối lại. Gói ho giữ giờ bắt đầu ở node.
  Xem [TIME_SYNC.md](TIME_SYNC.md). Bản mới đã build/test trên máy; chưa nạp
  hay kiểm chứng trên phần cứng.

- Quét chủ động, chọn đúng tên `MyDevice_01` hoặc MAC cấu hình.
- Khóa một địa chỉ nguồn cho tới khi relay khởi động lại; không nối chuỗi relay.
- Tự kết nối lại nguồn; giới hạn chờ kết nối và từng thủ tục GATT.
- Chuyển nguyên gói cough 8 byte và environment 4 byte, giữ UUID hiện có.
- Đệm 64 sự kiện ho trong RAM khi Pi mất kết nối. Đầy thì bỏ gói mới,
  tăng bộ đếm dropped và giữ nguyên gói đang chờ xác nhận.
- Pi đã cập nhật dùng ATT indication: chỉ bỏ đầu hàng đợi sau xác nhận.
  Bộ nhận cũ vẫn có thể dùng notification, nhưng không có xác nhận này.
- Đọc định kỳ trạng thái đeo/điện áp và AI; báo lỗi đọc nếu nguồn mất kết nối
  hoặc mẫu quá 10 giây, không trình bày dữ liệu cũ như dữ liệu hiện tại.
- Trạng thái hai chặng, RSSI và bộ đếm được đọc/notify qua BLE.
- Bản vá Pi giữ MAC của BreathSense làm device_id, giữ phân công người dùng
  trên dashboard; phát hiện và từ chối hai tuyến trùng một nguồn.
- LED0: nguồn đã đăng ký nhận dữ liệu. LED1: gateway đã đăng ký nhận ho.
- VCOM: 115200, 8N1, không flow control. `pi=1` trong log nghĩa là một BLE
  client đã kết nối (có thể là máy tính thử nghiệm, không xác thực danh tính Pi).

## Tình trạng kiểm chứng ngày 12/11/2026

- Build ARM thành công; đã nạp và verify trên J-Link **440364792 / COM5**.
- Đã đọc BLE thật từ máy tính: tên BSRelay_01, MAC **64:02:8F:64:12:88**,
  trạng thái chặng nguồn chưa nối và chặng gateway đã nối, bật indication được.
- Hai bộ test C qua: hàng đợi/parser và event handler BLE thực với radio mock.
- 23 test Python gateway qua (11 test có sẵn + 12 test relay).
- Bản vá Pi đã thử áp dụng sạch trên đúng commit gốc và đối chiếu nội dung.
- **Đã kiểm chứng với nguồn thật** C0:2C:ED:90:AD:BA: nhiệt độ/độ ẩm đi qua
  hai chặng tới máy tính; đọc được wear=1 và capture=1; nhận sự kiện ho
  counter=1 (WET theo AI) và counter=2 (UNKNOWN theo AI). Đầu phiên có một
  gói đang đệm, sau khi nhận bằng indication hàng đợi về 0. Cuối phiên:
  received=3, delivered=3, dropped=0, invalid=0.
- Đã kiểm tra ngắt/kết nối lại máy tính nhận; Long Range cần firmware node tương ứng.
- **Đã kiểm chứng** nguồn ngủ và nối lại Coded khi thức; chưa đo lại khoảng cách thực tế.
  Phần Pi được người dùng yêu cầu để sau; bộ nhận trong bài test là PC, không phải Pi.
- Hash firmware và các giới hạn kiểm chứng nằm trong `validation.json`.

## Chạy thử với hai kit

1. Cấp nguồn relay bằng USB. Relay không cần dây dữ liệu nối với Pi.
2. Bật kit BreathSense. Ngắt kết nối BLE trực tiếp từ điện thoại/Pi tới kit
   này để relay có thể kết nối. Dùng firmware node Long Range; cần đeo để bật BLE.
3. Đặt hai kit gần nhau lần đầu. Chờ LED0 sáng và log:
   `RELAY: source ready; advertising BSRelay_01 for Pi`.
4. Cài bản vá gateway bên dưới. Cho Pi kết nối **BSRelay_01**.
5. Tạo sự kiện thật; kiểm tra counter ho ở node, received/delivered ở relay
   và dữ liệu trên Pi. Tắt Pi tạm thời để thử bộ đệm, bật lại để nhận bù.
6. Di chuyển relay tới nơi vẫn nhận tốt cả hai chặng; so sánh thử trực tiếp
   và qua relay cùng vị trí/tư thế/điều kiện. Không suy ra số mét chỉ từ RSSI.

Nếu có nhiều kit cùng tên, sửa `RELAY_SOURCE_ADDRESS` trong `relay_config.h`
thành MAC thật, build/nạp lại. Bản hiện tại ghim MAC C0:2C:ED:90:AD:BA; sửa cấu hình nếu đổi kit nguồn. Đây không phải cơ chế xác thực bảo mật.

## Cập nhật bộ nhận BGM220/Pi

Đã sửa bản local của repo `NguyenDangDoanh/pybgapi-examples` trong
`.codex_tmp/pybgapi-examples`. Chưa triển khai lên Pi và chưa push repository.

`gateway-relay.patch` áp dụng trên commit
`9d83c69c41ecdbe42a0e60ab40fd54dbae188beb`; nếu bản Pi có chỉnh sửa khác,
phải kiểm tra patch trước, không ghi đè toàn bộ repository.

Chép patch tới Pi, tại thư mục repo:

```bash
git apply --check /path/to/gateway-relay.patch
git apply /path/to/gateway-relay.patch
python -m unittest discover -s tests -v
```

Giữ backend/dashboard đang dùng. Với bài thử một node, dừng tiến trình nhận
BLE trực tiếp cũ rồi khởi động bộ nhận đã cập nhật bằng:

```bash
python gateway/ble_host_modular/main.py "$PORT" \
  --xapi api/sl_bt.xapi \
  --name-prefix BSRelay_01 \
  --max-connections 1 \
  --service-uuid b5e00001-7a4b-4c6d-9e10-112233445566 \
  --cough-uuid b5e00002-7a4b-4c6d-9e10-112233445566 \
  --environment-uuid b5e00003-7a4b-4c6d-9e10-112233445566 \
  --backend-socket /tmp/cough_gw.sock -l INFO
```

`PORT` là đường dẫn serial BGM220 đang dùng trên Pi. Không chuyển BGM220 sang
firmware relay. Nếu relay chưa tìm được nguồn, Pi sẽ báo chưa đọc được source
identity và thử lại; đây là trạng thái dự kiến, không được gán dữ liệu cho MAC relay.

## GATT

Các UUID dùng hậu tố `-7a4b-4c6d-9e10-112233445566`:

| Tiền tố | Dữ liệu | Quyền |
|---|---|---|
| b5e00001 | Service BreathSense | — |
| b5e00002 | Cough 8 byte, nguyên bản | Read / Notify / Indicate |
| b5e00003 | Environment 4 byte, nguyên bản | Read / Notify |
| b5e00004 | UTC uint32 LE, chuyển xuống node | Read / Write |
| b5e00005 | Pendant 6 byte, nguyên bản | Read |
| b5e00006 | AI 10 byte, nguyên bản | Read |
| b5e00007 | Relay status 20 byte | Read / Notify |
| b5e00008 | Source identity 8 byte | Read |

Source identity: version=1, address_type, 6 byte MAC theo thứ tự little-endian.
Không có nguồn đã chọn: ATT error 0x0E. Mẫu chưa có/đã cũ: cũng 0x0E.

Relay status (mọi số nhiều byte little-endian):

| Byte | Ý nghĩa |
|---|---|
| 0 | Version 1 |
| 1 | 0 tìm nguồn, 1 đang nối, 2 khám phá GATT, 3 nguồn sẵn sàng |
| 2 | bit0 nguồn có kết nối; bit1 nguồn sẵn sàng; bit2 gateway nối; bit3 dùng indication; bit4 pendant còn mới; bit5 AI còn mới; bit6 relay có UTC; bit7 node đã ACK đồng bộ |
| 3 | Số sự kiện đang đệm |
| 4–5 | RSSI nguồn/gateway int8, 127 là chưa có |
| 6–9 | Số gói ho hợp lệ đã nhận |
| 10–13 | Số gói đã gửi (indication: đã ACK; notification: mới enqueue vào stack) |
| 14–17 | Gói ho bị bỏ vì đầy hàng đợi |
| 18–19 | Gói sai độ dài |

## Build và nạp

Mở `BreathSense_Relay.slcp` với Simplicity SDK 2025.12.3, chọn BRD2709A và GCC.
Khi chuyển máy, regenerate CMake vì đường dẫn SDK/toolchain được tạo cho máy hiện tại.
Không cần model AI hoặc extension TensorFlow cho relay.

```powershell
# CMake đã được generate trên máy hiện tại
cmake --preset project -S relay/cmake_gcc
cmake --build relay/cmake_gcc/build --config base --target BreathSense_Relay
# Script từ chối nạp vào board không phải BRD2709A
./relay/tools/flash_relay.ps1 -SerialNumber 440364792
```

Firmware: `cmake_gcc/build/base/BreathSense_Relay.s37`.
Đã giữ LFXO theo cấu hình board mới và xác nhận boot hoạt động; không sao chép
cấu hình chân hoặc clock của kit BreathSense sang board này.

Test C bằng compiler máy tính:

```powershell
cmake -S relay/tests -B relay/tests/build -DSILABS_SDK_PATH="<duong-dan-Simplicity-SDK>"
cmake --build relay/tests/build --config Debug
ctest --test-dir relay/tests/build -C Debug --output-on-failure
```

Test BLE trực tiếp bằng PC (thư viện Python bleak, một gateway tại một thời điểm):

```powershell
python relay/tools/inspect_relay.py --seconds 30
```

## Giới hạn

- Một relay phục vụ một nguồn và một gateway; không tự chuyển trực tiếp/relay.
- Chỉ mở rộng vùng phủ nếu cả hai chặng đều đủ sóng. Chưa đo mức tăng khoảng cách.
- Hàng đợi RAM mất khi relay mất nguồn; 64 gói không phải lưu trữ vô hạn.
- Indication xác nhận ở tầng ATT, không bảo đảm bản ghi đã được ghi vào SQLite.
  Chặng node -> relay vẫn dùng notification của firmware node hiện tại.
  Có thể có bản gửi lặp sau mất kết nối; không cam kết exactly-once qua reboot.
- Node phải nhận UTC ít nhất một lần sau reset/mất nguồn. Cơn ho trước đó
  giữ giờ unknown. Sau đồng bộ, node đếm giờ dù mất BLE; gói đã lưu không đổi giờ.
- Chưa thêm bonding/mã hóa, OTA, ghi Flash hoặc thiết kế nhiều relay.
- Đọc trạng thái có cache; môi trường chỉ giữ mẫu mới nhất, không đệm mọi mẫu.
- Firmware mẫu Silicon Labs giữ giấy phép Zlib trong các file được sử dụng.

## Long Range v2

Relay scans 1M+Coded and pins source C0:2C:ED:90:AD:BA. Source link prefers Coded S=8 (PHY=4); gateway stays 1M. Both kits confirm +10 dBm maximum. Source deliberately disconnects on off-body sleep; relay retains queued events and waits for reconnect. See [power and range guide](../docs/long_range_sleep_vi.md).
