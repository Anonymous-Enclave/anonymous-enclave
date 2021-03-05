#lang rosette/safe

(require
  "state.rkt"
  serval/lib/core
  serval/riscv/spec
  serval/riscv/base
  serval/spec/refcnt
  (prefix-in riscv: serval/riscv/objdump)
  (prefix-in sm: "generated/monitor/asm-offsets.rkt")
)

(provide (all-defined-out))


(define (impl-enclave-mode-ref mregions)
  (define block-cpus (find-block-by-name mregions 'cpus))
  (mblock-iload block-cpus (list (bv 0 64) 'in_enclave)))


(define (impl-enclave-mode-set! mregions val)
  (define block-cpus (find-block-by-name mregions 'cpus))
  (mblock-istore! block-cpus val (list (bv 0 64) 'in_enclave)))


(define (impl-curr-eid-ref mregions)
  (define block-cpus (find-block-by-name mregions 'cpus))
  (mblock-iload block-cpus (list (bv 0 64) 'eid)))


(define (impl-curr-server-eid-ref mregions)
  (define block-curr-server-eid (find-block-by-name mregions 'curr_server_eid))
  (mblock-iload block-curr-server-eid null))


(define (mregions-abstract mregions)
  (define enclave-mode (impl-enclave-mode-ref mregions))
  (define enclave-mode-bool (bitvector->bool enclave-mode))
  (define curr-eid (impl-curr-eid-ref mregions))
  (define curr-server-eid (impl-curr-server-eid-ref mregions))
  (define block-server_enclave_metadata (find-block-by-name mregions '__server_enclave_metadata))
  (define block-enclave_metadata (find-block-by-name mregions '__enclave_metadata))
  (define block-enclaves (find-block-by-name mregions '__enclaves))
  (define page-refcnt (make-havoc-refcnt))
  (define-symbolic* call-args i64)

  (state (zero-regs)
         enclave-mode-bool
         curr-eid
         curr-server-eid
        ;;;  server-enclave.name
         (lambda (server-id)
           (mblock-iload block-server_enclave_metadata (list server-id 'server_name)))
        ;;;  server-enclave.eid
         (lambda (server-id)
           (mblock-iload block-server_enclave_metadata (list server-id 'eid)))
        ;;;  enclave_t
         (lambda (eid index)
          (mblock-iload block-enclave_metadata (list eid index)))
        ;;; pages
         (lambda (pageno index)
          (mblock-iload block-enclaves (list pageno index)))
         page-refcnt
         call-args
         ))