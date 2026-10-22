#define main platform_bridge_main
#include "../platform_bridge.c"
#undef main
#include <assert.h>
int main(void)
{
    unsigned char frame[64],original[64];
    for(unsigned i=0;i<sizeof(frame);i++)frame[i]=(unsigned char)(i+17);
    memcpy(original,frame,sizeof(frame));
    usb_sequence_marker(frame,0x1234abcd,false);
    assert(!memcmp(frame,original,sizeof(frame)));
    usb_sequence_marker(frame,0x1234abcd,true);
    const unsigned char expected[8]={0x53,0x43,0x31,0x30,0xcd,0xab,0x34,0x12};
    for(unsigned i=0;i<8;i++){assert(frame[2*i]==expected[i]);assert(frame[2*i+1]==128);}
    assert(!memcmp(frame+16,original+16,sizeof(frame)-16));
    puts("USB marker default-off, magic/LE sequence/chroma/rest tests passed");
    return 0;
}
