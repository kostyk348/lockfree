/**
 * @file lockfree.h
 * @brief Lock-free primitives for FPU-less microcontrollers.
 *
 * A small, dependency-free collection of wait-free / lock-free concurrent
 * data structures for bare-metal firmware, written in portable C11 with
 * <stdatomic.h>. They pair naturally with the time-triggered, static-
 * allocation philosophy of DSO-TRON:
 *
 *   - lf_spsc_ring_t  : single-producer / single-consumer ring buffer
 *                        (wait-free, no CAS, just relaxed/acquire/release)
 *   - lf_mpsc_ring_t  : multi-producer  / single-consumer ring buffer
 *                        (lock-free, one atomic index CAS per push)
 *   - lf_seqlock_t    : seqlock for atomic reads of a shared struct
 *   - lf_flag_t       : a single atomic boolean (ISR <-> main signalling)
 *
 * All structures are caller-allocated and zero-initialised; no malloc,
 * no critical sections, no disabling of interrupts required for the SPSC
 * case (it is safe on a single writer / single reader even across an ISR).
 *
 * Note on Cortex-M0/M0+: the architecture has no native word CAS, so the
 * compiler emits a library lock for atomic_compare_exchange on 32-bit
 * words. For true lock-free behaviour on M0, restrict MPSC use to a single
 * producer, or port lf_mpsc to a __disable_irq/__enable_irq pair. SPSC and
 * the seqlock reader are lock-free everywhere.
 */
#ifndef LOCKFREE_H
#define LOCKFREE_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/*  SPSC ring buffer (single producer, single consumer)               */
/* ================================================================== */

typedef struct {
    uint8_t *buf;       /**< caller-owned storage, capacity*esize bytes */
    size_t   cap;       /**< number of slots */
    size_t   esize;     /**< element size in bytes */
    atomic_size_t head; /**< next write index (producer) */
    atomic_size_t tail; /**< next read index (consumer) */
} lf_spsc_ring_t;

/** Initialise an SPSC ring with caller-owned storage. */
static inline void lf_spsc_init(lf_spsc_ring_t *r, void *storage,
                                size_t cap, size_t esize) {
    r->buf = (uint8_t *)storage;
    r->cap = cap;
    r->esize = esize;
    atomic_store_explicit(&r->head, 0, memory_order_relaxed);
    atomic_store_explicit(&r->tail, 0, memory_order_relaxed);
}

static inline int lf_spsc_push(lf_spsc_ring_t *r, const void *item) {
    size_t head = atomic_load_explicit(&r->head, memory_order_relaxed);
    size_t tail = atomic_load_explicit(&r->tail, memory_order_acquire);
    if ((head - tail) == r->cap) return 0;           /* full */
    memcpy(&r->buf[(head % r->cap) * r->esize], item, r->esize);
    atomic_store_explicit(&r->head, head + 1, memory_order_release);
    return 1;
}

static inline int lf_spsc_pop(lf_spsc_ring_t *r, void *item) {
    size_t tail = atomic_load_explicit(&r->tail, memory_order_relaxed);
    size_t head = atomic_load_explicit(&r->head, memory_order_acquire);
    if (tail == head) return 0;                      /* empty */
    memcpy(item, &r->buf[(tail % r->cap) * r->esize], r->esize);
    atomic_store_explicit(&r->tail, tail + 1, memory_order_release);
    return 1;
}

static inline size_t lf_spsc_count(const lf_spsc_ring_t *r) {
    size_t head = atomic_load_explicit(&r->head, memory_order_acquire);
    size_t tail = atomic_load_explicit(&r->tail, memory_order_acquire);
    return head - tail;
}

/* ================================================================== */
/*  MPSC ring buffer (multi producer, single consumer)                */
/* ================================================================== */

typedef struct {
    uint8_t *buf;
    size_t   cap;
    size_t   esize;
    atomic_size_t head;   /**< producer cursor (CAS-updated) */
    atomic_size_t tail;   /**< consumer cursor */
} lf_mpsc_ring_t;

