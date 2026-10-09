#!/usr/bin/env python3
"""Bounded host regression: extract the actual semaphore callback/decision code."""
from pathlib import Path
import os
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parent
source = Path(os.environ.get("NVIDIA_SOURCE", str(root.parent / "source")))
modeset = (source / "kernel-open/nvidia-drm/nvidia-drm-modeset.c").read_text()
notifiers = (source / "src/nvidia-modeset/kapi/src/nvkms-kapi-notifiers.c").read_text()
actual = modeset[modeset.index("struct nv_drm_plane_fence_cb_data {"):
                 modeset.index("static int __nv_drm_convert_in_fences(")]
cancel = notifiers[notifiers.index("void nvKmsKapiCancelDisplaySemaphore("):
                   notifiers.index("NvU32 nvKmsKapiGetDisplaySemaphoreOffset(")]
conversion = modeset[modeset.index("static int __nv_drm_convert_in_fences("):
                     modeset.index("static int __nv_drm_get_syncpt_data(")]
regfail = conversion[conversion.index("        default:\n") + len("        default:\n"):]
regfail = regfail[:regfail.index("            return ret;") + len("            return ret;")]
prelude = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
struct list_head { struct list_head *next, *prev; };
static void INIT_LIST_HEAD(struct list_head *p) { p->next=p->prev=p; }
static bool list_empty(struct list_head *p) { return p->next==p; }
static void list_add_tail(struct list_head *p,struct list_head *h) {
 p->prev=h->prev;p->next=h;h->prev->next=p;h->prev=p;
}
static void list_del_init(struct list_head *p) {
 p->prev->next=p->next;p->next->prev=p->prev;INIT_LIST_HEAD(p);
}
#define list_first_entry(h,t,m) container_of((h)->next,t,m)
typedef struct { int n; } atomic_t;
static void atomic_set(atomic_t *p,int n) { p->n=n; }
static bool atomic_dec_and_test(atomic_t *p) { assert(p->n>0);return --p->n==0; }
typedef int spinlock_t;
static int locks,freed,fence_puts,signals,resets;
#define spin_lock_init(p) (*(p)=0)
#define spin_lock_irqsave(p,f) do { (f)=0;assert(!*(p));*(p)=1;locks++; } while(0)
#define spin_unlock_irqrestore(p,f) do { (void)(f);assert(*(p));*(p)=0;locks--; } while(0)
static void nv_drm_free(void *p) { assert(!locks);freed++;free(p); }
struct dma_fence_cb { int unused; };
struct dma_fence { int refs; };
static void dma_fence_put(struct dma_fence *p) { assert(p->refs>0);p->refs--;fence_puts++; }
typedef unsigned int NvU32;
enum { NVKMS_KAPI_SEMAPHORE_VALUE_NOT_READY, NVKMS_KAPI_SEMAPHORE_VALUE_READY,
       NVKMS_KAPI_SEMAPHORE_VALUE_DONE };
struct NvKmsKapiDevice { struct { void *pLinearAddress; } semaphore; };
struct nvKmsParsedSemaphore { int payload; };
static int slots[4];
static void nvKmsParseSemaphore(NvU32 index,void *address,struct nvKmsParsedSemaphore *p) {
 (void)address;p->payload=slots[index];
}
static void ResetSemaphore(struct NvKmsKapiDevice *d,NvU32 i,int val) {
 assert(d);slots[i]=val;resets++;
}
#define nvAssert assert
struct nv_drm_device { struct NvKmsKapiDevice *pDevice; };
struct NvKmsKapiLayerRequestedConfig {
 struct { struct { bool semaphoreSpecified; } syncParams; } config;
};
typedef struct nv_drm_atomic_state {
 struct list_head fence_semaphores;
} nv_drm_atomic_state_base_t;
static struct nv_drm_atomic_state *to_nv_atomic_state(nv_drm_atomic_state_base_t *p) { return p; }
static void signal_sem(struct NvKmsKapiDevice *d,NvU32 i) {
 assert(locks==1);assert(d);signals++;slots[i]=NVKMS_KAPI_SEMAPHORE_VALUE_READY;
}
void nvKmsKapiCancelDisplaySemaphore(struct NvKmsKapiDevice *,NvU32);
struct kapi { void (*signalDisplaySemaphore)(struct NvKmsKapiDevice *,NvU32);
 void (*cancelDisplaySemaphore)(struct NvKmsKapiDevice *,NvU32); };
