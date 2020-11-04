#include "encoding.h"

  .section ".payload","a",@progbits

#if __riscv_xlen==64

  .align RISCV_PGSHIFT + RISCV_PGLEVEL_BITS

  .globl _payload_start, _payload_end
_payload_start:
  .incbin BBL_PAYLOAD
_payload_end:

#else

  .align RISCV_PGSHIFT + RISCV_PGLEVEL_BITS - 1

  .globl _payload_start, _payload_end
_payload_start:
  .incbin BBL_PAYLOAD

  .align RISCV_PGSHIFT + RISCV_PGLEVEL_BITS - 1
  .globl _eapp_start, _eapp_end
_eapp_start:
  .incbin prime
_eapp_end:
_payload_end:

#endif
