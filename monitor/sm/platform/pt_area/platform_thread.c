void platform_enter_enclave_world()
{
  ///TODO: add register to indicate whether in encalve world or not
  return;
}

void platform_exit_enclave_world()
{
  ///TODO: add register to indicate whether in encalve world or not
  return;
}

int platform_check_in_enclave_world()
{
  ///TODO: add register to indicate whether in encalve world or not
  return 0;
}

/**
 * Compare the used satp and the enclave ptbr (encl_ptr)
 * It's supposed to be equal.
 */
int platform_check_enclave_authentication(struct enclave_t* enclave)
{
  if(enclave->thread_context.encl_ptbr != read_csr(satp))
    return -1;
  return 0;
}

/// Switch to enclave's ptbr (enclave_ptbr)
void platform_switch_to_enclave_ptbr(struct thread_state_t* thread, uintptr_t enclave_ptbr)
{
  write_csr(satp, enclave_ptbr);
}

/// Switch to host's ptbr (host_ptbr)
void platform_switch_to_host_ptbr(struct thread_state_t* thread, uintptr_t host_ptbr)
{
  write_csr(satp, host_ptbr);
}