static inline void lf_mpsc_init(lf_mpsc_ring_t *r, void *storage,
                                size_t cap, size_t esize) {
    r->buf = (uint8_t *)storage;
    r->cap = cap;
    r->esize = esize;
    atomic_store_explicit(&r->head, 0, memory_order_relaxed);
    atomic_store_explicit(&r->tail, 0, memory_order_relaxed);
}

/** Push from a producer. Lock-free (single CAS). Returns 1 on success. */
static inline int lf_mpsc_push(lf_mpsc_ring_t *r, const void *item) {
    size_t head = atomic_load_explicit(&r->head, memory_order_relaxed);
    size_t tail = atomic_load_explicit(&r->tail, memory_order_acquire);
    if ((head - tail) == r->cap) return 0;           /* full */
    memcpy(&r->buf[(head % r->cap) * r->esize], item, r->esize);
    /* publish: only one producer may win the slot at this head value */
    size_t next = head + 1;
    return atomic_compare_exchange_strong_explicit(
        &r->head, &head, next,
        memory_order_release, memory_order_relaxed);
}

static inline int lf_mpsc_pop(lf_mpsc_ring_t *r, void *item) {
    size_t tail = atomic_load_explicit(&r->tail, memory_order_relaxed);
    size_t head = atomic_load_explicit(&r->head, memory_order_acquire);
    if (tail == head) return 0;
    memcpy(item, &r->buf[(tail % r->cap) * r->esize], r->esize);
    atomic_store_explicit(&r->tail, tail + 1, memory_order_release);
    return 1;
}

/* ================================================================== */
/*  Seqlock: safe reads of a shared struct (one writer, many readers) */
/* ================================================================== */

typedef struct {
    atomic_uint_fast32_t seq;
} lf_seqlock_t;

static inline void lf_seqlock_init(lf_seqlock_t *s) {
    atomic_store_explicit(&s->seq, 0, memory_order_relaxed);
}

/** Begin a read. Returns the even sequence number observed. */
static inline uint32_t lf_seqlock_read_begin(const lf_seqlock_t *s) {
    uint32_t seq;
    do {
        seq = atomic_load_explicit(&s->seq, memory_order_acquire);
    } while (seq & 1u);                 /* wait until not mid-write */
    atomic_thread_fence(memory_order_acquire);
    return seq;
}

/** End a read. Returns 1 if the read is consistent (retry otherwise). */
static inline int lf_seqlock_read_retry(const lf_seqlock_t *s, uint32_t seq) {
    atomic_thread_fence(memory_order_acquire);
    return atomic_load_explicit(&s->seq, memory_order_acquire) == seq;
}

/** Begin a write (caller serialises writers). */
static inline void lf_seqlock_write_begin(lf_seqlock_t *s) {
    uint32_t seq = atomic_load_explicit(&s->seq, memory_order_relaxed) + 1;
    atomic_store_explicit(&s->seq, seq, memory_order_release);   /* odd */
    atomic_thread_fence(memory_order_release);
}

/** End a write. */
static inline void lf_seqlock_write_end(lf_seqlock_t *s) {
    uint32_t seq = atomic_load_explicit(&s->seq, memory_order_relaxed) + 1;
    atomic_store_explicit(&s->seq, seq, memory_order_release);   /* even */
}

/* ================================================================== */
/*  Atomic flag (ISR <-> main signalling)                             */
/* ================================================================== */

typedef atomic_uint_fast8_t lf_flag_t;

static inline void lf_flag_set(lf_flag_t *f) {
    atomic_store_explicit(f, 1u, memory_order_release);
}
static inline void lf_flag_clear(lf_flag_t *f) {
    atomic_store_explicit(f, 0u, memory_order_release);
}
static inline int lf_flag_test_and_clear(lf_flag_t *f) {
    return atomic_exchange_explicit(f, 0u, memory_order_acq_rel) == 1u;
}

#ifdef __cplusplus
}
#endif

#endif /* LOCKFREE_H */
