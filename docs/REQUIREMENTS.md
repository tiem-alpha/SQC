# Đặc tả Yêu cầu Hệ thống (Software Requirements Specification — `sqc`)

Tài liệu này mô tả đầy đủ các **Yêu cầu Chức năng (Functional Requirements)**, **Yêu cầu Phi chức năng (Non-Functional Requirements)** và **Tiêu chí Nghiệm thu (Acceptance Criteria)** cho công cụ phân tích tĩnh **`sqc`** (Static Quality & Hazard Analyzer cho C / Embedded C / RTOS).

---

## 1. Phạm vi & Mục tiêu Sản phẩm
- Phân tích mã nguồn C / Embedded C / RTOS (như `cmcell`) ở mức tĩnh bằng cách thiết lập tập đầu vào symbolic cho hàm entry (`sqc_main`) và mock các hàm ngoại vi/RTOS.
- Phát hiện sớm các lỗi **Undefined Behavior (UB)** và lỗi runtime có thể gây crash hoặc rò rỉ bộ nhớ, kèm **Concrete Input Witness** (giá trị đầu vào cụ thể tái hiện lỗi) và **Callstack** đầy đủ.
- Nâng cấp vượt trội về **tốc độ phân tích**, **tính dễ dùng (Zero `slevel` tuning)** và **độ trực quan của báo cáo HTML** so với các công cụ phân tích tĩnh truyền thống.

---

## 2. Yêu cầu Chức năng (Functional Requirements)

| Mã | Nhóm tính năng | Yêu cầu Chi tiết | Trạng thái |
|---|---|---|---|
| **FR-1** | **Phát hiện UB & Lỗi Runtime** | Phát hiện đầy đủ: `NullPointerDeref`, `OutOfBoundsAccess` (array/buffer/stack/heap), `DivisionByZero`, `SignedIntegerOverflow`, `InvalidBitShift`, `UninitializedRead`, `UseAfterFree`, `DoubleFree`, `InvalidFree`, `MemoryLeak`, `StringNotNullTerminated`, `AssertionViolation`, `CustomUndefinedBehavior`. | ✅ Đã triển khai |
| **FR-2** | **Hệ thống Nguyên thủy tiền tố `sqc_`** | Cung cấp header chuẩn `<sqc_builtin.h>` với toàn bộ tiền tố `sqc_`: `sqc_main`, `sqc_interval`, `sqc_int_interval`, `sqc_unsigned_char_interval`, `sqc_unsigned_long_interval`, `sqc_interval_split`, `sqc_make_unknown`, `sqc_make_cstring`, `sqc_nondet`, `sqc_nondet_ptr`, `sqc_nondet3_ptr`, `sqc_nondet5_ptr`, `sqc_find_variable`, `SQC_OVERRIDE`, `sqc_assert`, `sqc_assume`, `sqc_ub`, `sqc_requires_valid`. | ✅ Đã triển khai |
| **FR-3** | **Mock Ngoại vi & RTOS** | Hỗ trợ ghi đè hàm phần cứng/RTOS (`SQC_OVERRIDE` + weak symbol resolution trên LLVM IR), truy cập white-box biến `static` (`sqc_find_variable`), mô phỏng UART DMA circular buffer, RTOS Queue, Timer callback và vòng lặp task. | ✅ Đã triển khai |
| **FR-4** | **Built-in $O(1)$ Libc Semantic Summaries** | Tích hợp sẵn mô hình ngữ nghĩa $O(1)$ cho `sscanf`, `strlen`, `strcpy`, `strncpy`, `strcmp`, `strncmp`, `memcmp`, `memcpy`, `memmove`, `memset`, `snprintf`, `sprintf`, `strtol`, `strtoul`, `malloc`, `calloc`, `free` — không bắt người dùng viết stub thủ công hàng trăm dòng. | ✅ Đã triển khai |
| **FR-5** | **Đo Độ phủ LoC & Điều kiện Con (`&&`, `||`)** | - Tính tỷ lệ `%` Line-of-Code (LoC) Coverage theo **từng File**, **từng Hàm trong File**, và **hợp nhất toàn dự án**.<br/>- Theo dõi mặt nạ 2-bit (`seen_true`, `seen_false`) cho từng điều kiện con trong `if`, `while`, `for`, `?:` chứa `&&` hoặc `\|\|`: nếu có một vế điều kiện không bao giờ xảy ra thì dòng `if`/`while` được tô **Vàng (`COVERED_PARTIAL`)**, còn các lệnh bên trong thân nhánh vẫn được phân tích và tô **Xanh lá (`COVERED_SAFE`)**. | ✅ Đã triển khai |
| **FR-6** | **Báo cáo HTML Nền Trắng Tách Bạch 5 Trang** | Xuất file HTML độc lập (self-contained) với **giao diện nền trắng (Light Theme)** tách bạch 5 trang:<br/>1. **`Overview`**: Tổng quan KPI & điều hướng nhanh.<br/>2. **`Alarms & Callstack`**: Danh sách lỗi chi tiết kèm Predicate, Concrete Witness, Callstack từng bước và đoạn code lỗi.<br/>3. **`Coverage (Files & Functions)`**: Bảng tỷ lệ `%` LoC & Sub-Condition theo từng File và từng Hàm.<br/>4. **`Source Viewer (Line-by-Line)`**: Xem mã nguồn từng dòng với 6 trạng thái màu sắc.<br/>5. **`Analyses & Metrics`**: Thống kê thời gian chạy, Cache Hit Rate, Z3 queries. | ✅ Đã triển khai |
| **FR-7** | **Tương thích & Chuyển đổi Dự án Cũ** | Hỗ trợ đọc file cấu hình `sqc.json` / `config.json`, cung cấp lệnh `sqc migrate <dir>` tự động chuyển đổi driver cũ sang chuẩn `sqc_*`, và hỗ trợ xuất thêm `SARIF 2.1.0`. | ✅ Đã triển khai |