static struct kapi funcs={signal_sem,nvKmsKapiCancelDisplaySemaphore},*nvKms=&funcs;
#define NV_DRM_DEV_LOG_ERR(...) ((void)0)
'''
tests = r'''
static struct NvKmsKapiDevice device;
static struct nv_drm_device nvdev={&device};
static struct nv_drm_plane_fence_cb_data *fixture(nv_drm_atomic_state_base_t *s,
 struct NvKmsKapiLayerRequestedConfig *config,unsigned slot) {
 struct nv_drm_plane_fence_cb_data *p=calloc(1,sizeof(*p));assert(p);
 p->nv_dev=&nvdev;p->semaphore_index=slot;p->plane_req_config=config;
 atomic_set(&p->refs,2);spin_lock_init(&p->lock);INIT_LIST_HEAD(&p->transaction_entry);
 list_add_tail(&p->transaction_entry,&s->fence_semaphores);
 config->config.syncParams.semaphoreSpecified=true;slots[slot]=0;return p;
}
static int registration_failure(nv_drm_atomic_state_base_t *s,
 struct NvKmsKapiLayerRequestedConfig *plane_req_config,unsigned semaphore_index) {
 struct nv_drm_device *nv_dev=&nvdev;
 struct nv_drm_plane_fence_cb_data *fence_data=fixture(s,plane_req_config,semaphore_index);
 int ret=-22;
REGISTRATION_FAILURE
}
int main(void) {
 nv_drm_atomic_state_base_t s;struct NvKmsKapiLayerRequestedConfig config={0},other={0};
 struct dma_fence fence;struct nv_drm_plane_fence_cb_data *p;
 INIT_LIST_HEAD(&s.fence_semaphores);
 p=fixture(&s,&config,0);fence.refs=1;
 __nv_drm_plane_fence_cb(&fence,&p->dma_fence_cb);
 assert(p->refs.n==1&&freed==0&&slots[0]==1);
 nv_drm_finish_fence_semaphores(&s,true);
 assert(freed==1&&slots[0]==2&&!config.config.syncParams.semaphoreSpecified);
 puts("PASS signal before cancel, READY cancellation, references");
 p=fixture(&s,&config,0);fence.refs=1;
 nv_drm_finish_fence_semaphores(&s,true);
 assert(p->refs.n==1&&slots[0]==2);slots[0]=0; /* reused by another submission */
 p->nv_dev=(void *)1; /* canceled callback must never dereference this */
 __nv_drm_plane_fence_cb(&fence,&p->dma_fence_cb);
 assert(freed==2&&slots[0]==0&&signals==1);
 puts("PASS cancel before signal, reused slot, stale device pointer");
 p=fixture(&s,&config,0);fence.refs=1;
 nv_drm_finish_fence_semaphores(&s,false);
 assert(p->refs.n==1&&slots[0]==0&&config.config.syncParams.semaphoreSpecified);
 __nv_drm_plane_fence_cb(&fence,&p->dma_fence_cb);
 assert(freed==3&&slots[0]==1&&signals==2);
 puts("PASS success preserves callback GPU behavior");
 p=fixture(&s,&config,0);fence.refs=1;
 assert(registration_failure(&s,&other,1)==-22);
 assert(freed==4&&slots[1]==2&&!other.config.syncParams.semaphoreSpecified);
 nv_drm_finish_fence_semaphores(&s,true);
 p->nv_dev=(void *)1;__nv_drm_plane_fence_cb(&fence,&p->dma_fence_cb);
 assert(freed==5&&slots[0]==2&&signals==2&&fence_puts==4);
 assert(list_empty(&s.fence_semaphores)&&locks==0);
 puts("PASS actual registration-error block and cancellation of prior reservations");
 nv_drm_finish_fence_semaphores(&s,true);assert(freed==5);
 puts("PASS empty/repeated decision cleanup");
}
'''.replace("REGISTRATION_FAILURE", regfail)
with tempfile.TemporaryDirectory(prefix="dp-semaphore-host-") as directory:
    source = Path(directory) / "test.c"
    binary = Path(directory) / "test"
    source.write_text(prelude + cancel + actual + tests)
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-g", str(source), "-o", str(binary)], check=True)
    # LeakSanitizer cannot enumerate threads under this environment's ptrace.
    # Explicit allocation/refcount assertions still check every fixture free.
    environment = dict(os.environ)
    environment["ASAN_OPTIONS"] = "detect_leaks=0"
    subprocess.run([str(binary)], check=True, env=environment)
