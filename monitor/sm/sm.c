#include "atomic.h"
#include "sm.h"
#include "pmp.h"
#include "enclave.h"
#include "math.h"
#include "enclave_vm.h"
#include "server_enclave.h"
#include "relay_page.h"

#ifndef TARGET_PLATFORM_HEADER
#error "SM requires to specify a certain platform"
#endif
#include TARGET_PLATFORM_HEADER

/**
 * Init secure monitor by invoking platform_init
 */
void sm_init()
{
  platform_init();
}

uintptr_t sm_mm_init(uintptr_t paddr, uintptr_t size)
{
  uintptr_t retval = 0;

  retval = mm_init(paddr, size);

  return retval;
}

uintptr_t sm_mm_extend(uintptr_t paddr, uintptr_t size)
{
  uintptr_t retval = 0;

  retval = mm_init(paddr, size);

  return retval;
}

uintptr_t pt_area_base = 0;
uintptr_t pt_area_size = 0;
uintptr_t mbitmap_base = 0;
uintptr_t mbitmap_size = 0;
uintptr_t pgd_order = 0;
uintptr_t pmd_order = 0;
spinlock_t mbitmap_lock = SPINLOCK_INIT;

/**
 * This function validates whether the enclave environment is ready
 * It will check the PT_AREA and MBitmap.
 * If the two regions are properly configured, it means the host OS
 * has invoked SM_INIT sbi call and everything to run enclave is ready
 */
int enable_enclave()
{
  return pt_area_base && pt_area_size && mbitmap_base && mbitmap_size;
}

int init_mbitmap(uintptr_t _mbitmap_base, uintptr_t _mbitmap_size)
{
  page_meta* meta = (page_meta*)_mbitmap_base;
  uintptr_t cur = 0;
  while(cur < _mbitmap_size)
  {
    *meta = MAKE_PUBLIC_PAGE(NORMAL_PAGE);
    meta += 1;
    cur += sizeof(page_meta);
  }
  // printm("MAKE PUBLIC1\r\n");

  return 0;
}

int contain_private_range(uintptr_t pfn, uintptr_t pagenum)
{
  if(!enable_enclave())
    return 0;

  if(pfn < ((uintptr_t)DRAM_BASE >> RISCV_PGSHIFT))
    return -1;

  pfn = pfn - ((uintptr_t)DRAM_BASE >> RISCV_PGSHIFT);
  page_meta* meta = (page_meta*)mbitmap_base + pfn;
  if((uintptr_t)(meta + pagenum) > (mbitmap_base + mbitmap_size))
    return -1;

  uintptr_t cur = 0;
  while(cur < pagenum)
  {
    if(IS_PRIVATE_PAGE(*meta))
      return 1;
    meta += 1;
    cur += 1;
  }

  return 0;
}

/**
 * The function checks whether a range of physical meory is untrusted memory (for
 *  Host OS/apps to use)
 * Return value:
 * 	-1: some pages are not public (untrusted)
 * 	 0: all pages are public (untrusted)
 */
int test_public_range(uintptr_t pfn, uintptr_t pagenum)
{
  if(!enable_enclave())
    return 0;

  if(pfn < ((uintptr_t)DRAM_BASE >> RISCV_PGSHIFT))
    return -1;

  pfn = pfn - ((uintptr_t)DRAM_BASE >> RISCV_PGSHIFT);
  page_meta* meta = (page_meta*)mbitmap_base + pfn;
  if((uintptr_t)(meta + pagenum) > (mbitmap_base + mbitmap_size))
    return -1;

  uintptr_t cur = 0;
  while(cur < pagenum)
  {
    if(!IS_PUBLIC_PAGE(*meta))
      return -1;
    meta += 1;
    cur += 1;
  }

  return 0;
}

/**
 * This function will set a range of physical pages, [pfn, pfn+pagenum],
 *  to secure pages (or private pages).
 * This function only updates the metadata of physical pages, but not unmap
 * them in the host PT pages.
 * Also, the function will not check whether a page is already secure.
 * The caller of the function should be careful to perform the above two tasks.
 */
int set_private_range(uintptr_t pfn, uintptr_t pagenum)
{
  if(!enable_enclave())
    return 0;

  if(pfn < ((uintptr_t)DRAM_BASE >> RISCV_PGSHIFT))
    return -1;

  pfn = pfn - ((uintptr_t)DRAM_BASE >> RISCV_PGSHIFT);
  page_meta* meta = (page_meta*)mbitmap_base + pfn;
  if((uintptr_t)(meta + pagenum) > (mbitmap_base + mbitmap_size))
    return -1;

  uintptr_t cur = 0;
  while(cur < pagenum)
  {
    *meta = MAKE_PRIVATE_PAGE(*meta);
    meta += 1;
    cur += 1;
  }

  return 0;
}

/**
 * Similiar to set_private_pages, but now we turn a set of pages
 *  into public (or untrusted).
 */
int set_public_range(uintptr_t pfn, uintptr_t pagenum)
{
  if(!enable_enclave())
    return 0;

  if(pfn < ((uintptr_t)DRAM_BASE >> RISCV_PGSHIFT))
    return -1;

  pfn = pfn - ((uintptr_t)DRAM_BASE >> RISCV_PGSHIFT);
  page_meta* meta = (page_meta*)mbitmap_base + pfn;
  if((uintptr_t)(meta + pagenum) > (mbitmap_base + mbitmap_size))
    return -1;

  uintptr_t cur = 0;
  while(cur < pagenum)
  {
    *meta = MAKE_PUBLIC_PAGE(*meta);
    meta += 1;
    cur += 1;
  }
  // printm("MAKE PUBLIC2\r\n");

  return 0;
}

