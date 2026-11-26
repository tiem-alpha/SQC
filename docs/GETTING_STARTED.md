# Hướng dẫn Bắt đầu Nhanh với `sqc` (Getting Started)

Tài liệu này hướng dẫn chi tiết cách build công cụ **`sqc`**, cấu hình `sqc.json`, sử dụng đầy đủ bộ API `<sqc_builtin.h>`, chạy 4 ví dụ thực tế và áp dụng `sqc` cho dự án Embedded C / RTOS.

---

## 1. Yêu cầu Hệ thống & Build nhanh với CMake + Ninja

### Yêu cầu môi trường (Linux / Ubuntu)
- **LLVM 18 & Clang 18**: `/usr/lib/llvm-18`, `clang-18` (hoặc `clang` phiên bản 18+)
- **Z3 SMT Solver** (`libz3-dev` với `z3++.h`)
- **CMake >= 3.20** và **Ninja**
- Trình biên dịch C++20 (`g++` hoặc `clang++`)

### Các lệnh Build
Từ thư mục gốc `sqc/`:

```bash
cmake -S . -B build -G Ninja
ninja -C build
```

Sau khi build thành công, thư mục `build/` sẽ chứa:
- `./build/sqc` — Công cụ dòng lệnh chính của `sqc`.
- `./build/test_engine_unit` — Bộ unit test kiểm thử core engine (`DisjointIntervals`, `LinearSymExpr`, `GraphEquivalenceCache`, `Z3Refiner`).

Chạy toàn bộ unit test và 4 ví dụ thực tế chỉ với một lệnh:

```bash
./tests/run_tests.sh
```

---

## 2. Cấu hình Dự án với `sqc.json`

Mỗi dự án hoặc ví dụ sử dụng một file cấu hình JSON (`sqc.json`) định nghĩa các thông số biên dịch chung (`common`) và danh sách các bài phân tích (`analyses`):

```json
{
  "common": {
    "prefix_path": ".",
    "machdep": "gcc_arm_eabi"
  },
  "analyses": [
    {
      "name": "modem_urc_analysis",
      "main": "sqc_main",
      "files": [
        "modem/src/*.c",
        "tests/sqc_driver.c"
      ],
      "cpp-extra-args": [
        "-Imodem/inc",
        "-DUSE_RTOS=1"
      ]
    }
  ]
}
```

### Ý nghĩa các trường cấu hình
- `common.prefix_path`: Đường dẫn gốc tương đối so với thư mục chứa `sqc.json` dùng để tìm file nguồn.
- `common.machdep`: Kiến trúc mục tiêu (ví dụ `gcc_arm_eabi`, `gcc_x86_64`).
- `analyses[].name`: Tên của analysis unit (hiển thị trong báo cáo CLI và HTML).
- `analyses[].main`: Tên hàm entry point (mặc định là `"sqc_main"`).
- `analyses[].files`: Danh sách file `.c` cần phân tích, hỗ trợ wildcard `*` (ví dụ `"src/*.c"`).
- `analyses[].cpp-extra-args`: Các cờ preprocessor/include (`-I...`, `-D...`) truyền cho Clang frontend.

---

## 3. Bảng Tra Cứu API `<sqc_builtin.h>`

Trong file driver kiểm thử (ví dụ `sqc_driver.c`), chỉ cần `#include <sqc_builtin.h>` để sử dụng toàn bộ các nguyên thủy symbolic của `sqc`:

