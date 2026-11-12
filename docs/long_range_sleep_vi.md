# Long Range và tiết kiệm pin khi tháo

| Thiết bị | Board / MCU | Probe | Serial |
|---|---|---|---|
| BreathSense | BRD2608A / IM68 | 440345913 | COM14 |
| Relay | BRD2709A / IM48 | 440364792 | COM5 |

Hai firmware riêng, không nạp nhầm kit. Serial 115200, 8N1.

## Hoạt động

- Node quảng bá Coded S=8 trên cả primary/secondary PHY; relay quét 1M+Coded và ưu tiên kết nối S=8. Chặng relay–Pi/PC vẫn 1M. Cả hai kit xác nhận TX tối đa +10 dBm.
- Khởi động với mic, AI capture, RHT và BLE chưa hoạt động. Hai cửa sổ chuyển động không nằm phẳng (~4 giây) xác nhận đeo. Bốn cửa sổ nằm phẳng/yên (~8 giây) xác nhận tháo.
- UNKNOWN giữ quyết định nguồn gần nhất. Sau tháo, UNKNOWN/rung nhẹ không bật lại ứng dụng. Khi đang đeo, ngồi yên/UNKNOWN vẫn giữ theo dõi.
- Khi tháo: dừng mic/I2S/DMA, suy luận AI, đo RHT và ADC định kỳ, LED/PWM; cắt nguồn mic/RHT, dừng BLE advertising và kết nối. Các tác vụ chờ cờ; RAM được giữ khi còn nguồn.
- IMU giữ accelerometer LP 12.5 Hz, gyro/temp tắt, ngưỡng WOM khoảng 94 mg, ngắt PB1. MCU vào EM2. Chuyển động chỉ mở một đợt kiểm tra IMU tối đa khoảng 10 giây; phải xác nhận wear=1 mới bật ứng dụng.
- SPI được dừng trước EM2 và khởi tạo lại khi thức. Đã sửa cách xóa INT_ASYNC_RESET của IMU. UART không giữ EM1 để nhận dữ liệu khi ngủ.
- Khi thức, mic lấy cửa sổ âm thanh mới. Mô hình nằm trong RAM nhưng không suy luận lúc nghỉ. Ngủ ở đây là khi tháo kit; người vẫn đeo nhưng ngủ không phải yêu cầu tắt thiết bị.

Không tắt hoàn toàn IMU vì sẽ mất khả năng tự thức khi đeo. IMU chỉ ước lượng trạng thái đeo qua chuyển động/tư thế, không chứng minh tiếp xúc cơ thể.

## Đọc log và thử khoảng cách

- `source active PHY=4`: chặng nguồn Coded S=8; `Pi active PHY=1`: chặng về gateway 1M.
- `POWER: SLEEP armed`: đã yêu cầu ngủ; các tác vụ hoàn tất dừng sau đó. Bộ đếm `breathsense_em2_entries` đếm chuyển trạng thái EM2 thật.
- `motion wake ... apps stay OFF`: kiểm tra đeo, mic/AI chưa bật. `applications ON (confirmed wear=1)` mới bật lại.
- Relay `reason=0x1008`: timeout, có thể ngoài vùng phủ hoặc mất nguồn. `0x1013`: nguồn chủ động ngắt, đã quan sát lúc ngủ; mã này riêng lẻ không chứng minh tháo kit.
- `rssi=127` là chưa có số đo hợp lệ. Node ngủ không phát BLE nên không đọc trực tiếp pin/trạng thái mới được. Relay báo nguồn offline, giữ đệm, không dùng mẫu cũ như số đo hiện tại.

Đeo node, xác nhận PHY=4 rồi đi cùng tuyến từng mất kết nối ở khoảng 25 m. Giữ vị trí relay, ghi khoảng cách/vật cản/RSSI/sự kiện nhận được từng mốc. Khi mất sóng, quay lại và ghi thời gian tự nối. Lặp ba lượt cùng tư thế; không suy ra số mét chỉ từ RSSI. Điện thoại không hỗ trợ quét Coded cần kết nối qua relay.

## Kiểm chứng và giới hạn

Đã build/nạp/verify đúng hai kit, xác nhận S=8 và +10 dBm trên phần cứng, truyền nhiệt độ/độ ẩm thật qua hai chặng tới PC. Đã quan sát node vào EM2, không log/đo định kỳ lúc yên.

Unit test kiểm tra chính sách đeo, UNKNOWN, ngưỡng thời gian, bật/tắt radio, kết nối đến đúng lúc ngủ và tín hiệu điều khiển gộp. Bench trên phần cứng kiểm tra hai chu kỳ bật–tắt mic/AI/RHT/BLE, lấy dữ liệu thật và nối lại Coded. Bench dùng cửa sổ đeo giả lập và ngắt GPIO kích qua debugger, **không chứng minh độ chính xác đeo trên cổ hay ngưỡng WOM thực tế**. Bản cuối đã bỏ mã giả lập và nạp lại.

Còn cần thử đeo/tháo thật với người dùng, đo dòng và pin cúc áo, đo lại khoảng cách. Chưa tích hợp Pi trong lượt này. Chưa có số liệu để khẳng định số ngày dùng pin hoặc số mét tối đa. RAM queue mất khi mất nguồn.

## Nâng cấp tiếp theo đáng làm

1. Thời gian thật của từng sự kiện, session ID sau reboot, số thứ tự, ACK đầu cuối và lưu bền để gửi bù. Gói ho hiện vẫn có timestamp=0; không trình bày như giờ ho chính xác.
2. Báo cáo số ho trên mỗi giờ thực sự đeo, xu hướng theo vận động và môi trường, hiển thị UNKNOWN/độ tin cậy. RHT đo môi trường, không phải nhiệt độ cơ thể hay công cụ chẩn đoán.
3. Tự kiểm tra chất lượng mic, bão hòa/âm quá nhỏ, IMU lỗi, pin yếu, PHY/RSSI, số gói rơi và thời gian offline. Demo bằng số đo thực tế.
4. Ghép cặp/xác thực nguồn, cập nhật firmware có chữ ký và rollback; hiệu chuẩn, đánh giá nhiều người và tư thế.

Các mục này là đề xuất, chưa cài thêm trong lượt Long Range/tiết kiệm pin.

Tài liệu: [Silicon Labs Coded PHY](https://docs.silabs.com/bluetooth/latest/bluetooth-fundamentals-connections/coded-phy), [Extended advertiser](https://docs.silabs.com/bluetooth/latest/bluetooth-stack-api/sl-bt-extended-advertiser).