---

## 3. Yêu cầu Phi chức năng (Non-Functional Requirements)

| Mã | Tiêu chí | Yêu cầu Cụ thể |
|---|---|---|
| **NFR-1** | **Tốc độ Phân tích** | Không duyệt vét cạn từng giá trị trong dải; xử lý trên `DisjointIntervals` ($O(1)$), tái sử dụng kết quả qua `GraphEquivalenceCache` (nhánh có tập input tương đồng rơi vào cùng trường hợp phân tích chỉ duyệt 1 lần) và `ModRef ReadSet` framing. |
| **NFR-2** | **Tính Dễ Dùng (Zero-Tuning)** | Không yêu cầu người dùng tinh chỉnh tay `slevel`, `val-split-return`, hay `merge-after-loop`. Build bằng CMake + Ninja thành 1 binary native duy nhất `./build/sqc`. |
| **NFR-3** | **Độ Chính Xác (Low False Positives)** | Kết hợp `Z3Refiner` kiểm chứng tính khả thi đường đi, thu hẹp con trỏ theo `null_term_offset` (tránh false alarm trên vòng lặp chuỗi `for (; *c; ++c)`), và giới hạn Loop Widening đúng trên back-edge `PHINode`. |
| **NFR-4** | **Tính Mở Rộng & Bảo Trì Lâu Dài** | Viết bằng **Modern C++20** trên nền **LLVM 18 + Z3** (100% permissive license, kiến trúc module hóa rõ ràng giữa Frontend, Domains, Equivalence, Summaries, SMT, Coverage và Report). |

---

## 4. Tiêu chí Nghiệm thu (Acceptance Criteria)
1. Biên dịch thành công 100% (`0 errors`, `0 warnings`) cho `sqc` và `test_engine_unit`.
2. Đạt **47/47 PASS** trên bộ C++ Unit Test (`test_engine_unit`) kiểm chứng `DisjointIntervals`, `LinearSymExpr`, `GraphEquivalenceCache` (Full Hit + Remainder Slicing) và `Z3Refiner`.
3. Chạy thành công **4 bộ Examples** (`01_basic_hazards`, `02_graph_equivalence`, `03_subcondition_coverage`, `04_rtos_uart_dma_mock`) và xuất báo cáo HTML 5 trang nền trắng hợp lệ.
4. Chạy thực tế trên các dự án Embedded C / RTOS đạt tỷ lệ cảnh báo giả cực thấp và đo chính xác độ phủ LoC + Sub-Condition cho từng file & hàm.