uintptr_t sm_schrodinger_init(uintptr_t paddr, uintptr_t size)
{
  int ret = 0;
  if(!enable_enclave())
    return 0;

  if(paddr & (RISCV_PGSIZE-1) || !(paddr >= (uintptr_t)DRAM_BASE
        /*&& paddr + size <= (uintptr_t)DRAM_BASE + */))
    return -1;

  if(size < RISCV_PGSIZE || size & (RISCV_PGSIZE-1))
    return -1;

  spinlock_lock(&mbitmap_lock);

  uintptr_t pagenum = size >> RISCV_PGSHIFT;
  uintptr_t pfn = PADDR_TO_PFN(paddr);
  if(test_public_range(pfn, pagenum) != 0)
  {
    ret = -1;
    goto out;
  }

  ///fast path
  uintptr_t _pfn = pfn - ((uintptr_t)DRAM_BASE >> RISCV_PGSHIFT);
  page_meta* meta = (page_meta*)mbitmap_base + _pfn;
  uintptr_t cur = 0;
  while(cur < pagenum)
  {
    if(!IS_SCHRODINGER_PAGE(*meta))
      break;
    meta += 1;
    cur += 1;
    _pfn += 1;
  }
  if(cur >= pagenum)
  {
    ret = 0;
    goto out;
  }

  ///slow path
  uintptr_t *pte = (uintptr_t*)pt_area_base;
  uintptr_t pte_pos = 0;
  uintptr_t *pte_end = (uintptr_t*)(pt_area_base + pt_area_size);
  uintptr_t pfn_base = PADDR_TO_PFN((uintptr_t)DRAM_BASE) + _pfn;
  uintptr_t pfn_end = PADDR_TO_PFN(paddr + size);
  uintptr_t _pfn_base = pfn_base - ((uintptr_t)DRAM_BASE >> RISCV_PGSHIFT);
  uintptr_t _pfn_end = pfn_end - ((uintptr_t)DRAM_BASE >> RISCV_PGSHIFT);
  //check whether these page only has one mapping in the kernel
  //pte @ pt entry address
  //pfn @ the pfn in the current pte
  //pte_pos @ the offset begin the pte and pt_area_base
  while(pte < pte_end)
  {
    if(!IS_PGD(*pte) && PTE_VALID(*pte))
    {
      pfn = PTE_TO_PFN(*pte);
      //huge page entry
      if( ((unsigned long)pte >= pt_area_base + (1<<pgd_order)*RISCV_PGSIZE) && ((unsigned long)pte < pt_area_base + (1<<pgd_order)*RISCV_PGSIZE + (1<<pmd_order)*RISCV_PGSIZE)
      &&IS_LEAF_PTE(*pte))
      {
        //the schrodinger page is large than huge page
        if(pfn >= pfn_base && (pfn + RISCV_PTENUM) <= pfn_end)
        {
          _pfn = pfn - ((uintptr_t)DRAM_BASE >> RISCV_PGSHIFT);
          //mark the page as schrodinger page, note: a huge page has 512 schrodinger pages
          for(int i=0; i<RISCV_PTENUM; i++)
          {
            meta = ((page_meta*)mbitmap_base) + _pfn + i;
            //check whether this physical page is already be a schrodinger page, but pt psoition is not current position
            if(IS_SCHRODINGER_PAGE(*meta) && SCHRODINGER_PTE_POS(*meta) != pte_pos)
            {
              printm("M mode: schrodinger_init: page0x%lx is multi mapped\r\n", pfn);
              ret = -1;
              goto failed;
            }
            *meta = MAKE_SCHRODINGER_PAGE(0, pte_pos);
          }
          // printm("M mode: sm schrodinger init: make schrodinger page pte %lx and pfn %lx\r\n", pte, pfn);
        }
        else if(pfn >= pfn_end || (pfn+RISCV_PTENUM )<= pfn_base)
        {
          //There is no  overlap between the  pmd region and schrodinger region
        }
        else
        {
          printm("M mode: ERROR: schrodinger_init: non-split page\r\n");
          return -1;
          // map_pt_pte_page(pte);
        }
      }
      else if( ((unsigned long)pte >= pt_area_base + (1<<pgd_order)*RISCV_PGSIZE + (1<<pmd_order)*RISCV_PGSIZE) && ((unsigned long)pte < pt_area_base + pt_area_size))
      { //pte is located in the pte sub-area
        if(pfn >= pfn_base && pfn < pfn_end)
        {
          printm("M mode: schrodinger_init: pfn %lx in pte\r\n", pfn);
          _pfn = pfn - ((uintptr_t)DRAM_BASE >> RISCV_PGSHIFT);
          meta = (page_meta*)mbitmap_base + _pfn;
          //check whether this physical page is already be a schrodinger page, but pt psoition is not current position
          if(IS_SCHRODINGER_PAGE(*meta) && SCHRODINGER_PTE_POS(*meta) != pte_pos)
          {
            printm("M mode: schrodinger_init: page0x%lx is multi mapped\r\n", pfn);
            ret = -1;
            goto failed;
          }
          *meta = MAKE_SCHRODINGER_PAGE(0, pte_pos);
        }
      }
    }
    pte_pos += 1;
    pte += 1;
  }
  while(_pfn_base < _pfn_end)
  {
    meta = (page_meta*)mbitmap_base + _pfn_base;
    if(!IS_SCHRODINGER_PAGE(*meta))
      *meta = MAKE_ZERO_MAP_PAGE(*meta);
    _pfn_base += 1;
  }
out:
  spinlock_unlock(&mbitmap_lock);
  return ret;

failed:
  _pfn_base = pfn_base - ((uintptr_t)DRAM_BASE >> RISCV_PGSHIFT);
  _pfn_end = pfn_end - ((uintptr_t)DRAM_BASE >> RISCV_PGSHIFT);
  while(_pfn_base < _pfn_end)
  {
    meta = (page_meta*)mbitmap_base + _pfn_base;
    *meta = MAKE_PUBLIC_PAGE(NORMAL_PAGE);
    _pfn_base += 1;
  }
  //      printm("MAKE PUBLIC3\r\n");

  spinlock_unlock(&mbitmap_lock);
  return ret;
}

