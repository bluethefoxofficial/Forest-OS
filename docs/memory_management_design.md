# Memory Management Subsystem Design for a Hobby Monolithic Kernel

This document describes a practical, Linux-inspired memory management stack for a small monolithic kernel. It emphasizes implementable data structures, invariants, and execution-context rules rather than only high-level concepts.

## Layered Architecture Overview

Linux memory management is a composition of cooperating mechanisms, not a single algorithm. The layers below run from hardware upward; each layer exposes a narrow API to the next:

1. **Hardware MMU & Paging** — page tables, TLB, page faults, privilege enforcement.
2. **Per-process virtual memory (VM)** — address spaces, virtual memory areas (VMAs), demand paging, copy-on-write (COW), stack growth.
3. **Physical memory allocator** — buddy allocator providing page-sized blocks.
4. **Kernel object allocator** — SLAB/SLUB-style caches on top of pages.
5. **Page cache & unified page model** — file-backed and anonymous pages, reference counting, dirty tracking.
6. **Reclaim & eviction** — active/inactive LRU-like lists, swap, writeback.
7. **OOM handling** — detection, badness scoring, victim kill.
8. **Execution-context rules** — allocation flags, sleepability, interrupt safety.
9. **Synchronization & robustness** — locks, barriers, poisoning, guard pages.

Each layer maintains invariants that the next depends on (e.g., page tables only reference allocated pages; the buddy allocator never returns overlapping memory; SLAB caches only hand out initialized freelist nodes).

---

## 1. Virtual Memory and Address Spaces

### Page Tables and Translation

- **Page size:** typically 4 KiB; support larger pages later.
- **Page table structure:** architecture-dependent; assume a 4-level tree (PML4 → PDPT → PD → PT) for x86-64 or 2–3 levels for RISC-V. Each level indexes bits of the virtual address.
- **PTE fields:** present bit, read/write, user/supervisor, accessed, dirty, NX, physical frame number (PFN), and optional COW/soft-dirty bits.
- **Kernel vs user split:** e.g., top 128 TiB mapped for the kernel, lower half per-process for user. Kernel mappings are global (same tables for all processes), marked supervisor-only, and typically set as global to reduce TLB flushes.
- **Isolation:** user PTEs set with the user bit; kernel code never leaves writable+executable mappings; keep guard pages around kernel stacks.

#### Virtual-to-Physical Walk

1. CPU takes virtual address VA.
2. Uses CR3 (x86) / satp (RISC-V) to find root page table.
3. Walk levels using VA index bits; any missing entry with `present=0` triggers a page fault.
4. Final PTE supplies PFN and permissions. Combined with page offset to form PA.
5. TLB caches translations; TLB shootdown needed when unmapping or changing permissions.

### Building Address Spaces

- **Kernel:** identity-map early boot memory, then remap kernel image read-only; map device MMIO with uncached attributes; map per-CPU data and stacks.
- **User:** create a new top-level page table that shares kernel entries; map text/read-only data as RX, data as RW, stack as RW with a guard page; map heap using VMAs.
- **API:** `map_page(mm, va, page, prot)`, `unmap_page(mm, va)`, `protect_page(mm, va, prot)`, `switch_mm(mm)`.

### Page Fault Handling Path

Pseudo flow (process context):

```
page_fault_handler(addr, error) {
    vma = find_vma(current->mm, addr);
    if (!vma || addr < vma->start || addr >= vma->end)
        return segv(SIGSEGV, addr);             // no mapping

    if (write_fault(error) && !(vma->prot & PROT_WRITE))
        return segv(SIGSEGV, addr);             // permission fault

    if (present_pte(vma, addr)) {
        if (write_fault(error) && pte_is_cow(pte))
            return handle_cow(vma, addr, pte);
        return spurious();                      // e.g., protection-key fault
    }

    // Demand fault
    if (vma->flags & VM_ANON)
        return anon_fault(vma, addr);
    if (vma->flags & VM_FILE)
        return file_fault(vma, addr);
    if (vma->flags & VM_GROWSDOWN)
        return maybe_grow_stack(vma, addr);

    return segv(SIGSEGV, addr);
}
```

#### Demand Allocation

```
anon_fault(vma, addr) {
    page = alloc_pages(order=0, GFP_KERNEL);
    if (!page) return handle_oom(addr);
    zero_page(page);
    map_page(current->mm, page_align(addr), page, vma->prot | PTE_A | PTE_D);
}
```

