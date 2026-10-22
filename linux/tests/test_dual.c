#include "../kernel/dual_core.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    assert(dual_identity(0,0x55564331,0x20000,0xf,2));
    assert(dual_identity(1,0x55564331,0x20000,0x10f,3));
    assert(!dual_identity(0,0x55564331,0x10000,0xf,2));
    assert(!dual_identity(1,0x55564331,0x20000,0xf,2));
    assert(dual_payload_good(1,101,5,2621440));
    assert(!dual_payload_good(0,101,5,2621440));
    assert(!dual_payload_good(1,101,7,2621440));
    assert(!dual_payload_good(1,101,1,2621440));
    assert(!dual_payload_good(1,101,5,2621456));
    dual_u64 stamp;
    assert(dual_sof_timestamp(300,100,1000000,&stamp)==0 && stamp==1001000);
    assert(dual_sof_timestamp(200,100,1000000,&stamp)==0 && stamp==1000500);
    assert(dual_sof_timestamp(99,100,1000000,&stamp)<0); /* common tick reset */
    struct dual_core c;dual_init(&c,8);
    for(unsigned i=0;i<8;i++)assert(dual_queue(&c,i)==0);
    assert(dual_begin(&c,0)<0);
    dual_start(&c);
    int a=dual_begin(&c,0),b=dual_begin(&c,1);
    assert(a==0 && b==1 && a!=b);
    unsigned gen=c.generation,e0=c.ch[0].epoch,e1=c.ch[1].epoch;
    assert(dual_dma_done(&c,0,gen,e0)==0 && !c.ch[1].dma_done);
    assert(dual_dma_done(&c,1,gen,e1+1)<0);
    assert(dual_queue(&c,a)<0);
    assert(dual_complete(&c,1,101,1000,true)==-1);
    assert(dual_pop(&c,1001)==-1); /* older channel still in flight */
    assert(dual_complete(&c,0,100,2000,true)==-1);
    assert(dual_pop(&c,2001)==a && dual_pop(&c,2001)==b);
    assert(dual_queue(&c,a)==0 && dual_queue(&c,b)==0);
    a=dual_begin(&c,0);b=dual_begin(&c,1);
    assert(dual_complete(&c,1,103,3000,true)==-1);
    assert(dual_pop(&c,3001)==-1);
    /* Multiple newer READY frames do not prove the older active frame was lost. */
    int spare=dual_begin(&c,1);
    assert(dual_complete(&c,1,105,4000,true)==-1);
    assert(dual_pop(&c,4001)==-1);
    assert(dual_pop(&c,20003001)==b); /* gap bound, no infinite wait */
    assert(dual_complete(&c,0,102,20004000,true)==a); /* old late completion is ERROR */
    assert(dual_pop(&c,20004001)==spare);
    assert(dual_newer(0,0xffffffffu) && !dual_newer(0xffffffffu,0));
    dual_stop(&c);
    assert(dual_dma_done(&c,0,gen,e0)<0 && dual_begin(&c,1)<0);
    /* Stop/restart cannot accept stale epoch/generation, even if channel reused. */
    for(unsigned i=0;i<8;i++)c.slots[i].state=D_FREE;
    c.ch[0].id=c.ch[1].id=-1;dual_queue(&c,0);dual_start(&c);dual_begin(&c,0);
    assert(dual_dma_done(&c,0,gen,e0)<0);
    dual_complete(&c,0,0xffffffffu,0,true);
    assert(dual_pop(&c,1)==0);dual_queue(&c,0);dual_begin(&c,0);
    dual_complete(&c,0,0,2,true);assert(dual_pop(&c,3)==0);
    puts("PASS dual: owners, channel join, generation, reorder, bounded gap, wrap, STOPPING");
}