uintptr_t sm_print(uintptr_t paddr, uintptr_t size)
{
  int zero_map_num = 0;
  int single_map_num = 0;
  int multi_map_num = 0;
  uintptr_t pfn = PADDR_TO_PFN(paddr);
  uintptr_t _pfn = pfn - PADDR_TO_PFN((uintptr_t)DRAM_BASE);
  uintptr_t pagenum = size >> RISCV_PGSHIFT;
  page_meta* meta = (page_meta*)mbitmap_base + _pfn;
  //printm("sm_print:0x%lx, mbitmap_base:0x%lx, mbitmap_size:0x%lx, pagenum:0x%lx\r\n",sm_print, mbitmap_base, mbitmap_size, mbitmap_size/sizeof(page_meta));
  //printm("sm_print: pfn:0x%lx,_pfn:0x%lx,meta:0x%lx,end:0x%lx\r\n",pfn,_pfn,meta,mbitmap_base + mbitmap_size);
  uintptr_t i = 0;
  while(i < pagenum)
  {
    if(IS_ZERO_MAP_PAGE(*meta))
      zero_map_num+=1;
    else if(IS_SCHRODINGER_PAGE(*meta))
      single_map_num+=1;
    else
      multi_map_num+=1;
    i += 1;
    meta += 1;
  }
  printm("sm_print: paddr:0x%lx, zeromapnum:0x%lx,singleapnum:0x%lx,multimapnum:0x%lx\r\n",
      paddr, zero_map_num, single_map_num, multi_map_num);
  return 0;
}

/**
 * \brief split the pte (a huge page, 2M) into new_pte_addr (4K PT page)
 */
uintptr_t sm_map_pte(uintptr_t* pte, uintptr_t* new_pte_addr)
{
  unsigned long old_pte = *pte;
  unsigned long pte_attribute = PAGE_ATTRIBUTION(*pte);
  unsigned long pfn = PTE_TO_PFN(*pte);
  // printm("M mode: sm_map_pte finish pte %lx new_pte_addr %lx\r\n", pte, new_pte_addr);
  *pte = PA_TO_PTE((uintptr_t)new_pte_addr, PTE_V);
  for(int i = 0; i <RISCV_PTENUM; i++)
  {
    new_pte_addr[i] = PFN_TO_PTE((pfn + i), pte_attribute);
  }
  return 0;
}

/**
 * \brief it finds the target pte entry for a huge page
 * FIXME: turn the functionality into host OS
 */
uintptr_t sm_split_huge_page(unsigned long paddr, unsigned long size, uintptr_t split_pte)
{
  struct pt_entry_t split_pte_local;
  uintptr_t retval = 0;
  // printm("M mode: split huge page: paddr %lx size %lx split_pte %lx\r\n", paddr, size, split_pte);
  retval = copy_from_host(&split_pte_local,
      (struct pt_entry_t*)split_pte,
      sizeof(struct pt_entry_t));
  if(paddr < (uintptr_t)DRAM_BASE /*|| (paddr + size) > */)
    return -1;
  uintptr_t _pfn = PADDR_TO_PFN(paddr) - PADDR_TO_PFN((uintptr_t)DRAM_BASE);
  uintptr_t pfn_base = PADDR_TO_PFN((uintptr_t)DRAM_BASE) + _pfn;
  uintptr_t pfn_end = PADDR_TO_PFN(paddr + size);
  uintptr_t *pte = (uintptr_t*)pt_area_base;
  uintptr_t *pte_end = (uintptr_t*)(pt_area_base + pt_area_size);
    // printm("slow path for unmap pt region pfn_base %lx pfn_end %lx size %lx\r\n", pfn_base, pfn_end, size);
  while(pte < pte_end)
  {
    if(!IS_PGD(*pte) && PTE_VALID(*pte))
    {
      uintptr_t pfn = PTE_TO_PFN(*pte);
      if( ((unsigned long)pte >= pt_area_base + (1<<pgd_order)*RISCV_PGSIZE) && ((unsigned long)pte < pt_area_base + (1<<pgd_order)*RISCV_PGSIZE + (1<<pmd_order)*RISCV_PGSIZE)
      &&IS_LEAF_PTE(*pte))
      {
        // printm("pte %lx pfn %lx \r\n", pte, pfn);
        if(pfn >= pfn_end || (pfn+RISCV_PTENUM )<= pfn_base)
        {
          //There is no  overlap between the  pmd region and remap region
          pte += 1;
          continue;
        }
        else if(pfn_base<=pfn && pfn_end>=(pfn+RISCV_PTENUM))
        {
          pte += 1;
          continue;
        }
        else
        {
          // printm("M mode: split_huge_page: find the corresponding pt entry\r\n");
          split_pte_local.pte_addr = (unsigned long)pte;
          split_pte_local.pte = *pte;
	  break;
        }
      }
    }
    pte += 1;
  }
  retval = copy_to_host((struct pt_entry_t*)split_pte,
      &split_pte_local,
      sizeof(struct pt_entry_t));
  return 0;
}