`file_fault` reads or schedules I/O to fill the page cache page, then maps it.

#### Copy-on-Write (COW)

- On fork, map child and parent pages as read-only, set COW bit, increment page refcount.
- On write fault with COW:

```
handle_cow(vma, addr, pte) {
    old = pte_page(pte);
    new = alloc_pages(0, GFP_KERNEL);
    if (!new) return handle_oom(addr);
    copy_page(new, old);
    dec_ref(old);
    map_page(vma->mm, page_align(addr), new, vma->prot | PTE_A | PTE_D | PTE_WRITE);
}
```

#### Stack Growth Heuristic

- VMAs marked `VM_GROWSDOWN` permit faults slightly below the current stack pointer (e.g., 64 KiB guard window).
- Reject faults that are too far away to avoid runaway mappings.

#### Killing on Invalid Access

- Send `SIGSEGV` or equivalent; in-kernel faults with no fixup cause `BUG()`/panic.
- Never leave partially installed PTEs on failure; hold the per-mm page table lock during updates.

---

## 2. Physical Memory Management — Buddy Allocator

### Data Structures

```
#define MAX_ORDER 11                 // 2^10 pages max block size
struct page {
    uint8_t order;                   // order if free
    bool free;
    struct page *next;
    uint16_t refcount;               // for page cache/COW
};

struct free_area {
    struct page *free_list;          // singly linked list of free blocks
};

struct free_area free_areas[MAX_ORDER];
struct page *mem_map;                // array, one per physical page
```

### Initialization

1. Parse firmware memory map; mark reserved regions (firmware, kernel image, device windows).
2. Build `mem_map` covering usable RAM.
3. For each maximal aligned block, insert into the highest possible order list.
4. Invariant: free lists contain non-overlapping, correctly ordered blocks.

### Allocation

```
struct page *alloc_pages(int order) {
    for (int o = order; o < MAX_ORDER; o++) {
        if (!free_areas[o].free_list) continue;
        page = pop(&free_areas[o]);
        while (o > order) {
            o--;
            buddy = page + (1 << o);
            buddy->order = o;
            buddy->free = true;
            push(&free_areas[o], buddy);
        }
        page->free = false;
        page->order = order;
        page->refcount = 1;
        return page;
    }
    return NULL; // caller may trigger reclaim or OOM
}
```

### Free + Coalescing

```
void free_pages(struct page *page, int order) {
    while (order < MAX_ORDER - 1) {
        idx = page - mem_map;
        buddy_idx = idx ^ (1 << order);
        buddy = &mem_map[buddy_idx];
        if (!buddy->free || buddy->order != order)
            break;
        remove_from_free_list(buddy, order);
        if (buddy < page) page = buddy;
        order++;
    }
    page->order = order;
    page->free = true;
    push(&free_areas[order], page);
}
```

- **Fragmentation tradeoff:** higher-order blocks reduce fragmentation but may be scarce; prefer allocating lowest order sufficient, rely on coalescing.
- **API:** `alloc_pages(order, flags)` returning page pointer; `page_to_phys(page)` converts to PFN.

---

## 3. Kernel Object Allocator — SLAB/SLUB Style

### Cache Definition

```
struct kmem_cache {
    size_t obj_size;
    size_t align;
    struct slab *partial;       // slabs with free objects
    struct slab *full;
    struct slab *empty;
    struct percpu_cache *cpu;   // per-CPU freelists
};

struct slab {
    struct slab *next;
    void *freelist;             // singly-linked list of objects
    uint16_t inuse;
    uint16_t total;
};
```

### Slab Creation

- Allocate one or more pages from buddy.
- Carve objects aligned to `cache->align`.
- Initialize `freelist` by pointer-chaining objects.

### Allocation Path

```
void *kmem_cache_alloc(struct kmem_cache *c, gfp_t flags) {
    obj = percpu_pop(c->cpu);          // lock-free per-CPU fast path
    if (obj) return obj;

    slab = c->partial ? c->partial : new_slab(c, flags);
    if (!slab) return NULL;

    obj = slab->freelist;
    slab->freelist = *(void **)obj;
    slab->inuse++;

    if (!slab->freelist) move_slab(slab, &c->full);
    return obj;
}
```

