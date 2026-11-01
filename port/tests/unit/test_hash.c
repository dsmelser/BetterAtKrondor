/* The replay logs' hash (hash.c). */
#include <string.h>

#include "unit.h"

#include "../../platform/platform.h"

void unit_hash_covers_every_byte_and_chains(void) {
    unsigned char a[11] = "abcdefghij";
    unsigned char b[11];
    memcpy(b, a, sizeof a);
    uint32_t whole = platform_hash(0, a, sizeof a);
    UNIT_CHECK(platform_hash(0, b, sizeof b) == whole);
    /* A byte past the last whole word counts. */
    b[9] ^= 1;
    UNIT_CHECK(platform_hash(0, b, sizeof b) != whole);
    /* So does the order of the words. */
    memcpy(b, "efghabcdij", sizeof b);
    UNIT_CHECK(platform_hash(0, b, sizeof b) != whole);
    /* Chained calls depend on what came before. */
    UNIT_CHECK(platform_hash(platform_hash(0, a, 4), a + 4, 7) !=
               platform_hash(platform_hash(1, a, 4), a + 4, 7));
}
