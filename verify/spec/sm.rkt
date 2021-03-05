#lang rosette/safe

(require
  serval/riscv/objdump
  serval/riscv/shims
  serval/riscv/spec
  serval/riscv/pmp
  serval/lib/core
  serval/spec/refcnt
  serval/lib/unittest
  serval/spec/refinement
  "spec-ipc.rkt"
  "state.rkt" 
  "impl.rkt"
  "invariants.rkt"
  (only-in racket/base for values)
  (prefix-in sm: "generated/monitor/sm.globals.rkt")
  (prefix-in sm: "generated/monitor/sm.map.rkt")
  (prefix-in sm: "generated/monitor/sm.asm.rkt")
  (prefix-in sm: "generated/monitor/asm-offsets.rkt")
)


(define (find-symbol-start name)
  (define sym (find-symbol-by-name sm:symbols name))
  (bug-on (equal? sym #f) #:msg (format "find-symbol-start: No such symbol ~e" name))
  (bv (car sym) (XLEN)))

(define (find-symbol-end name)
  (define sym (find-symbol-by-name sm:symbols name))
  (bug-on (equal? sym #f) #:msg (format "find-symbol-end: No such symbol ~e" name))
  (bv (car (cdr sym)) (XLEN)))


(define (rep-invariant cpu)
  ;;; Set mscratch to the stack top of monitor
  (define mregions (cpu-mregions cpu))
  (define enclave-mode (impl-enclave-mode-ref mregions))
  (define enclave-mode-bool (bitvector->bool enclave-mode))

  (&& (bveq (csr-ref cpu 'mscratch) (bv sm:MTRAP_STACK 64))
      (bveq (csr-ref cpu 'mtvec) (find-symbol-start 'trap_vector))))


(define (init-rep-invariant cpu)
  ;;; Set mscratch to the stack top of monitor
  (csr-set! cpu 'mscratch (bv sm:MTRAP_STACK 64))
  (csr-set! cpu 'mtvec (find-symbol-start 'trap_vector)))


(define (check-fresh-cpu)
  ; Check that the rep invariant holds on init-rep-invariant
  (define cpu0 (init-cpu sm:symbols sm:globals))
  (init-rep-invariant cpu0)
  (check-unsat? (verify (rep-invariant cpu0)) "init rep invariants do not hold")

  (define (loweq s t)
    (&& (cpu-equal? s t)
        (bveq (impl-enclave-mode-ref (cpu-mregions s))
              (impl-enclave-mode-ref (cpu-mregions t)))))

  ; Check that init-rep-invariant does not overconstrain; the rep-invariant,
  ; i.e., executing init-rep-invariant; on a state satisfying the rep-invariant
  ; does not change anything.
  (define cpu1 (init-cpu sm:symbols sm:globals))
  (define cpu2 (init-cpu sm:symbols sm:globals))
  (with-asserts-only (begin
    (assert (loweq cpu1 cpu2))
    (assert (rep-invariant cpu1))
    (init-rep-invariant cpu2)
    (check-unsat? (verify (assert (cpu-equal? cpu1 cpu2))) "init-rep-invariant over-contrains")))
  (void))


(define (abs-function cpu)
  (define state (mregions-abstract (cpu-mregions cpu)))
  (set-state-regs! state
    (regs (gpr-ref cpu 'ra) (gpr-ref cpu 'sp) (gpr-ref cpu 'gp) (gpr-ref cpu 'tp) (gpr-ref cpu 't0)
          (gpr-ref cpu 't1) (gpr-ref cpu 't2) (gpr-ref cpu 's0) (gpr-ref cpu 's1) (gpr-ref cpu 'a0)
          (gpr-ref cpu 'a1) (gpr-ref cpu 'a2) (gpr-ref cpu 'a3) (gpr-ref cpu 'a4) (gpr-ref cpu 'a5)
          (gpr-ref cpu 'a6) (gpr-ref cpu 'a7) (gpr-ref cpu 's2) (gpr-ref cpu 's3) (gpr-ref cpu 's4)
          (gpr-ref cpu 's5) (gpr-ref cpu 's6) (gpr-ref cpu 's7) (gpr-ref cpu 's8) (gpr-ref cpu 's9)
          (gpr-ref cpu 's10) (gpr-ref cpu 's11) (gpr-ref cpu 't3) (gpr-ref cpu 't4) (gpr-ref cpu 't5)
          (gpr-ref cpu 't6)
          (csr-ref cpu 'satp) (csr-ref cpu 'scause) (csr-ref cpu 'scounteren) (csr-ref cpu 'sepc)
          (csr-ref cpu 'sscratch) (csr-ref cpu 'sstatus) (csr-ref cpu 'stvec) (csr-ref cpu 'stval)
          (csr-ref cpu 'mepc) (csr-ref cpu 'sip) (csr-ref cpu 'sie)
          (csr-ref cpu 'mip) (csr-ref cpu 'mideleg) (csr-ref cpu 'medeleg) (csr-ref cpu 'mie)))
  state)


(define (check-init-riscv)
  (define cpu (init-cpu sm:symbols sm:globals))
  (cpu-add-shim! cpu (find-symbol-start 'memset) memset-shim)

  (check-asserts-only (interpret-objdump-program cpu sm:instructions))

  (check-unsat? (verify (assert (rep-invariant cpu))))

  (define state (abs-function cpu))

  ; Check we didn't destory impl-visible state
  (check-unsat? (verify (assert (equal? state (abs-function cpu)))))
  ; Check that spec invariants hold on init state with some ghost state
  (check-unsat? (verify (assert (apply && (flatten (spec-invariants state))))))

  (void))
  

(define (make-impl-func callno)
  (lambda (cpu . args)
    (interpret-objdump-program cpu sm:instructions)))

(define (verify-function-refinement spec-func callno [args null])
  (define cpu (init-cpu sm:symbols sm:globals))
  (init-rep-invariant cpu)
  (cpu-add-shim! cpu (find-symbol-start 'memset) memset-shim)

  ; Initialize CPU state with the arguments
  (for ([reg '(a0 a1 a2 a3 a4 a5 a6)] [arg args])
    (gpr-set! cpu reg arg))
  (gpr-set! cpu 'a7 (bv callno (sm:XLEN)))
  (csr-set! cpu 'mcause (bv sm:ENCLAVE_CALL_TRAP 64))
  (set-cpu-pc! cpu (csr-ref cpu 'mtvec))

  (verify-refinement
    #:implstate cpu
    #:impl (make-impl-func callno)
    #:specstate (make-havoc-state)
    #:spec spec-func
    #:abs abs-function
    #:ri rep-invariant
    args))

(define (verify-ecall-refinement)
  (define cpu (init-cpu sm:symbols sm:globals))
  (define mregions (cpu-mregions cpu))
  (init-rep-invariant cpu)
  (cpu-add-shim! cpu (find-symbol-start 'memset) memset-shim)

  (csr-set! cpu 'mcause (bv sm:ENCLAVE_CALL_TRAP 64))
  (set-cpu-pc! cpu (csr-ref cpu 'mtvec))

  (split-cases
    (gpr-ref cpu 'a7)
    (list (bv sm:SBI_ACQUIRE_SERVER 64)
          (bv sm:SBI_CALL_ENCLAVE 64)
          (bv sm:SBI_ENCLAVE_RETURN 64)
    )
    (lambda (v)
      (gpr-set! cpu 'a7 v)

      (define (check name spec args)
        (test-case+ (format "sm ecall ~a" name)
          (check-equal? (asserts) null)
          (for ([reg '(a0 a1 a2 a3 a4 a5 a6)] [arg args])
            (gpr-set! cpu reg arg))
          (verify-refinement
            #:implstate cpu
            #:impl (make-impl-func #f)
            #:specstate (make-havoc-state)
            #:spec spec
            #:abs abs-function
            #:ri rep-invariant
            args)))

      (cond
        [(equal? v (bv sm:SBI_ACQUIRE_SERVER 64))
          (check "sm_server_enclave_acquire" (make-ecall-spec spec-acquire_server_enclave) (list (make-bv64)))]
        [(equal? v (bv sm:SBI_CALL_ENCLAVE 64))
          (check "sm_call_enclave" (make-ecall-spec spec-call_enclave) (list (make-bv64) (make-bv64) (make-bv64)))]
        [(equal? v (bv sm:SBI_ENCLAVE_RETURN 64))
          (check "sm_enclave_return" (make-ecall-spec spec-sm_enclave_return) (list (make-bv64)))]
      )
      (void))))


(define sm-ecall-tests
  (test-suite+ "sm ecall tests"
    (test-case+ "init riscv check" (check-init-riscv))
    (test-case+ "riscv fresh cpu check" (check-fresh-cpu))
    (verify-ecall-refinement)
))

(module+ test
  (time (run-tests sm-ecall-tests)))