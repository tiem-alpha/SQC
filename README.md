# `sqc` — Static Quality & Hazard Analyzer cho C / Embedded C / RTOS

**`sqc`** là công cụ phân tích tĩnh và kiểm chứng hình thức (Static Analysis & Formal Verification) viết bằng **C++20 + LLVM 18 + Z3 SMT Solver**, tập trung vào tốc độ cao, phát hiện chính xác các lỗi Undefined Behavior (UB) kèm Concrete Witness, và đo độ phủ mã nguồn chi tiết tới từng điều kiện con (`&&`, `||`).

---

## Tính năng Nổi bật

1. **Hazard-Guided Symbolic Partitioning (`Split-and-Filter` + `Z3Refiner`)**: Phân hoạch miền giá trị dựa trên các điều kiện lỗi $\Phi_{\text{err}}$ bằng `DisjointIntervals` và `LinearSymExpr`, xác minh on-demand bằng Z3 để sinh Concrete Witness chính xác mà không duyệt vét cạn.
2. **Graph Behavioral Equivalence & Remainder Slicing (`GraphEquivalenceCache`)**: Tổng quát hóa Miền Tương Đương Hành Vi Tối Đại ($\mathcal{R}_{eq}$) cho đồ thị hàm/CFG — các nhánh có đầu vào rơi vào cùng lớp tương đương đạt **Cache Hit 100%**, nhánh giao một phần chỉ phân tích đúng **phần dư (Remainder Slice)**.
3. **Compositional ModRef & Built-in $O(1)$ Libc Summaries**: Chiếu trạng thái xuống tập biến thực sự đọc (`ReadSet`), tích hợp sẵn mô hình ngữ nghĩa $O(1)$ cho `sscanf`, `strlen`, `strcpy`, `memcpy`, `snprintf`, `malloc`, `free` mà không cần viết hàng trăm dòng stub thủ công.
4. **File/Function LoC Coverage & Sub-Condition Coverage (`||`, `&&`)**: Đo tỷ lệ `%` dòng thực thi theo từng file & từng hàm, đồng thời tách biệt tô màu **6 trạng thái** trên báo cáo HTML tương tác (🟩 An toàn, 🟨 Thiếu điều kiện con trên `if`/`while`/`for`, 🟧 Có lỗi UB, 🟪 Chết sau UB nghiêm trọng, 🟥 Chưa được phủ, ⬜ Không thực thi).

---

## Kiến trúc Thư mục

```text
sqc/
├── CMakeLists.txt                # Cấu hình build C++20 + LLVM 18 + Z3
├── README.md                     # Trang tổng quan dự án
├── include/
│   ├── sqc_builtin.h             # Header chuẩn cho test driver (sqc_interval, SQC_OVERRIDE, ...)
│   └── sqc/                      # Các header nội bộ của core engine
├── src/                          # Mã nguồn C++20 core engine & CLI
├── tests/
│   ├── test_engine_unit.cpp      # Unit test cho DisjointIntervals, LinearSymExpr, Cache, Z3Refiner
│   └── run_tests.sh              # Script chạy unit test và kiểm chứng cả 4 ví dụ
├── examples/
│   ├── 01_basic_hazards/         # Null Deref, Signed Overflow, DivByZero (*a==15), OOB
│   ├── 02_graph_equivalence/     # GraphEquivalenceCache hits & Remainder Slicing
│   ├── 03_subcondition_coverage/ # Sub-condition coverage (||, &&) trên if/while + 6 màu
│   └── 04_rtos_uart_dma_mock/    # Mô phỏng Embedded Cellular / UART DMA + RTOS + sscanf O(1)
└── docs/
    ├── INTRODUCTION.md           # Giới thiệu tổng quan, 3 trụ cột thuật toán & 6 màu Coverage
    ├── GETTING_STARTED.md        # Hướng dẫn build, sqc.json, API <sqc_builtin.h> & thực hành
    ├── REQUIREMENTS.md           # Đặc tả yêu cầu chức năng (FR), phi chức năng (NFR) & nghiệm thu
    └── ARCHITECTURE.md           # Kiến trúc hệ thống chi tiết (C++20 + LLVM 18 + Z3) & các module
```

---

## Quickstart

### 1. Build với CMake + Ninja
```bash
cmake -S . -B build -G Ninja
ninja -C build
```

### 2. Chạy Unit Tests & Toàn bộ 4 Ví dụ
```bash
./tests/run_tests.sh
```

### 3. Chạy từng Ví dụ & Mở Báo cáo HTML
```bash
./build/sqc run -c examples/01_basic_hazards/sqc.json -o examples/01_basic_hazards/report.html
./build/sqc run -c examples/02_graph_equivalence/sqc.json -o examples/02_graph_equivalence/report.html
./build/sqc run -c examples/03_subcondition_coverage/sqc.json -o examples/03_subcondition_coverage/report.html
./build/sqc run -c examples/04_rtos_uart_dma_mock/sqc.json -o examples/04_rtos_uart_dma_mock/report.html
```

### 4. Chuyển đổi Dự án Cũ sang Chuẩn `sqc`
```bash
./build/sqc migrate /path/to/your_project/
```

---

## Tài liệu & Ví dụ

- **Giới thiệu & Thuật toán chi tiết**: [docs/INTRODUCTION.md](docs/INTRODUCTION.md)
- **Hướng dẫn Cài đặt, API & Thực hành**: [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md)
- **Đặc tả Yêu cầu (Requirement Doc)**: [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md)
- **Tài liệu Kiến trúc (Architecture Doc)**: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
- **Ví dụ thực tế**:
  - [examples/01_basic_hazards/](examples/01_basic_hazards/) — Phát hiện 4 lỗi UB kinh điển
  - [examples/02_graph_equivalence/](examples/02_graph_equivalence/) — Lớp tương đương hành vi & Remainder Slicing
  - [examples/03_subcondition_coverage/](examples/03_subcondition_coverage/) — Độ phủ điều kiện con (`||`, `&&`) & 6 màu hiển thị
  - [examples/04_rtos_uart_dma_mock/](examples/04_rtos_uart_dma_mock/) — Mô phỏng RTOS / UART DMA & `sscanf` không cần stub
