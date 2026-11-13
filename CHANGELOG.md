# Changelog

## 1.0.0 — 2026-11-13

The first release: 86 isolated tests of wasm-rtos 1.0.0, each a guest
program and a native runner, run by `run-tests.sh` and in CI against the
kernel's `main`, also under AddressSanitizer and UndefinedBehaviorSanitizer.
They cover scheduling and preemption, every synchronization object,
processes and shared programs, files, every device interface, the Arduino
core, paging, several page stores, swapping, snapshots, the whole system
saved and restored, and the frozen ABI.
