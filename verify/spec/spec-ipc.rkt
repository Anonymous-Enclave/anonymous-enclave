#lang rosette/safe

(require
  serval/lib/core
  serval/spec/refcnt
  serval/riscv/base
  "state.rkt"
  (only-in racket/base struct-copy)
  (prefix-in sm: "generated/monitor/asm-offsets.rkt")
)

(provide (all-defined-out))

(define mip-mask-MTIP (bv #b000010000000 64))
(define mip-mask-STIP (bv #b000000100000 64))
(define mip-mask-SSIP (bv #b000000000010 64))
(define mip-mask-SEIP (bv #b001000000000 64))

(define (pte->pfn pte)
  (bvlshr pte (bv sm:PTE_PPN_SHIFT 64)))

(define (pte-valid pte)
  (bvand pte (bv sm:PTE_V 64)))

(define (is-leaf-pte pte)
  (bitvector->bool (and (bvand pte (bv sm:PTE_V 64))
                        (or (bvand pte (bv sm:PTE_R 64))
                            (bvand pte (bv sm:PTE_X 64))))))

(define (set-return! state val)
  (set-state-regs! state (struct-copy regs (state-regs state) [a0 (bv val 64)])))


(define (make-ecall-spec spec)
  (lambda (state . args)
    (define newmepc (bvadd (bv 4 64) (regs-mepc (state-regs state))))
    (set-state-regs! state (struct-copy regs (state-regs state) [mepc newmepc]))
    (cond
      [else (apply spec state args)])))


(define (set-thread_context-prev_state state dest-eid src-eid)
  (update-state-enclave.thread_context.prev_state.slot! state dest-eid
    (state-enclave.thread_context.prev_state.slot state src-eid))
  (update-state-enclave.thread_context.prev_state.ra! state dest-eid
    (state-enclave.thread_context.prev_state.ra state src-eid))
  (update-state-enclave.thread_context.prev_state.sp! state dest-eid
    (state-enclave.thread_context.prev_state.sp state src-eid))
  (update-state-enclave.thread_context.prev_state.gp! state dest-eid
    (state-enclave.thread_context.prev_state.gp state src-eid))
  (update-state-enclave.thread_context.prev_state.tp! state dest-eid
    (state-enclave.thread_context.prev_state.tp state src-eid))

  (update-state-enclave.thread_context.prev_state.t0! state dest-eid
    (state-enclave.thread_context.prev_state.t0 state src-eid))
  (update-state-enclave.thread_context.prev_state.t1! state dest-eid
    (state-enclave.thread_context.prev_state.t1 state src-eid))
  (update-state-enclave.thread_context.prev_state.t2! state dest-eid
    (state-enclave.thread_context.prev_state.t2 state src-eid))

  (update-state-enclave.thread_context.prev_state.s0! state dest-eid
    (state-enclave.thread_context.prev_state.s0 state src-eid))
  (update-state-enclave.thread_context.prev_state.s1! state dest-eid
    (state-enclave.thread_context.prev_state.s1 state src-eid))

  (update-state-enclave.thread_context.prev_state.a0! state dest-eid
    (state-enclave.thread_context.prev_state.a0 state src-eid))
  (update-state-enclave.thread_context.prev_state.a1! state dest-eid
    (state-enclave.thread_context.prev_state.a1 state src-eid))
  (update-state-enclave.thread_context.prev_state.a2! state dest-eid
    (state-enclave.thread_context.prev_state.a2 state src-eid))
  (update-state-enclave.thread_context.prev_state.a3! state dest-eid
    (state-enclave.thread_context.prev_state.a3 state src-eid))
  (update-state-enclave.thread_context.prev_state.a4! state dest-eid
    (state-enclave.thread_context.prev_state.a4 state src-eid))
  (update-state-enclave.thread_context.prev_state.a5! state dest-eid
    (state-enclave.thread_context.prev_state.a5 state src-eid))
  (update-state-enclave.thread_context.prev_state.a6! state dest-eid
    (state-enclave.thread_context.prev_state.a6 state src-eid))
  (update-state-enclave.thread_context.prev_state.a7! state dest-eid
    (state-enclave.thread_context.prev_state.a7 state src-eid))
    
  (update-state-enclave.thread_context.prev_state.s2! state dest-eid
    (state-enclave.thread_context.prev_state.s2 state src-eid))
  (update-state-enclave.thread_context.prev_state.s3! state dest-eid
    (state-enclave.thread_context.prev_state.s3 state src-eid))
  (update-state-enclave.thread_context.prev_state.s4! state dest-eid
    (state-enclave.thread_context.prev_state.s4 state src-eid))
  (update-state-enclave.thread_context.prev_state.s5! state dest-eid
    (state-enclave.thread_context.prev_state.s5 state src-eid))
  (update-state-enclave.thread_context.prev_state.s6! state dest-eid
    (state-enclave.thread_context.prev_state.s6 state src-eid))
  (update-state-enclave.thread_context.prev_state.s7! state dest-eid
    (state-enclave.thread_context.prev_state.s7 state src-eid))
  (update-state-enclave.thread_context.prev_state.s8! state dest-eid
    (state-enclave.thread_context.prev_state.s8 state src-eid))
  (update-state-enclave.thread_context.prev_state.s9! state dest-eid
    (state-enclave.thread_context.prev_state.s9 state src-eid))
  (update-state-enclave.thread_context.prev_state.s10! state dest-eid
    (state-enclave.thread_context.prev_state.s10 state src-eid))
  (update-state-enclave.thread_context.prev_state.s11! state dest-eid
    (state-enclave.thread_context.prev_state.s11 state src-eid))

  (update-state-enclave.thread_context.prev_state.t3! state dest-eid
    (state-enclave.thread_context.prev_state.t3 state src-eid))
  (update-state-enclave.thread_context.prev_state.t4! state dest-eid
    (state-enclave.thread_context.prev_state.t4 state src-eid))
  (update-state-enclave.thread_context.prev_state.t5! state dest-eid
    (state-enclave.thread_context.prev_state.t5 state src-eid))
  (update-state-enclave.thread_context.prev_state.t6! state dest-eid
    (state-enclave.thread_context.prev_state.t6 state src-eid)))


(define (copy-prev-state state reg-pageno eid)
  (update-state-enclave.thread_context.prev_state.ra! state eid
    ((state-pages state) reg-pageno (bv 1 64)))
  (update-state-enclave.thread_context.prev_state.sp! state eid
    ((state-pages state) reg-pageno (bv 2 64)))
  (update-state-enclave.thread_context.prev_state.gp! state eid
    ((state-pages state) reg-pageno (bv 3 64)))
  (update-state-enclave.thread_context.prev_state.tp! state eid
    ((state-pages state) reg-pageno (bv 4 64)))

  (update-state-enclave.thread_context.prev_state.t0! state eid
    ((state-pages state) reg-pageno (bv 5 64)))
  (update-state-enclave.thread_context.prev_state.t1! state eid
    ((state-pages state) reg-pageno (bv 6 64)))
  (update-state-enclave.thread_context.prev_state.t2! state eid
    ((state-pages state) reg-pageno (bv 7 64)))

  (update-state-enclave.thread_context.prev_state.s0! state eid
    ((state-pages state) reg-pageno (bv 8 64)))
  (update-state-enclave.thread_context.prev_state.s1! state eid
    ((state-pages state) reg-pageno (bv 9 64)))

  (update-state-enclave.thread_context.prev_state.a0! state eid
    ((state-pages state) reg-pageno (bv 10 64)))
  (update-state-enclave.thread_context.prev_state.a1! state eid
    ((state-pages state) reg-pageno (bv 11 64)))
  (update-state-enclave.thread_context.prev_state.a2! state eid
    ((state-pages state) reg-pageno (bv 12 64)))
  (update-state-enclave.thread_context.prev_state.a3! state eid
    ((state-pages state) reg-pageno (bv 13 64)))
  (update-state-enclave.thread_context.prev_state.a4! state eid
    ((state-pages state) reg-pageno (bv 14 64)))
  (update-state-enclave.thread_context.prev_state.a5! state eid
    ((state-pages state) reg-pageno (bv 15 64)))
  (update-state-enclave.thread_context.prev_state.a6! state eid
    ((state-pages state) reg-pageno (bv 16 64)))
  (update-state-enclave.thread_context.prev_state.a7! state eid
    ((state-pages state) reg-pageno (bv 17 64)))
    
  (update-state-enclave.thread_context.prev_state.s2! state eid
    ((state-pages state) reg-pageno (bv 18 64)))
  (update-state-enclave.thread_context.prev_state.s3! state eid
    ((state-pages state) reg-pageno (bv 19 64)))
  (update-state-enclave.thread_context.prev_state.s4! state eid
    ((state-pages state) reg-pageno (bv 20 64)))
  (update-state-enclave.thread_context.prev_state.s5! state eid
    ((state-pages state) reg-pageno (bv 21 64)))
  (update-state-enclave.thread_context.prev_state.s6! state eid
    ((state-pages state) reg-pageno (bv 22 64)))
  (update-state-enclave.thread_context.prev_state.s7! state eid
    ((state-pages state) reg-pageno (bv 23 64)))
  (update-state-enclave.thread_context.prev_state.s8! state eid
    ((state-pages state) reg-pageno (bv 24 64)))
  (update-state-enclave.thread_context.prev_state.s9! state eid
    ((state-pages state) reg-pageno (bv 25 64)))
  (update-state-enclave.thread_context.prev_state.s10! state eid
    ((state-pages state) reg-pageno (bv 26 64)))
  (update-state-enclave.thread_context.prev_state.s11! state eid
    ((state-pages state) reg-pageno (bv 27 64)))

  (update-state-enclave.thread_context.prev_state.t3! state eid
    ((state-pages state) reg-pageno (bv 28 64)))
  (update-state-enclave.thread_context.prev_state.t4! state eid
    ((state-pages state) reg-pageno (bv 29 64)))
  (update-state-enclave.thread_context.prev_state.t5! state eid
    ((state-pages state) reg-pageno (bv 30 64)))
  (update-state-enclave.thread_context.prev_state.t6! state eid
    ((state-pages state) reg-pageno (bv 31 64))))


(define (enter-enclave-world state eid)
  (set-state-enclave-mode! state (bv 1 i64))
  (set-state-curr-eid! state eid))


(define (spec-call_enclave state reg-pageno callee-eid call-arg-pageno)
  (define caller-eid (get-curr-enclave-id state))
  (define top-caller-eid (state-enclave.top_caller_eid state caller-eid))
  (define cur-regs (state-regs state))

  (cond
    [(! (check-in-enclave-world state))
      (set-return! state (- sm:ERR_NOT_ENCLAVE_MODE))]
    [(|| (! (get-enclave state caller-eid))
         (! (bveq (state-enclave.state state caller-eid) (bv sm:RUNNING 64)))
         (! (bveq (state-enclave.thread_context.encl_ptbr state caller-eid) (regs-satp (state-regs state)))))
      (set-return! state (- sm:ERR_NO_VALID_ENCLAVE))]
    [(&& (! (bveq (state-enclave.caller_eid state caller-eid) (bv -1 64)))
         (|| (! (get-enclave state top-caller-eid))
             (! (bveq (state-enclave.state state top-caller-eid) (bv sm:RUNNING 64)))))
      (set-return! state (- sm:ERR_NO_TOP_CALLER_ENCLAVE))]
    [(|| (! (get-enclave state callee-eid))
         (! (bveq (state-enclave.type state callee-eid) (bv sm:SERVER_ENCLAVE 64)))
         (! (bveq (state-enclave.caller_eid state callee-eid) (bv -1 64)))
         (! (bveq (state-enclave.state state callee-eid) (bv sm:RUNNABLE 64))))
      (set-return! state (- sm:ERR_NO_VALID_CALLEE_ENCLAVE))]
    [(|| (bveq call-arg-pageno (bv 0 64))
         (bvult (state-call-args.req_size state call-arg-pageno) (bv sm:RISCV_PGSIZE 64))
         (! (bveq (bvand (state-call-args.req_size state call-arg-pageno) (bv (- sm:RISCV_PGSIZE 1) 64))
                  (bv 0 64))))
      (set-return! state (- sm:ERR_INVALID_CALL_ARG))]
    [else 
      (update-state-enclave.thread_context.prev_stvec! state callee-eid
        (state-enclave.thread_context.prev_stvec state caller-eid))
      (update-state-enclave.thread_context.prev_mie! state callee-eid
        (state-enclave.thread_context.prev_mie state caller-eid))
      (update-state-enclave.thread_context.prev_mideleg! state callee-eid
        (state-enclave.thread_context.prev_mideleg state caller-eid))
      (update-state-enclave.thread_context.prev_medeleg! state callee-eid
        (state-enclave.thread_context.prev_medeleg state caller-eid))
      (update-state-enclave.thread_context.prev_mepc! state callee-eid
        (state-enclave.thread_context.prev_mepc state caller-eid))
      (update-state-enclave.thread_context.prev_cache_binding! state callee-eid
        (state-enclave.thread_context.prev_cache_binding state caller-eid))

      (set-thread_context-prev_state state callee-eid caller-eid)

      (update-state-enclave.host_ptbr! state callee-eid
        (state-enclave.host_ptbr state caller-eid))
      (update-state-enclave.ocall_func_id! state callee-eid
        (state-enclave.ocall_func_id state caller-eid))
      (update-state-enclave.ocall_arg0! state callee-eid
        (state-enclave.ocall_arg0 state caller-eid))
      (update-state-enclave.ocall_arg1! state callee-eid
        (state-enclave.ocall_arg1 state caller-eid))
      (update-state-enclave.ocall_syscall_num! state callee-eid
        (state-enclave.ocall_syscall_num state caller-eid))

      (copy-prev-state state reg-pageno caller-eid)

      (update-state-enclave.thread_context.prev_stvec! state caller-eid (regs-stvec cur-regs))
      (update-state-enclave.thread_context.prev_mie! state caller-eid (regs-mie cur-regs))
      (update-state-enclave.thread_context.prev_mideleg! state caller-eid (regs-mideleg cur-regs))
      (update-state-enclave.thread_context.prev_medeleg! state caller-eid (regs-medeleg cur-regs))
      (update-state-enclave.thread_context.prev_mepc! state caller-eid (regs-mepc cur-regs))

      (update-state-pages! state (list reg-pageno page-index-valid?) (bv 0 64))

      (set-state-regs! state (struct-copy regs (state-regs state)
        [satp (state-enclave.thread_context.encl_ptbr state callee-eid)]
        [mip (bvand (bvand (bvand (bvand (regs-mip cur-regs) 
                                         (bvnot mip-mask-MTIP))
                                         (bvnot mip-mask-STIP))
                                         (bvnot mip-mask-SSIP))
                                         (bvnot mip-mask-SEIP))]
        [mepc (state-enclave.entry_point state callee-eid)]))

      (enter-enclave-world state callee-eid)

      (update-state-enclave.cur_callee_eid! state top-caller-eid callee-eid)
      (update-state-enclave.cur_callee_eid! state caller-eid callee-eid)
      (update-state-enclave.caller_eid! state callee-eid caller-eid)
      (update-state-enclave.top_caller_eid! state callee-eid top-caller-eid)

      (set-state-regs! state (struct-copy regs (state-regs state)
        [mie (bvor mip-mask-MTIP)]))

      (update-state-pages! state (list reg-pageno (bv 2 64)) (bv sm:ENCLAVE_DEFAULT_STACK_BASE 64))
      (update-state-pages! state (list reg-pageno (bv 10 64)) (state-call-args.req_arg state call-arg-pageno))
      (update-state-pages! state (list reg-pageno (bv 12 64)) (state-call-args.req_size state call-arg-pageno))
      (update-state-pages! state (list reg-pageno (bv 14 64)) (state-enclave.shm_size state callee-eid))
      
      (update-state-enclave.state! state callee-eid (bv sm:RUNNING 64))

        (set-return! state sm:MONITOR_SUCCESS)
]))