/*
 * Unmap a memory region, [paddr, paddr + size], from host's all PTEs
 * We can achieve a fast path unmapping if the unmapped pages are SCHRODINGER PAGEs.
 */
int unmap_mm_region(unsigned long paddr, unsigned long size)
{
  if(!enable_enclave())
    return 0;

  if(paddr < (uintptr_t)DRAM_BASE /*|| (paddr + size) > */)
    return -1;

  //fast path
  uintptr_t _pfn = PADDR_TO_PFN(paddr) - PADDR_TO_PFN((uintptr_t)DRAM_BASE);
  page_meta* meta = (page_meta*)mbitmap_base + _pfn;
  uintptr_t pagenum = size >> RISCV_PGSHIFT;
  uintptr_t cur = 0;
  while(cur < pagenum)
  {
    if(!IS_SCHRODINGER_PAGE(*meta))
      break;
    if(!IS_ZERO_MAP_PAGE(*meta))
    {
      //get pte addr in the pt_area region
      uintptr_t *pte = (uintptr_t*)pt_area_base + SCHRODINGER_PTE_POS(*meta);
      *pte = INVALIDATE_PTE(*pte);
    }
    cur += 1;
    _pfn += 1;
    meta += 1;
  }
  if(cur >= pagenum)
    return 0;

  //slow path
  if(_pfn != (PADDR_TO_PFN(paddr) - PADDR_TO_PFN((uintptr_t)DRAM_BASE)))
  {
    printm("M mode: Error in unmap_mm_region, pfn is conflict, _pfn_old is %lx _pfn_new is %lx\r\n",
		    _pfn, (PADDR_TO_PFN(paddr) - PADDR_TO_PFN((uintptr_t)DRAM_BASE)));
  }
  uintptr_t pfn_base = PADDR_TO_PFN((uintptr_t)DRAM_BASE) + _pfn;
  uintptr_t pfn_end = PADDR_TO_PFN(paddr + size);
  uintptr_t *pte = (uintptr_t*)pt_area_base;
  uintptr_t *pte_end = (uintptr_t*)(pt_area_base + pt_area_size);
  // printm("slow path for unmap pt region pfn_base %lx pfn_end %lx size %lx\r\n", pfn_base, pfn_end, size);

  while(pte < pte_end)
  {
    if(!IS_PGD(*pte) && PTE_VALID(*pte))
    {
      uintptr_t pfn = PTE_TO_PFN(*pte);
      if( ((unsigned long)pte >= pt_area_base + (1<<pgd_order)*RISCV_PGSIZE)
		      && ((unsigned long)pte < pt_area_base + (1<<pgd_order)*RISCV_PGSIZE + (1<<pmd_order)*RISCV_PGSIZE)
		      && IS_LEAF_PTE(*pte))
      {
        // printm("pte %lx pfn %lx \r\n", pte, pfn);
        if(pfn >= pfn_end || (pfn+RISCV_PTENUM )<= pfn_base)
        {
          //There is no  overlap between the  pmd region and remap region
          pte += 1;
          continue;
        }
        else if(pfn_base<=pfn && pfn_end>=(pfn+RISCV_PTENUM))
        {
          //This huge page is covered by remap region
          *pte = INVALIDATE_PTE(*pte);
        }
        else
        {
          printm("M mode: ERROR: unmap_mm_region: non-split page\r\n");
          return -1;
        }
      }
      else if( ((unsigned long)pte >= pt_area_base + (1<<pgd_order)*RISCV_PGSIZE + (1<<pmd_order)*RISCV_PGSIZE)
		      && ((unsigned long)pte < pt_area_base + pt_area_size)
		      && IS_LEAF_PTE(*pte))
      {
        if(pfn >= pfn_base && pfn < pfn_end)
        {
          *pte = INVALIDATE_PTE(*pte);
        }
      }
    }
    pte += 1;
  }

  return 0;
}

/**
 * Remap a set of pages to host PTEs.
 * It's usually used when we try to free a set of secure pages.
 */