| Nguyên thủy / Macro | Mô tả chức năng |
|---|---|
| `sqc_interval(min, max)` / `sqc_int_interval(min, max)` | Trả về số nguyên `int` symbolic nằm trong khoảng đóng `[min, max]`. |
| `sqc_long_long_interval(min, max)` | Trả về số nguyên `long long` symbolic trong `[min, max]`. |
| `sqc_unsigned_char_interval(min, max)` | Trả về `unsigned char` (`uint8_t`) symbolic trong `[min, max]`. |
| `sqc_unsigned_int_interval(min, max)` | Trả về `unsigned int` symbolic trong `[min, max]`. |
| `sqc_unsigned_long_interval(min, max)` | Trả về `unsigned long` symbolic trong `[min, max]`. |
| `sqc_interval_split(min, max)` | Tạo giá trị symbolic trong `[min, max]` với chế độ tách nhánh chính xác. |
| `sqc_make_unknown(ptr, size)` | Đánh dấu vùng nhớ `[ptr, ptr + size)` đã được khởi tạo với dữ liệu byte symbolic bất kỳ (phù hợp mô phỏng buffer DMA/UART). |
| `sqc_make_cstring(ptr, max_size)` | Khởi tạo chuỗi C symbolic có byte `'\0'` nằm trong `[0, max_size - 1]`. |
| `sqc_nondet(a, b)` | Lựa chọn bất định giữa hai giá trị số nguyên `a` hoặc `b`. |
| `sqc_nondet_ptr(p1, p2)` | Lựa chọn bất định giữa 2 con trỏ (ví dụ `sqc_nondet_ptr(&ctx, NULL)`). |
| `sqc_nondet3_ptr(p1, p2, p3)` / `sqc_nondet5_ptr(...)` | Lựa chọn bất định giữa 3 hoặc 5 con trỏ. |
| `sqc_find_variable("var_name", "file.c")` | Truy cập white-box lấy địa chỉ biến `static` nội bộ trong translation unit khác. |
| `SQC_OVERRIDE` | Annotation gắn lên hàm mock trong test driver để ghi đè hàm phần cứng/RTOS thực tế. |
| `sqc_assert(cond)` | Khẳng định điều kiện `cond` phải luôn đúng; báo lỗi `AssertionViolation` nếu có nhánh vi phạm. |
| `sqc_assume(cond)` | Thu hẹp trạng thái symbolic theo giả thiết `cond` (bỏ qua nhánh không thỏa mãn). |
| `sqc_ub(msg, trigger)` | Khai báo điểm kiểm tra Undefined Behavior tùy chỉnh kèm thông báo `msg`. |
| `sqc_requires_valid(ptr, byte_len)` / `sqc_requires_valid_read(ptr, byte_len)` | Kiểm tra tiền điều kiện vùng nhớ `[ptr, ptr + byte_len)` phải hợp lệ và không NULL. |

### Ví dụ minh họa sử dụng `<sqc_builtin.h>`

```c
#include <sqc_builtin.h>
#include <stdint.h>

/* Ghi đè hàm đọc UART DMA bằng mock symbolic */
SQC_OVERRIDE int uart_dma_read(char *buf, size_t max_len) {
    sqc_requires_valid(buf, max_len);
    sqc_make_unknown(buf, max_len);
    return sqc_interval(-1, (int)max_len);
}

int sqc_main(void) {
    int local_cfg = sqc_interval(0, 100);
    int *cfg_ptr = (int *)sqc_nondet_ptr(&local_cfg, NULL);

    int rc = process_device(cfg_ptr);

    /* Kiểm tra biến static bên trong device.c */
    uint32_t *err_cnt = (uint32_t *)sqc_find_variable("g_error_count", "device.c");
    if (err_cnt != NULL) {
        sqc_assert(*err_cnt == 0);
    }
    return rc;
}
```

---

## 4. Chạy 4 Ví Dụ Thực Tế & Xem Báo Cáo HTML

`sqc` đi kèm 4 ví dụ hoàn chỉnh trong thư mục `examples/`:

### Ví dụ 1: `01_basic_hazards` — Hazard-Guided Symbolic Partitioning
Phát hiện 4 lỗi UB kinh điển (`Null Pointer Dereference`, `Signed Integer Overflow`, `Division By Zero` khi `*a == 15`, và `Out-of-Bounds Array Access`) chỉ trong 1 lượt phân tích:
```bash
./build/sqc run -c examples/01_basic_hazards/sqc.json -o examples/01_basic_hazards/report.html
```

### Ví dụ 2: `02_graph_equivalence` — Graph Behavioral Equivalence & Remainder Slicing
Minh họa `GraphEquivalenceCache` học miền tương đương $\mathcal{R}_{eq} = [1, \text{INT\_MAX}]$ từ nhánh `[10, 50]`, đạt **Full Cache Hit** trên nhánh rời nhau `[100, 500]`, và cắt phần dư `[-20, 0]` trên nhánh `[-20, 300]` để bắt lỗi chia 0 tại `packet_len == -5`:
```bash
./build/sqc run -c examples/02_graph_equivalence/sqc.json -o examples/02_graph_equivalence/report.html
```

