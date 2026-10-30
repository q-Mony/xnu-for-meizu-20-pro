/* Bounded console retention for the meizu diagnostic experiment.
 * MEIZULOG on disk remains a 4 KiB header followed by chronological text.
 * Keep the boot-video prefix separate from the full power-of-two ring. */
#ifndef _CONSOLE_MEIZU_LOG_H_
#define _CONSOLE_MEIZU_LOG_H_
#define MEIZU_LOG_RING_SIZE (8U * 1024U * 1024U)
#define MEIZU_LOG_BLOCK_SIZE 4096U
#define MEIZU_LOG_PREFIX_CAP MEIZU_LOG_BLOCK_SIZE
#define MEIZU_LOG_DUMP_SIZE (MEIZU_LOG_BLOCK_SIZE + MEIZU_LOG_PREFIX_CAP + MEIZU_LOG_RING_SIZE)
_Static_assert((MEIZU_LOG_RING_SIZE & (MEIZU_LOG_RING_SIZE - 1)) == 0,
    "console ring indexing requires a power of two");
_Static_assert(MEIZU_LOG_DUMP_SIZE % MEIZU_LOG_BLOCK_SIZE == 0,
    "UFS staging must contain the final padded block");
/* Ring-only marker before an already formatted bare diagnostic message. */
#if !defined(__ASSEMBLER__) && !defined(ASSEMBLER)
#ifdef __cplusplus
extern "C" {
#endif
void meizu_log_timestamp_record(char kind);
#ifdef __cplusplus
}
#endif
#endif
#endif