int remap_mm_region(unsigned long paddr, unsigned long size)
{
  if(!enable_enclave())
    return 0;

  if(paddr < (uintptr_t)DRAM_BASE /*|| (paddr + size) > */)
    return -1;

  //fast path
  uintptr_t _pfn = PADDR_TO_PFN(paddr) - PADDR_TO_PFN((uintptr_t)DRAM_BASE);
  page_meta* meta = (page_meta*)mbitmap_base + _pfn;
  uintptr_t cur = 0;
  uintptr_t pagenum = size >> RISCV_PGSHIFT;
  while(cur < pagenum)
  {
    if(!IS_SCHRODINGER_PAGE(*meta))
      break;
    if(!IS_ZERO_MAP_PAGE(*meta))
    {
      uintptr_t *pte = (uintptr_t*)pt_area_base + SCHRODINGER_PTE_POS(*meta);
      *pte = VALIDATE_PTE(*pte);
    }
    cur += 1;
    _pfn += 1;
    meta += 1;
  }
  if(cur >= pagenum)
    return 0;

  //slow path
  // printm("slow path for remap pt region\r\n");
  uintptr_t pfn_base = PADDR_TO_PFN((uintptr_t)DRAM_BASE) + _pfn;
  uintptr_t pfn_end = PADDR_TO_PFN(paddr + size);
  uintptr_t *pte = (uintptr_t*)pt_area_base;
  uintptr_t *pte_end = (uintptr_t*)(pt_area_base + pt_area_size);
  while(pte < pte_end)
  {
    if(!IS_PGD(*pte))
    {
      uintptr_t pfn = PTE_TO_PFN(*pte);
      if( ((unsigned long)pte >= pt_area_base + (1<<pgd_order)*RISCV_PGSIZE) && ((unsigned long)pte < pt_area_base + (1<<pgd_order)*RISCV_PGSIZE + (1<<pmd_order)*RISCV_PGSIZE))
      {
        // printm("pte %lx pfn %lx \r\n", pte, pfn);
        if(pfn >= pfn_end || (pfn+RISCV_PTENUM )<= pfn_base)
        {
          //There is no  overlap between the  pmd region and remap region
          pte += 1;
          continue;
        }
        else if(pfn_base<=pfn && pfn_end>=(pfn+RISCV_PTENUM))
        {
          //This huge page is covered by remap region
          *pte = VALIDATE_PTE(*pte);
        }
        else
        {
          printm("M mode: ERROR: The partial of his huge page is belong to enclave and the rest is belong to untrusted OS\r\n");
          return -1;
        }
      }
      else if( ((unsigned long)pte >= pt_area_base + (1<<pgd_order)*RISCV_PGSIZE + (1<<pmd_order)*RISCV_PGSIZE) && ((unsigned long)pte < pt_area_base + pt_area_size))
      {
        if(pfn >= pfn_base && pfn < pfn_end)
        {
          *pte = VALIDATE_PTE(*pte);
        }
      }
    }
    pte += 1;
  }

  return 0;
}
/*
  pte_dest @ the location of pt entry in pt area
  pte_src @ the content of pt entry
*/
int set_single_pte(uintptr_t *pte_dest, uintptr_t pte_src)
{
  if(!enable_enclave())
  {
    *pte_dest = pte_src;
    return 0;
  }

  uintptr_t pfn = 0;
  uintptr_t _pfn = 0;
  page_meta* meta = NULL;
  int huge_page = 0;
  //check whether it is a huge page mapping
  if( ((unsigned long)pte_dest >= pt_area_base + (1<<pgd_order)*RISCV_PGSIZE) && ((unsigned long)pte_dest < pt_area_base + (1<<pgd_order)*RISCV_PGSIZE + (1<<pmd_order)*RISCV_PGSIZE)
      &&IS_LEAF_PTE(*pte_dest))
    huge_page = 1;
  else
    huge_page = 0;

  if(huge_page == 0)
  {
    //unmap the original page in the old pte
    if(!IS_PGD(*pte_dest) && PTE_VALID(*pte_dest))
    {
      pfn = PTE_TO_PFN(*pte_dest);
      _pfn = pfn - PADDR_TO_PFN((uintptr_t)DRAM_BASE);
      meta = (page_meta*)mbitmap_base + _pfn;
      if(IS_SCHRODINGER_PAGE(*meta))
      {
        *meta = MAKE_ZERO_MAP_PAGE(*meta);
      }
    }
    //map the new page according to the pte_src
    if(!IS_PGD(pte_src) && PTE_VALID(pte_src))
    {
      uintptr_t pte_pos = pte_dest - (uintptr_t*)pt_area_base;
      pfn = PTE_TO_PFN(pte_src);
      _pfn = pfn - PADDR_TO_PFN((uintptr_t)DRAM_BASE);
      meta = (page_meta*)mbitmap_base + _pfn;
      if(IS_ZERO_MAP_PAGE(*meta))
      {
        *meta = MAKE_SCHRODINGER_PAGE(0, pte_pos);
      }
      else if(IS_SCHRODINGER_PAGE(*meta))
      {
        *meta = MAKE_PUBLIC_PAGE(NORMAL_PAGE);
        printm("MAKE PUBLIC4 meta %lx\r\n", meta);

      }
    }

    *pte_dest = pte_src;
  }
  else
  {
    if(!IS_PGD(*pte_dest) && PTE_VALID(*pte_dest))
    {
      pfn = PTE_TO_PFN(*pte_dest);
      _pfn = pfn - PADDR_TO_PFN((uintptr_t)DRAM_BASE);
      for(int i = 0; i < RISCV_PTENUM; i++)
      {
        meta = (page_meta*)mbitmap_base + _pfn + i;
        if(IS_SCHRODINGER_PAGE(*meta))
        {
          *meta = MAKE_ZERO_MAP_PAGE(*meta);
        }
      }
      // printm("M mode: set single pte: clear the schrodinger page mapping pte %lx\r\n", pte_dest);
    }

    if(!IS_PGD(pte_src) && PTE_VALID(pte_src))
    {
      uintptr_t pte_pos = pte_dest - (uintptr_t*)pt_area_base;
      pfn = PTE_TO_PFN(pte_src);
      _pfn = pfn - PADDR_TO_PFN((uintptr_t)DRAM_BASE);
      for(int i = 0; i < RISCV_PTENUM; i++)
      {
        meta = (page_meta*)mbitmap_base + _pfn +i;
        if(IS_ZERO_MAP_PAGE(*meta))
          *meta = MAKE_SCHRODINGER_PAGE(0, pte_pos);
        else if(IS_SCHRODINGER_PAGE(*meta))
        {
          *meta = MAKE_PUBLIC_PAGE(NORMAL_PAGE);
                printm("MAKE PUBLIC4 meta %lx\r\n", meta);

        }
      }
    }

    *pte_dest = pte_src;
  }


  return 0;
}
/*
  Monitor check whether the pte mapping is legitimate.
  pte_addr @ page table location in the pt area
  pte_src @ the content of pte_entry
  pa @ the physical address in the pt entry

*/
int check_pt_location(uintptr_t pte_addr, uintptr_t pa, uintptr_t pte_src)
{
  if((pt_area_base < pte_addr) && ((pt_area_base + (1<<pgd_order)*RISCV_PGSIZE) > pte_addr))
  {
    if (((pt_area_base + (1<<pgd_order)*RISCV_PGSIZE) > pa) || ((pt_area_base + ((1<<pgd_order) + (1<<pmd_order))*RISCV_PGSIZE) < pa) )
    {
      printm("pt_area_base %lx pte_addr %lx pa %lx", pt_area_base, pte_addr, pa);
      printm("ERROR: invalid pt location\r\n");
      return -1;
    }
  }
  if(((pt_area_base + (1<<pgd_order)*RISCV_PGSIZE) < pte_addr) && ((pt_area_base + ((1<<pgd_order) + (1<<pmd_order))*RISCV_PGSIZE) > pte_addr))
  {
    if((pte_src & PTE_V) && !(pte_src & PTE_R) && !(pte_src & PTE_W) && !(pte_src & PTE_X))
    {
      if (((pt_area_base + ((1<<pgd_order) + (1<<pmd_order))*RISCV_PGSIZE) > pa) || ((pt_area_base + pt_area_size) < pa) )
      {
        printm("pt_area_base %lx pt_area_pte_base %lx pt_area_pte_end %lx pte_addr %lx pa %lx\r\n", pt_area_base, (pt_area_base + ((1<<pgd_order) + (1<<pmd_order))*RISCV_PGSIZE),
        (pt_area_base + pt_area_size), pte_addr, pa);
        printm("pte_src %lx\r\n", pte_src);
        printm("ERROR: invalid pt location\r\n");
        return -1;
      }
    }
  }
  return 0;
}

