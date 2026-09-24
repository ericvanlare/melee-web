/* Checked read-only SRAM leaf for original Synth startup. The helper injects
 * the pinned OSGetSoundMode body; this does not model OSInitSram or EXI. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dolphin/os.h>
#include <dolphin/os/OSRtc.h>

_Static_assert(sizeof(OSSram) == 20, "SDK SRAM ABI changed");
_Static_assert(offsetof(OSSram, flags) == 19, "SDK SRAM flag offset changed");
_Static_assert(sizeof(unsigned long) == 4, "getter requires source unsigned long");
static unsigned char settings[64];
static unsigned char initial[64];
static int initialized, locked;
static BOOL saved_interrupts;
static unsigned lock_count, unlock_count;

static void sram_fail(const char* message)
{
    fprintf(stderr, "source Synth SRAM: %s\n", message);
    abort();
}
void melee_web_source_synth_sram_initialize(const unsigned char* bytes, unsigned length)
{
    if (initialized || locked || !bytes || length != sizeof(settings))
        sram_fail("invalid initial SRAM ownership");
    memcpy(settings, bytes, sizeof(settings));
    memcpy(initial, bytes, sizeof(initial));
    initialized = 1;
}
OSSram* __OSLockSram(void)
{
    if (!initialized || locked) sram_fail("missing or nested SRAM ownership");
    saved_interrupts = OSDisableInterrupts();
    locked = 1;
    ++lock_count;
    return (OSSram*)settings;
}
BOOL __OSUnlockSram(BOOL commit)
{
    if (!initialized || !locked) sram_fail("SRAM unlock has no owner");
    /* Inspect the shared interrupt state without creating a second owner. */
    if (OSDisableInterrupts() != FALSE) sram_fail("SRAM lock lost interrupt mask");
    if (commit) sram_fail("SRAM writes are outside startup read boundary");
    if (memcmp(settings, initial, sizeof(settings))) sram_fail("SRAM bytes changed");
    locked = 0;
    ++unlock_count;
    OSRestoreInterrupts(saved_interrupts);
    return TRUE;
}
unsigned melee_web_source_synth_sram_reads(void)
{
    if (!initialized || locked || lock_count != unlock_count)
        sram_fail("SRAM read lifecycle is incomplete");
    return lock_count;
}

/* MELEE_WEB_PINNED_SOUND_MODE */
