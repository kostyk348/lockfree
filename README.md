# lockfree

[![CI](https://github.com/kostyk348/lockfree/actions/workflows/ci.yml/badge.svg)](https://github.com/kostyk348/lockfree/actions/workflows/ci.yml)


**Lock-free primitives for FPU-less microcontrollers.**

A small, dependency-free set of wait-free / lock-free concurrent data
structures for bare-metal firmware, in portable C11 (`<stdatomic.h>`). They
pair with the time-triggered, static-allocation philosophy of
[DSO-TRON](https://github.com/kostyk348/DSO-TRON) and the fixed-point
signal path of [qdsp](https://github.com/kostyk348/qdsp).

## Modules

| Primitive | Pattern | Notes |
|-----------|---------|-------|
| `lf_spsc_ring_t` | single-producer / single-consumer | wait-free; safe across an ISR boundary with **no** critical section |
| `lf_mpsc_ring_t` | multi-producer / single-consumer | lock-free; one atomic CAS per push |
| `lf_seqlock_t` | one writer / many readers | consistent struct reads without a mutex |
| `lf_flag_t` | ISR ↔ main signalling | `test_and_clear` atomic boolean |

All structures are caller-allocated and zero-initialised — no `malloc`, no
critical sections required for the SPSC case.

## Quick start

```c
#include "lockfree.h"

[![CI](https://github.com/kostyk348/lockfree/actions/workflows/ci.yml/badge.svg)](https://github.com/kostyk348/lockfree/actions/workflows/ci.yml)


uint8_t storage[CAP * ESIZE];
lf_spsc_ring_t q;
lf_spsc_init(&q, storage, CAP, ESIZE);

/* in ISR */
lf_spsc_push(&q, &sample);

/* in main loop */
if (lf_spsc_pop(&q, &out)) { /* process */ }
```

## Demo

`examples/demo.c` exercises every primitive single-threaded and verifies
FIFO order, full-buffer rejection, seqlock consistency, and the flag.

```bash
cc -O2 -std=c11 -o demo examples/demo.c && ./demo
# SPSC: pushed 8, popped in FIFO order = YES

[![CI](https://github.com/kostyk348/lockfree/actions/workflows/ci.yml/badge.svg)](https://github.com/kostyk348/lockfree/actions/workflows/ci.yml)

# MPSC: accepted 8 / 10 (one rejected when full) = YES

[![CI](https://github.com/kostyk348/lockfree/actions/workflows/ci.yml/badge.svg)](https://github.com/kostyk348/lockfree/actions/workflows/ci.yml)

# MPSC: popped 8 = YES

[![CI](https://github.com/kostyk348/lockfree/actions/workflows/ci.yml/badge.svg)](https://github.com/kostyk348/lockfree/actions/workflows/ci.yml)

# Seqlock: consistent reads = YES, retries = 7

[![CI](https://github.com/kostyk348/lockfree/actions/workflows/ci.yml/badge.svg)](https://github.com/kostyk348/lockfree/actions/workflows/ci.yml)

# Flag: test_and_clear returned 1, now 0

[![CI](https://github.com/kostyk348/lockfree/actions/workflows/ci.yml/badge.svg)](https://github.com/kostyk348/lockfree/actions/workflows/ci.yml)

```

## Cortex-M0 / M0+ caveat

ARMv6-M has no native 32-bit `CAS`, so the compiler emits a library lock
for `atomic_compare_exchange` in `lf_mpsc_push`. For true lock-free
behaviour on M0:

* restrict MPSC to a **single producer**, or
* port `lf_mpsc_push` to a `__disable_irq` / `__enable_irq` pair.

SPSC, the seqlock reader, and the flag are lock-free on every Cortex-M.

## License

Apache 2.0