uintptr_t sm_set_pte(uintptr_t flag, uintptr_t* pte_addr, uintptr_t pte_src, uintptr_t size)
{
  unsigned long ret = 0;

   //FIXME: Is pt_area memory region public? if so, check below is necessary.
  if(test_public_range(PADDR_TO_PFN((uintptr_t)pte_addr),1) < 0){
    return -1;
  }
  spinlock_lock(&mbitmap_lock);
  switch(flag)
  {
    case SBI_SET_PTE_ONE:
      if((!IS_PGD(pte_src)) && PTE_VALID(pte_src))
      {
        uintptr_t pfn = PTE_TO_PFN(pte_src);
        if (check_pt_location((uintptr_t)pte_addr, PTE_TO_PA(pte_src), pte_src) < 0)
        {
          ret = -1;
          break;
        }
        if(test_public_range(pfn, 1) < 0)
        {
          ret = -1;
          goto free_mbitmap_lock;
        }
      }
      set_single_pte(pte_addr, pte_src);
      //*pte_addr = pte_src;
      break;
    case SBI_PTE_MEMSET:
      if((!IS_PGD(pte_src)) && PTE_VALID(pte_src))
      {
        if(test_public_range(PTE_TO_PFN(pte_src),1) < 0)
        {
          ret = -1;
          goto free_mbitmap_lock;
        }
      }
      //memset(pte_addr, pte_src, size);
      uintptr_t i1 = 0;
      for(i1 = 0; i1 < size/sizeof(uintptr_t); ++i1, ++pte_addr)
      {
        set_single_pte(pte_addr, pte_src);
      }
      break;
    case SBI_PTE_MEMCPY:
      if(size % 8)
      {
        ret = -1;
        goto free_mbitmap_lock;
      }
      unsigned long i=0, pagenum=size>>3;
      for(i=0; i<pagenum; ++i)
      {
        uintptr_t pte = *((uintptr_t*)pte_src + i);
        if(!IS_PGD(pte) && PTE_VALID(pte))
        {
          if(test_public_range(PTE_TO_PFN(pte),1) < 0)
          {
            ret =-1;
            goto free_mbitmap_lock;
          }
        }
      }
      //memcpy(pte_addr, (char*)pte_src, size);
      for(i = 0; i< pagenum; ++i, ++pte_addr)
      {
        uintptr_t pte = *((uintptr_t*)pte_src + i);
        set_single_pte(pte_addr, pte);
      }
      break;
    default:
      ret = -1;
      break;
  }

free_mbitmap_lock:
  spinlock_unlock(&mbitmap_lock);
  return ret;
}

/**
 * SM_INIT: This is an SBI call provided by monitor
 *  The Host OS can invoke the call to init the enclave enviroment, with two regions: [pt_area_base, pt_area_base + area_size]
 *  and [mbitmap_base + mbitmap_size].
 *  The second region is for monitor to maintain metadata for each physical page (e.g., whether a page is secure/non-secure/or
 *  Schrodinger.
 *  The two regions will be protected by PMPs, and this function will synchronize the PMP configs to other HARTs (if have).
 *
 *  The function can only be invoked once (checked by monitor).
 */
