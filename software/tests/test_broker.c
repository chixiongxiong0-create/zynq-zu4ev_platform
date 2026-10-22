#include <assert.h>
#include "../capture_broker.h"
int main(void) {
 struct capture_broker b; broker_init(&b,8);
 assert(broker_dequeue(&b,0)==0);assert(broker_latest(&b,0)==0);
 assert(!broker_can_queue(&b,0));assert(broker_dp_hold(&b,0)==0);
 assert(broker_dequeue(&b,1)==0);assert(broker_latest(&b,1)==0);
 assert(!broker_can_queue(&b,0));assert(broker_dp_hold(&b,1)==0);
 /* Replacement event releases old active, latest/new active remains held. */
 assert(broker_dp_release(&b,0)==0);assert(broker_can_queue(&b,0));
 assert(broker_queue(&b,0)==0);assert(broker_dequeue(&b,0)==0);
 assert(broker_latest(&b,0)==0);assert(!broker_can_queue(&b,1));
 assert(broker_dp_release(&b,1)==0);assert(broker_can_queue(&b,1));
 assert(broker_queue(&b,1)==0);assert(broker_queue(&b,1)<0);
 assert(broker_dp_hold(&b,1)<0);assert(broker_dp_release(&b,1)<0);
 /* USB stop has no ownership operation on capture or DP. */
 assert(b.latest==0 && !broker_can_queue(&b,0));
 assert(broker_dequeue(&b,16)<0);
 return 0;
}
