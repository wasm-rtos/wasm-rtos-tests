# Test WASM apps

`test_wasm_apps/` contains source code for tiny WASM programs used only for OS testing.

The source files in this directory are tracked. Generated `.wasm` files must not be committed. Generated smoke test logs under `test_logs/` may be committed when requested for PR review.

The temporary `main.cpp` harness may later load `.wasm` files built from this folder for local integration testing.

This test folder is not an application framework and is not part of the final library API.

## Local build examples

Create a local output directory before building:

```bash
mkdir -p test_wasm_build
```

Build the empty start app:

```bash
clang --target=wasm32 -nostdlib -Wl,--no-entry -Wl,--export=app_main -O0 -o test_wasm_build/empty_start.wasm test_wasm_apps/empty_start.c
```

Build the finite simple loop app:

```bash
clang --target=wasm32 -nostdlib -Wl,--no-entry -Wl,--export=app_main -O0 -o test_wasm_build/simple_loop.wasm test_wasm_apps/simple_loop.c
```

Build the yield-once host import app:

```bash
clang --target=wasm32 -nostdlib -Wl,--no-entry -Wl,--export=app_main -O0 -o test_wasm_build/yield_once.wasm test_wasm_apps/yield_once.c
```

`yield_once.c` tests the first OS host import. It imports `env.os_yield`, calls it once, yields control back to the scheduler, resumes on a later scheduler run, and then finishes.

Build the delay-once host import app:

```bash
clang --target=wasm32 -nostdlib -Wl,--no-entry -Wl,--export=app_main -O0 -o test_wasm_build/delay_once.wasm test_wasm_apps/delay_once.c
```

`delay_once.c` tests the `env.os_delay_ms` host import. It should enter `OS_TASK_WAITING` after calling `os_delay_ms(10)`, it should not finish before the wake tick, and after enough `os_tick()` advancement it should resume and finish.

Build the bad-import negative diagnostics app:

```bash
clang --target=wasm32 -nostdlib -Wl,--no-entry -Wl,--export=app_main -O0 -o test_wasm_build/bad_import.wasm test_wasm_apps/bad_import.c
```

`bad_import.c` is a negative diagnostics test. It intentionally imports the unsupported required host function `env.missing_required_import`, so the smoke harness expects task creation to fail with populated OS WASM diagnostics.


Build the long-running spin-forever app:

```bash
clang --target=wasm32 -nostdlib -Wl,--no-entry -Wl,--export=app_main -O0 -o test_wasm_build/spin_forever.wasm test_wasm_apps/spin_forever.c
```

`spin_forever.c` intentionally never returns and imports no host functions. It verifies that the scheduler returns at bounded slice boundaries while keeping the WASM task alive.

Build the WASI exit app:

```bash
clang --target=wasm32-wasi --sysroot=/usr -O0 -o test_wasm_build/wasi_exit.wasm test_wasm_apps/wasi_exit.c
```

`wasi_exit.c` tests WASI `exit(7)`. It requires wasm3 WASI support and verifies that WASI process exit is reported as `OS_TASK_EXIT_EXPLICIT` with exit code `7`.

Build the get-time host import app:

```bash
clang --target=wasm32 -nostdlib -Wl,--no-entry -Wl,--export=app_main -O0 -o test_wasm_build/get_time_once.wasm test_wasm_apps/get_time_once.c
```

`get_time_once.c` tests the `env.os_get_time_ms` host import. The smoke harness sets the OS tick to `1234U`; the fixture reads that tick through the import and exits with code `0U` only when the imported time matches.

Build the custom host import registration app:

```bash
clang --target=wasm32 -nostdlib -Wl,--no-entry -Wl,--export=app_main -O0 -o test_wasm_build/custom_import.wasm test_wasm_apps/custom_import.c
```

`custom_import.c` tests host-owned import registration. The smoke harness registers `env.host_test_value` from native code before task creation, then verifies that the WASM task can import and call that user-provided function without adding the function to `os.c`.

Build the entry return-code app:

```bash
clang --target=wasm32 -nostdlib -Wl,--no-entry -Wl,--export=app_main -O0 -o test_wasm_build/return_code.wasm test_wasm_apps/return_code.c
```

`return_code.c` tests normal WASM entry-function completion with an `i32` return value. The smoke harness expects `OS_TASK_EXIT_RETURNED` and exit code `42`.

Build the scalar return-values app:

```bash
clang --target=wasm32 -nostdlib -Wl,--no-entry -Wl,--export=app_main_i32 -Wl,--export=app_main_i64 -Wl,--export=app_main_f32 -Wl,--export=app_main_f64 -O0 -o test_wasm_build/return_values.wasm test_wasm_apps/return_values.c
```

`return_values.c` tests typed entry-function return values for all wasm3-compatible scalar result types supported by the OS API: `i32`, `i64`, `f32`, and `f64`. The smoke harness also covers a minimal generated multi-value WASM module that returns all four scalar result types from one entry function.

Build the WASI puts app:

```bash
clang --target=wasm32-wasi --sysroot=/usr -O0 -o test_wasm_build/wasi_puts.wasm test_wasm_apps/wasi_puts.c
```

`wasi_puts.c` is a focused WASI compatibility probe. It uses libc `puts("hello world")`, which requires WASI stdout imports such as `wasi_snapshot_preview1.fd_write`. It is expected to run under `wasm-rtos` when the native build compiles wasm3 with WASI support enabled and includes `wasm3/source/m3_api_wasi.c`.

Build two WASI task-loop apps for scheduler switching tests:

```bash
clang --target=wasm32-wasi --sysroot=/usr -O0 -DWASI_TASK_NAME='"wasi_task_a"' -o test_wasm_build/wasi_task_a.wasm test_wasm_apps/wasi_task_loop.c
clang --target=wasm32-wasi --sysroot=/usr -O0 -DWASI_TASK_NAME='"wasi_task_b"' -o test_wasm_build/wasi_task_b.wasm test_wasm_apps/wasi_task_loop.c
```

`wasi_task_loop.c` is a WASI scheduler-switching probe. Build it twice with different `WASI_TASK_NAME` values, create two OS tasks from the generated modules, and call `os_schedule()` repeatedly from the host. Each WASI task prints its configured name through WASI stdout, so alternating host scheduler calls should show both task names over time.

## Temporary smoke harness

The temporary Visual Studio `main.cpp` smoke harness expects the generated files at `test_wasm_build/empty_start.wasm`, `test_wasm_build/simple_loop.wasm`, `test_wasm_build/yield_once.wasm`, `test_wasm_build/delay_once.wasm`, `test_wasm_build/bad_import.wasm`, `test_wasm_build/spin_forever.wasm`, `test_wasm_build/wasi_exit.wasm`, `test_wasm_build/get_time_once.wasm`, `test_wasm_build/custom_import.wasm`, `test_wasm_build/return_code.wasm`, and `test_wasm_build/return_values.wasm`. Build them with the commands above, then compile and run the harness with `os.c`, `hal.c`, and the local untracked `wasm3/` checkout available.

The harness writes its local diagnostic log to `test_logs/smoke_test.log`. Generated `.wasm` files are local artifacts and must not be committed. Generated smoke test logs may be committed when requested for PR review.