uintptr_t sm_sm_init(uintptr_t _pt_area_base, uintptr_t _pt_area_size, uintptr_t _mbitmap_base, uintptr_t _mbitmap_size)
{
  if(pt_area_base && pt_area_size && mbitmap_base && mbitmap_size)
    return -1UL;
  uintptr_t smregion_base = (uintptr_t)SM_BASE;
  uintptr_t smregion_size = (uintptr_t)SM_SIZE;
  if(region_overlap(_pt_area_base, _pt_area_size, smregion_base, smregion_size))
    return -1UL;
  if(region_overlap(_mbitmap_base, _mbitmap_size, smregion_base, smregion_size))
    return -1UL;
  if(region_overlap(_pt_area_base, _pt_area_size, _mbitmap_base, _mbitmap_size))
    return -1UL;
  if(illegal_pmp_addr(_pt_area_base, _pt_area_size) || illegal_pmp_addr(_mbitmap_base, _mbitmap_size))
    return -1UL;

  struct pmp_config_t pmp_config;
  pmp_config.paddr = _mbitmap_base;
  pmp_config.size = _mbitmap_size;
  pmp_config.perm = PMP_NO_PERM;
  pmp_config.mode = PMP_NAPOT;
  //pmp 2 is used to protect mbitmap
  set_pmp_and_sync(2, pmp_config);

  //should protect mbitmap before initializing it
  init_mbitmap(_mbitmap_base, _mbitmap_size);

  //enable pt_area and mbitmap
  //this step must be after initializing mbitmap
  pt_area_size = _pt_area_size;
  pt_area_base = _pt_area_base;
  mbitmap_base = _mbitmap_base;
  mbitmap_size = _mbitmap_size;

  pmp_config.paddr = _pt_area_base;
  pmp_config.size = _pt_area_size;
  pmp_config.perm = PMP_R;
  pmp_config.mode = PMP_NAPOT;
  //pmp 1 is used to protect pt_area
  //this step must be after enabling pt_area and mbitmap
  set_pmp_and_sync(1, pmp_config);

  return 0;
}

/**
 * \brief this function sets the pgd and pmd orders in PT Area
 */
uintptr_t sm_pt_area_separation(uintptr_t tmp_pgd_order, uintptr_t tmp_pmd_order)
{
  pgd_order = tmp_pgd_order;
  pmd_order = tmp_pmd_order;
  uintptr_t mstatus = read_csr(mstatus);
  /* Enable TVM here */
  mstatus = INSERT_FIELD(mstatus, MSTATUS_TVM, 1);
  write_csr(mstatus, mstatus);
  return 0;
}

uintptr_t sm_create_enclave(uintptr_t enclave_sbi_param)
{
  struct enclave_create_param_t enclave_sbi_param_local;
  uintptr_t retval = 0;
  if(test_public_range(PADDR_TO_PFN(enclave_sbi_param),1) < 0){
    return ENCLAVE_ERROR;
  }

  retval = copy_from_host(&enclave_sbi_param_local,
      (struct enclave_create_param_t*)enclave_sbi_param,
      sizeof(struct enclave_create_param_t));
  if(retval != 0)
    return ENCLAVE_ERROR;

  retval = create_enclave(enclave_sbi_param_local);

  return retval;
}

uintptr_t sm_attest_enclave(uintptr_t eid, uintptr_t report, uintptr_t nonce)
{
  uintptr_t retval;

  retval = attest_enclave(eid, report, nonce);

  return retval;
}

uintptr_t sm_attest_shadow_enclave(uintptr_t eid, uintptr_t report, uintptr_t nonce)
{
  uintptr_t retval;

  retval = attest_shadow_enclave(eid, report, nonce);

  return retval;
}

uintptr_t sm_run_enclave(uintptr_t* regs, uintptr_t eid, uintptr_t mm_arg_addr, uintptr_t mm_arg_size)
{
  uintptr_t retval = 0;

  retval = run_enclave(regs, (unsigned int)eid, mm_arg_addr, mm_arg_size);

  return retval;
}

uintptr_t sm_run_shadow_enclave(uintptr_t* regs, uintptr_t eid, uintptr_t shadow_enclave_run_args, uintptr_t mm_arg_addr, uintptr_t mm_arg_size)
{
  struct shadow_enclave_run_param_t enclave_sbi_param_local;
  uintptr_t retval = 0;
  if(test_public_range(PADDR_TO_PFN(shadow_enclave_run_args), 1) < 0){
    return ENCLAVE_ERROR;
  }
  retval = copy_from_host(&enclave_sbi_param_local,
      (struct shadow_enclave_run_param_t*)shadow_enclave_run_args,
      sizeof(struct shadow_enclave_run_param_t));
  if(retval != 0)
    return ENCLAVE_ERROR;

  retval = run_shadow_enclave(regs, (unsigned int)eid, enclave_sbi_param_local, mm_arg_addr, mm_arg_size);
  if (retval ==  ENCLAVE_ATTESTATION)
  {
    copy_to_host((struct shadow_enclave_run_param_t*)shadow_enclave_run_args,
      &enclave_sbi_param_local,
      sizeof(struct shadow_enclave_run_param_t));
  }
  return retval;
}

uintptr_t sm_stop_enclave(uintptr_t* regs, uintptr_t eid)
{
  uintptr_t retval = 0;

  retval = stop_enclave(regs, (unsigned int)eid);

  return retval;
}

