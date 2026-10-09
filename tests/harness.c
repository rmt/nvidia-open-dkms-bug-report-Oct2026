#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

struct list_head { struct list_head *next, *prev; };
#define INIT_LIST_HEAD(h) ((h)->next = (h)->prev = (h))
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
static bool list_empty(const struct list_head *h) { return h->next == h; }
static void list_add_tail(struct list_head *n, struct list_head *h) {
    n->prev = h->prev; n->next = h; h->prev->next = n; h->prev = n;
}
static void list_del(struct list_head *n) {
    assert(n->prev->next == n && n->next->prev == n);
    n->prev->next = n->next; n->next->prev = n->prev;
    n->next = n->prev = NULL;
}
static void list_del_init(struct list_head *n) { list_del(n); INIT_LIST_HEAD(n); }
#define list_first_entry_or_null(h,t,m) (list_empty(h) ? NULL : container_of((h)->next,t,m))
#define list_last_entry(h,t,m) container_of((h)->prev,t,m)
#define list_for_each_entry(p,h,m) \
    for (struct list_head *node_ = (h)->next; \
         node_ != (h) && ((p) = container_of(node_, __typeof__(*(p)), m), 1); \
         node_ = node_->next)
#define list_for_each_entry_safe(p,q,h,m) \
    for (struct list_head *node_ = (h)->next, *next_; \
         node_ != (h) && (next_ = node_->next, \
         (p) = container_of(node_, __typeof__(*(p)), m), \
         (q) = next_ == (h) ? NULL : container_of(next_, __typeof__(*(q)), m), \
         (void)(q), 1); node_ = next_)

typedef struct { bool held; int rank, previous; } spinlock_t;
static int lock_rank;
static void spin_lock(spinlock_t *l) {
    assert(!l->held && l->rank > lock_rank); l->held = true;
    l->previous = lock_rank; lock_rank = l->rank;
}
static void spin_unlock(spinlock_t *l) {
    assert(l->held && lock_rank == l->rank); l->held = false;
    lock_rank = l->previous;
}
static int warnings;
#define likely(x) (x)
#define WARN_ON(x) ((x) ? (++warnings, true) : false)
struct drm_device { spinlock_t event_lock; };
struct drm_crtc { struct drm_device *dev; int vblank_refs; };
struct drm_pending_vblank_event { int id; bool sent; };
struct drm_crtc_state { struct drm_pending_vblank_event *event; };
struct nv_drm_flip {
    struct drm_pending_vblank_event *event;
    bool vblank_ref;
    uint32_t pending_events;
    struct list_head list_entry, deferred_flip_list;
};
struct nv_drm_atomic_state;
typedef struct nv_drm_atomic_state nv_drm_atomic_state_base_t;
struct nv_drm_failed_atomic_state { struct list_head entry; nv_drm_atomic_state_base_t *state; };
struct nv_drm_crtc {
    unsigned head;
    struct list_head flip_list, failed_atomic_states;
    bool needs_full_modeset, flip_submission_pending;
    unsigned deferred_submission_events;
    spinlock_t flip_list_lock;
    struct drm_crtc base;
};
struct nv_drm_crtc_state { struct drm_crtc_state base; struct nv_drm_flip *nv_flip; };
struct nv_drm_atomic_state {
    int count, refs;
    struct drm_crtc *crtcs[2];
    struct drm_crtc_state *states[2];
    struct nv_drm_flip *submission_flip[2];
    struct nv_drm_failed_atomic_state failed[2];
};
struct nv_drm_device { struct drm_device base; int flip_event_wq; };
#define to_nv_crtc(p) container_of(p, struct nv_drm_crtc, base)
#define to_nv_crtc_state(p) container_of(p, struct nv_drm_crtc_state, base)
#define to_nv_device(p) container_of(p, struct nv_drm_device, base)
#define to_nv_atomic_state(p) (p)
#define for_each_new_crtc_in_state(s,c,cs,i) \
    for ((i)=0; (i)<(s)->count && ((c)=(s)->crtcs[i], (cs)=(s)->states[i], (void)(cs), 1); ++(i))
