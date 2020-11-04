#include "sm.h"
#include "enclave.h"
#include "enclave_vm.h"
#include "server_enclave.h"
#include "ipi.h"
#include "relay_page.h"
#include TARGET_PLATFORM_HEADER

/**************************************************************/
/*                                 called by enclave                                                        */
/**************************************************************/
/*
 * Monitor is responsible for change the relay page ownership:
 * It can be divide into two phases: First  umap the relay page for caller
 * enclave, and map the relay page for subsequent enclave asynchronously.
 * Second, change the relay page ownership entry in the relay page linke memory. 
 *
 * note: the relay_page_addr_u is the virtual address of relay page. However in the relay page entry,
 * it binds the enclave name with the physical address.
 * 
 * The first enclave in the enclave call chain can only hold single relay page region (now version),
 * but can split to atmost N piece ans transfer to different enclaves. The following enclave can receive\
 * multiple relay page entry. 
 */
uintptr_t transfer_relay_page(struct enclave_t *enclave, unsigned long relay_page_addr_u, unsigned long relay_page_size, char *enclave_name_u)
{
  uintptr_t ret = 0;
  char *enclave_name = NULL;
  unsigned long relay_page_addr = 0;

  enclave_name = va_to_pa((uintptr_t*)(enclave->root_page_table), enclave_name_u);
  relay_page_addr = (unsigned long)va_to_pa((uintptr_t*)(enclave->root_page_table), (char *)relay_page_addr_u);
  if(!enclave_name)
  {
    ret = -1UL;
    goto failed;
  }
  //unmap the relay page for call enclave
  unmap((uintptr_t*)(enclave->root_page_table), relay_page_addr_u, relay_page_size);  
  // if ((enclave->mm_arg_paddr[0] < relay_page_addr) || ((enclave->mm_arg_paddr[0] + enclave->mm_arg_size[0]) 
  // > (relay_page_addr + relay_page_size)))
  // {
  //   __free_relay_page_entry(enclave->mm_arg_paddr[0], enclave->mm_arg_size[0]);
  //   if ((enclave->mm_arg_paddr[0] < relay_page_addr) && ((enclave->mm_arg_paddr[0] + enclave->mm_arg_size[0]) 
  //   == (relay_page_addr + relay_page_size)))
  //   {
  //     __alloc_relay_page_entry(enclave->enclave_name, enclave->mm_arg_paddr[0], relay_page_addr - enclave->mm_arg_paddr[0]);
  //     __alloc_relay_page_entry(enclave->enclave_name, relay_page_addr, enclave->mm_arg_size[0] - (relay_page_addr - enclave->mm_arg_paddr[0]) );
  //   }
  // }
  for (int kk = 0; kk < 5; kk++)
  {
    if(enclave->mm_arg_paddr[kk] == relay_page_addr)
    {
      enclave->mm_arg_paddr[kk] = 0;
      enclave->mm_arg_size[kk] = 0;
    }
  }
  // enclave->mm_arg_paddr[0] = 0;
  // enclave->mm_arg_size[0] = 0;
  //change the relay page ownership
  if (change_relay_page_ownership((unsigned long)relay_page_addr, relay_page_size, enclave_name) < 0)
  {
    ret = -1UL;
    printm("M mode: transfer realy page: change relay page ownership failed \r\n");
  }
  release_enclave_metadata_lock();
  return ret;
failed:
  release_enclave_metadata_lock();
  printm("M MODE: transfer_relay_page failed\r\n");
  return ret;
}
/*
  Handle the asyn enclave call. Obtain the corresponding relay page virtual address and size, and
  invoke the transfer_relay_page.
 */
uintptr_t asyn_enclave_call(uintptr_t* regs, uintptr_t enclave_name, uintptr_t arg)
{
  uintptr_t ret = 0;
  struct enclave_t *enclave = NULL;
  unsigned long relay_page_addr = 0;
  int eid = 0;
  if(check_in_enclave_world() < 0)
  {
    printm("M mode: CPU not in the enclave mode\r\n");
    return -1UL;
  }

  acquire_enclave_metadata_lock();

  eid = get_curr_enclave_id();
  enclave = __get_enclave(eid);
  if(!enclave)
  {
    ret = -1UL;
    goto failed;
  }
  struct call_enclave_arg_t call_arg;
  struct call_enclave_arg_t* call_arg0 = va_to_pa((uintptr_t*)(enclave->root_page_table), (void*)arg);
  if(!call_arg0)
  {
    ret = -1UL;
    goto failed;
  }
  copy_from_host(&call_arg, call_arg0, sizeof(struct call_enclave_arg_t));
  if (transfer_relay_page(enclave, call_arg.req_vaddr, call_arg.req_size, (char *)enclave_name) < 0)
  {
    printm("M mode: asyn_enclave_call: transfer relay page is failed\r\n");
    goto failed;
  }
  release_enclave_metadata_lock();
  return ret;
failed:
  release_enclave_metadata_lock();
  printm("M MODE: asyn_enclave_call failed\r\n");
  return ret;
}

