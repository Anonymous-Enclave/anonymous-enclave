#lang rosette/safe

(require
  serval/lib/core
  serval/riscv/spec
  serval/riscv/base
  serval/spec/refcnt
  (prefix-in sm: "generated/monitor/asm-offsets.rkt")
  (prefix-in sm: "generated/monitor/sm.globals.rkt")
  (prefix-in sm: "generated/monitor/sm.map.rkt"))

(provide
  (all-defined-out)
  (all-from-out serval/riscv/spec))

(struct state (regs
               enclave-mode
               curr-eid
               curr-server-eid
               server-enclave.name
               server-enclave.eid
               enclave_t
               pages
               page-refcnt
               call-args
              )
  #:transparent #:mutable
  #:methods gen:equal+hash
  [(define (equal-proc s t equal?-recur)
     (define-symbolic server-id i64)
     (define-symbolic eid i64)
     (define-symbolic pageno index i64)
     (&& (equal?-recur (state-regs s) (state-regs t))
         (equal?-recur (state-enclave-mode s) (state-enclave-mode t))
         (equal?-recur (state-curr-eid s) (state-curr-eid t))
         (equal?-recur (state-curr-server-eid s) (state-curr-server-eid t))
         ;;; server-enclave
         (forall (list server-id)
                 (=> (server-id-valid? server-id)
                     (&& (bveq ((state-server-enclave.name s) server-id) ((state-server-enclave.name t) server-id))
                         (bveq ((state-server-enclave.eid s) server-id) ((state-server-enclave.eid t) server-id)))))
         ;;; enclave_t
         (forall (list eid index)
                 (=> (&& (eid-valid? eid) (enclave-index-valid? index))
                     (bveq ((state-enclave_t s) eid index) ((state-enclave_t t) eid index))))))
         ;;; pages
         (forall (list pageno index)
                 (=> (&& (page-valid? pageno) (page-index-valid? index))
                     (bveq ((state-pages s) pageno index) ((state-pages t) pageno index))))))

   (define (hash-proc s hash-recur) 1)
   (define (hash2-proc s hash2-recur) 2)]
    ; pretty-print function
  #:methods gen:custom-write
  [(define (write-proc s port mode)
     (fprintf port "(state")
     (fprintf port "\n  regs . ~a" (state-regs s))
     (fprintf port "\n  enclave-mode ~a" (state-enclave-mode s))
     (fprintf port "\n  curr-eid ~a" (state-curr-eid s))
     (fprintf port ")"))])


(define (make-havoc-regs)
  (define-symbolic*
    ra sp gp tp t0 t1 t2 s0 s1 a0 a1 a2 a3 a4 a5 a6 a7 s2 s3 s4 s5 s6 s7 s8 s9 s10 s11 t3 t4 t5 t6
    satp scause scounteren sepc sscratch sstatus stvec stval mepc sip sie mip mideleg medeleg mie
    (bitvector 64))
  (regs ra sp gp tp t0 t1 t2 s0 s1 a0 a1 a2 a3 a4 a5 a6 a7 s2 s3 s4 s5 s6 s7 s8 s9 s10 s11 t3 t4 t5 t6
    satp scause scounteren sepc sscratch sstatus stvec stval mepc sip sie mip mideleg medeleg mie))

(define (make-havoc-state)
  (define-symbolic* enclave-mode boolean?)
  (define-symbolic* curr-eid i64)
  (define-symbolic* curr-server-eid i64)
  (define-symbolic* server-enclave.name (~> i64 i64)) 
  (define-symbolic* server-enclave.eid (~> i64 i64)) 
  (define-symbolic* enclave_t (~> i64 i64 i64))
  (define-symbolic* pages (~> i64 i64 i64))
  (define page-refcnt (make-havoc-refcnt))
  (define-symbolic* call-args i64)

  (state (make-havoc-regs)
          enclave-mode
          curr-eid
          curr-server-eid
          server-enclave.name
          server-enclave.eid
          enclave_t
          pages
          page-refcnt
          call-args
          ))


(define-syntax-rule (make-state-updater name getter setter)
  (define (name state indices value)
    (setter state (update (getter state) indices value))))