uintptr_t sm_resume_enclave(uintptr_t* regs, uintptr_t eid)
{
  uintptr_t retval = 0;
  uintptr_t resume_func_id = regs[11];
  switch(resume_func_id)
  {
    case RESUME_FROM_TIMER_IRQ:
      //printm("resume from timer irq\r\n");
      //*HLS()->timecmp = regs[12];
      //clear_csr(mip, MIP_STIP);
      //set_csr(mie, MIP_MTIP);
      retval = resume_enclave(regs, eid);
      break;
    case RESUME_FROM_STOP:
      //printm("resume from stop\r\n");
      //TODO: maybe it is better to create a new SBI_CALL for wake enclave?
      retval = wake_enclave(regs, eid);
      break;
    case RESUME_FROM_OCALL:
      retval = resume_from_ocall(regs, eid);
      break;
    default:
      break;
  }

  return retval;
}

uintptr_t sm_destroy_enclave(uintptr_t *regs, uintptr_t enclave_id)
{
  uintptr_t ret = 0;

  ret = destroy_enclave(regs, enclave_id);

  return ret;
}

uintptr_t sm_exit_enclave(uintptr_t* regs, uintptr_t retval)
{
  uintptr_t ret = 0;

  ret = exit_enclave(regs, retval);

  return ret;
}

uintptr_t sm_enclave_ocall(uintptr_t* regs, uintptr_t ocall_id, uintptr_t arg0, uintptr_t arg1)
{
  uintptr_t ret = 0;
  switch(ocall_id)
  {
    case OCALL_MMAP:
      ret = enclave_mmap(regs, arg0, arg1);
      break;
    case OCALL_UNMAP:
      ret = enclave_unmap(regs, arg0, arg1);
      break;
    case OCALL_SYS_WRITE:
      ret = enclave_sys_write(regs);
      break;
    case OCALL_SBRK:
      ret = enclave_sbrk(regs, arg0);
      break;
    case OCALL_READ_SECT:
      ret = enclave_read_sec(regs,arg0);
      break;
    case OCALL_WRITE_SECT:
      ret = enclave_write_sec(regs, arg0);
      break;
    default:
      ret = -1UL;
      break;
  }

  return ret;
}

uintptr_t sm_do_timer_irq(uintptr_t *regs, uintptr_t mcause, uintptr_t mepc)
{
  uintptr_t ret = 0;

  ret = do_timer_irq(regs, mcause, mepc);

  return ret;
}

uintptr_t sm_handle_yield(uintptr_t *regs, uintptr_t mcause, uintptr_t mepc)
{
  uintptr_t ret = 0;

  ret = do_yield(regs, mcause, mepc);

  return ret;
}

uintptr_t sm_create_server_enclave(uintptr_t enclave_sbi_param)
{
  struct enclave_create_param_t enclave_sbi_param_local;
  uintptr_t retval = 0;
  if(test_public_range(PADDR_TO_PFN(enclave_sbi_param),1)<0){
    return ENCLAVE_ERROR;
  }
  retval = copy_from_host(&enclave_sbi_param_local,
      (struct enclave_create_param_t*)enclave_sbi_param,
      sizeof(struct enclave_create_param_t));
  if(retval != 0)
    return ENCLAVE_ERROR;

  retval = create_server_enclave(enclave_sbi_param_local);

  return retval;
}

uintptr_t sm_create_shadow_enclave(uintptr_t enclave_sbi_param)
{
  struct enclave_create_param_t enclave_sbi_param_local;
  uintptr_t retval = 0;
  if(test_public_range(PADDR_TO_PFN(enclave_sbi_param),1) < 0){
    return ENCLAVE_ERROR;
  }
  retval = copy_from_host(&enclave_sbi_param_local,
      (struct enclave_create_param_t*)enclave_sbi_param,
      sizeof(struct enclave_create_param_t));
  if(retval != 0)
    return ENCLAVE_ERROR;

  retval = create_shadow_enclave(enclave_sbi_param_local);

  return retval;
}

uintptr_t sm_destroy_server_enclave(uintptr_t *regs, uintptr_t enclave_id)
{
  uintptr_t ret = 0;

  ret = destroy_server_enclave(regs, enclave_id);

  return ret;
}

uintptr_t sm_server_enclave_acquire(uintptr_t *regs, uintptr_t server_name)
{
  uintptr_t ret = 0;

  ret = acquire_server_enclave(regs, (char*)server_name);

  return ret;
}

uintptr_t sm_call_enclave(uintptr_t* regs, uintptr_t eid, uintptr_t arg)
{
  uintptr_t retval = 0;

  retval = call_enclave(regs, (unsigned int)eid, arg);

  return retval;
}

uintptr_t sm_enclave_return(uintptr_t* regs, uintptr_t arg)
{
  uintptr_t ret = 0;

  ret = enclave_return(regs, arg);

  return ret;
}

uintptr_t sm_asyn_enclave_call(uintptr_t *regs, uintptr_t enclave_name, uintptr_t arg)
{
  uintptr_t ret = 0;

  ret = asyn_enclave_call(regs, enclave_name, arg);
  printm("M mode: sm_asyn_enclave_call is finished \r\n");
  return ret;
}

uintptr_t sm_split_mem_region(uintptr_t *regs, uintptr_t mem_addr, uintptr_t mem_size, uintptr_t split_addr)
{
  uintptr_t ret = 0;

  ret = split_mem_region(regs, mem_addr, mem_size, split_addr);

  return ret;
}
