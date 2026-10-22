/* Debug-only staging breakpoint; production build does not compile this symbol. */
#ifdef SC130_RAM_BOOT_TEST
__attribute__((noinline,used,externally_visible)) void Sc130RamBootReady(void)
{ __asm__ volatile("nop" ::: "memory"); }
#endif
