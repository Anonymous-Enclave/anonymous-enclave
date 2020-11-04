#include "sm.h"
#include "enclave.h"
#include "enclave_vm.h"
#include "enclave_mm.h"
#include "atomic.h"
#include "mtrap.h"
#include "math.h"

/// mm_region_list maintains the (free?) secure pages in monitor
static struct mm_region_list_t *mm_region_list;
static spinlock_t mm_regions_lock = SPINLOCK_INIT;
extern spinlock_t mbitmap_lock;

/**
 * This function will turn a set of untrusted pages to secure pages.
 * Frist, it will valiated the range is valid.
 * Then, it ensures the pages are untrusted/public now.
 * Afterthat, it updates the metadata of the pages into secure (or private).
 * Last, it unmaps the pages from the host PTEs.
 *
 * FIXME: we should re-consider the order of the last two steps.
 */
int check_and_set_secure_memory(unsigned long paddr, unsigned long size)
{
  int ret = 0;
  if(paddr & (RISCV_PGSIZE-1) || size < RISCV_PGSIZE || size & (RISCV_PGSIZE-1))
  {
    ret = -1;
    return ret;
  }

  spinlock_lock(&mbitmap_lock);

  if(test_public_range(PADDR_TO_PFN(paddr), size >> RISCV_PGSHIFT) != 0)
  {
    ret = -1;
    goto out;
  }
  set_private_range(PADDR_TO_PFN(paddr), size >> RISCV_PGSHIFT);
  unmap_mm_region(paddr, size);

out:
  spinlock_unlock(&mbitmap_lock);
  return ret;
}

/**
 * Free a set of secure pages.
 * It turn the secure pgaes into unsecure (or public)
 * and remap all the pages back to host's PTEs
 */
int __free_secure_memory(unsigned long paddr, unsigned long size)
{
  int ret = 0;

  set_public_range(PADDR_TO_PFN(paddr), size >> RISCV_PGSHIFT);
  remap_mm_region(paddr, size);

out:
  return ret;
}

int free_secure_memory(unsigned long paddr, unsigned long size)
{
  int ret = 0;
  spinlock_lock(&mbitmap_lock);

  set_public_range(PADDR_TO_PFN(paddr), size >> RISCV_PGSHIFT);
  remap_mm_region(paddr, size);

out:
  spinlock_unlock(&mbitmap_lock);
  return ret;
}

/**
 * mm_init adds a new range into mm_region_list for monitor/enclaves to use
 */
uintptr_t mm_init(uintptr_t paddr, unsigned long size)
{
  uintptr_t ret = 0;
  spinlock_lock(&mm_regions_lock);

  if(size < RISCV_PGSIZE || (paddr & (RISCV_PGSIZE-1)) || (size & (RISCV_PGSIZE-1)))
  {
    ret = -1;
    goto out;
  }

  if(check_and_set_secure_memory(paddr, size) != 0)
  {
    ret = -1;
    goto out;
  }

  struct mm_region_list_t* list = (struct mm_region_list_t*)paddr;
  list->paddr = paddr;
  list->size = size;
  list->next = mm_region_list;
  mm_region_list = list;

out:
  spinlock_unlock(&mm_regions_lock);
  return ret;
}

/**
 * mm_alloc returns a memory region
 * The returned memory size is put into resp_size, and the addr in return value
 */
void* mm_alloc(unsigned long req_size, unsigned long *resp_size)
{
  void* ret = NULL;
  spinlock_lock(&mm_regions_lock);

  if(!mm_region_list)
  {
    ret = NULL;
    goto out;
  }

  ret = (void*)(mm_region_list->paddr);
  *resp_size = mm_region_list->size;
  mm_region_list = mm_region_list->next;

out:
  spinlock_unlock(&mm_regions_lock);
  return ret;
}

/**
 * mm_free frees a memory region back to mm_region_list 
 */
int mm_free(void* paddr, unsigned long size)
{
  int ret = 0;
  spinlock_lock(&mm_regions_lock);

  if(size < RISCV_PGSIZE || ((uintptr_t)paddr & (RISCV_PGSIZE-1)) != 0)
  {
    ret = -1;
    goto out;
  }

  struct mm_region_list_t* list = (struct mm_region_list_t*)paddr;
  list->paddr = (uintptr_t)paddr;
  list->size = size;
  list->next = mm_region_list;
  mm_region_list = list;

out:
  spinlock_unlock(&mm_regions_lock);
  return ret;
}

/** 
 * grant enclave access to enclave's memory, it's an empty function now
 */
int grant_enclave_access(struct enclave_t* enclave)
{
  return 0;
}

/** 
 * It's an empty function now.
 */
int retrieve_enclave_access(struct enclave_t *enclave)
{
  return 0;
}
