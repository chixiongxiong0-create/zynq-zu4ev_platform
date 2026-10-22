#define main stage_frame_main
#include "../stage-frame.c"
#undef main
#include <assert.h>
int main(void)
{
 unsigned char *source=malloc(FRAME_BYTES),*buffer=malloc(FRAME_BYTES+2);
 assert(source&&buffer);
 for(size_t i=0;i<FRAME_BYTES;i++)source[i]=(unsigned char)(i*37u+i/256u);
 buffer[0]=0x5a;buffer[FRAME_BYTES+1]=0xa5;
 assert(stage_copy(buffer+1,source,FRAME_BYTES)==0);
 assert(buffer[0]==0x5a&&buffer[FRAME_BYTES+1]==0xa5);
 for(size_t i=0;i<FRAME_BYTES;i++)assert(buffer[i+1]==source[i]);
 free(source);free(buffer);
 puts("TEST_PASS stage-frame full2621440 bytecopy readback bounds");return 0;
}
