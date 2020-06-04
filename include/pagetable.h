#ifndef KTF_PAGETABLE_H
#define KTF_PAGETABLE_H

#include <compiler.h>
#include <page.h>

#ifndef __ASSEMBLY__

typedef uint64_t pgentry_t;

union pte {
    pgentry_t entry;
    struct __packed {
        unsigned int flags:12;
        unsigned long :47;
        unsigned int flags_top:5;
    };
    struct __packed {
        unsigned long paddr:52;
        unsigned int :12;
    };
    struct __packed {
        unsigned int P:1, RW:1, US:1, PWT:1, PCD:1, A:1, D:1, PAT:1, G:1;
        unsigned int IGN0:3;
        unsigned long mfn:40;
        unsigned int IGN1:7;
        unsigned int PKE: 4, NX:1;
    };
};
typedef union pte pte_t;

union pde {
    pgentry_t entry;
    struct __packed {
        unsigned int flags:12;
        unsigned long :51;
        unsigned int flags_top:1;
    };
    struct __packed {
        unsigned long paddr:52;
        unsigned int :12;
    };
    struct __packed {
        unsigned int P:1, RW:1, US:1, PWT:1, PCD:1, A:1, IGN0:1, Z:1;
        unsigned int IGN1:4;
        unsigned long mfn:40;
        unsigned int IGN2:11, NX:1;
    };
};
typedef union pde pde_t;

union pdpe {
    pgentry_t entry;
    struct __packed {
        unsigned int flags:12;
        unsigned long :51;
        unsigned int flags_top:1;
    };
    struct __packed {
        unsigned long paddr:52;
        unsigned int :12;
    };
    struct __packed {
        unsigned int P:1, RW:1, US:1, PWT:1, PCD:1, A:1, IGN0:1, Z:1;
        unsigned int IGN1:4;
        unsigned long mfn:40;
        unsigned int IGN2:11, NX:1;
    };
};
typedef union pdpe pdpe_t;

#if defined (__x86_64__)
union pml4 {
    pgentry_t entry;
    struct __packed {
        unsigned int flags:12;
        unsigned long :51;
        unsigned int flags_top:1;
    };
    struct __packed {
        unsigned long paddr:52;
        unsigned int :12;
    };
    struct __packed {
        unsigned int P:1, RW:1, US:1, PWT:1, PCD:1, A:1, IGN0:1, Z:1;
        unsigned int IGN1:4;
        unsigned long mfn:40;
        unsigned int IGN2:11, NX:1;
    };
};
typedef union pml4 pml4_t;
#endif

union cr3 {
    unsigned long reg;
    struct __packed {
        unsigned long paddr:52;
        unsigned int :12;
    };
    struct __packed {
        unsigned int IGN0:3, PWT:1, PCD:1, IGN1:7;
        unsigned long mfn:40;
        unsigned int RSVD:12;
    };
    struct __packed {
        unsigned int PCID:12;
        unsigned long :52;
    };
};
typedef union cr3 cr3_t;

extern cr3_t cr3;

typedef unsigned int pt_index_t;

static inline pt_index_t l1_table_index(const void *va) { return (_ul(va) >> L1_PT_SHIFT) & (L1_PT_ENTRIES - 1); }
static inline pt_index_t l2_table_index(const void *va) { return (_ul(va) >> L2_PT_SHIFT) & (L2_PT_ENTRIES - 1); }
static inline pt_index_t l3_table_index(const void *va) { return (_ul(va) >> L3_PT_SHIFT) & (L3_PT_ENTRIES - 1); }
#if defined (__x86_64__)
static inline pt_index_t l4_table_index(const void *va) { return (_ul(va) >> L4_PT_SHIFT) & (L4_PT_ENTRIES - 1); }
#endif

static inline unsigned long l1_index_to_virt(pt_index_t idx) { return _ul(idx) << L1_PT_SHIFT; }
static inline unsigned long l2_index_to_virt(pt_index_t idx) { return _ul(idx) << L2_PT_SHIFT; }
static inline unsigned long l3_index_to_virt(pt_index_t idx) { return _ul(idx) << L3_PT_SHIFT; }
#if defined (__x86_64__)
static inline unsigned long l4_index_to_virt(pt_index_t idx) { return _ul(idx) << L4_PT_SHIFT; }
#endif

