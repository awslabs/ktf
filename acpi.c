#include <ktf.h>
#include <lib.h>
#include <string.h>
#include <acpi.h>
#include <page.h>
#include <pagetable.h>
#include <percpu.h>
#include <mm/pmm.h>

acpi_table_t *acpi_tables[128];
unsigned max_acpi_tables;

static unsigned nr_cpus;

static inline uint8_t get_checksum(void *ptr, size_t len) {
    uint8_t checksum = 0;

    for (int i = 0; i < len; i++)
        checksum += *((uint8_t *) ptr + i);

    return checksum;
}

static inline bool validate_rsdp(rsdp_rev1_t *ptr) {
    const char *rsdp_signature = "RSD PTR ";

    if (memcmp(rsdp_signature, &ptr->signature, sizeof(ptr->signature)))
        return false;

    size_t size = sizeof(rsdp_rev2_t);
    if (ptr->rev < 2)
        size = sizeof(rsdp_rev1_t);

    if (get_checksum(ptr, size) != 0x0)
        return false;

    return true;
}

static inline void *find_rsdp(void *from, void *to) {
    /* RSDP structure is 16 bytes aligned */
    from = _ptr(_ul(from) & ~_ul(0x10));

    for (void *addr = from; _ptr(addr) < to; addr += 0x10) {
        rsdp_rev1_t *rsdp = addr;

        if (validate_rsdp(rsdp)) {
            printk("ACPI: RSDP [%p] v%02x %.*s\n",
                   virt_to_paddr(rsdp), rsdp->rev, sizeof(rsdp->oem_id), rsdp->oem_id);
            return rsdp;
        }
    }

    return NULL;
}

static rsdp_rev1_t *acpi_find_rsdp(void) {
    uint32_t ebda_addr;
    rsdp_rev1_t *rsdp;

    ebda_addr = (* (uint16_t *) paddr_to_virt_kern(EBDA_ADDR_ENTRY)) << 4;
    rsdp = find_rsdp(paddr_to_virt_kern(ebda_addr), paddr_to_virt_kern(ebda_addr + KB(1)));
    if (rsdp)
        return rsdp;

    rsdp = find_rsdp(paddr_to_virt_kern(BIOS_EXPANSION_ADDR_START), paddr_to_virt_kern(MB(1)));
    if (rsdp)
        return rsdp;

   return NULL;
}

static inline void *acpi_map_table(paddr_t pa) {
    unsigned offset = pa & ~PAGE_MASK;
    mfn_t mfn = paddr_to_mfn(pa);

    if (mfn_invalid(mfn))
        return NULL;

    return kmap(mfn, PAGE_ORDER_4K, L1_PROT) + offset;
}

static inline rsdt_t *acpi_find_rsdt(const rsdp_rev1_t *rsdp) {
    rsdt_t *rsdt = acpi_map_table(rsdp->rsdt_paddr);

    if (RSDT_SIGNATURE != rsdt->header.signature)
        return NULL;

    if (get_checksum(rsdt, rsdt->header.length) != 0x0)
        return NULL;

    return rsdt;
}

static inline xsdt_t *acpi_find_xsdt(const rsdp_rev2_t *rsdp) {
    xsdt_t *xsdt = acpi_map_table(rsdp->xsdt_paddr);

    if (XSDT_SIGNATURE != xsdt->header.signature)
        return NULL;

    if (get_checksum(xsdt, xsdt->header.length) != 0x0)
        return NULL;

    return xsdt;
}

static void acpi_dump_tables(void) {
    for (int i = 0; i < max_acpi_tables; i++) {
        acpi_table_t *tab = acpi_tables[i];
        acpi_table_hdr_t *hdr = &tab->header;

        printk("ACPI: %.*s [%p] %04x (v%04x %.*s %04x %.*s %08x)\n",
               sizeof(hdr->signature), &hdr->signature, tab, hdr->length, hdr->rev,
               sizeof(hdr->oem_id), hdr->oem_id, hdr->oem_rev, sizeof(hdr->asl_compiler_id),
               hdr->asl_compiler_id, hdr->asl_compiler_rev);
    }
}

static unsigned process_madt_entries(void) {
    acpi_madt_t *madt = (acpi_madt_t *) acpi_find_table(MADT_SIGNATURE);
    acpi_madt_entry_t *entry;

    printk("ACPI: [MADT] LAPIC Addr: %p, Flags: %08x\n", madt->lapic_addr, madt->flags);

    for (void *addr = madt->entry; addr < (_ptr(madt) + madt->header.length); addr += entry->len) {
        entry = addr;

        switch (entry->type) {
            case ACPI_MADT_TYPE_LAPIC: {
                acpi_madt_processor_t *madt_cpu = (acpi_madt_processor_t *) entry->data;
                percpu_t *percpu = get_percpu_page(madt_cpu->apic_proc_id);

                percpu->id = madt_cpu->apic_proc_id;
                percpu->apic_id = madt_cpu->apic_id;
                percpu->enabled = madt_cpu->flags & 0x1;
                if (madt_cpu->apic_proc_id == 0)
                    percpu->bsp = true;

                nr_cpus++;
                printk("ACPI: [MADT] APIC Processor ID: %u, APIC ID: %u, Flags: %08x\n",
                       madt_cpu->apic_proc_id, madt_cpu->apic_id, madt_cpu->flags);
                break;
            }
            case ACPI_MADT_TYPE_IOAPIC:
            case ACPI_MADT_TYPE_IRQ_SRC:
            case ACPI_MADT_TYPE_NMI:
            case ACPI_MADT_TYPE_LAPIC_ADDR:
                break;
            default:
                panic("Unknown ACPI MADT entry type: %u\n", entry->type);
        }
    }

    return nr_cpus;
}

acpi_table_t *acpi_find_table(uint32_t signature) {
    for (int i = 0; i < max_acpi_tables; i++) {
        acpi_table_t *tab = acpi_tables[i];

        if (tab->header.signature == signature)
            return tab;
    }

    return NULL;
}

unsigned acpi_get_nr_cpus(void) {
    return nr_cpus;
}

#define ACPI_NR_TABLES(ptr) (((ptr)->header.length - sizeof((ptr)->header)) / sizeof(*(ptr)->entry))
void init_acpi(void) {
    printk("Initializing ACPI support\n");

    rsdp_rev1_t *rsdp = acpi_find_rsdp();
    if (!rsdp)
        return;

    if (rsdp->rev < 2) {
        rsdt_t *rsdt = acpi_find_rsdt(rsdp);

        for (int i = 0; i < ACPI_NR_TABLES(rsdt); i++) {
            acpi_table_t *tab = acpi_map_table(rsdt->entry[i]);

            if (get_checksum(tab, tab->header.length) == 0x0)
                acpi_tables[max_acpi_tables++] = tab;
        }
    }
    else {
        xsdt_t *xsdt = acpi_find_xsdt((rsdp_rev2_t *) rsdp);

        for (int i = 0; i < ACPI_NR_TABLES(xsdt); i++) {
            paddr_t tab_pa = _ul(xsdt->entry[i].high) << 32 | xsdt->entry[i].low;
            acpi_table_t *tab = acpi_map_table(tab_pa);

            if (get_checksum(tab, tab->header.length) == 0x0)
                acpi_tables[max_acpi_tables++] = tab;
        }
    }

    acpi_dump_tables();
    process_madt_entries();
}
