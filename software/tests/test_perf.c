#include "../perf_stats.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    struct perf_metric m={0};
    assert(perf_percentile(&m,99)==0);
    perf_add(&m,0);perf_add(&m,1000);perf_add(&m,2000);perf_add(&m,8000);
    assert(m.count==4 && m.sum==11000 && m.max==8000);
    assert(perf_percentile(&m,50)==1000);
    assert(perf_percentile(&m,99)==8000);
    perf_add(&m,8001);
    assert(perf_percentile(&m,100)==16000);
    perf_add(&m,UINT64_C(9000000000));
    assert(perf_percentile(&m,100)>=UINT64_C(9000000000));
    m=(struct perf_metric){0};perf_add(&m,UINT64_MAX);
    assert(perf_percentile(&m,99)==UINT64_MAX);
    puts("perf empty, quantiles, boundary rounding and large latency passed");
    return 0;
}
