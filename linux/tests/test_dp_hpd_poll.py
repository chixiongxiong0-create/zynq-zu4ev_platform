"""Exercise the actual DP detect function with HPD/AUX mocks; no kernel required."""
from pathlib import Path
import subprocess,sys,tempfile
root=Path(sys.argv[1])
s=(root/'zynqmp_dp.c').read_text()
start=s.index('static enum drm_connector_status __zynqmp_dp_bridge_detect(')
end=s.index('static enum drm_connector_status zynqmp_dp_bridge_detect(',start)
function=s[start:end]
head=r'''
#include <assert.h>
#include <stdio.h>
typedef unsigned int u32;typedef unsigned char u8;
enum drm_connector_status {connector_status_disconnected,connector_status_connected};
struct zynqmp_dp_link_config{int max_rate;unsigned max_lanes;};
struct zynqmp_dp{struct zynqmp_dp_link_config link_config;int lock,aux;u8 dpcd[16];unsigned num_lanes;enum drm_connector_status status;void *dev;};
static unsigned signal_state,reads,sleeps;static int aux_result=16;
#define ZYNQMP_DP_INTERRUPT_SIGNAL_STATE 0x130
#define ZYNQMP_DP_INTERRUPT_SIGNAL_STATE_HPD 1
#define DP_HIGH_BIT_RATE2 540000
#define lockdep_assert_held(x) ((void)(x))
#define dev_dbg(...) ((void)0)
#define min_t(t,a,b) ((t)(a)<(t)(b)?(t)(a):(t)(b))
static u32 zynqmp_dp_read(struct zynqmp_dp *d,unsigned reg){(void)d;assert(reg==0x130);reads++;return signal_state;}
static void msleep(unsigned t){(void)t;sleeps++;}
static int drm_dp_dpcd_read(int *aux,int off,u8 *out,unsigned size){(void)aux;(void)off;(void)out;(void)size;return aux_result;}
static int drm_dp_max_link_rate(u8 *dpcd){(void)dpcd;return 810000;}
static unsigned drm_dp_max_lane_count(u8 *dpcd){(void)dpcd;return 4;}
'''
tail=r'''
int main(void){
 struct zynqmp_dp d={.num_lanes=2};
 assert(__zynqmp_dp_bridge_detect(&d)==connector_status_disconnected);
 assert(reads==1 && sleeps==0 && "absent sink must not block capture/USB for one second");
 signal_state=1;reads=0;
 assert(__zynqmp_dp_bridge_detect(&d)==connector_status_connected);
 assert(reads==1 && sleeps==0 && d.link_config.max_rate==540000 && d.link_config.max_lanes==2);
 aux_result=-5;
 assert(__zynqmp_dp_bridge_detect(&d)==connector_status_disconnected);
 puts("DP_HPD_DETECT_PASS low-fast high-capability-clamp aux-failure");
}
'''
with tempfile.TemporaryDirectory() as temp:
 p=Path(temp);(p/'test.c').write_text(head+function+tail)
 subprocess.run(['cc','-std=c11','-O2',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
kms=(root/'zynqmp_kms.c').read_text()
assert 'connector->polled = DRM_CONNECTOR_POLL_CONNECT;' in kms,'missed HPD needs kernel connect polling'
print('DP_HPD_CONNECT_POLL_CONFIG_PASS')
assert kms.index('ret = zynqmp_dpsub_kms_init(dpsub);') < kms.index('drm_kms_helper_poll_init(drm);'), 'HPD callbacks/polling must start after connector creation'
print('DP_HPD_INIT_ORDER_PASS')
