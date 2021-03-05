#include <io/kbuild.h>
#include <asm/csr.h>
#include <asm/csr_bits/pmpcfg.h>
#include <asm/csr_bits/status.h>
#include <sys/encoding.h>
#include <monitor/enclave.h>
#include <monitor/server_enclave.h>
#include <monitor/sm.h>
#include <monitor/verify.h>
#include <monitor/enclave_vm.h>
#include <monitor/encoding.h>
#define INDEX(sym, str, mem) \
        DEFINE(sym, offsetof(struct str, mem) / 8)

void asm_offsets(void)
{
    /**
     *  Constant
     */
    DEFINE(CONFIG_BOOT_CPU, CONFIG_BOOT_CPU);
    DEFINE(NAME_LEN, NAME_LEN);
    DEFINE(PTE_PPN_SHIFT, PTE_PPN_SHIFT);
    DEFINE(RISCV_PGSHIFT, RISCV_PGSHIFT);
    DEFINE(RISCV_PGSIZE, RISCV_PGSIZE);
    DEFINE(RISCV_PGLEVEL_BITS, RISCV_PGLEVEL_BITS);
    DEFINE(VA_BITS, VA_BITS);
    DEFINE(PTE_V, PTE_V);
    DEFINE(PTE_R, PTE_R);
    DEFINE(PTE_X, PTE_X);
    DEFINE(RISCV_PGLEVELS, RISCV_PGLEVELS);
    DEFINE(XLEN, XLEN); // 64-bit
    DEFINE(MTRAP_STACK, MTRAP_STACK);  // MTRAP_STACK =  MACHINE_STACK_TOP() - MENTRY_FRAME_SIZE
    DEFINE(MAX_ENCLAVE_METADATA_REGION, MAX_ENCLAVE_METADATA_REGION);  // The number of metadata region
    DEFINE(ENCLAVES_PER_METADATA_REGION, ENCLAVES_PER_METADATA_REGION);  // sizeof(metadata_region) = sizeof(enclave_t) * ENCLAVES_PER_METADATA_REGION
    DEFINE(ENCLAVE_NUMBER, ENCLAVE_NUMBER);  /* Max enclave number for pre-allocate version */
    DEFINE(SERVER_ENCLAVE_NUMBER, SERVER_ENCLAVE_NUMBER);  /* Max server enclave number for pre-allocate version */
    DEFINE(ENCLAVE_SIZE, ENCLAVE_SIZE);  /* Enclave size for pre-allocate version, 16Mb */
    DEFINE(ENCLAVE_PAGE_NUMBER, ENCLAVE_PAGE_NUMBER);  /* Enclave page number */
    DEFINE(SERVER_ENCLAVE_SIZE, SERVER_ENCLAVE_SIZE);  /* Server enclave size for pre-allocate version, 16Mb */
    DEFINE(ENCLAVE_DEFAULT_STACK_BASE, ENCLAVE_DEFAULT_STACK_BASE);  /* Default stack base of enclave */

    
    /**
     * Encoding
     */
    DEFINE(SR_TVM, SR_TVM);


    /**
     * Error code
     */
    DEFINE(MONITOR_SUCCESS, MONITOR_SUCCESS);  /* Success return */
    DEFINE(ENOSYS, ENOSYS);  /* Invalid system call number */
    DEFINE(ERR_GENERAL_FAULT, ERR_GENERAL_FAULT);  /* Faults */
    DEFINE(ERR_NO_ENCLAVE, ERR_NO_ENCLAVE);  /* Enclave not found */
    DEFINE(ERR_NOT_ENCLAVE_MODE, ERR_NOT_ENCLAVE_MODE);  /* Not in enclave mode */
    DEFINE(ERR_EID_OUT_OF_BOUNDARY, ERR_EID_OUT_OF_BOUNDARY);  /* Eid out of boundary */
    DEFINE(ERR_NO_SERVER_NAME, ERR_NO_SERVER_NAME);  /* Server enclave name not specified */
    DEFINE(ERR_NO_SERVER_ENCLAVE, ERR_NO_SERVER_ENCLAVE);  /* Server enclave not found */
    DEFINE(ERR_NO_VALID_ENCLAVE, ERR_NO_VALID_ENCLAVE);  /* Valid enclave not found */
    DEFINE(ERR_NO_TOP_CALLER_ENCLAVE, ERR_NO_TOP_CALLER_ENCLAVE);  /* Top caller enclave not found */
    DEFINE(ERR_NO_VALID_CALLEE_ENCLAVE, ERR_NO_VALID_CALLEE_ENCLAVE);  /* Valid callee enclave not found */
    DEFINE(ERR_INVALID_CALL_ARG, ERR_INVALID_CALL_ARG);  /* Invalid call_cags */


    /**
     *  SBI_CALL
     */
    DEFINE(ENCLAVE_CALL_TRAP, 8);
    DEFINE(SBI_ACQUIRE_SERVER, SBI_ACQUIRE_SERVER);
    DEFINE(SBI_CALL_ENCLAVE, SBI_CALL_ENCLAVE);
    DEFINE(SBI_ENCLAVE_RETURN, SBI_ENCLAVE_RETURN);


    /**
     *  Enumeration
     */

    /* enclave_type_t */
    DEFINE(NORMAL_ENCLAVE, NORMAL_ENCLAVE); // 0
    DEFINE(SERVER_ENCLAVE, SERVER_ENCLAVE); // 1

    /* enclave_state_t */
    DEFINE(DESTROYED, DESTROYED);   // -1
    DEFINE(INVALID, INVALID);        // 0
    DEFINE(FRESH, FRESH);           // 1
    DEFINE(RUNNABLE, RUNNABLE);
    DEFINE(RUNNING, RUNNING);
    DEFINE(STOPPED, STOPPED);
    DEFINE(ATTESTING, ATTESTING);
    DEFINE(OCALLING, OCALLING);

    /**
     *  Struct
     */

    /* enclave_t */
    INDEX(ENCLAVE_T_EID, enclave_t, eid);

    INDEX(ENCLAVE_T_TYPE, enclave_t, type); 
    INDEX(ENCLAVE_T_STATE, enclave_t, state);  

    INDEX(ENCLAVE_T_TEXT_VMA, enclave_t, text_vma);
    INDEX(ENCLAVE_T_STACK_VMA, enclave_t, stack_vma);
    INDEX(ENCLAVE_T_STACK_TOP, enclave_t, _stack_top);
    INDEX(ENCLAVE_T_HEAP_VMA, enclave_t, heap_vma);
    INDEX(ENCLAVE_T_HEAP_TOP, enclave_t, _heap_top);
    INDEX(ENCLAVE_T_MMAP_VMA, enclave_t, mmap_vma);
    INDEX(ENCLAVE_T_PMA_LIST, enclave_t, pma_list);
    INDEX(ENCLAVE_T_FREE_PAGES, enclave_t, free_pages);
    INDEX(ENCLAVE_T_FREE_PAGES_NUM, enclave_t, free_pages_num);
    INDEX(ENCLAVE_T_ROOT_PAGE_TABLE, enclave_t, root_page_table);
    INDEX(ENCLAVE_T_HOST_PTBR, enclave_t, host_ptbr);
    INDEX(ENCLAVE_T_ENTRY_POINT, enclave_t, entry_point);
    INDEX(ENCLAVE_T_KBUFFER, enclave_t, kbuffer);
    INDEX(ENCLAVE_T_KBUFFER_SIZE, enclave_t, kbuffer_size);
    INDEX(ENCLAVE_T_SHM_PADDR, enclave_t, shm_paddr);
    INDEX(ENCLAVE_T_SHM_SIZE, enclave_t, shm_size);
    INDEX(ENCLAVE_T_MM_ARG_PADDR, enclave_t, mm_arg_paddr);
    INDEX(ENCLAVE_T_MM_ARG_SIZE, enclave_t, mm_arg_size);
    INDEX(ENCLAVE_T_OCALL_FUNC_ID, enclave_t, ocall_func_id);
    INDEX(ENCLAVE_T_OCALL_ARG0, enclave_t, ocall_arg0);
    INDEX(ENCLAVE_T_OCALL_ARG1, enclave_t, ocall_arg1);
    INDEX(ENCLAVE_T_OCALL_SYSCALL_NUM, enclave_t, ocall_syscall_num);
    /* struct thread_state_t */
    INDEX(ENCLAVE_T_THREAD_CONTEXT_ENCL_PTBR, enclave_t, thread_context.encl_ptbr);
    INDEX(ENCLAVE_T_THREAD_CONTEXT_PREV_STVEC, enclave_t, thread_context.prev_stvec);
    INDEX(ENCLAVE_T_THREAD_CONTEXT_PREV_MIE, enclave_t, thread_context.prev_mie);
    INDEX(ENCLAVE_T_THREAD_CONTEXT_PREV_MIDELEG, enclave_t, thread_context.prev_mideleg);
    INDEX(ENCLAVE_T_THREAD_CONTEXT_PREV_MEDELEG, enclave_t, thread_context.prev_medeleg);
    INDEX(ENCLAVE_T_THREAD_CONTEXT_PREV_MEPC, enclave_t, thread_context.prev_mepc);
    INDEX(ENCLAVE_T_THREAD_CONTEXT_PREV_CACHE_BINDING, enclave_t, thread_context.prev_cache_binding);
    /* struct general_registers_t */
    INDEX(ENCLAVE_T_GENERAL_REGISTERS_T_SLOT, enclave_t, thread_context.prev_state.slot);
    INDEX(ENCLAVE_T_GENERAL_REGISTERS_T_RA, enclave_t, thread_context.prev_state.ra);
    INDEX(ENCLAVE_T_GENERAL_REGISTERS_T_SP, enclave_t, thread_context.prev_state.sp);
    INDEX(ENCLAVE_T_GENERAL_REGISTERS_T_GP, enclave_t, thread_context.prev_state.gp);
    INDEX(ENCLAVE_T_GENERAL_REGISTERS_T_TP, enclave_t, thread_context.prev_state.tp);
    INDEX(ENCLAVE_T_GENERAL_REGISTERS_T_T0, enclave_t, thread_context.prev_state.t0);
    INDEX(ENCLAVE_T_GENERAL_REGISTERS_T_T1, enclave_t, thread_context.prev_state.t1);
    INDEX(ENCLAVE_T_GENERAL_REGISTERS_T_T2, enclave_t, thread_context.prev_state.t2);
    INDEX(ENCLAVE_T_GENERAL_REGISTERS_T_S0, enclave_t, thread_context.prev_state.s0);
    INDEX(ENCLAVE_T_GENERAL_REGISTERS_T_S1, enclave_t, thread_context.prev_state.s1);
    INDEX(ENCLAVE_T_GENERAL_REGISTERS_T_A0, enclave_t, thread_context.prev_state.a0);
    INDEX(ENCLAVE_T_GENERAL_REGISTERS_T_A1, enclave_t, thread_context.prev_state.a1);
    INDEX(ENCLAVE_T_GENERAL_REGISTERS_T_A2, enclave_t, thread_context.prev_state.a2);
    INDEX(ENCLAVE_T_GENERAL_REGISTERS_T_A3, enclave_t, thread_context.prev_state.a3);
    INDEX(ENCLAVE_T_GENERAL_REGISTERS_T_A4, enclave_t, thread_context.prev_state.a4);
    INDEX(ENCLAVE_T_GENERAL_REGISTERS_T_A5, enclave_t, thread_context.prev_state.a5);
    INDEX(ENCLAVE_T_GENERAL_REGISTERS_T_A6, enclave_t, thread_context.prev_state.a6);
    INDEX(ENCLAVE_T_GENERAL_REGISTERS_T_A7, enclave_t, thread_context.prev_state.a7);
    INDEX(ENCLAVE_T_GENERAL_REGISTERS_T_S2, enclave_t, thread_context.prev_state.s2);
    INDEX(ENCLAVE_T_GENERAL_REGISTERS_T_S3, enclave_t, thread_context.prev_state.s3);
    INDEX(ENCLAVE_T_GENERAL_REGISTERS_T_S4, enclave_t, thread_context.prev_state.s4);
    INDEX(ENCLAVE_T_GENERAL_REGISTERS_T_S5, enclave_t, thread_context.prev_state.s5);
    INDEX(ENCLAVE_T_GENERAL_REGISTERS_T_S6, enclave_t, thread_context.prev_state.s6);
    INDEX(ENCLAVE_T_GENERAL_REGISTERS_T_S7, enclave_t, thread_context.prev_state.s7);
    INDEX(ENCLAVE_T_GENERAL_REGISTERS_T_S8, enclave_t, thread_context.prev_state.s8);
    INDEX(ENCLAVE_T_GENERAL_REGISTERS_T_S9, enclave_t, thread_context.prev_state.s9);
    INDEX(ENCLAVE_T_GENERAL_REGISTERS_T_S10, enclave_t, thread_context.prev_state.s10);
    INDEX(ENCLAVE_T_GENERAL_REGISTERS_T_S11, enclave_t, thread_context.prev_state.s11);
    INDEX(ENCLAVE_T_GENERAL_REGISTERS_T_T3, enclave_t, thread_context.prev_state.t3);
    INDEX(ENCLAVE_T_GENERAL_REGISTERS_T_T4, enclave_t, thread_context.prev_state.t4);
    INDEX(ENCLAVE_T_GENERAL_REGISTERS_T_T5, enclave_t, thread_context.prev_state.t5);
    INDEX(ENCLAVE_T_GENERAL_REGISTERS_T_T6, enclave_t, thread_context.prev_state.t6);

    INDEX(ENCLAVE_T_TOP_CALLER_EID, enclave_t, top_caller_eid);
    INDEX(ENCLAVE_T_CALLER_EID, enclave_t, caller_eid);
    INDEX(ENCLAVE_T_CUR_CALLEE_EID, enclave_t, cur_callee_eid);

    /* server_enclave_t */
    INDEX(SERVER_ENCLAVE_T_SERVER_NAME, __server_enclave_t, server_name); 
    INDEX(SERVER_ENCLAVE_T_EID, __server_enclave_t, eid);
    // DEFINE(SERVER_ENCLAVE_T_SERVER_NAME_SIZE, 8);

    /* cpu_state_t[MAX_HARTS] */
    INDEX(CPU_STATE_T_IN_ENCLAVE, cpu_state_t, in_enclave);
    INDEX(CPU_STATE_T_EID, cpu_state_t, eid);
    DEFINE(CPU_STATE_T_SIZE, 16);

    /* call_enclave_arg_t */
    INDEX(CALL_ENCLAVE_ARG_REQ_ARG, call_enclave_arg_t, req_arg);
    INDEX(CALL_ENCLAVE_ARG_RESP_VAL, call_enclave_arg_t, resp_val);
    // INDEX(CALL_ENCLAVE_ARG_REQ_VADDR, call_enclave_arg_t, req_vaddr);
    INDEX(CALL_ENCLAVE_ARG_REQ_SIZE, call_enclave_arg_t, req_size);
    // INDEX(CALL_ENCLAVE_ARG_RESP_VADDR, call_enclave_arg_t, resp_vaddr);
    INDEX(CALL_ENCLAVE_ARG_RESP_SIZE, call_enclave_arg_t, resp_size);
}