### Ví dụ 3: `03_subcondition_coverage` — Sub-Condition Coverage (`||`, `&&` trên `if`/`while`)
Minh họa bảng màu 6 trạng thái: dòng `if (status == 1 || status == 99)` và `while (count < 4 && hw_ready)` được tô **Vàng (`COVERED_PARTIAL`)** do thiếu chiều của một điều kiện con, trong khi thân lệnh bên trong vẫn tô **Xanh lá (`COVERED_SAFE`)**, còn nhánh/hàm không thể đạt tới tô **Đỏ (`UNCOVERED`)**:
```bash
./build/sqc run -c examples/03_subcondition_coverage/sqc.json -o examples/03_subcondition_coverage/report.html
```

### Ví dụ 4: `04_rtos_uart_dma_mock` — Mô phỏng Embedded Cellular / UART DMA + RTOS
Minh họa cấu trúc dự án thực tế gồm mã ứng dụng (`rtos_dma_app.c`) và test driver (`sqc_driver.c`) sử dụng `SQC_OVERRIDE`, `sqc_make_unknown`, `sqc_nondet_ptr`, `sqc_find_variable`, và gọi trực tiếp `sscanf("+CSQ: %d,%d", ...)` thông qua built-in $O(1)$ summary của `sqc` (không cần file stub):
```bash
./build/sqc run -c examples/04_rtos_uart_dma_mock/sqc.json -o examples/04_rtos_uart_dma_mock/report.html
```

### Cách mở xem Báo cáo HTML Tương tác
Mỗi file `report.html` (hoặc `sqc_report.html`) là một trang HTML độc lập (self-contained) không cần web server. Mở trực tiếp bằng trình duyệt:

```bash
xdg-open examples/01_basic_hazards/report.html
```

Bên trong báo cáo gồm 4 phần chính:
1. **Analyses Summary & Cache Metrics**: Thời gian chạy, số lệnh IR phân tích, số lần **Equivalence Cache Hit** và **Remainder Slice**.
2. **Hazards / Undefined Behaviors Table**: Danh sách lỗi kèm vị trí file:dòng, biểu thức lỗi $\Phi_{\text{err}}$, Callstack và **Concrete Witness**.
3. **File & Function LoC Coverage Table**: Bảng tỷ lệ `%` dòng thực thi và tỷ lệ điều kiện con của từng file và từng hàm.
4. **Interactive Source Viewer**: Trình xem mã nguồn tô đủ 6 màu sắc kèm chú thích chi tiết cho từng điều kiện con bị thiếu (`Never True` / `Never False`).

---

## 5. Chuyển Đổi Dự Án Cũ (`sqc migrate`) & Áp Dụng cho Dự Án RTOS (`cmcell`)

### Tự động chuyển đổi driver cũ sang chuẩn `sqc`
Nếu bạn đã có sẵn các file driver kiểm thử cũ, `sqc` cung cấp lệnh kiểm tra và chuẩn hóa:

```bash
./build/sqc migrate /path/to/your_project/
```

Lệnh này tự động quét đệ quy các file `.c`, `.h`, `.json` để chuẩn hóa về `<sqc_builtin.h>`, `sqc_main`, `sqc_interval`, `sqc_make_unknown`, `sqc_nondet_ptr`, `sqc_find_variable`, `sqc_override`, `sqc_ub`...

### Áp dụng nhanh cho dự án RTOS thực tế (như `cmcell`)
1. **Giữ nguyên mã nguồn firmware** (`modem/`, `com/`, `cellular/`), không cần sửa code sản phẩm.
2. **Tạo file driver `sqc_driver.c`** mock các lớp phần cứng/RTOS (`uart_dma`, `rtos_queue`, `rtos_timer`, `mutex`) bằng `SQC_OVERRIDE` và cung cấp dữ liệu đầu vào bằng `sqc_make_unknown` / `sqc_interval`.
3. **Không cần viết stub thủ công cho `sscanf`, `strlen`, `memcpy`, `snprintf`**: `sqc` tự động dùng built-in $O(1)$ summaries.
4. **Chạy toàn bộ các analysis units và gộp báo cáo**:
   ```bash
   ./build/sqc run -c /path/to/cmcell/sqc.json -o cmcell_report.html
   ```
   Báo cáo `cmcell_report.html` sẽ hợp nhất toàn bộ các lỗi UB và tính tổng **Consolidated File & Function Coverage %** trên toàn dự án.