static inline void *virt_from_index(pt_index_t l4, pt_index_t l3, pt_index_t l2, pt_index_t l1) {
    return _ptr(l4_index_to_virt(l4) | l3_index_to_virt(l3) | l2_index_to_virt(l2) | l1_index_to_virt(l1));
}

#if defined (__x86_64__)
static inline pml4_t *l4_table_entry(pml4_t *tab, const void *va) { return &tab[l4_table_index(va)]; }
#endif
static inline pdpe_t *l3_table_entry(pdpe_t *tab, const void *va) { return &tab[l3_table_index(va)]; }
static inline pde_t  *l2_table_entry(pde_t *tab, const void *va)  { return &tab[l2_table_index(va)]; }
static inline pte_t  *l1_table_entry(pte_t *tab, const void *va)  { return &tab[l1_table_index(va)]; }

static inline pgentry_t pgentry_from_paddr(paddr_t pa, unsigned long flags) {
    return (pgentry_t) ((pa & ~(PADDR_MASK & PAGE_MASK)) | (flags & _PAGE_ALL_FLAGS));
}

static inline pgentry_t pgentry_from_mfn(mfn_t mfn, unsigned long flags) {
    return pgentry_from_paddr(mfn_to_paddr(mfn), flags);
}

static inline pgentry_t pgentry_from_virt(const void *va, unsigned long flags) {
    return pgentry_from_paddr(virt_to_paddr(va), flags);
}

static inline pml4_t *_get_l4_table(const cr3_t *cr3) {
    return (pml4_t *) mfn_to_virt_kern(cr3->mfn);
}

static inline pml4_t *get_l4_table(void) {
    return _get_l4_table(&cr3);
}

static inline pdpe_t *get_l3_table(const void *va) {
    pml4_t *l3_entry = l4_table_entry(get_l4_table(), va);

    return (pdpe_t *) mfn_to_virt_kern(l3_entry->mfn);
}

static inline pde_t *get_l2_table(const void *va) {
    pdpe_t *l2_entry = l3_table_entry(get_l3_table(va), va);

    return (pde_t *) mfn_to_virt_kern(l2_entry->mfn);
}

static inline pte_t *get_l1_table(const void *va) {
    pde_t *l1_entry = l2_table_entry(get_l2_table(va), va);

    return (pte_t *) mfn_to_virt_kern(l1_entry->mfn);
}

static inline pte_t *get_pte(const void *va) {
    return l1_table_entry(get_l1_table(va), va);
}

#if defined (__x86_64__)
static inline void set_pml4(const void *va, paddr_t pa, unsigned long flags) {
    pml4_t *l4e = l4_table_entry(get_l4_table(), va);

    l4e->entry = pgentry_from_paddr(pa, flags);
}
#endif

static inline void set_pdpe(const void *va, paddr_t pa, unsigned long flags) {
    pdpe_t *l3e = l3_table_entry(get_l3_table(va), va);

    l3e->entry = pgentry_from_paddr(pa, flags);
}

static inline void set_pde(const void *va, paddr_t pa, unsigned long flags) {
    pde_t *l2e = l2_table_entry(get_l2_table(va), va);

    l2e->entry = pgentry_from_paddr(pa, flags);
}

static inline void set_pte(const void *va, paddr_t pa, unsigned long flags) {
    pte_t *l1e = l1_table_entry(get_l1_table(va), va);

    l1e->entry = pgentry_from_paddr(pa, flags);
}

/* External declarations */

extern pte_t l1_pt_entries[L1_PT_ENTRIES];
extern pde_t l2_pt_entries[L2_PT_ENTRIES];
extern pdpe_t l3_pt_entries[L3_PT_ENTRIES];
#if defined (__x86_64__)
extern pml4_t l4_pt_entries[L4_PT_ENTRIES];
#elif defined (__i386__)
#endif

extern void init_pagetables(void);
extern void init_user_pagetables(void);
extern void dump_pagetables(void);
#endif /* __ASSEMBLY__ */

#endif /* KTF_PAGETABLE_H */
