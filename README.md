# HEIMDALLR: Scalable Enclaves for Modular Applications

**HEIMDALLR** is a scalable enclave design with architectural support, which can support fine-grained memory isolation, scalable integrity protection, ownership transfer-based IPC and shadow fork. In HEIMDALLR, it also proposes some optional hardware extensions include memory encryption and on-demand cache line locking.

Now, HEIMDALLR is designed in RISCV architecture and extends the functionality of monitor in machine mode, also HEIMDALLR needs few hardware modifications in RISCV core to support fined-grained memory isolation. But later we will realize a pure software implementation. 

HEIMDALLR can support scalable and modular enclave which is suitable in the cloud scenario, such as serverless, data processing, e.g.. HEIMDALLR can ensure these traditional cloud applications with enhanced security and defend against the powerful attacker with high privilege like VMM administrator.

HEIMDALLR will open source in **august the first**.