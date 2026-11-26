# Kế hoạch Triển khai & Review — Nâng cấp `sqc` Hỗ trợ Đầy đủ C & C++ (Phase 2)

**Điều phối & Review:** Gemini 4 Argon (Medium)  
**Thực thi Code:** Subagents (Gemini 4 Neon Medium)

---

## 1. Mục tiêu Nâng cấp
Mở rộng `sqc` từ công cụ phân tích C nhúng thành nền tảng phân tích tĩnh **hỗ trợ đầy đủ cả C (C99/C11/C23) lẫn C++ (C++11/14/17/20)**, kèm bộ API khai báo tập input linh hoạt và cơ chế phân tích State Machine tự động mà không cần mock biến `static`/`global`.

---

## 2. Phân rã 4 Gói Công việc Độc lập (Work Packages)

### WP-1: C++ Frontend, Demangling & Header API mở rộng
- **File phụ trách:** `include/sqc_builtin.h`, `src/frontend.cpp`, `src/coverage.cpp`, `src/report.cpp`
- **Chi tiết triển khai:**
  1. **C++ Compiler Dispatch (`src/frontend.cpp`):**
     - Nhận diện đuôi `.cpp`, `.cc`, `.cxx` để gọi `/usr/bin/clang++-18 -std=c++20 -emit-llvm -c -g -gcolumn-info -O0 -Xclang -disable-O0-optnone` (hỗ trợ trộn lẫn file `.c` và `.cpp` trong cùng 1 analysis unit).
     - Mở rộng `ConfigLoader::expand_source_files` hỗ trợ wildcard `*.cpp`, `*.cc`.
  2. **Symbol Demangling (`llvm::demangle`):**
     - Demangle tên hàm C++ trong `analyzer.cpp`, `coverage.cpp` và `report.cpp` (ví dụ `_ZN6Sensor7processEPi` $\rightarrow$ `Sensor::process(int*)`) trên cả Callstack, bảng Coverage và CLI.
  3. **API Input linh hoạt (`include/sqc_builtin.h`):**
     - `sqc_range(lo, hi)` (alias rõ nghĩa của `sqc_interval`).
     - `sqc_one_of_i64(count, ...)` / macro `sqc_one_of(...)` — tập giá trị rời rạc chính xác trên `DisjointIntervals`.
     - `sqc_ranges_i64(pair_count, ...)` / macro `sqc_ranges(...)` — hợp nhiều khoảng `[lo1, hi1] ∪ [lo2, hi2]`.
     - `sqc_any_ptr(T)` — tự động cấp phát vùng nhớ symbolic `sizeof(T)` với `Nullability::MayNull` (`NULL` hoặc trỏ tới dải đầy đủ của `T`).
     - `sqc_run_until_fixpoint(fn, arg)` — chạy lặp hàm sự kiện/State Machine cho đến khi trạng thái toàn cục hội tụ (`S_new ⊆ S_old`).

### WP-2: Kiểu Số thực (`float`/`double`) & Cấu trúc Giá trị Nâng cao
- **File phụ trách:** `include/sqc/intervals.h`, `src/intervals.cpp`, `include/sqc/state.h`, `src/state.cpp`
- **Chi tiết triển khai:**
  1. **`FloatInterval`:** Thêm cấu trúc khoảng số thực `[lo, hi]` kèm cờ `may_be_nan`, `may_be_inf` vào `AbstractValue`.
  2. **Các phép toán số thực:** Hỗ trợ `FAdd`, `FSub`, `FMul`, `FDiv` (bắt lỗi chia `0.0` / sinh `NaN`/`Inf`), `FNeg`, `FCmpInst` (`OEQ`, `ONE`, `OLT`, `OLE`, `OGT`, `OGE`...), và ép kiểu `SIToFP`, `UIToFP`, `FPToSI`, `FPToUI`, `FPExt`, `FPTrunc`.
  3. **Aggregate & Atomic Values:** Hỗ trợ `ExtractValueInst`, `InsertValueInst` (cho struct/std::pair trả về theo giá trị), `AtomicRMWInst`, `AtomicCmpXchgInst` (cho code đa luồng / RTOS).

