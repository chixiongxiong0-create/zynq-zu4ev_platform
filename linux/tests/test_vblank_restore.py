import pathlib,re,subprocess,tempfile,sys
src=pathlib.Path(sys.argv[1]).read_text()
match=re.search(r'static void zynqmp_dpsub_crtc_atomic_begin\(.*?\n\}',src,re.S)
assert match
harness=r'''
#include <assert.h>
#include <stdbool.h>
struct drm_crtc_state { bool active,mode_changed,active_changed,connectors_changed; };
struct drm_crtc { struct drm_crtc_state *state; };
struct drm_atomic_state { int unused; };
static int calls;
static bool drm_atomic_crtc_needs_modeset(const struct drm_crtc_state *s)
{ return s->mode_changed || s->active_changed || s->connectors_changed; }
static void drm_crtc_vblank_on(struct drm_crtc *c) { (void)c;calls++; }
FUNCTION
int main(void) {
 struct drm_crtc_state s={.active=true};struct drm_crtc c={.state=&s};
 /* Pure framebuffer flips must preserve software vblank count. */
 for(int i=0;i<120;i++)zynqmp_dpsub_crtc_atomic_begin(&c,0);
 assert(calls==0);
 /* Initial enable/resume and full active modesets each need restoration. */
 s.active_changed=true;zynqmp_dpsub_crtc_atomic_begin(&c,0);assert(calls==1);
 s.active_changed=false;s.mode_changed=true;zynqmp_dpsub_crtc_atomic_begin(&c,0);assert(calls==2);
 s.mode_changed=false;s.connectors_changed=true;zynqmp_dpsub_crtc_atomic_begin(&c,0);assert(calls==3);
 /* A disable commit must leave the preceding atomic_disable's vblank_off intact. */
 s.active=false;s.active_changed=true;zynqmp_dpsub_crtc_atomic_begin(&c,0);assert(calls==3);
 return 0;
}
'''.replace('FUNCTION',match.group(0))
with tempfile.TemporaryDirectory() as td:
 p=pathlib.Path(td);(p/'test.c').write_text(harness)
 subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Wno-unused-parameter','-Wno-unused-function',str(p/'test.c'),'-o',str(p/'test')],check=True)
 result=subprocess.run([str(p/'test')])
 print('vblank callback lifecycle: PASS' if result.returncode==0 else 'vblank callback lifecycle: FAIL', 'subprocess_returncode='+str(result.returncode),flush=True)
 raise SystemExit(result.returncode)


