#ifndef _PMP_H
#define _PMP_H

#include <stdint.h>
#include "encoding.h"

///number of PMP registers
#if __riscv_xlen == 64
#define NPMP 8
#else
#define NPMP 8
#endif

///already defined in machine/encoding.h
/*
//R/W/X/A/L field in PMP configuration registers
#define PMP_R     0x01
#define PMP_W     0x02
#define PMP_X     0x04
#define PMP_A     0x18
#define PMP_L     0x80

//encoding of A field in PMP configuration registers
#define PMP_TOR   0x08
#define PMP_NA4   0x10
#define PMP_NAPOT 0x18
*/
#define PMP_OFF   0x00
#define PMP_NO_PERM  0

/**
 * pmpfcg register's structure
 * |63     56|55     48|47     40|39     32|31     24|23     16|15      8|7       0|
 * | pmp7cfg | pmp6cfg | pmp5cfg | pmp4cfg | pmp3cfg | pmp2cfg | pmp1cfg | pmp1cfg |
 */
#define PMPCFG_BIT_NUM            8
#define PMPCFG_BITS               0xFF
#define PMP_PER_CFG_REG           (sizeof(uintptr_t) * 8 / PMPCFG_BIT_NUM)

#if __riscv_xlen == 64
#define LIST_OF_PMP_REGS  X(0,0)  X(1,0)  X(2,0)  X(3,0) \
                           X(4,0)  X(5,0)  X(6,0)  X(7,0) \
                           X(8,2)  X(9,2)  X(10,2) X(11,2) \
                          X(12,2) X(13,2) X(14,2) X(15,2)
#else
#define LIST_OF_PMP_REGS  X(0,0)  X(1,0)  X(2,0)  X(3,0) \
                           X(4,1)  X(5,1)  X(6,1)  X(7,1) \
                           X(8,2)  X(9,2)  X(10,2) X(11,2) \
                          X(12,3) X(13,3) X(14,3) X(15,3)
#endif /* __riscv_xlen == 64 */

#define PMP_SET(n, g, addr, pmpc) do { \
  uintptr_t oldcfg = read_csr(pmpcfg##g); \
  pmpc |= (oldcfg & ~((uintptr_t)PMPCFG_BITS << (uintptr_t)PMPCFG_BIT_NUM*(n%PMP_PER_CFG_REG))); \
  asm volatile ("la t0, 1f\n\t" \
                "csrrw t0, mtvec, t0\n\t" \
                "csrw pmpaddr"#n", %0\n\t" \
                "csrw pmpcfg"#g", %1\n\t" \
                "sfence.vma\n\t"\
                ".align 2\n\t" \
                "1: csrw mtvec, t0 \n\t" \
                : : "r" (addr), "r" (pmpc) : "t0"); \
} while(0)

#define PMP_READ(n, g, addr, pmpc) do { \
  asm volatile("csrr %0, pmpaddr"#n : "=r"(addr) :); \
  asm volatile("csrr %0, pmpcfg"#g : "=r"(pmpc) :); \
} while(0)

struct pmp_config_t
{
  uintptr_t paddr;
  unsigned long size;
  uintptr_t perm;
  uintptr_t mode;
};

void set_pmp_and_sync(int pmp_idx, struct pmp_config_t);
void clear_pmp_and_sync(int pmp_idx);
void set_pmp(int pmp_idx, struct pmp_config_t);
void clear_pmp(int pmp_idx);
struct pmp_config_t get_pmp(int pmp_idx);
int region_overlap(uintptr_t pa0, uintptr_t size0, uintptr_t pa1, uintptr_t size1);
int region_contain(uintptr_t pa0, uintptr_t size0, uintptr_t pa1, uintptr_t size1);
int illegal_pmp_addr(uintptr_t addr, uintptr_t size);

#endif /* _PMP_H */
