/**
 * @file demo.c
 * @brief lockfree API demonstration (single-threaded, logic check).
 *
 * A real firmware would call lf_spsc_push() from an ISR and lf_spsc_pop()
 * from main, or lf_mpsc_push() from several tasks. This demo exercises the
 * data structures single-threaded to prove the ring/SPSC/MPSC/seqlock logic
 * and the flag, and verifies seqlock read consistency under a simulated
 * writer. It compiles on any C11 toolchain:
 *
 *     cc -O2 -std=c11 -o demo examples/demo.c
 */
#include <stdio.h>
#include "lockfree.h"

#define CAP 8

int main(void) {
    /* ---- SPSC ring: uint32_t slots ---- */
    uint32_t storage[CAP];
    lf_spsc_ring_t spsc;
    lf_spsc_init(&spsc, storage, CAP, sizeof(uint32_t));

    for (uint32_t i = 1; i <= CAP; i++) {
        if (!lf_spsc_push(&spsc, &i)) { printf("push %u FAILED (full)\n", i); return 1; }
    }
    if (lf_spsc_push(&spsc, &((uint32_t){99}))) { printf("push past full wrongly OK\n"); return 1; }

    uint32_t v;
    int ok = 1;
    for (uint32_t i = 1; i <= CAP; i++) {
        if (!lf_spsc_pop(&spsc, &v) || v != i) { printf("pop mismatch: got %u want %u\n", v, i); ok = 0; break; }
    }
    printf("SPSC: pushed %d, popped in FIFO order = %s\n", CAP, ok ? "YES" : "NO");

    /* ---- MPSC ring ---- */
    uint32_t mstorage[CAP];
    lf_mpsc_ring_t mpsc;
    lf_mpsc_init(&mpsc, mstorage, CAP, sizeof(uint32_t));
    int pushed = 0;
    for (uint32_t i = 100; i < 100 + CAP + 2; i++) {
        if (lf_mpsc_push(&mpsc, &i)) pushed++;
    }
    printf("MPSC: accepted %d / %d (one rejected when full) = %s\n",
           pushed, CAP + 2, pushed == CAP ? "YES" : "NO");
    int popped = 0;
    while (lf_mpsc_pop(&mpsc, &v)) popped++;
    printf("MPSC: popped %d = %s\n", popped, popped == CAP ? "YES" : "NO");

    /* ---- Seqlock: consistent read under writer ---- */
    typedef struct { uint32_t a, b, c; } sensor_t;
    static sensor_t shared;
    static lf_seqlock_t slk;
    lf_seqlock_init(&slk);

    int retries = 0, consistent = 1;
    for (int trial = 0; trial < 1000; trial++) {
        lf_seqlock_write_begin(&slk);
        shared.a = trial; shared.b = trial + 1; shared.c = trial + 2;
        lf_seqlock_write_end(&slk);

        uint32_t seq = lf_seqlock_read_begin(&slk);
        sensor_t snap = shared;
        if (!lf_seqlock_read_retry(&slk, seq)) { retries++; continue; }
        if (snap.b != snap.a + 1 || snap.c != snap.a + 2) consistent = 0;
    }
    printf("Seqlock: consistent reads = %s, retries = %d\n", consistent ? "YES" : "NO", retries);

    /* ---- Flag ---- */
    static lf_flag_t flag;
    lf_flag_set(&flag);
    int got = lf_flag_test_and_clear(&flag);
    printf("Flag: test_and_clear returned %d, now %d\n", got, (int)atomic_load(&flag));
    return (ok && pushed == CAP && popped == CAP && consistent && got) ? 0 : 1;
}