(make-state-updater update-state-server-enclave.name! state-server-enclave.name set-state-server-enclave.name!)
(make-state-updater update-state-server-enclave.eid! state-server-enclave.eid set-state-server-enclave.eid!)
(make-state-updater update-state-enclave_t! state-enclave_t set-state-enclave_t!)
(make-state-updater update-state-pages! state-pages set-state-pages!)


(define (check-in-enclave-world state)
  (state-enclave-mode state))

(define (get-curr-enclave-id state)
  (state-curr-eid state))

(define (get-curr-server-eid state)
  (state-curr-server-eid state))

;;; server-id start from zero
(define (server-id-valid? server-id)
  (bvult server-id (bv sm:SERVER_ENCLAVE_NUMBER 64)))

;;; 64-bit used for enclave.state
(define (eid-valid? eid)
  (bvult eid (bv sm:ENCLAVE_NUMBER 64)))

(define (page-valid? pageno)
  (bvult pageno (bv sm:ENCLAVE_PAGE_NUMBER 64)))

(define (page-index-valid? index)
  (bvult index (bv 512 64)))

(define (get-enclave state eid)
  (bvult eid (bv sm:ENCLAVE_NUMBER 64)))

(define (enclave-index-valid? index)
  (bvult index (bv 512 64)))


;;; eid
(define (state-enclave.eid state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_EID 64)))

(define (update-state-enclave.eid! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_EID 64)) value))

;;; type
(define (state-enclave.type state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_TYPE 64)))

(define (update-state-enclave.type! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_TYPE 64)) value))

;;; state
(define (state-enclave.state state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_STATE 64)))

(define (update-state-enclave.state! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_STATE 64)) value))

;;; host_ptbr
(define (state-enclave.host_ptbr state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_HOST_PTBR 64)))

(define (update-state-enclave.host_ptbr! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_HOST_PTBR 64)) value))

;;; entry_point
(define (state-enclave.entry_point state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_ENTRY_POINT 64)))

(define (update-state-enclave.entry_point! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_ENTRY_POINT 64)) value))

;;; shm_size
(define (state-enclave.shm_size state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_SHM_SIZE 64)))

(define (update-state-enclave.shm_size! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_SHM_SIZE 64)) value))

;;; ocall_func_id
(define (state-enclave.ocall_func_id state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_OCALL_FUNC_ID 64)))

(define (update-state-enclave.ocall_func_id! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_OCALL_FUNC_ID 64)) value))

;;; ocall_arg0
(define (state-enclave.ocall_arg0 state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_OCALL_ARG0 64)))

(define (update-state-enclave.ocall_arg0! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_OCALL_ARG0 64)) value))

;;; ocall_arg1
(define (state-enclave.ocall_arg1 state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_OCALL_ARG1 64)))

(define (update-state-enclave.ocall_arg1! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_OCALL_ARG1 64)) value))

;;; ocall_syscall_num
(define (state-enclave.ocall_syscall_num state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_OCALL_SYSCALL_NUM 64)))

(define (update-state-enclave.ocall_syscall_num! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_OCALL_SYSCALL_NUM 64)) value))

;;; thread_context
(define (state-enclave.thread_context.encl_ptbr state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_THREAD_CONTEXT_ENCL_PTBR 64)))
(define (state-enclave.thread_context.prev_stvec state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_THREAD_CONTEXT_PREV_STVEC 64)))
(define (state-enclave.thread_context.prev_mie state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_THREAD_CONTEXT_PREV_MIE 64)))
(define (state-enclave.thread_context.prev_mideleg state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_THREAD_CONTEXT_PREV_MIDELEG 64)))
(define (state-enclave.thread_context.prev_medeleg state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_THREAD_CONTEXT_PREV_MEDELEG 64)))
(define (state-enclave.thread_context.prev_mepc state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_THREAD_CONTEXT_PREV_MEPC 64)))
(define (state-enclave.thread_context.prev_cache_binding state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_THREAD_CONTEXT_PREV_CACHE_BINDING 64)))

(define (state-enclave.thread_context.prev_state.slot state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_SLOT 64)))
(define (state-enclave.thread_context.prev_state.ra state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_RA 64)))
(define (state-enclave.thread_context.prev_state.sp state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_SP 64)))
(define (state-enclave.thread_context.prev_state.gp state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_GP 64)))
(define (state-enclave.thread_context.prev_state.tp state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_TP 64)))

