#include "pmp.h"
#include "ipi.h"
#include <stddef.h>

///set pmp and sync all harts
void set_pmp_and_sync(int pmp_idx_arg, struct pmp_config_t pmp_config_arg)
{
  struct pmp_config_t* pmp_config = NULL;
  int* pmp_idx = NULL;

  spinlock_lock(&ipi_mail_lock);

  //set current hart's pmp
  set_pmp(pmp_idx_arg, pmp_config_arg);
  //sync all other harts
  ipi_mail.event = IPI_PMP_SYNC;
  pmp_config = (void*)ipi_mail.data;
  pmp_idx = (void*)ipi_mail.data + sizeof(struct pmp_config_t);
  *pmp_config = pmp_config_arg;
  *pmp_idx = pmp_idx_arg;

  send_ipi_mail(0xFFFFFFFF, 1);

  spinlock_unlock(&ipi_mail_lock);

  return;
}

///clear pmp and sync all harts
void clear_pmp_and_sync(int pmp_idx)
{
  struct pmp_config_t pmp_config = {0,};
  
  pmp_config.mode = PMP_OFF;
  set_pmp_and_sync(pmp_idx, pmp_config);

  return;
}

/**
 * \brief set current hart's pmp
 *
 * \param pmp_idx the index of target PMP register
 *
 * \param pmp_cfg_t the configuration of the PMP register
 */
void set_pmp(int pmp_idx, struct pmp_config_t pmp_cfg_t)
{
  uintptr_t pmp_address = 0;
  uintptr_t old_config = 0;
  uintptr_t pmp_config = ((pmp_cfg_t.mode & PMP_A) | (pmp_cfg_t.perm & (PMP_R|PMP_W|PMP_X)))
    << ((uintptr_t)PMPCFG_BIT_NUM * (pmp_idx % PMP_PER_CFG_REG));

  switch(pmp_cfg_t.mode)
  {
    case PMP_NAPOT:
      if(pmp_cfg_t.paddr == 0 && pmp_cfg_t.size == -1UL)
        pmp_address = -1UL;
      else
        pmp_address = (pmp_cfg_t.paddr | ((pmp_cfg_t.size>>1)-1)) >> 2;
      break;
    case PMP_TOR:
      pmp_address = pmp_cfg_t.paddr;
      break;
    case PMP_NA4:
      pmp_address = pmp_cfg_t.paddr;
    case PMP_OFF:
      pmp_address = 0;
      break;
    default:
      pmp_address = 0;
      break;
  }

  switch(pmp_idx)
  {
#define X(n, g) case n: { PMP_SET(n, g, pmp_address, pmp_config); break; }
    LIST_OF_PMP_REGS
#undef X

    default:
      break;
  }

  return;
}

/**
 * \brief clear the configuration of a PMP register
 *
 * \param pmp_idx the index of target PMP register
 */
void clear_pmp(int pmp_idx)
{
  struct pmp_config_t pmp_cfg_t;

  pmp_cfg_t.mode = PMP_OFF;
  pmp_cfg_t.perm = PMP_NO_PERM;
  pmp_cfg_t.paddr = 0;
  pmp_cfg_t.size = 0;
  set_pmp(pmp_idx, pmp_cfg_t);

  return;
}

///get the configuration of a pmp register (pmp_idx)
struct pmp_config_t get_pmp(int pmp_idx)
{
  struct pmp_config_t pmp = {0,};
  uintptr_t pmp_address = 0;
  uintptr_t pmp_config = 0;
  unsigned long order = 0;
  unsigned long size = 0;

  switch(pmp_idx)
  {
#define X(n, g) case n: { PMP_READ(n, g, pmp_address, pmp_config); break; }
    LIST_OF_PMP_REGS
#undef X
    default:
      break;
  }

  pmp_config >>= (uintptr_t)PMPCFG_BIT_NUM * (pmp_idx % PMP_PER_CFG_REG);
  pmp_config &= PMPCFG_BITS;
  switch(pmp_config & PMP_A)
  {
    case PMP_NAPOT:
      while(pmp_address & 1)
      {
        order += 1;
        pmp_address >>= 1;
      }
      order += 3;
      size = 1 << order;
      pmp_address <<= (order-1);
      break;
    case PMP_NA4:
      size = 4;
      break;
    case PMP_TOR:
      break;
    case PMP_OFF:
      pmp_address = 0;
      size = 0;
      break;
  }

  pmp.mode = pmp_config & PMP_A;
  pmp.perm = pmp_config & (PMP_R | PMP_W | PMP_X);
  pmp.paddr = pmp_address;
  pmp.size = size;

  return pmp;
}

/**
 * \brief Check the validness of a range to be PMP config
 *  	  e.g., the size should be powers of 2
 *
 * \param paddr the start address of the PMP region
 * \param size the size of the PMP region
 */
int illegal_pmp_addr(uintptr_t paddr, uintptr_t size)
{
  if((size == 0) || (size & (size - 1)))
    return -1;

  if(size < RISCV_PGSIZE)
    return -1;

  if(paddr & (size - 1))
    return -1;

  return 0;
}

///check whether two regions are overlapped
int region_overlap(uintptr_t pa0, uintptr_t size0, uintptr_t pa1, uintptr_t size1)
{
  return (pa0 <= pa1 && (pa0 + size0) > pa1) || (pa1 <= pa0 && (pa1 + size1) > pa0);
}

///check whether two regions are included
int region_contain(uintptr_t pa0, uintptr_t size0, uintptr_t pa1, uintptr_t size1)
{
  return (pa0 <= pa1 && (pa0 + size0) >= (pa1 + size1))
    || (pa1 <= pa0 && (pa1 + size1) >= (pa0 + size0));
}