### Free Path

```
void kmem_cache_free(struct kmem_cache *c, void *obj) {
    slab = slab_from_obj(obj);
    *(void **)obj = slab->freelist;
    slab->freelist = obj;
    slab->inuse--;

    if (slab->inuse + 1 == slab->total) move_slab(slab, &c->partial);
    if (slab->inuse == 0) maybe_reclaim_slab(c, slab);
}
```

- **Per-CPU caches:** batch transfer objects between per-CPU freelists and slabs to avoid global locks in interrupt context.
- **Cache coloring:** rotate starting offset within the page when building slabs to reduce index-aliasing in caches.
- **Reclaim:** under pressure, empty slabs can be freed back to buddy; shrinkers may request caches to release memory.

---

## 4. Page Cache & Unified File-Backed Model

- **Representation:** each inode maintains a radix/tree or xarray mapping from file page index to `struct page *`.
- **Reference counts:** `page->refcount` tracks mappings + cache users. COW or multiple `mmap` mappings increment the refcount.
- **Dirty tracking:** PTE dirty bit or explicit `mark_page_dirty` sets `page->dirty`; writeback queues dirty pages.
- **Shared pages:** `read()/write()` and `mmap()` use the same cached page. A page fault on a file VMA looks up/allocates the cached page, loads from disk if needed, then maps it.

Pseudo for lookup:

```
page_cache_get(inode, index) {
    page = radix_lookup(inode->cache, index);
    if (!page) {
        page = alloc_pages(0, GFP_KERNEL);
        if (!page) return NULL;
        read_page_from_disk(inode, index, page);
        radix_insert(inode->cache, index, page);
    }
    inc_ref(page);
    return page;
}
```

---

## 5. Reclaim and Eviction Policies

### Two-List Approximate LRU

- Maintain **active** and **inactive** lists of reclaimable pages per zone.
- **Promotion:** page fault or `mark_page_accessed` moves a page to active or sets `referenced` bit.
- **Demotion:** inactive list populated by aging active pages whose referenced bit is clear.
- **Reclaim scanner:** scans inactive list; if page unreferenced, evict or writeback; if referenced, move back to active.

#### Scanner Pseudocode

```
reclaim_scan(target_pages) {
    while (freed < target_pages) {
        page = pop_lru(inactive);
        if (!page) refill_inactive_from_active();

        if (page->referenced) { page->referenced = false; move_to_active(page); continue; }
        if (page->dirty) { enqueue_writeback(page); move_to_active(page); continue; }

        if (page_mapped(page)) unmap_all_ptes(page); // TLB shootdown
        free_pages(page, 0);
        freed++;
    }
}
```

- **Anonymous vs file-backed:** anonymous pages need swap space before eviction; file-backed can be dropped if clean or written back if dirty.

### Swap

- **Trigger:** when free pages low and reclaim cannot free enough clean/file-backed pages.
- **Swap slots:** bitmap or free list per swap device; `swap_entry_t` stored in PTE when a page is swapped out.
- **Swap-out path:**
  1. Select anonymous page.
  2. Allocate swap slot; write page to disk.
  3. Replace PTE with non-present entry encoding swap slot; clear `present`, set `swapped`.
  4. Drop physical page (free to buddy).
- **Swap-in path (fault):** read slot, allocate page, fill, set PTE present, free slot.

### Avoiding Thrash

- Tune scan ratio of active vs inactive, use referenced bits to resist evicting hot pages, and throttle if rapid refaults occur.

---

## 6. Out-of-Memory Handling

- **Detection:** after reclaim and swap fail to produce pages for `alloc_pages`, declare OOM.
- **Badness score:** heuristic like `score = rss_pages * 10 + oom_adj - nice`; penalize privileged or kernel threads.
- **Victim kill:** send fatal signal; unmap its address space; drop page cache references, which frees memory via refcount reaching zero.
- **Safety:** perform in process context; avoid holding locks that victim exit will need; prefer not to kill PID 1 or kernel threads.

---

## 7. Execution Context Rules

- **Contexts:**
  - **Interrupt:** cannot sleep; must use `GFP_ATOMIC`/non-blocking paths; only per-CPU or lock-free data allowed; avoid I/O.
  - **Softirq/tasklet:** similar to interrupt, though can take spinlocks; still no sleeping.
  - **Process:** may sleep, perform I/O, use `GFP_KERNEL`.
