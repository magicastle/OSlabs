#include <defs.h>
#include <x86.h>
#include <stdio.h>
#include <string.h>
#include <swap.h>
#include <list.h>


list_entry_t pra_list_head;
list_entry_t* current_clock;


static int
_clock_init_mm(struct mm_struct *mm)
{     
    list_init(&pra_list_head);
    mm->sm_priv = &pra_list_head;
    current_clock = &pra_list_head;
    return 0;
}


static int
_clock_map_swappable(struct mm_struct *mm, uintptr_t addr, struct Page *page, int swap_in)
{
    list_entry_t *head=(list_entry_t*) mm->sm_priv;
    list_entry_t *entry=&(page->pra_page_link);
    assert(entry != NULL && head != NULL);
    list_add_before(current_clock, entry);
    return 0;
}


static int
_clock_swap_out_victim(struct mm_struct *mm, struct Page ** ptr_page, int in_tick)
{
    list_entry_t *head = (list_entry_t*) mm->sm_priv;
    assert(head->next != head);
    pte_t* current_pte = NULL;
    struct Page* current_page = NULL;
    if (current_clock == head) {
        current_clock = current_clock->next;
    }
    while (1) {
        /*
          A  D    op
          0  0    get
          0  1    0  0 write disk
          1  0    0  0
          1  1    0  1  
        */

        // Get current pointer's PTE.
        current_page = le2page(current_clock, pra_page_link);
        current_pte = get_pte(mm->pgdir, current_page->pra_vaddr, 0);
        assert(current_pte != NULL);
        int accessed = (((*current_pte) & PTE_A) != 0);
        int dirty = (((*current_pte) & PTE_D) != 0);

        if (!accessed && !dirty) {
            break;
        }
        (*current_pte) = (*current_pte) & (~PTE_A);
        if (accessed + dirty == 1) {
            (*current_pte) = (*current_pte) & (~PTE_D);
        }
        do {
            current_clock = current_clock->next;
        }while (current_clock == head);
    }
    list_del(current_clock);
    current_clock = current_clock->next;
    *ptr_page = current_page;
    return 0;
}

static void
mark_read(uintptr_t la) {
	pte_t* pt_entry = get_pte(boot_pgdir, la, 0);
	assert(pt_entry != NULL);
	(*pt_entry) = (*pt_entry) | (PTE_A);
}

static void
mark_write(uintptr_t la) {
	pte_t* pt_entry = get_pte(boot_pgdir, la, 0);
	assert(pt_entry != NULL);
	(*pt_entry) = (*pt_entry) | (PTE_A);
	(*pt_entry) = (*pt_entry) | (PTE_D);
}

static int
_clock_check_swap(void) {

    for (int i = 1; i < 5; ++ i) {
		uintptr_t la = (i << 12);
		pte_t* pt_entry = get_pte(boot_pgdir, la, 0);
		assert(pt_entry != NULL);
		(*pt_entry) = (*pt_entry) & (~PTE_A);
		(*pt_entry) = (*pt_entry) & (~PTE_D);
	}

    //a b c d
	cprintf("write Virt Page c in clock_check_swap\n");
    *(unsigned char *)0x3000 = 0x0c;
    mark_write(0x3000);
    assert(pgfault_num==4);
    //a b c(AD) d
    cprintf("read Virt Page a in clock_check_swap\n");
    assert(*(unsigned char *)0x1000 == 0x0a);
    mark_read(0x1000);
    assert(pgfault_num==4);
    //a(A) b c(AD) d
    cprintf("write Virt Page e in clock_check_swap\n");
    *(unsigned char *)0x5000 = 0x0e;
    mark_write(0x5000);
    assert(pgfault_num==5);
    //a e c(AD) d
    cprintf("write Virt Page b in clock_check_swap\n");
    *(unsigned char *)0x2000 = 0x0b;
    mark_write(0x2000);
    assert(pgfault_num==6);
    //a(A) b c(D) e
    return 0;
}


static int
_clock_init(void)
{
    return 0;
}

static int
_clock_set_unswappable(struct mm_struct *mm, uintptr_t addr)
{
    return 0;
}

static int
_clock_tick_event(struct mm_struct *mm)
{ return 0; }


struct swap_manager swap_manager_clock =
{
     .name            = "extended clock swap manager",
     .init            = &_clock_init,
     .init_mm         = &_clock_init_mm,
     .tick_event      = &_clock_tick_event,
     .map_swappable   = &_clock_map_swappable,
     .set_unswappable = &_clock_set_unswappable,
     .swap_out_victim = &_clock_swap_out_victim,
     .check_swap      = &_clock_check_swap,
};
