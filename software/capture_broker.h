#ifndef CAPTURE_BROKER_H
#define CAPTURE_BROKER_H
#include <errno.h>
#include <stdbool.h>
#include <string.h>
/* USB copy is synchronous; only latest retention and DP own capture buffers. */
struct capture_broker { unsigned count; int latest; bool queued[16],dp[16]; };
static inline void broker_init(struct capture_broker *b,unsigned count)
{ memset(b,0,sizeof(*b));b->count=count>16?16:count;b->latest=-1;for(unsigned i=0;i<b->count;i++)b->queued[i]=true; }
static inline int broker_dequeue(struct capture_broker *b,unsigned i)
{ if(i>=b->count || !b->queued[i] || b->dp[i] || b->latest==(int)i)return -EINVAL;b->queued[i]=false;return 0; }
static inline int broker_latest(struct capture_broker *b,unsigned i)
{ if(i>=b->count || b->queued[i])return -EINVAL;b->latest=(int)i;return 0; }
static inline bool broker_can_queue(const struct capture_broker *b,unsigned i)
{ return i<b->count && !b->queued[i] && !b->dp[i] && b->latest!=(int)i; }
static inline int broker_queue(struct capture_broker *b,unsigned i)
{ if(!broker_can_queue(b,i))return -EBUSY;b->queued[i]=true;return 0; }
static inline int broker_dp_hold(struct capture_broker *b,unsigned i)
{ if(i>=b->count || b->queued[i] || b->dp[i])return -EINVAL;b->dp[i]=true;return 0; }
static inline int broker_dp_release(struct capture_broker *b,unsigned i)
{ if(i>=b->count || !b->dp[i])return -EINVAL;b->dp[i]=false;return 0; }
#endif
