#ifndef SC130_CONTRACT_H
#define SC130_CONTRACT_H
#include <linux/errno.h>
static inline int sc130_export_status(unsigned long pointer_value)
{
    if(!pointer_value)return -ENOMEM;
    if(pointer_value>=(unsigned long)-4095L)return (int)(long)pointer_value;
    return 0;
}
/* Shared pure predicates: exercised natively, used by the kernel worker. */
static inline int sc130_result_ready(int dma_done,int result_valid,int timedout,
                                    unsigned flags,unsigned epoch,unsigned expected)
{
    return timedout || (result_valid && (flags!=1 || epoch!=expected || dma_done));
}
static inline int sc130_result_good(int dma_done,int result_valid,int timedout,
                                   unsigned flags,unsigned epoch,unsigned expected,
                                   unsigned words,unsigned lines)
{
    return dma_done && result_valid && !timedout && flags==1 && epoch==expected &&
           words==655360 && lines==1024;
}
#endif
