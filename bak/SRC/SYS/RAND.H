#ifndef BAK_RAND_H
#define BAK_RAND_H

/**
 * @file    RAND.H
 * @brief   Public interface to the game's pseudo-random number generator.
 * @details Declares the house PRNG (@ref rand, its reseed hook
 *          @ref _rand_reset_indices) and the roll macros (@ref RND, @ref RND2,
 *          @ref RNDR) layered on top of it. Every caller that includes this
 *          header gets the game generator in place of the C runtime's.
 */

/**
 * @brief Roll in [0, @p n - 1] from a masked draw.
 * @details Takes the low 12 bits of a @ref rand draw (@c rand() & 0xfff), then
 *          reduces modulo @p n.
 * @param n Exclusive upper bound; must be > 0.
 */
#define RND(n) ((u16)(rand() & 0xfff) % (n))
/**
 * @brief Cheap roll in [0, @p n - 1] with no 12-bit mask.
 * @details Reduces a full @ref rand draw modulo @p n. When @p n is a power of
 *          two the modulo folds to a single AND.
 * @param n Exclusive upper bound; must be > 0.
 */
#define RND2(n) ((u16)rand() % (n))
/**
 * @brief Roll in [@p lo, @p hi] inclusive.
 * @details Built on the masked 12-bit @ref rand draw (see @ref RND).
 * @param lo Inclusive lower bound.
 * @param hi Inclusive upper bound; must be >= @p lo.
 */
#define RNDR(lo, hi) ((lo) + (u16)(rand() & 0xfff) % ((hi) - (lo) + 1))

/**
 * @brief   The game's core pseudo-random generator.
 * @details A subtractive lagged-Fibonacci PRNG that deliberately shadows the C
 *          runtime's @c rand, so every includer draws from the game generator.
 * @return  The next pseudo-random word.
 */
extern s16 rand(void);
/**
 * @brief   Reseed the generator's two ring cursors to their starting offsets.
 * @details Restarts the walk over the state ring without altering the ring
 *          contents. Called once at boot.
 */
extern void _rand_reset_indices(void);

/**
 * @brief   The cosmetic stream: not in the original.
 * @details The original drew its particles, render jitter, ambient sound and animation
 *          scripts' timing from the one generator its combat and encounters use. So what
 *          was drawn, and how many frames a wait took, moved the sequence the rules see.
 *          Those draws come from here instead: the same generator on a ring of its own.
 * @return  The next pseudo-random word of the cosmetic stream.
 */
extern s16 rand_fx(void);
/** @brief @ref RND on the cosmetic stream. */
#define RND_FX(n) ((u16)(rand_fx() & 0xfff) % (n))
/** @brief @ref RND2 on the cosmetic stream. */
#define RND2_FX(n) ((u16)rand_fx() % (n))
/** @brief @ref RNDR on the cosmetic stream. */
#define RNDR_FX(lo, hi) ((lo) + (u16)(rand_fx() & 0xfff) % ((hi) - (lo) + 1))

#endif
