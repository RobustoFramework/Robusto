#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define FRM2_SWEEP_COUNT 60U
#define FRM2_SWEEP_END 61U
#define FRM2_SWEEP_INTERVAL_US 1000000LL
#define FRM2_SWEEP_TIMEOUT_US 70000000LL
#define FRM2_SWEEP_GRACE_US 2000000LL
#define FRM2_BYTE_SCAN_MIN 256U
#define FRM2_BYTE_SCAN_MAX 384U
#define FRM2_BYTE_SCAN_SIZES (FRM2_BYTE_SCAN_MAX - FRM2_BYTE_SCAN_MIN + 1U)
#define FRM2_BYTE_SCAN_COUNT (FRM2_BYTE_SCAN_SIZES * 3U)
#define FRM2_BYTE_SCAN_TIMEOUT_US 420000000LL
#define FRM2_TIGHT_SCAN_MIN 337U
#define FRM2_TIGHT_SCAN_MAX 342U
#define FRM2_TIGHT_SCAN_COUNT 30U

typedef enum {
    FRM2_SWEEP_COARSE,
    FRM2_SWEEP_BYTE_SCAN,
    FRM2_SWEEP_TIGHT_SCAN
} frm2_sweep_mode_t;

static const uint16_t frm2_sweep_sizes[] = {256, 384, 448, 490, 491, 512};
static const uint8_t frm2_sweep_magic[8] = {'H', 'L', 'O', 'W', 'F', 'S', '0', '1'};

static inline void frm2_tight_order(uint64_t run, uint16_t order[FRM2_TIGHT_SCAN_COUNT])
{
    for (unsigned int i = 0; i < FRM2_TIGHT_SCAN_COUNT; ++i)
        order[i] = FRM2_TIGHT_SCAN_MIN + i / 5U;
    uint32_t random = (uint32_t)run ^ (uint32_t)(run >> 32U);
    for (unsigned int i = FRM2_TIGHT_SCAN_COUNT - 1U; i > 0; --i) {
        random = random * UINT32_C(1664525) + UINT32_C(1013904223);
        unsigned int j = random % (i + 1U);
        uint16_t value = order[i];
        order[i] = order[j];
        order[j] = value;
    }
}

static inline uint16_t frm2_sweep_count(frm2_sweep_mode_t mode)
{
    if (mode == FRM2_SWEEP_TIGHT_SCAN) return FRM2_TIGHT_SCAN_COUNT;
    return mode == FRM2_SWEEP_BYTE_SCAN ? FRM2_BYTE_SCAN_COUNT : FRM2_SWEEP_COUNT;
}

static inline uint16_t frm2_sweep_end(frm2_sweep_mode_t mode)
{
    return frm2_sweep_count(mode) + 1U;
}

static inline uint16_t frm2_sweep_length(uint16_t sequence, frm2_sweep_mode_t mode,
                                        uint64_t run)
{
    if (sequence == 0 || sequence == frm2_sweep_end(mode)) return 24;
    if (sequence > frm2_sweep_count(mode)) return 0;
    if (mode == FRM2_SWEEP_TIGHT_SCAN) {
        uint16_t order[FRM2_TIGHT_SCAN_COUNT];
        frm2_tight_order(run, order);
        return order[sequence - 1U];
    }
    if (mode == FRM2_SWEEP_BYTE_SCAN) {
        unsigned int index = (sequence - 1U) % FRM2_BYTE_SCAN_SIZES;
        unsigned int pass = (sequence - 1U) / FRM2_BYTE_SCAN_SIZES;
        return pass == 1U ? FRM2_BYTE_SCAN_MAX - index : FRM2_BYTE_SCAN_MIN + index;
    }
    return frm2_sweep_sizes[(sequence - 1U) / 10U];
}

static inline uint8_t frm2_sweep_byte(uint64_t run, uint16_t sequence,
                                     unsigned int offset, frm2_sweep_mode_t mode)
{
    if (offset == 7U) return (uint8_t)('1' + mode);
    if (offset < 8U) return frm2_sweep_magic[offset];
    if (offset < 16U) return (uint8_t)(run >> ((offset - 8U) * 8U));
    if (offset < 18U) return (uint8_t)(sequence >> ((offset - 16U) * 8U));
    if (offset < 20U)
        return (uint8_t)(frm2_sweep_length(sequence, mode, run) >> ((offset - 18U) * 8U));
    uint64_t folded = run ^ (run >> 32U);
    folded ^= folded >> 16U;
    folded ^= folded >> 8U;
    return (uint8_t)((offset * 73U) ^ (offset >> 3U) ^ (sequence * 29U) ^ folded ^
                     (mode == FRM2_SWEEP_TIGHT_SCAN ? 0xa5U : 0U));
}

static inline uint16_t frm2_sweep_build(uint8_t frame[512], uint64_t run,
                                       uint16_t sequence, frm2_sweep_mode_t mode)
{
    uint16_t length = frm2_sweep_length(sequence, mode, run);
    for (unsigned int i = 0; i < length; ++i)
        frame[i] = frm2_sweep_byte(run, sequence, i, mode);
    return length;
}

static inline bool frm2_sweep_candidate(const uint8_t *frame, unsigned int length)
{
    return length >= sizeof(frm2_sweep_magic) &&
           memcmp(frame, frm2_sweep_magic, sizeof(frm2_sweep_magic) - 1U) == 0 &&
           frame[7] >= '1' && frame[7] <= '3';
}

/* Return the first invalid offset; -1 means the entire declared packet matches. */
static inline int frm2_sweep_validate(const uint8_t *frame, unsigned int length,
                                      uint64_t *run, uint16_t *sequence)
{
    *run = 0;
    *sequence = 0;
    if (length < 24U) return (int)length;
    if (frame[7] < '1' || frame[7] > '3') return 7;
    frm2_sweep_mode_t mode = frame[7] == '3' ? FRM2_SWEEP_TIGHT_SCAN :
                             frame[7] == '2' ? FRM2_SWEEP_BYTE_SCAN : FRM2_SWEEP_COARSE;
    for (unsigned int i = 0; i < 8U; ++i)
        *run |= (uint64_t)frame[8U + i] << (i * 8U);
    *sequence = (uint16_t)(frame[16] | ((uint16_t)frame[17] << 8U));
    if (*run == 0) return 8;
    if (*sequence > frm2_sweep_end(mode)) return 16;
    if (length != frm2_sweep_length(*sequence, mode, *run)) return 18;
    for (unsigned int i = 0; i < length; ++i)
        if (frame[i] != frm2_sweep_byte(*run, *sequence, i, mode)) return (int)i;
    return -1;
}

typedef struct {
    uint64_t run;
    uint64_t seen[(FRM2_BYTE_SCAN_COUNT + 63U) / 64U];
    uint8_t source[6];
    int64_t began, ended;
    unsigned int corrupt, duplicates;
    bool active, start, end, reported;
    frm2_sweep_mode_t mode;
} frm2_sweep_rx_t;

static inline bool frm2_sweep_has(const frm2_sweep_rx_t *state, uint16_t sequence)
{
    unsigned int index = sequence - 1U;
    return (state->seen[index / 64U] & (UINT64_C(1) << (index % 64U))) != 0;
}

typedef struct {
    uint64_t run;
    uint8_t destination[6];
    int64_t due;
    uint16_t sequence;
    bool selected, done;
    frm2_sweep_mode_t mode;
} frm2_sweep_tx_t;
