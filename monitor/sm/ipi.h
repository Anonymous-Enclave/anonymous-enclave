#ifndef _IPI_H
#define _IPI_H

#include "atomic.h"
#include <string.h>
#include "stdint.h"

#define IPI_PMP_SYNC         0x1
#define IPI_STOP_ENCLAVE     0x2
#define IPI_DESTROY_ENCLAVE  0x3

struct ipi_mail_t
{
  uintptr_t event;
  char data[40];
};

extern struct ipi_mail_t ipi_mail;

extern spinlock_t ipi_mail_lock;

extern int ipi_mail_pending[];

void send_ipi_mail(uintptr_t dest_hart, uintptr_t need_sync);

void wait_pending_ipi(uintptr_t mask);

void handle_ipi_mail(uintptr_t *regs);

#endif /* _IPI_H */
