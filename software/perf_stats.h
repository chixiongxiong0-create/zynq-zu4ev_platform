#ifndef SC130_PERF_STATS_H
#define SC130_PERF_STATS_H
#include <stdint.h>
#include <stdio.h>
/* Log2 microsecond buckets; reported percentiles are upper bounds, not exact
 * quantiles. 48 buckets cover multi-year latencies without per-frame storage or sorting. */
struct perf_metric { uint64_t count,sum,max,bucket[48]; };
static inline void perf_add(struct perf_metric *m,uint64_t ns)
{
    unsigned b=0;uint64_t bound=1000;
    while(ns>bound && b<47){bound*=2;b++;}
    m->count++;m->sum+=ns;if(ns>m->max)m->max=ns;m->bucket[b]++;
}
static inline uint64_t perf_percentile(const struct perf_metric *m,unsigned pc)
{
    if(!m->count)return 0;
    uint64_t target=(m->count*pc+99)/100,total=0,bound=1000;
    for(unsigned i=0;i<48;i++,bound*=2){total+=m->bucket[i];if(total>=target)return i==47 && m->max>bound?m->max:bound;}
    return m->max;
}
static inline void perf_print(const char *name,const struct perf_metric *m)
{
    fprintf(stderr,"PERF metric=%s n=%llu mean_us=%.3f max_us=%.3f p50_upper_us=%.3f p95_upper_us=%.3f p99_upper_us=%.3f\n",
        name,(unsigned long long)m->count,m->count?(double)m->sum/m->count/1000:0,
        m->max/1000.0,perf_percentile(m,50)/1000.0,perf_percentile(m,95)/1000.0,perf_percentile(m,99)/1000.0);
}
#endif