- **Flags:** `GFP_KERNEL` (may block/reclaim), `GFP_ATOMIC` (no block), `GFP_NOWAIT` (fail fast), `GFP_HIGHUSER` (user pages), etc.
- **Bug pattern:** sleeping while holding spinlock or in interrupt leads to deadlock; using `GFP_KERNEL` in IRQ can recurse into reclaim, causing crashes.

---

## 8. Synchronization and Concurrency

- **Buddy allocator:** global spinlocks per order/zone; mitigate contention with per-CPU page caches for order-0 allocations.
- **SLAB:** per-CPU freelists lock-free; global lists guarded by spinlocks; disable interrupts when manipulating per-CPU lists in IRQ context.
- **Page tables:** per-mm read-write lock; PTE updates use atomic writes; TLB shootdowns coordinated with IPIs.
- **Page cache:** radix/xarray uses RCU for lookups, spinlocks for insertion/removal.
- **Memory barriers:** ensure PTE visible before setting present bit; use `smp_wmb()` before waking waiters after page is ready.

---

## 9. Robustness & Debugging Features

- **Guard pages:** unmapped page below each kernel/user stack to catch overflow.
- **Red zones:** padding around slab objects; fill with canary values and verify on free.
- **Page poisoning:** fill freed pages with pattern (e.g., 0xAA) to detect use-after-free.
- **KASAN-lite:** optional shadow memory to detect out-of-bounds accesses in kernel.
- **Refcount debug:** saturating refcounts and checks on underflow.
- **Panic-on-`BUG_ON`/assert:** fail fast on invariant violation to avoid silent corruption.

---

## Workflows

### Page Fault (Demand/COW) Workflow

1. CPU raises page fault with address + error code.
2. Kernel locates VMA, validates access.
3. If COW and write: allocate page, copy, remap writable.
4. If absent and valid: allocate/bring page (zeroed or file-backed), install PTE, flush TLB entry.
5. On failure: reclaim/swap; if still failing, OOM or SIGSEGV.

### Page Allocation Workflow

1. Caller chooses flags (`GFP_KERNEL` vs `GFP_ATOMIC`).
2. Try per-CPU cache (fast path for order-0).
3. Fallback to buddy; if empty and flags allow, trigger reclaim/swap.
4. Return page or NULL; caller handles OOM.

### SLAB Allocation Workflow

1. Try per-CPU freelist; if empty, pull a slab from partial or allocate new slab pages.
2. Pop object from slab freelist; update `inuse`; move slab state lists.
3. Freeing pushes back, potentially returning empty slab to buddy.

### Reclaim Workflow

1. Kswapd or direct reclaim scans inactive list.
2. For clean file pages: drop mapping and free.
3. For dirty pages: queue writeback, move active; retry later.
4. For anonymous pages: allocate swap, write out, replace PTE with swap entry.

---

## Incremental Implementation Roadmap

1. **Boot-time buddy allocator** — parse memory map, provide `alloc_pages/free_pages`; simple spinlock protection.
2. **Minimal page tables & faults** — set up kernel mappings, create per-process page tables, implement basic page fault to zero-fill anonymous pages; no COW yet.
3. **SLAB allocator** — fixed-size caches for common objects (tasks, inodes); per-CPU freelists.
4. **COW & `fork()` support** — add refcounted pages, write-fault handler.
5. **Page cache + `mmap`** — shared file-backed pages; dirty tracking; simple writeback stub.
6. **LRU-like reclaim** — active/inactive lists; reclaim scanner; integrate with allocation slow path.
7. **Swap support** — swap slots, swap-out/in, anonymous reclaim.
8. **OOM killer** — badness heuristic, victim kill path.
9. **Robustness tooling** — guard pages, poisoning, red zones, optional sanitizers.
10. **Optimizations** — cache coloring, large pages, NUMA awareness, transparent huge pages.

### Invariants to Maintain Always

- No present PTE points to an unallocated or freed page.
- `page->refcount` never underflows; pages freed only when refcount hits zero.
- Free lists contain non-overlapping blocks; buddy order matches list.
- Interrupt context never sleeps or uses blocking allocation.
- TLB coherence: any PTE removal/change is followed by shootdown/flush.

Following this staged plan keeps the kernel usable while progressively approaching Linux-like sophistication.
