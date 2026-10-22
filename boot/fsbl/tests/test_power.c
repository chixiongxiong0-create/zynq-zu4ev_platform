#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define SC130_POWER_TEST
#define SC130_POWER_POLL_LIMIT 16U
static uint32_t state, writes, reads, ready_after;
uint32_t Xil_In32(uintptr_t address) {
 assert(address == 0xffd80100U && "only power status is readable");
 ++reads;
 if (ready_after && reads >= ready_after) state = 0x100000U;
 return state;
}
void Xil_Out32(uintptr_t address, uint32_t value) {
 ++writes;
 assert(value == 0x100000U);
 assert(address == (writes == 1 ? 0xffd80118U : 0xffd80120U));
 assert(writes <= 2);
}
void usleep(unsigned delay) {
 (void)delay;
 assert(0 && "pre-PSU power request must not use an uninitialized timer");
}
#include "../sc130_power.c"
int main(void) {
 state=0x100000U;
 assert(sc130_power_up(0x100000U)==1 && writes==0 && reads==1);
 state=0; reads=0;
 assert(sc130_power_up(0x100000U)==0 && writes==2);
 assert(reads==1+SC130_POWER_POLL_LIMIT);
 state=0; reads=0; writes=0; ready_after=4;
 assert(sc130_power_up(0x100000U)==1 && reads==4 && writes==2);
 puts("POWER_TEST_PASS");
 return 0;
}