(define (state-enclave.thread_context.prev_state.t0 state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_T0 64)))
(define (state-enclave.thread_context.prev_state.t1 state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_T1 64)))
(define (state-enclave.thread_context.prev_state.t2 state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_T2 64)))

(define (state-enclave.thread_context.prev_state.s0 state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_S0 64)))
(define (state-enclave.thread_context.prev_state.s1 state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_S1 64)))

(define (state-enclave.thread_context.prev_state.a0 state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_A0 64)))
(define (state-enclave.thread_context.prev_state.a1 state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_A1 64)))
(define (state-enclave.thread_context.prev_state.a2 state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_A2 64)))
(define (state-enclave.thread_context.prev_state.a3 state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_A3 64)))
(define (state-enclave.thread_context.prev_state.a4 state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_A4 64)))
(define (state-enclave.thread_context.prev_state.a5 state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_A5 64)))
(define (state-enclave.thread_context.prev_state.a6 state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_A6 64)))
(define (state-enclave.thread_context.prev_state.a7 state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_A7 64)))

(define (state-enclave.thread_context.prev_state.s2 state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_S2 64)))
(define (state-enclave.thread_context.prev_state.s3 state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_S3 64)))
(define (state-enclave.thread_context.prev_state.s4 state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_S4 64)))
(define (state-enclave.thread_context.prev_state.s5 state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_S5 64)))
(define (state-enclave.thread_context.prev_state.s6 state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_S6 64)))
(define (state-enclave.thread_context.prev_state.s7 state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_S7 64)))
(define (state-enclave.thread_context.prev_state.s8 state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_S8 64)))
(define (state-enclave.thread_context.prev_state.s9 state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_S9 64)))
(define (state-enclave.thread_context.prev_state.s10 state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_S10 64)))
(define (state-enclave.thread_context.prev_state.s11 state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_S11 64)))

(define (state-enclave.thread_context.prev_state.t3 state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_T3 64)))
(define (state-enclave.thread_context.prev_state.t4 state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_T4 64)))
(define (state-enclave.thread_context.prev_state.t5 state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_T5 64)))
(define (state-enclave.thread_context.prev_state.t6 state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_T6 64)))


(define (update-state-enclave.thread_context.encl_ptbr! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_THREAD_CONTEXT_ENCL_PTBR 64)) value))
(define (update-state-enclave.thread_context.prev_stvec! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_THREAD_CONTEXT_PREV_STVEC 64)) value))
(define (update-state-enclave.thread_context.prev_mie! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_THREAD_CONTEXT_PREV_MIE 64)) value))
(define (update-state-enclave.thread_context.prev_mideleg! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_THREAD_CONTEXT_PREV_MIDELEG 64)) value))
(define (update-state-enclave.thread_context.prev_medeleg! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_THREAD_CONTEXT_PREV_MEDELEG 64)) value))
(define (update-state-enclave.thread_context.prev_mepc! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_THREAD_CONTEXT_PREV_MEPC 64)) value))
(define (update-state-enclave.thread_context.prev_cache_binding! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_THREAD_CONTEXT_PREV_CACHE_BINDING 64)) value))

(define (update-state-enclave.thread_context.prev_state.slot! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_SLOT 64)) value))
(define (update-state-enclave.thread_context.prev_state.ra! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_RA 64)) value))
(define (update-state-enclave.thread_context.prev_state.sp! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_SP 64)) value))
(define (update-state-enclave.thread_context.prev_state.gp! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_GP 64)) value))
(define (update-state-enclave.thread_context.prev_state.tp! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_TP 64)) value))

(define (update-state-enclave.thread_context.prev_state.t0! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_T0 64)) value))
(define (update-state-enclave.thread_context.prev_state.t1! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_T1 64)) value))
(define (update-state-enclave.thread_context.prev_state.t2! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_T2 64)) value))

(define (update-state-enclave.thread_context.prev_state.s0! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_S0 64)) value))
(define (update-state-enclave.thread_context.prev_state.s1! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_S1 64)) value))