#define NV_DRM_DEV_LOG_INFO(d, ...) ((void)(d))
static void wake_up_all(int *wq) { ++*wq; }
#ifdef NV_DRM_ATOMIC_COMMIT_STRUCT_PRESENT
static void drm_atomic_commit_get(nv_drm_atomic_state_base_t *s) { ++s->refs; }
#else
static void drm_atomic_state_get(nv_drm_atomic_state_base_t *s) { ++s->refs; }
#endif
static int order[8], sent, freed, allocated;
static struct nv_drm_flip *freed_ptrs[8];
static void nv_drm_crtc_send_vblank_event(struct drm_crtc *c, struct drm_pending_vblank_event *e) {
    assert(c->dev->event_lock.held && !e->sent && sent < 8);
    e->sent = true; order[sent++] = e->id;
}
static void drm_crtc_vblank_put(struct drm_crtc *c) {
    assert(c->dev->event_lock.held && c->vblank_refs > 0); --c->vblank_refs;
}
static bool nv_drm_vblank_module_param = true;
static int drm_crtc_vblank_get(struct drm_crtc *c) { ++c->vblank_refs; return 0; }
static void nv_drm_free(struct nv_drm_flip *p) {
    if (!p) return;
    assert(freed < 8); freed_ptrs[freed++] = p; free(p);
}
#include "actual-functions.h"

