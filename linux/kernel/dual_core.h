#ifndef SC130_DUAL_CORE_H
#define SC130_DUAL_CORE_H
#ifdef __KERNEL__
#include <linux/types.h>
typedef u32 dual_u32;
typedef u64 dual_u64;
typedef s32 dual_s32;
#else
#include <stdint.h>
#include <stdbool.h>
typedef uint32_t dual_u32;
typedef uint64_t dual_u64;
typedef int32_t dual_s32;
#endif
#define DUAL_MAX 16
#define DUAL_GAP_NS 20000000ull
static inline bool dual_identity(unsigned channel,dual_u32 id,dual_u32 version,dual_u32 cap,dual_u32 phase)
{ return channel<2 && id==0x55564331 && version==0x20000 && cap==(0xf|(channel<<8)) && phase==(2|channel); }
static inline bool dual_payload_good(unsigned channel,dual_u32 seq,dual_u32 guard,dual_u32 bytes)
{ return channel<2 && (seq&1)==channel && (guard&7)==5 && bytes==2621440; }
static inline int dual_sof_timestamp(dual_u64 tick,dual_u64 base_tick,dual_u64 base_ns,dual_u64 *stamp)
{
    if(tick<base_tick || tick-base_tick>((~(dual_u64)0)-base_ns)/5)return -1;
    *stamp=base_ns+(tick-base_tick)*5;return 0;
}
enum dual_state { D_FREE, D_QUEUED, D_CH0, D_CH1, D_READY, D_USER };
struct dual_slot { enum dual_state state; dual_u32 seq; dual_u64 ready_ns; };
struct dual_active { int id; dual_u32 epoch, generation; bool dma_done; };
struct dual_core {
    struct dual_slot slots[DUAL_MAX];
    struct dual_active ch[2];
    unsigned count;
    dual_u32 generation,last;
    bool stopping,last_valid;
};
static inline bool dual_newer(dual_u32 a,dual_u32 b) { return (dual_s32)(a-b)>0; }
static inline void dual_init(struct dual_core *c,unsigned count)
{
    *c=(struct dual_core){.count=count>DUAL_MAX?DUAL_MAX:count,.stopping=true};
    c->ch[0].id=c->ch[1].id=-1;
}
static inline void dual_start(struct dual_core *c)
{ c->generation++;c->stopping=false;c->last_valid=false; }
static inline void dual_stop(struct dual_core *c)
{ c->stopping=true;c->generation++; }
static inline int dual_queue(struct dual_core *c,unsigned id)
{
    if(id>=c->count || (c->slots[id].state!=D_FREE && c->slots[id].state!=D_USER))return -1;
    c->slots[id].state=D_QUEUED;return 0;
}
static inline int dual_begin(struct dual_core *c,unsigned ch)
{
    if(ch>1 || c->stopping || c->ch[ch].id>=0)return -1;
    for(unsigned i=0;i<c->count;i++)if(c->slots[i].state==D_QUEUED) {
        c->slots[i].state=ch?D_CH1:D_CH0;
        c->ch[ch].id=(int)i;c->ch[ch].epoch++;c->ch[ch].generation=c->generation;c->ch[ch].dma_done=false;
        return (int)i;
    }
    return -1;
}
static inline bool dual_matches(struct dual_core *c,unsigned ch,dual_u32 gen,dual_u32 epoch)
{ return ch<2 && !c->stopping && c->ch[ch].id>=0 && c->generation==gen && c->ch[ch].generation==gen && c->ch[ch].epoch==epoch; }
static inline int dual_dma_done(struct dual_core *c,unsigned ch,dual_u32 gen,dual_u32 epoch)
{ if(!dual_matches(c,ch,gen,epoch))return -1;c->ch[ch].dma_done=true;return 0; }
static inline int dual_complete(struct dual_core *c,unsigned ch,dual_u32 seq,dual_u64 now,bool good)
{
    int id=c->ch[ch].id;if(id<0)return -1;c->ch[ch].id=-1;
    if(!good || (c->last_valid && !dual_newer(seq,c->last))) {c->slots[id].state=D_USER;return id;}
    c->slots[id]=(struct dual_slot){D_READY,seq,now};return -1;
}
static inline int dual_pop(struct dual_core *c,dual_u64 now)
{
    if(c->stopping)return -1;
    int best=-1;dual_u64 oldest=~(dual_u64)0;
    for(unsigned i=0;i<c->count;i++)if(c->slots[i].state==D_READY) {
        if(c->slots[i].ready_ns<oldest)oldest=c->slots[i].ready_ns;
        if(best<0 || dual_newer(c->slots[best].seq,c->slots[i].seq))best=(int)i;
    }
    if(best<0)return -1;
    bool gap=!c->last_valid || c->slots[best].seq!=c->last+1;
    bool other_pending=c->ch[0].id>=0 || c->ch[1].id>=0;
    if(gap && other_pending && now-oldest<DUAL_GAP_NS)return -1;
    c->last=c->slots[best].seq;c->last_valid=true;c->slots[best].state=D_USER;return best;
}
#endif