(define (update-state-enclave.thread_context.prev_state.a0! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_A0 64)) value))
(define (update-state-enclave.thread_context.prev_state.a1! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_A1 64)) value))
(define (update-state-enclave.thread_context.prev_state.a2! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_A2 64)) value))
(define (update-state-enclave.thread_context.prev_state.a3! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_A3 64)) value))
(define (update-state-enclave.thread_context.prev_state.a4! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_A4 64)) value))
(define (update-state-enclave.thread_context.prev_state.a5! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_A5 64)) value))
(define (update-state-enclave.thread_context.prev_state.a6! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_A6 64)) value))
(define (update-state-enclave.thread_context.prev_state.a7! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_A7 64)) value))

(define (update-state-enclave.thread_context.prev_state.s2! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_S2 64)) value))
(define (update-state-enclave.thread_context.prev_state.s3! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_S3 64)) value))
(define (update-state-enclave.thread_context.prev_state.s4! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_S4 64)) value))
(define (update-state-enclave.thread_context.prev_state.s5! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_S5 64)) value))
(define (update-state-enclave.thread_context.prev_state.s6! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_S6 64)) value))
(define (update-state-enclave.thread_context.prev_state.s7! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_S7 64)) value))
(define (update-state-enclave.thread_context.prev_state.s8! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_S8 64)) value))
(define (update-state-enclave.thread_context.prev_state.s9! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_S9 64)) value))
(define (update-state-enclave.thread_context.prev_state.s10! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_S10 64)) value))
(define (update-state-enclave.thread_context.prev_state.s11! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_S11 64)) value))

(define (update-state-enclave.thread_context.prev_state.t3! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_T3 64)) value))
(define (update-state-enclave.thread_context.prev_state.t4! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_T4 64)) value))
(define (update-state-enclave.thread_context.prev_state.t5! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_T5 64)) value))
(define (update-state-enclave.thread_context.prev_state.t6! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_GENERAL_REGISTERS_T_T6 64)) value))

;;; top_caller_eid
(define (state-enclave.top_caller_eid state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_TOP_CALLER_EID 64)))

(define (update-state-enclave.top_caller_eid! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_TOP_CALLER_EID 64)) value))

;;; caller_eid
(define (state-enclave.caller_eid state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_CALLER_EID 64)))

(define (update-state-enclave.caller_eid! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_CALLER_EID 64)) value))

;;; cur_callee_eid
(define (state-enclave.cur_callee_eid state eid)
  ((state-enclave_t state) eid (bv sm:ENCLAVE_T_CUR_CALLEE_EID 64)))

(define (update-state-enclave.cur_callee_eid! state eid value)
  (update-state-enclave_t! state (list eid (bv sm:ENCLAVE_T_CUR_CALLEE_EID 64)) value))

;;; call-args
(define (state-call-args.req_arg state pageno)
  (apply (state-pages state) (list pageno (bvpointer sm:CALL_ENCLAVE_ARG_REQ_ARG))))
(define (state-call-args.resp_val state pageno)
  (apply (state-pages state) (list pageno (bvpointer sm:CALL_ENCLAVE_ARG_RESP_VAL))))
(define (state-call-args.req_size state pageno)
  (apply (state-pages state) (list pageno (bvpointer sm:CALL_ENCLAVE_ARG_REQ_SIZE))))
(define (state-call-args.resp_size state pageno)
  (apply (state-pages state) (list pageno (bvpointer sm:CALL_ENCLAVE_ARG_RESP_SIZE))))

(define (update-state-call-args.req_arg! state pageno value)
  (update-state-pages! state (list pageno (bvpointer sm:CALL_ENCLAVE_ARG_REQ_ARG)) value))
(define (update-state-call-args.resp_val! state pageno value)
  (update-state-pages! state (list pageno (bvpointer sm:CALL_ENCLAVE_ARG_RESP_VAL)) value))
(define (update-state-call-args.req_size! state pageno value)
  (update-state-pages! state (list pageno (bvpointer sm:CALL_ENCLAVE_ARG_REQ_SIZE)) value))
(define (update-state-call-args.resp_size! state pageno value)
  (update-state-pages! state (list pageno (bvpointer sm:CALL_ENCLAVE_ARG_RESP_SIZE)) value))