struct fixture {
    struct nv_drm_device dev;
    struct nv_drm_crtc crtc[2];
    struct nv_drm_crtc_state cs[2];
    struct nv_drm_atomic_state state;
    struct drm_pending_vblank_event a, b;
};
static void init(struct fixture *f, int count) {
    *f = (struct fixture){0}; sent = freed = allocated = warnings = lock_rank = 0;
    f->dev.base.event_lock.rank = 1;
    f->a.id = 1; f->b.id = 2;
    f->state.count = count; f->state.refs = 1;
    for (int i=0; i<2; ++i) {
        f->crtc[i].head = i; f->crtc[i].base.dev = &f->dev.base;
        f->crtc[i].flip_list_lock.rank = 2;
        INIT_LIST_HEAD(&f->crtc[i].flip_list);
        INIT_LIST_HEAD(&f->crtc[i].failed_atomic_states);
        f->state.crtcs[i] = &f->crtc[i].base;
        f->state.states[i] = &f->cs[i].base;
    }
}
static struct nv_drm_flip *flip(struct fixture *f, int head,
                              struct drm_pending_vblank_event *e, int pending) {
    struct nv_drm_flip *p = calloc(1, sizeof(*p)); assert(p);
    ++allocated;
    INIT_LIST_HEAD(&p->list_entry); INIT_LIST_HEAD(&p->deferred_flip_list);
    p->pending_events = pending; p->event = e; p->vblank_ref = e != NULL;
    if (e) ++f->crtc[head].base.vblank_refs;
    return p;
}
static void enqueue(struct nv_drm_crtc *c, struct nv_drm_flip *p) {
    nv_drm_crtc_enqueue_flip(c, p);
    assert(!c->flip_list_lock.held && lock_rank == 0);
}
static void done(struct fixture *f) {
    for (int i=0; i<2; ++i) {
        assert(list_empty(&f->crtc[i].flip_list));
        assert(f->crtc[i].base.vblank_refs == 0);
        assert(!f->crtc[i].flip_list_lock.held);
    }
    assert(!f->dev.base.event_lock.held && lock_rank == 0 && warnings == 0);
    assert(allocated == freed);
}
static void retained(struct fixture *f, int count) {
    assert(f->state.refs == 1 + count);
    for (int i=0; i<count; ++i) {
        struct list_head *h = &f->crtc[i].failed_atomic_states;
        assert(h->next == &f->state.failed[i].entry && h->prev == h->next);
        assert(f->state.failed[i].state == &f->state);
        assert(f->crtc[i].needs_full_modeset);
    }
}
static void fifo(void) {
    struct fixture f; init(&f, 1);
    struct nv_drm_flip *a = flip(&f,0,&f.a,2), *b = flip(&f,0,&f.b,1);
    enqueue(&f.crtc[0], a); enqueue(&f.crtc[0], b);
    f.crtc[0].flip_submission_pending = true;
    __nv_drm_handle_flip_event(&f.crtc[0]);
    assert(sent == 0 && a->pending_events == 2 && f.crtc[0].deferred_submission_events == 1);
    nv_drm_end_submission(&f.dev.base, &f.state);
    assert(sent == 0 && a->pending_events == 1 && !f.crtc[0].flip_submission_pending);
    __nv_drm_handle_flip_event(&f.crtc[0]);
    assert(sent == 1 && order[0] == 1 && f.crtc[0].base.vblank_refs == 1);
    __nv_drm_handle_flip_event(&f.crtc[0]);
    assert(sent == 2 && order[1] == 2 && freed == 2); done(&f);
    puts("PASS FIFO A then B with deferred A IRQ and multi-event A");
}
static void abort_case(bool old, bool transferred) {
    struct fixture f; init(&f,1);
    struct nv_drm_flip *a = NULL;
    if (old) { a = flip(&f,0,&f.a,1); enqueue(&f.crtc[0],a); }
    struct nv_drm_flip *b = flip(&f,0,transferred ? &f.b : NULL,1);
    f.cs[0].nv_flip = b;
    if (transferred) { enqueue(&f.crtc[0],b); f.state.submission_flip[0] = b; }
    else f.cs[0].base.event = &f.b;
    f.crtc[0].flip_submission_pending = true;
    if (old) __nv_drm_handle_flip_event(&f.crtc[0]);
    nv_drm_abort_submission(&f.dev.base,&f.state);
    retained(&f,1);
    assert(!f.cs[0].nv_flip && !f.cs[0].base.event && !f.state.submission_flip[0]);
    if (old) {
        assert(sent == 0 && freed == 0 && a->pending_events == 1);
        assert(f.crtc[0].flip_list.next == &a->list_entry && f.crtc[0].flip_list.prev == &a->list_entry);
        assert(a->deferred_flip_list.next == &b->list_entry);
        assert(f.crtc[0].base.vblank_refs == (transferred ? 2 : 1));
    } else { assert(sent == 1 && order[0] == 2 && freed == 1); }
    nv_drm_end_submission(&f.dev.base,&f.state);
    assert(sent == (old ? 2 : 1) && order[old ? 1 : 0] == 2);
    if (old) assert(order[0] == 1 && freed == 2);
    assert(f.crtc[0].deferred_submission_events == 0);
    retained(&f,1); done(&f);
    printf("PASS abort B old=%d transferred=%d; event order, refs, targeted removal\n", old, transferred);
}
static void retained_multi(void) {
    struct fixture f; init(&f,2);
    nv_drm_abort_submission(&f.dev.base,&f.state);
    retained(&f,2);
    nv_drm_end_submission(&f.dev.base,&f.state);
    retained(&f,2); done(&f);
    puts("PASS failed state retains one reference per affected CRTC without flips");
}
static void failed_then_success(void) {
    struct fixture f; init(&f,1);
    struct drm_pending_vblank_event c = {.id = 3};
    struct nv_drm_flip *a = flip(&f,0,&f.a,1), *b = flip(&f,0,&f.b,1);
    enqueue(&f.crtc[0],a); enqueue(&f.crtc[0],b);
    f.cs[0].nv_flip = b; f.state.submission_flip[0] = b;
    f.crtc[0].flip_submission_pending = true;
    nv_drm_abort_submission(&f.dev.base,&f.state);
    nv_drm_end_submission(&f.dev.base,&f.state);
    assert(sent == 0 && freed == 0);
    /* Successful C has a userspace event, but produces no hardware IRQ. */
    struct nv_drm_flip *cf = flip(&f,0,NULL,0);
    cf->event = &c;
    struct nv_drm_crtc_state cs = {.nv_flip = cf};
    actual_success_defer(&f.crtc[0], &cs, &f.crtc[0].base);
    assert(!cs.nv_flip && f.crtc[0].base.vblank_refs == 3);
    assert(a->deferred_flip_list.next == &b->list_entry);
    assert(b->list_entry.next == &cf->list_entry);
    __nv_drm_handle_flip_event(&f.crtc[0]);
    assert(sent == 3 && order[0] == 1 && order[1] == 2 && order[2] == 3);
    retained(&f,1); done(&f);
    puts("PASS A old pending, B failed, C successful without HW IRQ: A,B,C");
}
int main(void) {
    fifo(); abort_case(true,true); abort_case(false,true);
    abort_case(true,false); abort_case(false,false); retained_multi(); failed_then_success();
    return 0;
}
