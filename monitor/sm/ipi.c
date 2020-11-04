#include "mtrap.h"
#include "fdt.h"
#include "disabled_hart_mask.h"
#include "ipi.h"
#include "pmp.h"
#include "enclave.h"

#ifndef TARGET_PLATFORM_HEADER
#error "SM requires to specify a certain platform"
#endif
#include TARGET_PLATFORM_HEADER

///remember to acquire ipi_mail_lock before using these data structs
struct ipi_mail_t ipi_mail = {0,};
int ipi_mail_pending[MAX_HARTS] = {0,};

spinlock_t ipi_mail_lock = SPINLOCK_INIT;

///remember to acquire ipi_mail_lock before using this function
void wait_pending_ipi(uintptr_t mask)
{
  uintptr_t incoming_ipi = 0;
  mask &= hart_mask;

  ///wait until all other harts have handled IPI
  for(uintptr_t i=0, m=mask; m; ++i, m>>=1)
  {
    if((m & 1) && (!((disabled_hart_mask >> i) & 1))
        && (i != read_csr(mhartid)))
    {
      while(atomic_read(&ipi_mail_pending[i]))
      {
        incoming_ipi |= atomic_swap(HLS()->ipi, 0);
      }
    }
  }

  ///if we got an IPI, restore it
  if(incoming_ipi)
  {
    *HLS()->ipi = incoming_ipi;
    mb();
  }
}

///remember to acquire ipi_mail_lock before using this function
void send_ipi_mail(uintptr_t dest_hart, uintptr_t need_sync)
{
  ///send IPIs to every other hart
  uintptr_t mask = hart_mask;
  mask &= dest_hart;

  for(uintptr_t i=0, m = mask; m; ++i, m>>=1)
  {
    if((m & 1) && (!((disabled_hart_mask >> i) & 1))
        && (i != read_csr(mhartid)))
    {
      //printm("hart%d: send to hart %d\r\n", read_csr(mhartid), i);
      atomic_or(&OTHER_HLS(i)->mipi_pending, IPI_MAIL);
      mb();
      ipi_mail_pending[i] = 1;
      *OTHER_HLS(i)->ipi = 1;
    }
  }

  if(need_sync)
    wait_pending_ipi(mask);
}

/**
 * \brief It's the entry to handle IPI related interrupts
 *
 * \param regs the trapped registers
 *
 * It will handle different IPI evnets, including PMP_SYNC,
 * STOP_ENCLAVE, and DESTROY_ENCLAVE.
 */
void handle_ipi_mail(uintptr_t *regs)
{
  char* mail_data = ipi_mail.data;
  int pmp_idx = 0;
  struct pmp_config_t pmp_config;
  uintptr_t host_ptbr = 0;
  int enclave_id = 0;
  //printm("hart%d: handle ipi event%x\r\n", read_csr(mhartid), ipi_mail.event);

  switch(ipi_mail.event)
  {
    case IPI_PMP_SYNC:
      pmp_config = *(struct pmp_config_t*)(ipi_mail.data);
      pmp_idx = *(int*)((void*)ipi_mail.data + sizeof(struct pmp_config_t));
      set_pmp(pmp_idx, pmp_config);
      break;
    case IPI_STOP_ENCLAVE:
      host_ptbr = *((uintptr_t*)mail_data);
      enclave_id = *((int*)(mail_data + sizeof(uintptr_t)));
      ipi_stop_enclave(regs, host_ptbr, enclave_id);
      break;
    case IPI_DESTROY_ENCLAVE:
      host_ptbr = *((uintptr_t*)mail_data);
      enclave_id = *((int*)(mail_data + sizeof(uintptr_t)));
      ipi_destroy_enclave(regs, host_ptbr, enclave_id);
      break;
    default:
      handle_ipi_mail_platform(ipi_mail.event);
      break;
  }

  ipi_mail_pending[read_csr(mhartid)] = 0;
}
