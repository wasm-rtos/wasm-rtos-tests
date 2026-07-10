# wasm-rtos-tests

This repository contains isolated integration tests for `wasm-rtos`.

## Mandatory test layout

Every test must have its own directory under `tests/`. The directory name is the test name.

```text
tests/
  queue_api/
    main.c
    queue_api.c
    queue_api.wasm
    queue_api.log
```

Each test directory contains:

- `main.c` — the complete native test runner for one specific OS feature;
- `<test_name>.c` — the guest C program compiled to WebAssembly;
- `<test_name>.wasm` — the generated WebAssembly module;
- `<test_name>.log` — the log generated when the local `main.c` runs the module.

There is no repository-level `main.c`, `build/`, or `logs/` directory.

## Isolation rules

Every test must be fully self-contained.

- The complete test scenario must be readable in that test's `main.c`.
- `main.c` must initialize the OS, load its local `.wasm`, create the required tasks, run the scheduler, verify the expected behavior, clean up, and write its local `.log`.
- A test must not call a shared test runner.
- A test must not select behavior through `TEST_KIND`, `HOST_TEST_KIND`, or similar dispatch macros.
- A test must not include C source files from another test directory.
- A test must not depend on state created by another test.
- Shared test-support code is forbidden. The `tests/support/` directory must not exist.

Code may be duplicated between test runners when that keeps each test independent and makes the tested OS behavior explicit.

## Running all tests

Run the same automation locally with:

```bash
bash ./run-tests.sh
```

For every directory under `tests/`, the script:

1. verifies that `main.c` and `<test_name>.c` exist;
2. compiles `<test_name>.c` into `<test_name>.wasm`;
3. compiles the directory's standalone `main.c` into a temporary native runner;
4. executes the runner from inside the test directory;
5. leaves `<test_name>.log` in that directory;
6. removes the temporary native runner.

A missing file, build failure, failed assertion, or non-zero runner exit code fails the complete run.

## GitHub Actions

`.github/workflows/run-tests.yml` runs `run-tests.sh` automatically for pushes, pull requests, and manual workflow dispatches.

The workflow installs the native and WASI toolchains, builds and executes every isolated test, and uploads the complete `tests/` directory as the `isolated-tests` artifact. The artifact contains the generated `.wasm` modules and `.log` files.

The workflow does not maintain a central list of tests. Adding a correctly structured directory automatically adds that test to CI.

## Adding a test

Create a directory whose name describes the OS feature being tested:

```text
tests/new_feature/
  main.c
  new_feature.c
```

`new_feature.c` must expose the WebAssembly entry point needed by the test. `main.c` must contain the complete host-side verification for that feature and must create `new_feature.log` when executed.

Do not add shared test helpers or modify a central test dispatcher. The GitHub workflow discovers the new directory automatically.