/*
 * Split relay page into two pieces:
 * it will update the relay page entry in the global link memory,
 * and add a new splitted entry. Also, it will update the enclave->mm_arg_paddr
 * and enclave->mm_arg_size. If the relay page owned by single enclave is upper
 * than RELAY_PAGE_NUM, an error will be reported.
 */
uintptr_t split_mem_region(uintptr_t *regs, uintptr_t mem_addr_u, uintptr_t mem_size, uintptr_t split_addr_u)
{
  uintptr_t ret = 0;
  struct enclave_t *enclave = NULL;
  uintptr_t mem_addr = 0, split_addr = 0;
  int eid = 0;
  if(check_in_enclave_world() < 0)
  {
    printm("M mode: CPU not in the enclave mode\r\n");
    return -1UL;
  }

  acquire_enclave_metadata_lock();

  eid = get_curr_enclave_id();
  enclave = __get_enclave(eid);
  if(!enclave)
  {
    ret = -1UL;
    goto failed;
  }
  if((split_addr_u < mem_addr_u) || (split_addr_u > (mem_addr_u + mem_size)))
  {
    printm("M mode: split_mem_region: split address is not in the relay page region \r\n");
    ret = -1UL;
    goto failed;
  }
  mem_addr = (unsigned long)va_to_pa((uintptr_t*)(enclave->root_page_table), (char *)mem_addr_u);
  split_addr = (unsigned long)va_to_pa((uintptr_t*)(enclave->root_page_table), (char *)split_addr_u);
  int found_corres_entry = 0;
  for(int kk = 0; kk < RELAY_PAGE_NUM; kk++)
  {
    if ((enclave->mm_arg_paddr[kk] == mem_addr) && (enclave->mm_arg_size[kk] == mem_size))
    {
      unsigned long split_size = enclave->mm_arg_paddr[kk] + enclave->mm_arg_size[kk] - split_addr;
      int found_empty_entry = 0;
      //free the old relay page entry in the global link memory
      __free_relay_page_entry(enclave->mm_arg_paddr[kk], enclave->mm_arg_size[kk]);
      //adjust the relay page region for enclave metadata
      enclave->mm_arg_size[kk] = split_addr - enclave->mm_arg_paddr[kk];
      //add the adjusted relay page entry in the global link memory
      __alloc_relay_page_entry(enclave->enclave_name, enclave->mm_arg_paddr[kk], enclave->mm_arg_size[kk]);
      //find the empty relay page entry for this enclave 
      printm("M mode: split_mem_region1: split addr %lx split size %lx \r\n", enclave->mm_arg_paddr[kk], enclave->mm_arg_size[kk]);
      for(int jj = kk; jj < RELAY_PAGE_NUM; jj++)
      {
        if ((enclave->mm_arg_paddr[jj] == 0) && (enclave->mm_arg_size[jj] == 0))
        {
          //add the new splitted relay page entry in the enclave metadata
          enclave->mm_arg_paddr[jj] = split_addr;
          enclave->mm_arg_size[jj] = split_size;
          printm("M mode: split_mem_region2: split addr %lx split size %lx \r\n", enclave->mm_arg_paddr[jj], enclave->mm_arg_size[jj]);
          __alloc_relay_page_entry(enclave->enclave_name, enclave->mm_arg_paddr[jj], enclave->mm_arg_size[jj]);
          found_empty_entry = 1;
          break;
        }
      }
      if (!found_empty_entry)
      {
        printm("M mode: split mem region: can not find the empty entry for splitted relay page \r\n");
        ret = -1UL;
        goto failed;
      }
      found_corres_entry = 1;
      break;
    }
  }
  if (!found_corres_entry)
  {
    printm("M mode: split mem region: can not find the correspongind relay page region\r\n");
    ret = -1UL;
    goto failed;
  }
  release_enclave_metadata_lock();
  return ret;
failed:
  release_enclave_metadata_lock();
  printm("M MODE: split_mem_region failed\r\n");
  return ret;
}
