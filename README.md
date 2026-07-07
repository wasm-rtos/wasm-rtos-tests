# wasm-rtos-tests

`wasm-rtos-tests` is a companion test repository for [`wasm-rtos`](https://github.com/wasm-rtos/wasm-rtos).

The repository owns the test runner, test WASM application sources, build logs, generated local build outputs, and GitHub Actions automation. The `wasm-rtos` core stays a library and is included here as a Git submodule.

## Repository layout

```text
wasm-rtos-tests/
  README.md
  main.c
  build/
  tests/
  logs/
  wasm-rtos/
```

Expected ownership:

- `main.c` is the native host test runner. It loads generated `.wasm` files, creates `wasm-rtos` tasks, advances time, runs the scheduler, and reports pass/fail results.
- `tests/` contains C sources for WASM test applications.
- `build/` contains generated local build output. Generated test `.wasm` files live directly in this directory, and the native test runner binary can also be written here.
- `logs/` contains local test logs.
- `wasm-rtos/` is a Git submodule pointing to the core library repository.

Do not commit generated `.wasm` files, native binaries, or logs unless a release process explicitly requires them.

## Contribution language

Use English only for repository documentation, comments, commit messages, pull request titles, pull request descriptions, issue text, and code review discussions.

## Clone

Clone this repository with submodules:

```bash
git clone --recurse-submodules https://github.com/wasm-rtos/wasm-rtos-tests.git
cd wasm-rtos-tests
git submodule sync --recursive
git submodule update --init --recursive --remote
```

Always synchronize, initialize, and update submodules before building or running tests. This repository depends on the `wasm-rtos/` Git submodule, so run these commands after cloning, pulling, or switching branches:

```bash
git submodule sync --recursive
git submodule update --init --recursive --remote
```

## Toolchain requirements

Required for native builds:

- C compiler such as `clang` or `gcc`.
- `make`, `cmake`, or direct shell commands depending on your local build style.

Required for bare WASM test applications:

- `clang` with `wasm32` target support.
- `wasm-ld` or LLVM linker support through `clang`.

Required for WASI test applications:

- `clang` with `wasm32-wasi` target support.
- A WASI sysroot, usually from `wasi-libc` or a WASI SDK.

On Ubuntu GitHub Actions runners, install the common packages with:

```bash
sudo apt-get update
sudo apt-get install -y clang lld wasi-libc
```

## Build directories

Create build directories before compiling:

```bash
mkdir -p build logs
```

Recommended generated paths:

```text
build/*.wasm
build/wasm-rtos-tests
logs/*.log
```

## Build bare WASM test applications

Bare WASM tests do not use WASI. They export an application entry function such as `app_main` and are compiled with `--target=wasm32`, `-nostdlib`, and `--no-entry`.

Example:

```bash
clang \
  --target=wasm32 \
  -nostdlib \
  -Wl,--no-entry \
  -Wl,--export=app_main \
  -O0 \
  -o build/empty_start.wasm \
  tests/empty_start.c
```

Use this style for tests such as:

```text
tests/empty_start.c
tests/simple_loop.c
tests/yield_once.c
tests/delay_once.c
tests/bad_import.c
tests/spin_forever.c
tests/get_time_once.c
tests/custom_import.c
tests/return_code.c
tests/snapshot_yield.c
tests/snapshot_delay.c
```

For a source file with multiple exported entry functions, pass one `-Wl,--export=<name>` option for each entry.

Example:

```bash
clang \
  --target=wasm32 \
  -nostdlib \
  -Wl,--no-entry \
  -Wl,--export=app_main_i32 \
  -Wl,--export=app_main_i64 \
  -Wl,--export=app_main_f32 \
  -Wl,--export=app_main_f64 \
  -O0 \
  -o build/return_values.wasm \
  tests/return_values.c
```

## Build WASI test applications

WASI tests use WASI imports and should be compiled with the `wasm32-wasi` target. They normally use `_start` as the entry point.

Example without an explicit sysroot:

```bash
clang \
  --target=wasm32-wasi \
  -O0 \
  -o build/wasi_exit.wasm \
  tests/wasi_exit.c
```

Example with an explicit sysroot:

```bash
clang \
  --target=wasm32-wasi \
  --sysroot "$WASI_SYSROOT" \
  -O0 \
  -o build/wasi_exit.wasm \
  tests/wasi_exit.c
```

Use this style for tests such as:

```text
tests/wasi_exit.c
tests/wasi_puts.c
tests/wasi_task_loop.c
```

The native runner must also build `wasm-rtos` and its `wasm3` backend with WASI support enabled when running WASI test applications. If the native build does not include WASI support, skip WASI test applications or fail with a clear diagnostic.

## Build `main.c` with `wasm-rtos`

`main.c` is a native executable, not a WASM file. It should compile together with the `wasm-rtos` core sources and the `wasm3` backend sources from the submodule.

Minimal direct build example:

```bash
cc \
  -std=c11 \
  -Wall \
  -Wextra \
  -Iwasm-rtos \
  main.c \
  wasm-rtos/os.c \
  wasm-rtos/hal.c \
  wasm-rtos/wasm3/source/*.c \
  -o build/wasm-rtos-tests
```

If your wasm3 build requires configuration macros, add them to the native compile command with `-D...` flags. Keep those flags in one script so local builds and CI builds use the same wasm3 configuration.

Run the native test runner after building test applications:

```bash
./build/wasm-rtos-tests build
```

Recommended runner behavior:

- Accept the WASM directory path as `argv[1]`.
- Load each `.wasm` file from that directory.
- Use `app_main` for bare test applications unless the test explicitly names another export.
- Use `_start` for WASI test applications.
- Write logs to `logs/`.
- Return exit code `0` on success and non-zero on failure.


## GitHub Actions resilience

The `Build WASM tests` workflow is intentionally fault-tolerant. Each major phase writes its output under `logs/`, records a phase status in `logs/status.env`, uploads the `build/` and `logs/` artifacts, and then commits any generated `build/*.wasm` files and log files on non-pull-request runs before the final status gate fails the job. This keeps diagnostics available in the repository even when a WASM compile, native runner build, or runtime test fails.

When diagnosing CI failures, start with these files:

```text
logs/toolchain_install.log
logs/wasm_build.log
logs/generated_wasm_files.log
logs/native_build.log
logs/native_run.log
logs/smoke_test.log
logs/status.env
```

## Suggested local build commands

Build everything locally:

```bash
git submodule update --init --recursive
mkdir -p build logs

clang --target=wasm32 -nostdlib -Wl,--no-entry -Wl,--export=app_main -O0 \
  -o build/empty_start.wasm tests/empty_start.c

clang --target=wasm32-wasi -O0 \
  -o build/wasi_exit.wasm tests/wasi_exit.c

cc -std=c11 -Wall -Wextra -Iwasm-rtos \
  main.c wasm-rtos/os.c wasm-rtos/hal.c wasm-rtos/wasm3/source/*.c \
  -o build/wasm-rtos-tests

./build/wasm-rtos-tests build
```

## Recommended `.gitignore`

Use this `.gitignore` in `wasm-rtos-tests`:

```gitignore
/build/
/logs/
*.wasm
*.o
*.obj
*.exe
*.dll
*.so
*.dylib
*.a
*.lib
```

Keep `tests/*.c`, `main.c`, and build scripts tracked.

## Checklist for agents and contributors

Before opening a pull request:

1. Update or add test source files under `tests/`.
2. Build bare test applications with the bare WASM command and WASI test applications with the WASI command.
3. Build all WASM test applications.
4. Build `main.c` with `wasm-rtos` and `wasm3` sources.
5. Run the native test runner.
6. Confirm generated files stay under `build/` and logs stay under `logs/`.
7. Do not commit generated `.wasm` files unless the release process explicitly requires them.