### WP-3: Ngữ nghĩa C++ (`InvokeInst`, `vtable`, `new`/`delete`, State Machine Fixpoint)
- **File phụ trách:** `src/summaries.cpp`, `src/analyzer.cpp`
- **Chi tiết triển khai:**
  1. **C++ Exception Control Flow (`InvokeInst` / `LandingPadInst`):**
     - Xử lý `llvm::InvokeInst` trong `analyzer.cpp`: chạy hàm đích, đẩy nhánh bình thường sang `invoke->getNormalDest()` và nhánh ngoại lệ (khi có khả năng throw) sang `invoke->getUnwindDest()`.
  2. **C++ Heap & RAII Summaries (`src/summaries.cpp`):**
     - Hỗ trợ `operator new` (`_Znwm`), `operator new[]` (`_Znam`), `operator delete` (`_ZdlPv`, `_ZdlPvm`), `operator delete[]` (`_ZdaPv`, `_ZdaPvm`), `realloc`.
     - Theo dõi `alloc_kind` (`Malloc` vs `NewScalar` vs `NewArray`) trên `MemoryRegion` để phát hiện **Mismatched `new[]`/`delete`**, **Double-Delete**, **Use-After-Free**, và **Memory Leak**.
  3. **Virtual Dispatch (`vtable`):**
     - Đảm bảo `populate_constant_into_region` nạp đầy đủ mảng con trỏ hàm trong `vtable` (`_ZTV*`) để lời gọi hàm ảo `base->method()` phân giải chính xác `func_targets`.
  4. **Built-in `sqc_one_of`, `sqc_ranges`, `sqc_any_ptr`, `sqc_run_until_fixpoint`:**
     - `sqc_run_until_fixpoint(fn, arg)`: Lặp gọi `analyze_function(fn, ...)` tối đa 16 vòng và **dừng ngay khi `post_state.is_subset_of(prev_state)`** — tự động phủ toàn bộ trạng thái đạt tới được của State Machine mà không cần mock biến `static`/`global`.

### WP-4: Bộ Test C++ & Example Thực tế + Cập nhật Tài liệu
- **File phụ trách:** `tests/test_engine_unit.cpp`, `examples/05_cpp_features/`, `docs/INTRODUCTION.md`, `docs/GETTING_STARTED.md`
- **Chi tiết triển khai:**
  1. Thêm `examples/05_cpp_features/` (`sensor_cpp.cpp`, `sqc.json`):
     - Minh họa class C++ có kế thừa + hàm ảo (`virtual`), `new`/`delete` (bắt Memory Leak & Use-After-Free), `float` chia `0.0`, `try/catch`, `sqc_one_of`, `sqc_any_ptr(int)`, và State Machine chạy bằng `sqc_run_until_fixpoint`.
  2. Cập nhật `tests/run_tests.sh` chạy kiểm chứng tự động cả Example 05 (C++) cùng 4 Example C.
  3. Cập nhật tài liệu `GETTING_STARTED.md` với bảng API mới và hướng dẫn phân tích dự án C++.

---

## 3. Tiêu chí Review & Nghiệm thu (Argon 4 Checklist)
1. **Build sạch:** `cmake -B build -G Ninja && ninja -C build` đạt **0 lỗi** trên cả `sqc` và `test_engine_unit`.
2. **Không hồi quy (Zero Regression):** Toàn bộ 47 Unit Tests cũ + 4 Example C + `cmcell` (`urc-parsers`) vẫn PASS 100%.
3. **Kiểm chứng C++ (Example 05):**
   - Biên dịch & phân tích `.cpp` bằng `clang++-18` thành công.
   - Tên hàm trên báo cáo HTML hiển thị dạng demangled (`Class::method(...)`).
   - Bắt đúng lỗi C++ (`UseAfterFree` / `MemoryLeak` trên `new`/`delete`, `DivisionByZero` trên `float`, `NullPointerDeref` từ `sqc_any_ptr`, và đa hình `virtual`).
   - Báo cáo HTML 5 trang nền trắng sinh ra hợp lệ, hiển thị đúng Coverage LoC & Sub-Condition cho code C++.
