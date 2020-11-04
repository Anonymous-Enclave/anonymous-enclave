#include "enclave.h"
#include "enclave_vm.h"
#include "sm.h"
#include "math.h"
#include "ipi.h"
#include "mtrap.h"
#include "attest.h"
#include <string.h>
#include "relay_page.h"
#include TARGET_PLATFORM_HEADER

int eapp_args = 0;

static struct cpu_state_t cpus[MAX_HARTS] = {{0,}, };

///whether cpu is in enclave-mode
int cpu_in_enclave(int i)
{
  return cpus[i].in_enclave;
}

///the eid of current cpu (if it is in enclave mode)
int cpu_eid(int i)
{
  return cpus[i].eid;
}

///spinlock
static spinlock_t enclave_metadata_lock = SPINLOCK_INIT;
void acquire_enclave_metadata_lock()
{
  spinlock_lock(&enclave_metadata_lock);
}
void release_enclave_metadata_lock()
{
  spinlock_unlock(&enclave_metadata_lock);
}

///enclave metadata
struct link_mem_t* enclave_metadata_head = NULL;
struct link_mem_t* enclave_metadata_tail = NULL;

struct link_mem_t* shadow_enclave_metadata_head = NULL;
struct link_mem_t* shadow_enclave_metadata_tail = NULL;

struct link_mem_t* relay_page_head = NULL;
struct link_mem_t* relay_page_tail = NULL;

static int enclave_name_cmp(char* name1, char* name2)
{
  for(int i=0; i<NAME_LEN; ++i)
  {
    if(name1[i] != name2[i])
    {
      return 1;
    }
    if(name1[i] == 0)
    {
      return 0;
    }
  }
  return 0;
}
///copy data from host
uintptr_t copy_from_host(void* dest, void* src, size_t size)
{
  memcpy(dest, src, size);
  return 0;
}

///copy data to host
uintptr_t copy_to_host(void* dest, void* src, size_t size)
{
  memcpy(dest, src, size);
  return 0;
}

int copy_word_to_host(unsigned int* ptr, uintptr_t value)
{
  *ptr = value;
  return 0;
}

int copy_dword_to_host(uintptr_t* ptr, uintptr_t value)
{
  *ptr = value;
  return 0;
}

///should only be called after acquire enclave_metadata_lock
static void enter_enclave_world(int eid)
{
  cpus[read_csr(mhartid)].in_enclave = 1;
  cpus[read_csr(mhartid)].eid = eid;

  platform_enter_enclave_world();
}

int get_curr_enclave_id()
{
  return cpus[read_csr(mhartid)].eid;
}

///should only be called after acquire enclave_metadata_lock
static void exit_enclave_world()
{
  cpus[read_csr(mhartid)].in_enclave = 0;
  cpus[read_csr(mhartid)].eid = -1;

  platform_exit_enclave_world();
}

///check whether we are in enclave-world through in_enclave state
int check_in_enclave_world()
{
  if(!(cpus[read_csr(mhartid)].in_enclave))
    return -1;

  if(platform_check_in_enclave_world() < 0)
    return -1;

  return 0;
}

///invoke the platform-specific authentication
static int check_enclave_authentication()
{
  if(platform_check_enclave_authentication() != 0)
    return -1;

  return 0;
}

///wrapper of the platform-specific switch func
static void switch_to_enclave_ptbr(struct thread_state_t* thread, uintptr_t ptbr)
{
  platform_switch_to_enclave_ptbr(thread, ptbr);
}

///wrapper of the platform-specific switch func
static void switch_to_host_ptbr(struct thread_state_t* thread, uintptr_t ptbr)
{
  platform_switch_to_host_ptbr(thread, ptbr);
}

/**
 * \brief it creates a new link_mem_t list, with the total size (mem_size), each 
 * 	entry is slab_size
 */
struct link_mem_t* init_mem_link(unsigned long mem_size, unsigned long slab_size)
{
  int retval = 0;
  struct link_mem_t* head;
  unsigned long resp_size = 0;
  printm("M mode: init_mem_link mem size %lx \r\n", mem_size);
  head = (struct link_mem_t*)mm_alloc(mem_size, &resp_size);
  
  if(head == NULL)
    return NULL;
  else
    memset((void*)head, 0, resp_size);

  if(resp_size <= sizeof(struct link_mem_t) + slab_size)
  {
    mm_free(head, resp_size);
    return NULL;
  }

  head->mem_size = resp_size;
  head->slab_size = slab_size;
  head->slab_num = (resp_size - sizeof(struct link_mem_t)) / slab_size;
  void* align_addr = (char*)head + sizeof(struct link_mem_t);
  head->addr = (char*)size_up_align((unsigned long)align_addr, slab_size);
  head->next_link_mem = NULL;

  return head;
}

/**
 * \brief create a new link_mem_t entry and append it into tail
 */
struct link_mem_t* add_link_mem(struct link_mem_t** tail)
{
  struct link_mem_t* new_link_mem;
  int retval = 0;
  unsigned long resp_size = 0;

  new_link_mem = (struct link_mem_t*)mm_alloc((*tail)->mem_size, &resp_size);

  if (new_link_mem == NULL)
    return NULL;
  else
    memset((void*)new_link_mem, 0, resp_size);

  if(resp_size <= sizeof(struct link_mem_t) + (*tail)->slab_size)
  {
    mm_free(new_link_mem, resp_size);
  }

  (*tail)->next_link_mem = new_link_mem;
  new_link_mem->mem_size = resp_size;
  new_link_mem->slab_num = (resp_size - sizeof(struct link_mem_t)) / (*tail)->slab_size;
  new_link_mem->slab_size = (*tail)->slab_size;
  void* align_addr = (char*)new_link_mem + sizeof(struct link_mem_t);
  new_link_mem->addr = (char*)size_up_align((unsigned long)align_addr, (*tail)->slab_size);
  new_link_mem->next_link_mem = NULL;
  
  *tail = new_link_mem;

  return new_link_mem;
}

/**
 * \brief remove the entry (indicated by ptr) in the head's list
 */
int remove_link_mem(struct link_mem_t** head, struct link_mem_t* ptr)
{
  struct link_mem_t *cur_link_mem, *tmp_link_mem;
  int retval =0;

  cur_link_mem = *head;
  if (cur_link_mem == ptr)
  {
    *head = cur_link_mem->next_link_mem;
    mm_free(cur_link_mem, cur_link_mem->mem_size);
    return 1;
  }

  for(cur_link_mem; cur_link_mem != NULL; cur_link_mem = cur_link_mem->next_link_mem)
  {
    if (cur_link_mem->next_link_mem == ptr)
    {
      tmp_link_mem = cur_link_mem->next_link_mem;
      cur_link_mem->next_link_mem = cur_link_mem->next_link_mem->next_link_mem;
      //FIXME
      mm_free(tmp_link_mem, tmp_link_mem->mem_size);
      return retval;
    }
  }

  return retval;
}

/** 
 * \brief alloc an enclave_t structure from encalve_metadata_head
 * 
 * eid represents the location in the list
 * sometimes you may need to acquire lock before calling this function
 */
struct enclave_t* __alloc_enclave()
{
  struct link_mem_t *cur, *next;
  struct enclave_t* enclave = NULL;
  int i, found, eid;

  ///enclave metadata list hasn't be initialized yet
  if(enclave_metadata_head == NULL)
  {
    enclave_metadata_head = init_mem_link(ENCLAVE_METADATA_REGION_SIZE, sizeof(struct enclave_t));
    if(!enclave_metadata_head)
    {
      //printm("M mode: __alloc_enclave: don't have enough mem\r\n");
      goto alloc_eid_out;
    }
    enclave_metadata_tail = enclave_metadata_head;
  }

  found = 0;
  eid = 0;
  for(cur = enclave_metadata_head; cur != NULL; cur = cur->next_link_mem)
  {
    for(i = 0; i < (cur->slab_num); i++)
    {
      enclave = (struct enclave_t*)(cur->addr) + i;
      if(enclave->state == INVALID)
      {
        memset((void*)enclave, 0, sizeof(struct enclave_t));
        enclave->state = FRESH;
        enclave->eid = eid;
        found = 1;
        break;
      }
      eid++;
    }
    if(found)
      break;
  }
  /**
   * FIXME(DD): currently, we use eid to represent the location in the head
   * 		but what about an link_mem_region (in the mid) is freed?
   * 		It may violate the eid used in the enclaves after the freed region
   * 		We should clarify whether this will happen, and how to handle.
   */

  ///don't have enough enclave metadata
  if(!found)
  {
    next = add_link_mem(&enclave_metadata_tail);
    if(next == NULL)
    {
      printm("M mode: __alloc_enclave: don't have enough mem\r\n");
      enclave = NULL;
      goto alloc_eid_out;
    }
    enclave = (struct enclave_t*)(next->addr);
    memset((void*)enclave, 0, sizeof(struct enclave_t));
    enclave->state = FRESH;  
    enclave->eid = eid;
  }

alloc_eid_out:
  return enclave;
}

///sometimes you may need to acquire lock before calling this function
int __free_enclave(int eid)
{
  struct link_mem_t *cur, *next;
  struct enclave_t *enclave = NULL;
  int i, found, count, ret_val;

  found = 0;
  count = 0;
  for(cur = enclave_metadata_head; cur != NULL; cur = cur->next_link_mem)
  {
    if(eid < (count + cur->slab_num))
    {
      enclave = (struct enclave_t*)(cur->addr) + (eid - count);
      memset((void*)enclave, 0, sizeof(struct enclave_t));
      enclave->state = INVALID;
      found = 1;
      ret_val = 0;
      break;
    }
    count += cur->slab_num;
  }

  //haven't alloc this eid 
  if(!found)
  {
    printm("M mode: __free_enclave: haven't alloc this eid\r\n");
    ret_val = -1;
  }

  return ret_val;
}

//sometimes you may need to acquire lock before calling this function
struct enclave_t* __get_enclave(int eid)
{
  struct link_mem_t *cur, *next;
  struct enclave_t *enclave;
  int i, found, count;

  found = 0;
  count = 0;
  for(cur = enclave_metadata_head; cur != NULL; cur = cur->next_link_mem)
  {
    if(eid < (count + cur->slab_num))
    {
      enclave = (struct enclave_t*)(cur->addr) + (eid - count);
      found = 1;
      break;
    }

    count += cur->slab_num;
  }

  //haven't alloc this eid 
  if(!found)
  {
    printm("M mode: __get_enclave: haven't alloc this enclave\r\n");
    enclave = NULL;
  }

  return enclave;
}

/*
  check whether the enclave name is duplicated
  return 0 if the enclave name is unique, otherwise
  return -1
*/
int check_enclave_name(char *enclave_name, int target_eid)
{
  struct link_mem_t *cur, *next;
  struct enclave_t* enclave = NULL;
  int i, found, eid;
  eid = 0;
  for(cur = enclave_metadata_head; cur != NULL; cur = cur->next_link_mem)
  {
    for(i = 0; i < (cur->slab_num); i++)
    {
      enclave = (struct enclave_t*)(cur->addr) + i;
      if((enclave->state > INVALID) &&(enclave_name_cmp(enclave_name, enclave->enclave_name)==0) && (target_eid != eid))
      {
        printm("M mode: check enclave name: enclave name is already existed, enclave name is %s\r\n", enclave_name);
        return -1;
      }
      eid++;
    }
  }
  return 0;
}

///sometimes you may need to acquire lock before calling this function
static struct shadow_enclave_t* __alloc_shadow_enclave()
{
  struct link_mem_t *cur, *next;
  struct shadow_enclave_t* shadow_enclave = NULL;
  int i, found, eid;

  //enclave metadata list hasn't be initialized yet
  if(shadow_enclave_metadata_head == NULL)
  {
    shadow_enclave_metadata_head = init_mem_link(SHADOW_ENCLAVE_METADATA_REGION_SIZE, sizeof(struct shadow_enclave_t));
    if(!shadow_enclave_metadata_head)
    {
      //printm("M mode: __alloc_enclave: don't have enough mem\r\n");
      goto alloc_eid_out;
    }
    shadow_enclave_metadata_tail = shadow_enclave_metadata_head;
  }

  found = 0;
  eid = 0;
  for(cur = shadow_enclave_metadata_head; cur != NULL; cur = cur->next_link_mem)
  {
    for(i = 0; i < (cur->slab_num); i++)
    {
      shadow_enclave = (struct shadow_enclave_t*)(cur->addr) + i;
      if(shadow_enclave->state == INVALID)
      {
        memset((void*)shadow_enclave, 0, sizeof(struct shadow_enclave_t));
        shadow_enclave->state = FRESH;
        shadow_enclave->eid = eid;
        found = 1;
        break;
      }
      eid++;
    }
    if(found)
      break;
  }
  /**
   * FIXME(DD): it seems shadow enclave's eid will overlap with normal enclave's eid.
   *        this is weird.
   */

  ///don't have enough enclave metadata
  if(!found)
  {
    next = add_link_mem(&shadow_enclave_metadata_tail);
    if(next == NULL)
    {
      printm("M mode: __alloc_enclave: don't have enough mem\r\n");
      shadow_enclave = NULL;
      goto alloc_eid_out;
    }
    shadow_enclave = (struct shadow_enclave_t*)(next->addr);
    memset((void*)shadow_enclave, 0, sizeof(struct shadow_enclave_t));
    shadow_enclave->state = FRESH;  
    shadow_enclave->eid = eid;
  }

alloc_eid_out:
  return shadow_enclave;
}

///sometimes you may need to acquire lock before calling this function
static int __free_shadow_enclave(int eid)
{
  struct link_mem_t *cur, *next;
  struct shadow_enclave_t *shadow_enclave = NULL;
  int i, found, count, ret_val;

  found = 0;
  count = 0;
  for(cur = shadow_enclave_metadata_head; cur != NULL; cur = cur->next_link_mem)
  {
    if(eid < (count + cur->slab_num))
    {
      shadow_enclave = (struct shadow_enclave_t*)(cur->addr) + (eid - count);
      memset((void*)shadow_enclave, 0, sizeof(struct shadow_enclave_t));
      shadow_enclave->state = INVALID;
      found = 1;
      ret_val = 0;
      break;
    }
    count += cur->slab_num;
  }

  //haven't alloc this eid 
  if(!found)
  {
    printm("M mode: __free_enclave: haven't alloc this eid\r\n");
    ret_val = -1;
  }

  return ret_val;
}

///sometimes you may need to acquire lock before calling this function
static struct shadow_enclave_t* __get_shadow_enclave(int eid)
{
  struct link_mem_t *cur, *next;
  struct shadow_enclave_t *shadow_enclave;
  int i, found, count;

  found = 0;
  count = 0;
  for(cur = shadow_enclave_metadata_head; cur != NULL; cur = cur->next_link_mem)
  {
    if(eid < (count + cur->slab_num))
    {
      shadow_enclave = (struct shadow_enclave_t*)(cur->addr) + (eid - count);
      found = 1;
      break;
    }

    count += cur->slab_num;
  }

  //haven't alloc this eid 
  if(!found)
  {
    printm("M mode: __get_enclave: haven't alloc this shadow_enclave\r\n");
    shadow_enclave = NULL;
  }

  return shadow_enclave;
}

/**
 * \brief this function is used to handle IPC in enclave,
 * 	  it will return the last enclave in the chain.
 * 	  This is used to help us identify the real executing encalve.
 */
struct enclave_t* __get_real_enclave(int eid)
{
  struct enclave_t* enclave = __get_enclave(eid);
  if(!enclave)
    return NULL;

  struct enclave_t* real_enclave = NULL;
  if(enclave->cur_callee_eid == -1)
    real_enclave = enclave;
  else
    real_enclave = __get_enclave(enclave->cur_callee_eid);

  return real_enclave;
}


/********************************************/
/*                   Relay Page             */
/********************************************/

/*
  allocate a new entry in the link memory, if link head is NULL, we initialize the link memory.
  When the ownership of  relay page is changed, we need first destroy the old relay page entry which
  records the out of  data ownership of relay page, and then allocate the new relay page entry with
  new ownership.

  Return value:
  relay_pagfe_entry @ allocate the relay page successfully
  NULL @ allcate the relay page is failed
 */
struct relay_page_entry_t* __alloc_relay_page_entry(char *enclave_name, unsigned long relay_page_addr, unsigned long relay_page_size)
{
  struct link_mem_t *cur, *next;
  struct relay_page_entry_t* relay_page_entry = NULL;
  int found = 0;

  //relay_page_entry metadata list hasn't be initialized yet
  if(relay_page_head == NULL)
  {
    relay_page_head = init_mem_link(sizeof(struct relay_page_entry_t)*ENTRY_PER_RELAY_PAGE_REGION, sizeof(struct relay_page_entry_t));
    if(!relay_page_head)
    {
      printm("M mode: __alloc_relay_page_entry: init mem link is failed \r\n");
      goto failed;
    }
    relay_page_tail = relay_page_head;
  }

  //check whether relay page is owned by another enclave
  for(cur = relay_page_head; cur != NULL; cur = cur->next_link_mem)
  {
    for(int i = 0; i < (cur->slab_num); i++)
    {
      relay_page_entry = (struct relay_page_entry_t*)(cur->addr) + i;
      if(relay_page_entry->addr == relay_page_addr)
      {
        printm("M mode: __alloc_relay_page_entry: the relay page is owned by another enclave\r\n");
        relay_page_entry = (void*)(-1UL);
        goto failed;
      }
    }
  }
  //traverse the link memory and check whether there is an empty entry in the link memoy
  found = 0;
  int link_mem_index = 0;
  for(cur = relay_page_head; cur != NULL; cur = cur->next_link_mem)
  {
    for(int i = 0; i < (cur->slab_num); i++)
    {
      relay_page_entry = (struct relay_page_entry_t*)(cur->addr) + i;
      //address in the relay page entry remains zero which means the entry is not used
      if(relay_page_entry->addr == 0)
      {
        memcpy(relay_page_entry->enclave_name, enclave_name, NAME_LEN);
        relay_page_entry->addr = relay_page_addr;
        relay_page_entry->size = relay_page_size;
        found = 1;
        break;
      }
    }
    if(found)
      break;
    link_mem_index = link_mem_index + 1; 
  }

  //don't have enough memory to allocate a new entry in current link memory, so allocate a new link memory 
  if(!found)
  {
    next = add_link_mem(&relay_page_tail);
    if(next == NULL)
    {
      printm("M mode: __alloc_relay_page_entry: don't have enough mem\r\n");
      relay_page_entry = NULL;
      goto failed;
    }
    relay_page_entry = (struct relay_page_entry_t*)(next->addr);
    memcpy(relay_page_entry->enclave_name, enclave_name, NAME_LEN);
    relay_page_entry->addr = relay_page_addr;
    relay_page_entry->size = relay_page_size;
  }

  return relay_page_entry;

failed:
  if(relay_page_entry)
    memset((void*)relay_page_entry, 0, sizeof(struct relay_page_entry_t));

  return NULL;
}

/*
free the relay page indexed by the the given enclave name.
now we just set the address in the relay page netry to zero
which means this relay page entry is not used. 

return value:
0 : free the relay_page successfully
-1 : can not find the corresponding relay page 
 */
int __free_relay_page_entry(unsigned long relay_page_addr, unsigned long relay_page_size)
{
  struct link_mem_t *cur, *next;
  struct relay_page_entry_t *relay_page_entry = NULL;
  int found = 0, ret_val = 0;

  for(cur = relay_page_head; cur != NULL; cur = cur->next_link_mem)
  {
    for(int i = 0; i < (cur->slab_num); i++)
    {
      relay_page_entry = (struct relay_page_entry_t*)(cur->addr) + i;
      //find the corresponding relay page entry by given address and size
      if((relay_page_entry->addr >= relay_page_addr) && ((relay_page_entry->addr + relay_page_entry->size) <= (relay_page_addr + relay_page_size)))
      {
        found = 1;
        memset(relay_page_entry->enclave_name, 0, NAME_LEN);
        relay_page_entry->addr = 0;
        relay_page_entry->size = 0;
      }
    }
  }
  //haven't alloc this relay page
  if(!found)
  {
    printm("M mode: __free_relay_page_entry: relay page  [%lx : %lx + %lx]is not existed \r\n", relay_page_addr, relay_page_addr, relay_page_size);
    ret_val = -1;
  }

  return ret_val;
}

/*
 * retrieve the relay page entry by given the enclave name
 * enclave_name: get the realy page entry with given enclave name
 * slab_index: find the corresponding relay page entry after the $slab_index$ in the link memory
 * link_mem_index: find the corresponding relay page entry after $the link_mem_index$ link memory
 */
struct relay_page_entry_t* __get_relay_page_by_name(char* enclave_name, int *slab_index, int *link_mem_index)
{
  struct link_mem_t *cur;
  struct relay_page_entry_t *relay_page_entry = NULL;
  int i, found, k;

  found = 0;
  cur = relay_page_head;
  for (k  = 0; k < (*link_mem_index); k++)
    cur = cur->next_link_mem;
  
  i = *slab_index;
  for(cur; cur != NULL; cur = cur->next_link_mem)
  {
    for(i; i < (cur->slab_num); ++i)
    {
      relay_page_entry = (struct relay_page_entry_t*)(cur->addr) + i;
      if((relay_page_entry->addr != 0) && enclave_name_cmp(relay_page_entry->enclave_name, enclave_name)==0)
      {
        found = 1;
        *slab_index = i+1;
        //check whether slab_index is overflow
        if ((i+1) >= (cur->slab_num))
        {
          *slab_index = 0;
          *link_mem_index = (*link_mem_index) + 1;
        }
        break;
      }
    }
    if(found)
      break;
    *link_mem_index = (*link_mem_index) + 1;
    i=0;
  }

  //haven't alloc this eid 
  if(!found)
  {
    printm("M mode: __get_relay_page_by_name: haven't alloc this enclave:%s\r\n", enclave_name);
    return NULL;
  }

  return relay_page_entry;
}

/*
  Change the relay page ownership, delete the old relay page entry in the link memory
  and add an entry with new ownership .
  If the relay page is not existed, reture error.
 */
uintptr_t change_relay_page_ownership(unsigned long relay_page_addr, unsigned long relay_page_size, char *enclave_name)
{
  uintptr_t ret_val = 0;
  if ( __free_relay_page_entry( relay_page_addr,  relay_page_size) < 0)
  {
    printm("M mode: change_relay_page_ownership: can not free relay page which needs transfer the ownership\n");
    ret_val = -1;
    return ret_val;
  }
  if (__alloc_relay_page_entry(enclave_name, relay_page_addr, relay_page_size) == NULL)
  {
    printm("M mode: change_relay_page_ownership: can not alloc relay page entry, addr is %lx\r\n", relay_page_addr);
  }
  return ret_val;
}




/**
 * \brief swap states from host to enclaves, e.g., satp, stvec, etc.
 * 	  it is used when we run/resume enclave/shadow-encalves.
 */
static int swap_from_host_to_enclave(uintptr_t* host_regs, struct enclave_t* enclave)
{
  //grant encalve access to memory
  if(grant_enclave_access(enclave) < 0)
    return -1;

  //save host context
  swap_prev_state(&(enclave->thread_context), host_regs);

  //different platforms have differnt ptbr switch methods
  switch_to_enclave_ptbr(&(enclave->thread_context), enclave->thread_context.encl_ptbr);

  //save host trap vector
  swap_prev_stvec(&(enclave->thread_context), read_csr(stvec));

  //TODO: save host cache binding
  //swap_prev_cache_binding(&enclave -> threads[0], read_csr(0x356));

  //disable interrupts
  swap_prev_mie(&(enclave->thread_context), read_csr(mie));
  clear_csr(mip, MIP_MTIP);
  clear_csr(mip, MIP_STIP);
  clear_csr(mip, MIP_SSIP);
  clear_csr(mip, MIP_SEIP);

  //disable interrupts/exceptions delegation
  swap_prev_mideleg(&(enclave->thread_context), read_csr(mideleg));
  swap_prev_medeleg(&(enclave->thread_context), read_csr(medeleg));

  //swap the mepc to transfer control to the enclave
  swap_prev_mepc(&(enclave->thread_context), read_csr(mepc)); 

  //set mstatus to transfer control to u mode
  uintptr_t mstatus = read_csr(mstatus);
  mstatus = INSERT_FIELD(mstatus, MSTATUS_MPP, PRV_U);
  write_csr(mstatus, mstatus);

  //mark that cpu is in enclave world now
  enter_enclave_world(enclave->eid);

  __asm__ __volatile__ ("sfence.vma" : : : "memory");

  return 0;
}

/**
 * \brief similiar to swap_from_host_to_enclave
 */
static int swap_from_enclave_to_host(uintptr_t* regs, struct enclave_t* enclave)
{
  //retrieve enclave access to memory
  retrieve_enclave_access(enclave);

  //restore host context
  swap_prev_state(&(enclave->thread_context), regs);

  //restore host's ptbr
  switch_to_host_ptbr(&(enclave->thread_context), enclave->host_ptbr);

  //restore host stvec
  swap_prev_stvec(&(enclave->thread_context), read_csr(stvec));

  //TODO: restore host cache binding
  //swap_prev_cache_binding(&(enclave->thread_context), );
  
  //restore interrupts
  swap_prev_mie(&(enclave->thread_context), read_csr(mie));

  //restore interrupts/exceptions delegation
  swap_prev_mideleg(&(enclave->thread_context), read_csr(mideleg));
  swap_prev_medeleg(&(enclave->thread_context), read_csr(medeleg));

  //transfer control back to kernel
  swap_prev_mepc(&(enclave->thread_context), read_csr(mepc));

  //restore mstatus
  uintptr_t mstatus = read_csr(mstatus);
  mstatus = INSERT_FIELD(mstatus, MSTATUS_MPP, PRV_S);
  write_csr(mstatus, mstatus);

  //mark that cpu is out of enclave world now
  exit_enclave_world();

  __asm__ __volatile__ ("sfence.vma" : : : "memory");

  return 0;
}

static int __enclave_call(uintptr_t* regs, struct enclave_t* top_caller_enclave, struct enclave_t* caller_enclave, struct enclave_t* callee_enclave)
{
  //move caller's host context to callee's host context
  uintptr_t encl_ptbr = callee_enclave->thread_context.encl_ptbr;
  memcpy((void*)(&(callee_enclave->thread_context)), (void*)(&(caller_enclave->thread_context)), sizeof(struct thread_state_t));
  callee_enclave->thread_context.encl_ptbr = encl_ptbr;
  callee_enclave->host_ptbr = caller_enclave->host_ptbr;
  callee_enclave->ocall_func_id = caller_enclave->ocall_func_id;
  callee_enclave->ocall_arg0 = caller_enclave->ocall_arg0;
  callee_enclave->ocall_arg1 = caller_enclave->ocall_arg1;
  callee_enclave->ocall_syscall_num = caller_enclave->ocall_syscall_num; 

  //save caller's enclave context on its prev_state
  swap_prev_state(&(caller_enclave->thread_context), regs);
  caller_enclave->thread_context.prev_stvec = read_csr(stvec);
  caller_enclave->thread_context.prev_mie = read_csr(mie);
  caller_enclave->thread_context.prev_mideleg = read_csr(mideleg);
  caller_enclave->thread_context.prev_medeleg = read_csr(medeleg);
  caller_enclave->thread_context.prev_mepc = read_csr(mepc);

  //clear callee's enclave context
  memset((void*)regs, 0, sizeof(struct general_registers_t));

  //different platforms have differnt ptbr switch methods
  switch_to_enclave_ptbr(&(callee_enclave->thread_context), callee_enclave->thread_context.encl_ptbr);

  //callee use caller's stvec

  //callee use caller's cache binding

  //callee use caller's mie/mip
  clear_csr(mip, MIP_MTIP);
  clear_csr(mip, MIP_STIP);
  clear_csr(mip, MIP_SSIP);
  clear_csr(mip, MIP_SEIP);

  //callee use caller's interrupts/exceptions delegation

  //transfer control to the callee enclave
  write_csr(mepc, callee_enclave->entry_point);

  //callee use caller's mstatus

  //mark that cpu is in callee enclave world now
  enter_enclave_world(callee_enclave->eid);

  top_caller_enclave->cur_callee_eid = callee_enclave->eid;
  caller_enclave->cur_callee_eid = callee_enclave->eid;
  callee_enclave->caller_eid = caller_enclave->eid;
  callee_enclave->top_caller_eid = top_caller_enclave->eid;

  __asm__ __volatile__ ("sfence.vma" : : : "memory");

  return 0;
}

static int __enclave_return(uintptr_t* regs, struct enclave_t* callee_enclave, struct enclave_t* caller_enclave, struct enclave_t* top_caller_enclave)
{
  //restore caller's context
  memcpy((void*)regs, (void*)(&(caller_enclave->thread_context.prev_state)), sizeof(struct general_registers_t));
  swap_prev_stvec(&(caller_enclave->thread_context), callee_enclave->thread_context.prev_stvec);
  swap_prev_mie(&(caller_enclave->thread_context), callee_enclave->thread_context.prev_mie);
  swap_prev_mideleg(&(caller_enclave->thread_context), callee_enclave->thread_context.prev_mideleg);
  swap_prev_medeleg(&(caller_enclave->thread_context), callee_enclave->thread_context.prev_medeleg);
  swap_prev_mepc(&(caller_enclave->thread_context), callee_enclave->thread_context.prev_mepc);

  //restore caller's host context
  memcpy((void*)(&(caller_enclave->thread_context.prev_state)), (void*)(&(callee_enclave->thread_context.prev_state)), sizeof(struct general_registers_t));

  //clear callee's enclave context
  uintptr_t encl_ptbr = callee_enclave->thread_context.encl_ptbr;
  memset((void*)(&(callee_enclave->thread_context)), 0, sizeof(struct thread_state_t));
  callee_enclave->thread_context.encl_ptbr = encl_ptbr;
  callee_enclave->host_ptbr = 0;
  callee_enclave->ocall_func_id = NULL;
  callee_enclave->ocall_arg0 = NULL;
  callee_enclave->ocall_arg1 = NULL;
  callee_enclave->ocall_syscall_num = NULL;

  //different platforms have differnt ptbr switch methods
  switch_to_enclave_ptbr(&(caller_enclave->thread_context), caller_enclave->thread_context.encl_ptbr);

  clear_csr(mip, MIP_MTIP);
  clear_csr(mip, MIP_STIP);
  clear_csr(mip, MIP_SSIP);
  clear_csr(mip, MIP_SEIP);

  //mark that cpu is in caller enclave world now
  enter_enclave_world(caller_enclave->eid);
  top_caller_enclave->cur_callee_eid = caller_enclave->eid;
  caller_enclave->cur_callee_eid = -1;
  callee_enclave->caller_eid = -1;
  callee_enclave->top_caller_eid = -1;

  __asm__ __volatile__ ("sfence.vma" : : : "memory");

  return 0;
}

/**
 * \brief free a list of memory indicated by pm_area_struct.
 * 	  the pages are zero-ed and turned back to host.
 */
void free_enclave_memory(struct pm_area_struct *pma)
{
  uintptr_t paddr = 0;
  uintptr_t size = 0;

  extern spinlock_t mbitmap_lock;
  spinlock_lock(&mbitmap_lock);

  while(pma)
  {
    paddr = pma->paddr;
    size = pma->size;
    pma = pma->pm_next;
    //we can not clear the first page as it will be used to free mem by host
    memset((void*)(paddr + RISCV_PGSIZE), 0, size - RISCV_PGSIZE);
    //printm("M mode: free_enclave_memory:paddr:0x%lx, size:0x%lx\r\n", paddr, size);
    __free_secure_memory(paddr, size);
  }

  spinlock_unlock(&mbitmap_lock);
}

/**************************************************************/
/*                   called by host                           */
/**************************************************************/

uintptr_t create_enclave(struct enclave_create_param_t create_args)
{
  struct enclave_t* enclave = NULL;
  uintptr_t ret = 0;
  int need_free_secure_memory = 0;

  acquire_enclave_metadata_lock();

  if(!enable_enclave())
  {
    ret = ENCLAVE_ERROR;
    goto failed;
  }

  //check enclave memory layout
  if(check_and_set_secure_memory(create_args.paddr, create_args.size) != 0)
  {
    ret = ENCLAVE_ERROR;
    goto failed;
  }
  need_free_secure_memory = 1;

  //check enclave memory layout
  if(check_enclave_layout(create_args.paddr + RISCV_PGSIZE, 0, -1UL, create_args.paddr, create_args.paddr + create_args.size) != 0)
  {
    ret = ENCLAVE_ERROR;
    goto failed;
  }

  enclave = __alloc_enclave();
  if(!enclave)
  {
    ret = ENCLAVE_NO_MEM;
    goto failed;
  }
  enclave->entry_point = create_args.entry_point;
  enclave->ocall_func_id = create_args.ecall_arg0;
  enclave->ocall_arg0 = create_args.ecall_arg1;
  enclave->ocall_arg1 = create_args.ecall_arg2;
  enclave->ocall_syscall_num = create_args.ecall_arg3;
  enclave->kbuffer = create_args.kbuffer;
  enclave->kbuffer_size = create_args.kbuffer_size;
  enclave->shm_paddr = create_args.shm_paddr;
  enclave->shm_size = create_args.shm_size;
  enclave->host_ptbr = read_csr(satp);
  enclave->root_page_table = create_args.paddr + RISCV_PGSIZE;
  enclave->thread_context.encl_ptbr = ((create_args.paddr+RISCV_PGSIZE) >> RISCV_PGSHIFT) | SATP_MODE_CHOICE;
  enclave->type = NORMAL_ENCLAVE;
  enclave->state = FRESH;
  enclave->caller_eid = -1;
  enclave->top_caller_eid = -1;
  enclave->cur_callee_eid = -1;
  memcpy(enclave->enclave_name, create_args.name, NAME_LEN);


  //traverse vmas
  struct pm_area_struct* pma = (struct pm_area_struct*)(create_args.paddr);
  struct vm_area_struct* vma = (struct vm_area_struct*)(create_args.paddr + sizeof(struct pm_area_struct));
  pma->paddr = create_args.paddr;
  pma->size = create_args.size;
  pma->free_mem = create_args.free_mem;
  if(pma->free_mem < pma->paddr || pma->free_mem >= pma->paddr+pma->size
      || pma->free_mem & ((1<<RISCV_PGSHIFT) - 1))
  {
    ret = ENCLAVE_ERROR;
    goto failed;
  }
  pma->pm_next = NULL;
  enclave->pma_list = pma;
  traverse_vmas(enclave->root_page_table, vma);

  //FIXME: here we assume there are exactly text(include text/data/bss) vma and stack vma
  while(vma)
  {
    if(vma->va_start == ENCLAVE_DEFAULT_TEXT_BASE)
    {
      enclave->text_vma = vma;
    }
    if(vma->va_end == ENCLAVE_DEFAULT_STACK_BASE)
    {
      enclave->stack_vma = vma;
      enclave->_stack_top = enclave->stack_vma->va_start;
    }
    vma->pma = pma;
    vma = vma->vm_next;
  }
  if(enclave->text_vma)
    enclave->text_vma->vm_next = NULL;
  if(enclave->stack_vma)
    enclave->stack_vma->vm_next = NULL;
  enclave->_heap_top = ENCLAVE_DEFAULT_HEAP_BASE;
  enclave->heap_vma = NULL;
  enclave->mmap_vma = NULL;

  enclave->free_pages = NULL;
  enclave->free_pages_num = 0;
  uintptr_t free_mem = create_args.paddr + create_args.size - RISCV_PGSIZE;
  while(free_mem >= create_args.free_mem)
  {
    struct page_t *page = (struct page_t*)free_mem;
    page->paddr = free_mem;
    page->next = enclave->free_pages;
    enclave->free_pages = page;
    enclave->free_pages_num += 1;
    free_mem -= RISCV_PGSIZE;
  }

  //check kbuffer
  if(create_args.kbuffer_size < RISCV_PGSIZE || create_args.kbuffer & (RISCV_PGSIZE-1) || create_args.kbuffer_size & (RISCV_PGSIZE-1))
  {
    ret = ENCLAVE_ERROR;
    goto failed;
  }
  mmap((uintptr_t*)(enclave->root_page_table), &(enclave->free_pages), ENCLAVE_DEFAULT_KBUFFER, create_args.kbuffer, create_args.kbuffer_size);

  //check shm
  if(create_args.shm_paddr && create_args.shm_size &&
      !(create_args.shm_paddr & (RISCV_PGSIZE-1)) && !(create_args.shm_size & (RISCV_PGSIZE-1)))
  {
    mmap((uintptr_t*)(enclave->root_page_table), &(enclave->free_pages), ENCLAVE_DEFAULT_SHM_BASE, create_args.shm_paddr, create_args.shm_size);
    enclave->shm_paddr = create_args.shm_paddr;
    enclave->shm_size = create_args.shm_size;
  }
  else
  {
    enclave->shm_paddr = 0;
    enclave->shm_size = 0;
  }
  hash_enclave(enclave, (void*)(enclave->hash), 0);
  copy_word_to_host((unsigned int*)create_args.eid_ptr, enclave->eid);

out:
  release_enclave_metadata_lock();
  return ret;

failed:
  if(need_free_secure_memory)
  {
    free_secure_memory(create_args.paddr, create_args.size);
  }
  if(enclave)
  {
    __free_enclave(enclave->eid);
  }
  release_enclave_metadata_lock();
  return ret;
}

uintptr_t create_shadow_enclave(struct enclave_create_param_t create_args)
{
  struct enclave_t* enclave = NULL;
  uintptr_t ret = 0;
  int need_free_secure_memory = 0;
  acquire_enclave_metadata_lock();
  eapp_args = 0;
  if(!enable_enclave())
  {
    ret = ENCLAVE_ERROR;
    goto failed;
  }

  //check enclave memory layout
  if(check_and_set_secure_memory(create_args.paddr, create_args.size) != 0)
  {
    ret = ENCLAVE_ERROR;
    goto failed;
  }
  need_free_secure_memory = 1;
  //check enclave memory layout
  if(check_enclave_layout(create_args.paddr + RISCV_PGSIZE, 0, -1UL, create_args.paddr, create_args.paddr + create_args.size) != 0)
  {
    ret = ENCLAVE_ERROR;
    goto failed;
  }
  struct shadow_enclave_t* shadow_enclave;
  shadow_enclave = __alloc_shadow_enclave();
  if(!shadow_enclave)
  {
    printm("M mode: create enclave: no enough memory to alloc_shadow_enclave\n");
    ret = ENCLAVE_NO_MEM;
    goto failed;
  }
  shadow_enclave->entry_point = create_args.entry_point;
  //first page is reserve for page link
  shadow_enclave->root_page_table = create_args.paddr + RISCV_PGSIZE;
  shadow_enclave->thread_context.encl_ptbr = ((create_args.paddr+RISCV_PGSIZE) >> RISCV_PGSHIFT) | SATP_MODE_CHOICE;
  hash_shadow_enclave(shadow_enclave, (void*)(shadow_enclave->hash), 0);
  copy_word_to_host((unsigned int*)create_args.eid_ptr, shadow_enclave->eid);
out:
  spinlock_unlock(&enclave_metadata_lock);
  return ret;

failed:
  if(need_free_secure_memory)
  {
    free_secure_memory(create_args.paddr, create_args.size);
  }
  spinlock_unlock(&enclave_metadata_lock);
  return ret;
}

uintptr_t attest_enclave(uintptr_t eid, uintptr_t report_ptr, uintptr_t nonce)
{
  struct enclave_t* enclave = NULL;
  int attestable = 1;
  struct report_t report;
  enclave_state_t old_state = INVALID;
  acquire_enclave_metadata_lock();
  enclave = __get_enclave(eid);
  if(!enclave || (enclave->state != FRESH && enclave->state != STOPPED)
      || enclave->host_ptbr != read_csr(satp))
  {
    attestable = 0;
  }
  else
  {
    old_state = enclave->state;
    enclave->state = ATTESTING;
  }
  release_enclave_metadata_lock();

  if(!attestable)
  {
    printm("M mode: attest_enclave: enclave%d is not attestable\r\n", eid);
    return -1UL;
  }

  memcpy((void*)(report.dev_pub_key), (void*)DEV_PUB_KEY, PUBLIC_KEY_SIZE);
  memcpy((void*)(report.sm.hash), (void*)SM_HASH, HASH_SIZE);
  memcpy((void*)(report.sm.sm_pub_key), (void*)SM_PUB_KEY, PUBLIC_KEY_SIZE);
  memcpy((void*)(report.sm.signature), (void*)SM_SIGNATURE, SIGNATURE_SIZE);

  hash_enclave(enclave, (void*)(report.enclave.hash), nonce);
  sign_enclave((void*)(report.enclave.signature), (void*)(report.enclave.hash));
  report.enclave.nonce = nonce;

  //printHex((unsigned char*)(report.enclave.signature), 64);

  copy_to_host((void*)report_ptr, (void*)(&report), sizeof(struct report_t));

  acquire_enclave_metadata_lock();
  enclave->state = old_state;
  release_enclave_metadata_lock();
  return 0;
}

uintptr_t attest_shadow_enclave(uintptr_t eid, uintptr_t report_ptr, uintptr_t nonce)
{
  struct shadow_enclave_t* shadow_enclave = NULL;
  int attestable = 1;
  struct report_t report;
  acquire_enclave_metadata_lock();
  shadow_enclave = __get_shadow_enclave(eid);
  release_enclave_metadata_lock();

  if(!attestable)
  {
    printm("M mode: attest_enclave: enclave%d is not attestable\r\n", eid);
    return -1UL;
  }
  update_hash_shadow_enclave(shadow_enclave, (char *)shadow_enclave->hash, nonce);
  memcpy((char *)(report.enclave.hash), (char *)shadow_enclave->hash, HASH_SIZE);
  memcpy((void*)(report.dev_pub_key), (void*)DEV_PUB_KEY, PUBLIC_KEY_SIZE);
  memcpy((void*)(report.sm.hash), (void*)SM_HASH, HASH_SIZE);
  memcpy((void*)(report.sm.sm_pub_key), (void*)SM_PUB_KEY, PUBLIC_KEY_SIZE);
  memcpy((void*)(report.sm.signature), (void*)SM_SIGNATURE, SIGNATURE_SIZE);
  sign_enclave((void*)(report.enclave.signature), (void*)(report.enclave.hash));
  report.enclave.nonce = nonce;

  copy_to_host((void*)report_ptr, (void*)(&report), sizeof(struct report_t));

  return 0;
}

uintptr_t run_enclave(uintptr_t* regs, unsigned int eid, uintptr_t mm_arg_addr, uintptr_t mm_arg_size)
{
  struct enclave_t* enclave;
  uintptr_t retval = 0;
  struct relay_page_entry_t* relay_page_entry;

  acquire_enclave_metadata_lock();

  enclave = __get_enclave(eid);
  if(!enclave || enclave->state != FRESH || enclave->type == SERVER_ENCLAVE)
  {
    printm("M mode: run_enclave: enclave%d can not be accessed!\r\n", eid);
    retval = -1UL;
    goto run_enclave_out;
  }

  /** We bind a host process (host_ptbr) during run_enclave, which will be checked during resume */
  enclave->host_ptbr = read_csr(satp);
  //if the mm_arg_size is zero but mm_arg_addr is not zero, it means the relay page is transfer from other enclave 
  unsigned long mmap_offset = 0;
  if(mm_arg_addr && !mm_arg_size)
  {
    if (check_enclave_name(enclave->enclave_name, eid) < 0)
    {
      printm("M mode：run enclave: check enclave name is failed\r\n");
      goto run_enclave_out;
    }
    int slab_index = 0, link_mem_index = 0;
    int kk = 0;
    while((relay_page_entry = __get_relay_page_by_name(enclave->enclave_name, &slab_index, &link_mem_index)) != NULL)
    {
      mmap((uintptr_t*)(enclave->root_page_table), &(enclave->free_pages), ENCLAVE_DEFAULT_MM_ARG_BASE + mmap_offset, relay_page_entry->addr, relay_page_entry->size);
      mmap_offset = mmap_offset + relay_page_entry->size;
      if (enclave->mm_arg_paddr[0] == 0)
      {
        enclave->mm_arg_paddr[kk] = relay_page_entry->addr;
        enclave->mm_arg_size[kk] = relay_page_entry->size;
      }
      else
      {
        // enclave->mm_arg_size = enclave->mm_arg_size + relay_page_entry->size;
        enclave->mm_arg_paddr[kk] = relay_page_entry->addr;
        enclave->mm_arg_size[kk] = relay_page_entry->size;
      }
      kk = kk + 1;
    }
    if ((relay_page_entry == NULL) && (enclave->mm_arg_paddr[0] == 0))
    {
      printm("M mode: run_enclave: get relay page by name is failed \r\n");
      goto run_enclave_out;
    }
  }
  else if(mm_arg_addr && mm_arg_size)
  {
    //check whether the enclave name is duplicated
    printm("M mode: run enclave with it's own relay page\r\n");
    if (check_enclave_name(enclave->enclave_name, eid) < 0)
    {
      printm("M mode：run enclave: check enclave name is failed\r\n");
      goto run_enclave_out;
    }
    if (__alloc_relay_page_entry(enclave->enclave_name, mm_arg_addr, mm_arg_size) ==NULL)
    {
      printm("M mode: run enclave: alloc relay page is failed \r\n");
      goto run_enclave_out;
    }
    //check the relay page is not mapping in other enclave, and unmap the relay page for host
    if(check_and_set_secure_memory(mm_arg_addr, mm_arg_size) != 0)
    {
      retval = -1UL;
      goto run_enclave_out;
    }
    enclave->mm_arg_paddr[0] = mm_arg_addr;
    enclave->mm_arg_size[0] = mm_arg_size;
    mmap_offset = mm_arg_size;
    mmap((uintptr_t*)(enclave->root_page_table), &(enclave->free_pages), ENCLAVE_DEFAULT_MM_ARG_BASE, mm_arg_addr, mm_arg_size);

  }
  //the relay page is transfered from another enclave

  if(swap_from_host_to_enclave(regs, enclave) < 0)
  {
    printm("M mode: run_enclave: enclave can not be run\r\n");
    retval = -1UL;
    goto run_enclave_out;
  }

  //set return address to enclave
  write_csr(mepc, (uintptr_t)(enclave->entry_point));

  //enable timer interrupt
  set_csr(mie, MIP_MTIP);

  //set default stack
  regs[2] = ENCLAVE_DEFAULT_STACK_BASE;

  //pass parameters
  if(enclave->shm_paddr)
    regs[10] = ENCLAVE_DEFAULT_SHM_BASE;
  else
    regs[10] = 0;
  retval = regs[10];
  regs[11] = enclave->shm_size;
  regs[12] = (eapp_args-1) % 5;
  if(enclave->mm_arg_paddr[0])
    regs[13] = ENCLAVE_DEFAULT_MM_ARG_BASE;
  else
    regs[13] = 0;
  regs[14] = mmap_offset;
  eapp_args = eapp_args+1;

  enclave->state = RUNNING;
  printm("M mode: run_enclave: run the enclave....\r\n");
run_enclave_out:
  release_enclave_metadata_lock();
  return retval;
}

uintptr_t run_shadow_enclave(uintptr_t* regs, unsigned int eid, struct shadow_enclave_run_param_t enclave_run_param, uintptr_t mm_arg_addr, uintptr_t mm_arg_size)
{
  struct enclave_t* enclave;
  uintptr_t retval = 0;
  acquire_enclave_metadata_lock();
  struct shadow_enclave_t* shadow_enclave;
  struct relay_page_entry_t* relay_page_entry;
  shadow_enclave = __get_shadow_enclave(eid);
  enclave = __alloc_enclave();
  if(!enclave)
  {
    printm("create enclave from shadow enclave is failed\r\n");
    retval = ENCLAVE_NO_MEM;
    goto run_enclave_out;
  }
  if(check_and_set_secure_memory(enclave_run_param.free_page, enclave_run_param.size) != 0)
  {
    retval = ENCLAVE_ERROR;
    goto run_enclave_out;
  }
  //TODO: map schrodinger mem when run shadow enclave
  enclave->free_pages = NULL;
  enclave->free_pages_num = 0;
  uintptr_t free_mem = enclave_run_param.free_page + enclave_run_param.size - 2*RISCV_PGSIZE;
  while(free_mem >= enclave_run_param.free_page + 2*RISCV_PGSIZE)
  {
    struct page_t *page = (struct page_t*)free_mem;
    page->paddr = free_mem;
    page->next = enclave->free_pages;
    enclave->free_pages = page;
    enclave->free_pages_num += 1;
    free_mem -= RISCV_PGSIZE;
  }
  int copy_page_table_ret;
  copy_page_table_ret = __copy_page_table((pte_t*) (shadow_enclave->root_page_table), &(enclave->free_pages), 2, (pte_t*)(enclave_run_param.free_page + RISCV_PGSIZE));
  if (copy_page_table_ret < 0)
  {
    printm("copy_page_table fail\r\n");
    retval = ENCLAVE_ERROR;
    goto run_enclave_out;
  }
  copy_page_table_ret =  map_empty_page((uintptr_t*)(enclave_run_param.free_page + RISCV_PGSIZE), &(enclave->free_pages), ENCLAVE_DEFAULT_STACK-ENCLAVE_DEFAULT_STACK_SIZE, ENCLAVE_DEFAULT_STACK_SIZE);
  if (copy_page_table_ret < 0)
  {
    printm("alloc stack for shadow enclave fail\r\n");
    retval = ENCLAVE_ERROR;
    goto run_enclave_out;
  }
  enclave->entry_point = shadow_enclave->entry_point;
  //FIXME:
  enclave->ocall_func_id = enclave_run_param.ecall_arg0;
  enclave->ocall_arg0 = enclave_run_param.ecall_arg1;
  enclave->ocall_arg1 = enclave_run_param.ecall_arg2;
  enclave->ocall_syscall_num = enclave_run_param.ecall_arg3;
  enclave->kbuffer = enclave_run_param.kbuffer;
  enclave->kbuffer_size = enclave_run_param.kbuffer_size;
  enclave->shm_paddr = enclave_run_param.shm_paddr;
  enclave->shm_size = enclave_run_param.shm_size; 
  enclave->host_ptbr = read_csr(satp);
  enclave->root_page_table = enclave_run_param.free_page + RISCV_PGSIZE;
  enclave->thread_context.encl_ptbr = ((enclave_run_param.free_page + RISCV_PGSIZE) >> RISCV_PGSHIFT) | SATP_MODE_CHOICE;
  enclave->type = NORMAL_ENCLAVE;
  enclave->state = FRESH;
  enclave->caller_eid = -1;
  enclave->top_caller_eid = -1;
  enclave->cur_callee_eid = -1;
  memcpy(enclave->enclave_name, enclave_run_param.name, NAME_LEN);

  //traverse vmas
  struct pm_area_struct* pma = (struct pm_area_struct*)(enclave_run_param.free_page);
  struct vm_area_struct* vma = (struct vm_area_struct*)(enclave_run_param.free_page + sizeof(struct pm_area_struct));
  //
  pma->paddr = enclave_run_param.free_page;
  pma->size = enclave_run_param.size;
  pma->free_mem = enclave_run_param.free_page + 2*RISCV_PGSIZE;
  pma->pm_next = NULL;
  enclave->pma_list = pma;
  traverse_vmas(enclave->root_page_table, vma);
  while(vma)
  {
      //printm("M mode: vma start %lx and vma end %lx\r\n", vma->va_start, vma->va_end);
    if(vma->va_start == ENCLAVE_DEFAULT_TEXT_BASE)
    {
      enclave->text_vma = vma;
    }
    if(vma->va_end == ENCLAVE_DEFAULT_STACK_BASE)
    {
      enclave->stack_vma = vma;
      enclave->_stack_top = enclave->stack_vma->va_start;
    }
    vma->pma = pma;
    vma = vma->vm_next;
  }
  if(enclave->text_vma)
    enclave->text_vma->vm_next = NULL;
  if(enclave->stack_vma)
    enclave->stack_vma->vm_next = NULL;
  enclave->_heap_top = ENCLAVE_DEFAULT_HEAP_BASE;
  enclave->heap_vma = NULL;
  enclave->mmap_vma = NULL;

  if(enclave_run_param.kbuffer_size < RISCV_PGSIZE || enclave_run_param.kbuffer & (RISCV_PGSIZE-1) || enclave_run_param.kbuffer_size & (RISCV_PGSIZE-1))
  {
    retval = ENCLAVE_ERROR;
    goto run_enclave_out;
  }
  mmap((uintptr_t*)(enclave->root_page_table), &(enclave->free_pages), ENCLAVE_DEFAULT_KBUFFER, enclave_run_param.kbuffer, enclave_run_param.kbuffer_size);

  //check shm
  if(enclave_run_param.shm_paddr && enclave_run_param.shm_size &&
      !(enclave_run_param.shm_paddr & (RISCV_PGSIZE-1)) && !(enclave_run_param.shm_size & (RISCV_PGSIZE-1)))
  {
    mmap((uintptr_t*)(enclave->root_page_table), &(enclave->free_pages), ENCLAVE_DEFAULT_SHM_BASE, enclave_run_param.shm_paddr, enclave_run_param.shm_size);
    enclave->shm_paddr = enclave_run_param.shm_paddr;
    enclave->shm_size = enclave_run_param.shm_size;
  }
  else
  {
    enclave->shm_paddr = 0;
    enclave->shm_size = 0;
  }

  copy_word_to_host((unsigned int*)enclave_run_param.eid_ptr, enclave->eid);

  //map the relay page
  unsigned long mmap_offset = 0;
  if(mm_arg_addr && !mm_arg_size)
  {
    if (check_enclave_name(enclave->enclave_name, enclave->eid) < 0)
    {
      printm("M mode：run shadow enclave: check enclave name is failed\r\n");
      goto run_enclave_out;
    }
    int slab_index = 0, link_mem_index = 0;
    int kk = 0;
    while((relay_page_entry = __get_relay_page_by_name(enclave->enclave_name, &slab_index, &link_mem_index)) != NULL)
    {
      mmap((uintptr_t*)(enclave->root_page_table), &(enclave->free_pages), ENCLAVE_DEFAULT_MM_ARG_BASE + mmap_offset, relay_page_entry->addr, relay_page_entry->size);
      mmap_offset = mmap_offset + relay_page_entry->size;
      if (enclave->mm_arg_paddr[0] == 0)
      {
        enclave->mm_arg_paddr[kk] = relay_page_entry->addr;
        enclave->mm_arg_size[kk] = relay_page_entry->size;
      }
      else
      {
        // enclave->mm_arg_size = enclave->mm_arg_size + relay_page_entry->size;
        enclave->mm_arg_paddr[kk] = relay_page_entry->addr;
        enclave->mm_arg_size[kk] = relay_page_entry->size;
      }
      kk = kk + 1;
    }
    if ((relay_page_entry == NULL) && (enclave->mm_arg_paddr[0] == 0))
    {
      printm("M mode: run_shadow_enclave: get relay page by name is failed \r\n");
      goto run_enclave_out;
    }
  }
  else if(mm_arg_addr && mm_arg_size)
  {
    //check whether the enclave name is duplicated
    printm("M mode: run shadow enclave with it's own relay page\r\n");
    if (check_enclave_name(enclave->enclave_name, enclave->eid) < 0)
    {
      printm("M mode：run shadow enclave: check enclave name is failed\r\n");
      goto run_enclave_out;
    }
    if (__alloc_relay_page_entry(enclave->enclave_name, mm_arg_addr, mm_arg_size) ==NULL)
    {
      printm("M mode: run shadow enclave: alloc relay page is failed \r\n");
      goto run_enclave_out;
    }
    //check the relay page is not mapping in other enclave, and unmap the relay page for host
    if(check_and_set_secure_memory(mm_arg_addr, mm_arg_size) != 0)
    {
      retval = -1UL;
      goto run_enclave_out;
    }
    enclave->mm_arg_paddr[0] = mm_arg_addr;
    enclave->mm_arg_size[0] = mm_arg_size;
    mmap_offset = mm_arg_size;
    mmap((uintptr_t*)(enclave->root_page_table), &(enclave->free_pages), ENCLAVE_DEFAULT_MM_ARG_BASE, mm_arg_addr, mm_arg_size);

  }
  //end map the relay page

  if(swap_from_host_to_enclave(regs, enclave) < 0)
  {
    printm("M mode: run_shadow_enclave: enclave can not be run\r\n");
    retval = -1UL;
    goto run_enclave_out;
  }

  //set return address to enclave
  write_csr(mepc, (uintptr_t)(enclave->entry_point));

  //enable timer interrupt
  set_csr(mie, MIP_MTIP);

  //set default stack
  regs[2] = ENCLAVE_DEFAULT_STACK;

  //pass parameters
  if(enclave->shm_paddr)
    regs[10] = ENCLAVE_DEFAULT_SHM_BASE;
  else
    regs[10] = 0;
  retval = regs[10];
  regs[11] = enclave->shm_size;
  regs[12] = (eapp_args) % 5;
  if(enclave->mm_arg_paddr[0])
    regs[13] = ENCLAVE_DEFAULT_MM_ARG_BASE;
  else
    regs[13] = 0;
  regs[14] = mmap_offset;
  eapp_args = eapp_args+1;

  enclave->state = RUNNING;
  printm("M mode: run shadow enclave...\r\n");
run_enclave_out:
  release_enclave_metadata_lock();
  return retval;
}

//host use this function to stop an executing enclave
uintptr_t stop_enclave(uintptr_t* regs, unsigned int eid)
{
  uintptr_t retval = 0, wait_ipi = 0, dest_hart = 0;
  struct enclave_t *enclave = NULL;
  if(check_in_enclave_world() == 0)
  {
    printm("M mode: stop_enclave: should not be called by enclave itself\r\n");
    return -1UL;
  }

  spinlock_lock(&ipi_mail_lock);
  acquire_enclave_metadata_lock();

  enclave = __get_enclave(eid);
  if(!enclave || enclave->state <= FRESH || enclave->state == ATTESTING || enclave->state == OCALLING || enclave->host_ptbr != read_csr(satp) || enclave->type == SERVER_ENCLAVE)
  {
    printm("M mode: stop_enclave: enclave%d can not be accessed!\r\n", eid);
    retval = -1UL;
    goto stop_enclave_out;
  }

  //printm("M mode: stop_enclave: now we stop enclave%d\r\n", eid);
  if(enclave->state != RUNNING)
  {
    //printm("enclave %d is not running, no need to send ipi\r\n", eid);
    enclave->state = STOPPED;
  }
  else
  {
    //cpus' state will be protected by enclave_metadata_lock
    for(int i = 0; i < MAX_HARTS; ++i)
    {
      if(cpus[i].in_enclave && cpus[i].eid == eid)
        dest_hart |= (1 << i);
    }
    ipi_mail.event = IPI_STOP_ENCLAVE;
    *((uintptr_t*)(ipi_mail.data)) = read_csr(satp);
    *((int*)(ipi_mail.data + sizeof(uintptr_t))) = eid;
    //printm("M mode: stop_enclave: send ipi to dest_hart0x%lx\r\n", dest_hart);
    send_ipi_mail(dest_hart, 0);
    wait_ipi = 1;
  }

stop_enclave_out:
  release_enclave_metadata_lock();

  //should wait after release enclave_metadata_lock to avoid deadlock
  if(wait_ipi)
    wait_pending_ipi(dest_hart);

  spinlock_unlock(&ipi_mail_lock);
  return retval;
}

//host use this function to wake a stopped enclave
uintptr_t wake_enclave(uintptr_t* regs, unsigned int eid)
{
  uintptr_t retval = 0;
  struct enclave_t* enclave = NULL;

  acquire_enclave_metadata_lock();

  enclave = __get_real_enclave(eid);
  if(!enclave || enclave->state != STOPPED || enclave->host_ptbr != read_csr(satp))
  {
    printm("M mode: wake_enclave: enclave%d can not be accessed!\r\n", eid);
    retval = -1UL;
    goto wake_enclave_out;
  }

  //printm("M mode: wake_enclave: wake enclave%d\r\n", eid);
  enclave->state = RUNNABLE;

wake_enclave_out:
  release_enclave_metadata_lock();
  return retval;
}

//host use this fucntion to re-enter enclave world
uintptr_t resume_enclave(uintptr_t* regs, unsigned int eid)
{
  uintptr_t retval = 0;
  struct enclave_t* enclave = NULL;

  acquire_enclave_metadata_lock();
  //printm("M mode: resume_enclave: enclave%d want to resume\r\n", eid);
  enclave = __get_real_enclave(eid);
  if(!enclave || enclave->state <= FRESH || enclave->host_ptbr != read_csr(satp))
  {
    printm("M mode: resume_enclave: enclave%d can not be accessed\r\n", eid);
    retval = -1UL;
    goto resume_enclave_out;
  }

  if(enclave->state == STOPPED)
  {
    //printm("M mode: resume_enclave: enclave%d is stopped\r\n", eid);
    retval = ENCLAVE_TIMER_IRQ;
    goto resume_enclave_out;
  }
  if(enclave->state != RUNNABLE)
  {
    printm("M mode: resume_enclave: enclave%d is not runnable\r\n", eid);
    retval = -1UL;
    goto resume_enclave_out;
  }

  if(swap_from_host_to_enclave(regs, enclave) < 0)
  {
    printm("M mode: resume_enclave: enclave can not be resume\r\n");
    retval = -1UL;
    goto resume_enclave_out;
  }
  enclave->state = RUNNING;
  //regs[10] will be set to retval when mcall_trap return, so we have to
  //set retval to be regs[10] here to succuessfully restore context
  //TODO: retval should be set to indicate success or fail when resume from ocall
  retval = regs[10];
resume_enclave_out:
  release_enclave_metadata_lock();
  return retval;
}

uintptr_t mmap_after_resume(struct enclave_t *enclave, uintptr_t paddr, uintptr_t size)
{
  uintptr_t retval = 0;
  //uintptr_t vaddr = ENCLAVE_DEFAULT_MMAP_BASE;
  uintptr_t vaddr = enclave->thread_context.prev_state.a1;
  if(!vaddr) vaddr = ENCLAVE_DEFAULT_MMAP_BASE - (size - RISCV_PGSIZE);
  if(check_and_set_secure_memory(paddr, size) < 0)
  {
    printm("M mode: mmap_after_resume: check_secure_memory(0x%lx, 0x%lx) failed\r\n", paddr, size);
    retval = -1UL;
    return retval;
  }
  struct pm_area_struct *pma = (struct pm_area_struct*)paddr;
  struct vm_area_struct *vma = (struct vm_area_struct*)(paddr + sizeof(struct pm_area_struct));
  pma->paddr = paddr;
  pma->size = size;
  pma->pm_next = NULL;
  //vma->va_start = vaddr - (size - RISCV_PGSIZE);
  //vma->va_end = vaddr;
  vma->va_start = vaddr;
  vma->va_end = vaddr + size - RISCV_PGSIZE;
  vma->vm_next = NULL;
  vma->pma = pma;
  if(insert_vma(&(enclave->mmap_vma), vma, ENCLAVE_DEFAULT_MMAP_BASE) < 0)
  {
    vma->va_end = enclave->mmap_vma->va_start;
    vma->va_start = vma->va_end - (size - RISCV_PGSIZE);
    vma->vm_next = enclave->mmap_vma;
    enclave->mmap_vma = vma;
  }
  insert_pma(&(enclave->pma_list), pma);
  mmap((uintptr_t*)(enclave->root_page_table), &(enclave->free_pages), vma->va_start, paddr+RISCV_PGSIZE, size-RISCV_PGSIZE);
  retval = vma->va_start;

  return retval;
}

uintptr_t sbrk_after_resume(struct enclave_t *enclave, uintptr_t paddr, uintptr_t size)
{
  uintptr_t retval = 0;
  intptr_t req_size = (intptr_t)(enclave->thread_context.prev_state.a1);
  if(req_size <= 0)
  {
    //printm("sbrk_after_resume:_heap_top:0x%lx\r\n", enclave->_heap_top);
    return enclave->_heap_top;
  }
  if(check_and_set_secure_memory(paddr, size) < 0)
  {
    retval = -1UL;
    return retval;
  }

  struct pm_area_struct *pma = (struct pm_area_struct*)paddr;
  struct vm_area_struct *vma = (struct vm_area_struct*)(paddr + sizeof(struct pm_area_struct));
  pma->paddr = paddr;
  pma->size = size;
  pma->pm_next = NULL;
  vma->va_start = enclave->_heap_top;
  vma->va_end = vma->va_start + size - RISCV_PGSIZE;
  vma->vm_next = NULL;
  vma->pma = pma;
  vma->vm_next = enclave->heap_vma;
  enclave->heap_vma = vma;
  enclave->_heap_top = vma->va_end;
  insert_pma(&(enclave->pma_list), pma);
  mmap((uintptr_t*)(enclave->root_page_table), &(enclave->free_pages), vma->va_start, paddr+RISCV_PGSIZE, size-RISCV_PGSIZE);
  retval = enclave->_heap_top;
  //printm("sbrk_after_resume:_heap_top:0x%lx\r\n", enclave->_heap_top);

  return retval;
}

//host use this fucntion to re-enter enclave world
uintptr_t resume_from_ocall(uintptr_t* regs, unsigned int eid)
{
  uintptr_t retval = 0;
  uintptr_t ocall_func_id = regs[12];
  struct enclave_t* enclave = NULL;

  acquire_enclave_metadata_lock();

  enclave = __get_real_enclave(eid);
  if(!enclave || enclave->state != OCALLING || enclave->host_ptbr != read_csr(satp))
  {
    retval = -1UL;
    goto out;
  }

  switch(ocall_func_id)
  {
    case OCALL_MMAP:
      retval = mmap_after_resume(enclave, regs[13], regs[14]);
      if(retval == -1UL)
        goto out;
      break;
    case OCALL_UNMAP:
      retval = 0;
      break;
    case OCALL_SYS_WRITE:
      retval = enclave->thread_context.prev_state.a0;
      break;
    case OCALL_SBRK:
      retval = sbrk_after_resume(enclave, regs[13], regs[14]);
      if(retval == -1UL)
        goto out;
      break;
    case OCALL_READ_SECT:
      retval = regs[13];
      break;
    case OCALL_WRITE_SECT:
      retval = regs[13];
      break;
    default:
      retval = 0;
      break;
  }

  if(swap_from_host_to_enclave(regs, enclave) < 0)
  {
    retval = -1UL;
    goto out;
  }
  enclave->state = RUNNING;

out:
  release_enclave_metadata_lock();
  return retval;
}

//host call this function to destroy an existing enclave
uintptr_t destroy_enclave(uintptr_t* regs, unsigned int eid)
{
  uintptr_t retval = 0;
  struct enclave_t *enclave = NULL;
  uintptr_t dest_hart = 0;
  int wait_ipi = 0;
  struct pm_area_struct* pma = NULL;
  int need_free_enclave_memory = 0;

  spinlock_lock(&ipi_mail_lock);
  acquire_enclave_metadata_lock();

  enclave = __get_enclave(eid);
  if(!enclave || enclave->state < FRESH || enclave->type == SERVER_ENCLAVE)
  {
    printm("M mode: destroy_enclave: enclave%d can not be accessed\r\n", eid);
    retval = -1UL;
    goto destroy_enclave_out;
  }

  if(enclave->state != RUNNING)
  {
    pma = enclave->pma_list;
    need_free_enclave_memory = 1;
    __free_enclave(eid);
  }
  else
  {
    //cpus' state will be protected by enclave_metadata_lock
    for(int i = 0; i < MAX_HARTS; ++i)
    {
      if(cpus[i].in_enclave && cpus[i].eid == eid)
        dest_hart |= (1 << i);
    }
    ipi_mail.event = IPI_DESTROY_ENCLAVE;
    *((uintptr_t*)(ipi_mail.data)) = read_csr(satp);
    *((int*)(ipi_mail.data + sizeof(uintptr_t))) = eid;
    send_ipi_mail(dest_hart, 0);
    wait_ipi = 1;
  }

destroy_enclave_out:
  release_enclave_metadata_lock();

  //should wait after release enclave_metadata_lock to avoid deadlock
  if(wait_ipi)
    wait_pending_ipi(dest_hart);

  spinlock_unlock(&ipi_mail_lock);

  if(need_free_enclave_memory)
  {
    free_enclave_memory(pma);
  }

  return retval;
}


/**************************************************************/
/*                   called by enclave                        */
/**************************************************************/
uintptr_t exit_enclave(uintptr_t* regs, unsigned long enclave_retval)
{
  struct enclave_t *enclave = NULL;
  int eid = 0;
  uintptr_t ret = 0;
  struct pm_area_struct *pma = NULL;
  int need_free_enclave_memory = 0;
  printm("M mode: exit_enclave: begin exit enclave\n");
  if(check_in_enclave_world() < 0)
  {
    printm("M mode: exit_enclave: cpu is not in enclave world now\r\n");
    return -1UL;
  }

  acquire_enclave_metadata_lock();

  eid = get_curr_enclave_id();
  enclave = __get_enclave(eid);
  if(!enclave || check_enclave_authentication(enclave) != 0 || enclave->type == SERVER_ENCLAVE)
  {
    printm("M mode: exit_enclave: enclave%d can not be accessed!\r\n", eid);
    ret = -1UL;
    goto exit_enclave_out;
  }
  swap_from_enclave_to_host(regs, enclave);

  pma = enclave->pma_list;
  need_free_enclave_memory = 1;
  unsigned long mm_arg_paddr[RELAY_PAGE_NUM];
  unsigned long mm_arg_size[RELAY_PAGE_NUM];
  for(int kk = 0; kk < RELAY_PAGE_NUM; kk++)
  {
    mm_arg_paddr[kk] = enclave->mm_arg_paddr[kk];
    mm_arg_size[kk] = enclave->mm_arg_size[kk];
  }
  __free_enclave(eid);

exit_enclave_out:

  if(need_free_enclave_memory)
  {
    free_enclave_memory(pma);
    // if(mm_arg_paddr && mm_arg_size)
    // {
    //   //remap the relay page to the host
    //   printm("M mode: exit enclave: remap the relay page into host\r\n");
    //   __free_secure_memory(mm_arg_paddr, mm_arg_size);
    //   __free_relay_page_entry(mm_arg_paddr, mm_arg_size);
    // }
    for(int kk = 0; kk < RELAY_PAGE_NUM; kk++)
    {
      if (mm_arg_paddr[kk])
      {
        __free_secure_memory(mm_arg_paddr[kk], mm_arg_size[kk]);
        __free_relay_page_entry(mm_arg_paddr[kk], mm_arg_size[kk]);
      }
    }
  }
  release_enclave_metadata_lock();
  printm("M mode: exit_enclave: end exit enclave\r\n");
  return ret;
}

uintptr_t enclave_mmap(uintptr_t* regs, uintptr_t vaddr, uintptr_t size)
{
  uintptr_t ret = 0;
  int eid = get_curr_enclave_id();
  struct enclave_t* enclave = NULL;
  if(check_in_enclave_world() < 0)
    return -1;
  if(vaddr)
  {
    if(vaddr & (RISCV_PGSIZE-1) || size < RISCV_PGSIZE || size & (RISCV_PGSIZE-1))
      return -1;
  }

  acquire_enclave_metadata_lock();

  enclave = __get_enclave(eid);
  if(!enclave || check_enclave_authentication(enclave)!=0 || enclave->state != RUNNING)
  {
    ret = -1UL;
    goto out;
  }

  copy_dword_to_host((uintptr_t*)enclave->ocall_func_id, OCALL_MMAP);
  //copy_dword_to_host((uintptr_t*)enclave->ocall_arg0, vaddr);
  copy_dword_to_host((uintptr_t*)enclave->ocall_arg1, size + RISCV_PGSIZE);

  swap_from_enclave_to_host(regs, enclave);
  enclave->state = OCALLING;
  ret = ENCLAVE_OCALL;

out:
  release_enclave_metadata_lock();
  return ret;
}

uintptr_t enclave_unmap(uintptr_t* regs, uintptr_t vaddr, uintptr_t size)
{
  //printm("enclave_unmap: vaddr:0x%lx, size:%d\r\n", vaddr, size);
  uintptr_t ret = 0;
  int eid = get_curr_enclave_id();
  struct enclave_t* enclave = NULL;
  struct vm_area_struct *vma = NULL;
  struct pm_area_struct *pma = NULL;
  int need_free_secure_memory = 0;
  if(check_in_enclave_world() < 0)
    return -1;

  acquire_enclave_metadata_lock();

  enclave = __get_enclave(eid);
  if(!enclave || check_enclave_authentication(enclave)!=0 || enclave->state != RUNNING)
  {
    ret = -1UL;
    goto out;
  }

  vma = find_vma(enclave->mmap_vma, vaddr, size);
  if(!vma)
  {
    ret = -1UL;
    goto out;
  }
  pma = vma->pma;
  delete_vma(&(enclave->mmap_vma), vma);
  delete_pma(&(enclave->pma_list), pma);
  vma->vm_next = NULL;
  pma->pm_next = NULL;
  unmap((uintptr_t*)(enclave->root_page_table), vma->va_start, vma->va_end - vma->va_start);
  need_free_secure_memory = 1;

  copy_dword_to_host((uintptr_t*)enclave->ocall_func_id, OCALL_UNMAP);
  copy_dword_to_host((uintptr_t*)enclave->ocall_arg0, pma->paddr);
  copy_dword_to_host((uintptr_t*)enclave->ocall_arg1, pma->size);
  //printm("enclave_unmap: paddr:0x%lx, size:%d\r\n", pma->paddr, pma->size);

  swap_from_enclave_to_host(regs, enclave);
  enclave->state = OCALLING;
  ret = ENCLAVE_OCALL;

out:
  release_enclave_metadata_lock();
  if(need_free_secure_memory)
  {
    free_enclave_memory(pma);
  }
  return ret;
}

uintptr_t enclave_sbrk(uintptr_t* regs, intptr_t size)
{
  uintptr_t ret = 0;
  uintptr_t abs_size = 0;
  int eid = get_curr_enclave_id();
  struct enclave_t* enclave = NULL;
  struct pm_area_struct *pma = NULL;
  struct vm_area_struct *vma = NULL;
  if(check_in_enclave_world() < 0)
    return -1;
  if(size < 0)
  {
    abs_size = 0 - size;
    //printm("enclave_sbrk:size:-0x%lx\r\n", abs_size);
  }
  else
  {
    abs_size = size;
    //printm("enclave_sbrk:size:0x%lx\r\n", abs_size);
  }
  if(abs_size & (RISCV_PGSIZE-1))
    return -1;

  acquire_enclave_metadata_lock();

  enclave = __get_enclave(eid);
  if(!enclave || check_enclave_authentication(enclave)!=0 || enclave->state != RUNNING)
  {
    ret = -1UL;
    goto out;
  }

  if(size == 0)
  {
    ret = enclave->_heap_top;
    goto out;
  }
  if(size < 0)
  {
    uintptr_t dest_va = enclave->_heap_top - abs_size;
    vma = enclave->heap_vma;
    while(vma && vma->va_start >= dest_va)
    {
      struct pm_area_struct *cur_pma = vma->pma;
      delete_pma(&(enclave->pma_list), cur_pma);
      cur_pma->pm_next = pma;
      pma = cur_pma;
      unmap((uintptr_t*)(enclave->root_page_table), vma->va_start, vma->va_end - vma->va_start);
      enclave->heap_vma = vma->vm_next;
      vma = vma->vm_next;
    }
    if(enclave->heap_vma)
      enclave->_heap_top = enclave->heap_vma->va_end;
    else
      enclave->_heap_top = ENCLAVE_DEFAULT_HEAP_BASE;
  }
  copy_dword_to_host((uintptr_t*)enclave->ocall_func_id, OCALL_SBRK);
  copy_dword_to_host((uintptr_t*)enclave->ocall_arg0, (uintptr_t)pma);
  if(size > 0)
    copy_dword_to_host((uintptr_t*)enclave->ocall_arg1, size + RISCV_PGSIZE);
  else
    copy_dword_to_host((uintptr_t*)enclave->ocall_arg1, size);
  //printm("M mode: enclave_sbrk: pma:0x%lx\r\n", pma);
  //printm("M mode: enclave_sbrk: size:0x%lx\r\n", size);

  swap_from_enclave_to_host(regs, enclave);
  enclave->state = OCALLING;
  ret = ENCLAVE_OCALL;

out:
  release_enclave_metadata_lock();
  if(pma)
  {
    free_enclave_memory(pma);
  }
  return ret;
}

uintptr_t enclave_sys_write(uintptr_t* regs)
{
  uintptr_t ret = 0;
  int eid = get_curr_enclave_id();
  struct enclave_t* enclave = NULL;
  if(check_in_enclave_world() < 0)
    return -1;

  acquire_enclave_metadata_lock();

  enclave = __get_enclave(eid);
  if(!enclave || check_enclave_authentication(enclave)!=0 || enclave->state != RUNNING)
  {
    ret = -1UL;
    goto out;
  }
  copy_dword_to_host((uintptr_t*)enclave->ocall_func_id, OCALL_SYS_WRITE);
  //copy_dword_to_host((uintptr_t*)enclave->ocall_arg0, vaddr);
  //copy_dword_to_host((uintptr_t*)enclave->ocall_arg1, size + RISCV_PGSIZE);

  swap_from_enclave_to_host(regs, enclave);
  enclave->state = OCALLING;
  ret = ENCLAVE_OCALL;
out:
  release_enclave_metadata_lock();
  return ret;
}

//uintptr_t call_enclave(uintptr_t* regs, unsigned int callee_eid, uintptr_t arg0, uintptr_t vaddr, uintptr_t size, uintptr_t retaddr)
uintptr_t call_enclave(uintptr_t* regs, unsigned int callee_eid, uintptr_t arg)
{
  struct enclave_t* top_caller_enclave = NULL;
  struct enclave_t* caller_enclave = NULL;
  struct enclave_t* callee_enclave = NULL;
  struct vm_area_struct* vma = NULL;
  struct pm_area_struct* pma = NULL;
  uintptr_t retval = 0;
  int caller_eid = get_curr_enclave_id();
  if(check_in_enclave_world() < 0)
    return -1;

  acquire_enclave_metadata_lock();

  caller_enclave = __get_enclave(caller_eid);
  if(!caller_enclave || caller_enclave->state != RUNNING || check_enclave_authentication(caller_enclave) != 0)
  {
    printm("M mode: call_enclave: enclave%d can not execute call_enclave!\r\n", caller_eid);
    retval = -1UL;
    goto out;
  }
  if(caller_enclave->caller_eid != -1)
    top_caller_enclave = __get_enclave(caller_enclave->top_caller_eid);
  else
    top_caller_enclave = caller_enclave;
  if(!top_caller_enclave || top_caller_enclave->state != RUNNING)
  {
    printm("M mode: call_enclave: enclave%d can not execute call_enclave!\r\n", caller_eid);
    retval = -1UL;
    goto out;
  }
  callee_enclave = __get_enclave(callee_eid);
  if(!callee_enclave || callee_enclave->type != SERVER_ENCLAVE || callee_enclave->caller_eid != -1 || callee_enclave->state != RUNNABLE)
  {
    printm("M mode: call_enclave: enclave%d can not be accessed!\r\n", callee_eid);
    retval = -1UL;
    goto out;
  }

  struct call_enclave_arg_t call_arg;
  struct call_enclave_arg_t* call_arg0 = va_to_pa((uintptr_t*)(caller_enclave->root_page_table), (void*)arg);
  if(!call_arg0)
  {
    retval = -1UL;
    goto out;
  }
  copy_from_host(&call_arg, call_arg0, sizeof(struct call_enclave_arg_t));
  if(call_arg.req_vaddr != 0)
  {
    if(call_arg.req_vaddr & (RISCV_PGSIZE-1) || call_arg.req_size < RISCV_PGSIZE || call_arg.req_size & (RISCV_PGSIZE-1))
    {
      retval = -1UL;
      goto out;
    }
    vma = find_vma(caller_enclave->mmap_vma, call_arg.req_vaddr, call_arg.req_size);
    if(!vma)
    {
      retval = -1UL;
      goto out;
    }
    pma = vma->pma;
    delete_vma(&(caller_enclave->mmap_vma), vma);
    delete_pma(&(caller_enclave->pma_list), pma);
    vma->vm_next = NULL;
    pma->pm_next = NULL;
    unmap((uintptr_t*)(caller_enclave->root_page_table), vma->va_start, vma->va_end - vma->va_start);
    if(insert_vma(&(callee_enclave->mmap_vma), vma, ENCLAVE_DEFAULT_MMAP_BASE) < 0)
    {
      vma->va_end = callee_enclave->mmap_vma->va_start;
      vma->va_start = vma->va_end - (pma->size - RISCV_PGSIZE);
      vma->vm_next = callee_enclave->mmap_vma;
      callee_enclave->mmap_vma = vma;
    }
    insert_pma(&(callee_enclave->pma_list), pma);
    mmap((uintptr_t*)(callee_enclave->root_page_table), &(callee_enclave->free_pages), vma->va_start, pma->paddr + RISCV_PGSIZE, pma->size - RISCV_PGSIZE);
  }
  if(__enclave_call(regs, top_caller_enclave, caller_enclave, callee_enclave) < 0)
  {
    printm("M mode: call_enclave: enclave can not be run\r\n");
    retval = -1UL;
    goto out;
  }

  //set return address to enclave
  write_csr(mepc, (uintptr_t)(callee_enclave->entry_point));

  //enable timer interrupt
  set_csr(mie, MIP_MTIP);

  //set default stack
  regs[2] = ENCLAVE_DEFAULT_STACK_BASE;

  //map kbuffer
  mmap((uintptr_t*)(callee_enclave->root_page_table), &(callee_enclave->free_pages), ENCLAVE_DEFAULT_KBUFFER, top_caller_enclave->kbuffer, top_caller_enclave->kbuffer_size);

  //pass parameters
  regs[10] = call_arg.req_arg;
  if(call_arg.req_vaddr)
    regs[11] = vma->va_start;
  else
    regs[11] = 0;
  regs[12] = call_arg.req_size;
  if(callee_enclave->shm_paddr){
    regs[13] = ENCLAVE_DEFAULT_SHM_BASE;
  }
  else{
    regs[13] = 0;
  }
  regs[14] = callee_enclave->shm_size;
  retval = call_arg.req_arg;

  callee_enclave->state = RUNNING;
  //printm("calll_enclave: now we are entering server:%d, encl_ptbr:0x%lx\r\n", callee_enclave->eid, read_csr(satp));
out:
  release_enclave_metadata_lock();
  return retval;
}

//uintptr_t enclave_return(uintptr_t* regs, uintptr_t enclave_retval, uintptr_t vaddr, uintptr_t size)
uintptr_t enclave_return(uintptr_t* regs, uintptr_t arg)
{
  // printm("M mode: enclave_return: retval of enclave is %lx\r\n", enclave_retval);

  struct enclave_t *enclave = NULL;
  struct enclave_t *caller_enclave = NULL;
  struct enclave_t *top_caller_enclave = NULL;
  int eid = 0;
  uintptr_t ret = 0;
  struct vm_area_struct* vma = NULL;
  struct pm_area_struct *pma = NULL;
  uintptr_t ret_vaddr = 0;
  uintptr_t ret_size = 0;

  if(check_in_enclave_world() < 0)
  {
    printm("M mode: enclave_return: cpu is not in enclave world now\r\n");
    return -1UL;
  }

  acquire_enclave_metadata_lock();

  eid = get_curr_enclave_id();
  enclave = __get_enclave(eid);
  if(!enclave || check_enclave_authentication(enclave) != 0 || enclave->type != SERVER_ENCLAVE)
  {
    printm("M mode: enclave_return: enclave%d can not return!\r\n", eid);
    ret = -1UL;
    goto out;
  }
  struct call_enclave_arg_t ret_arg;
  struct call_enclave_arg_t* ret_arg0 = va_to_pa((uintptr_t*)(enclave->root_page_table), (void*)arg);
  if(!ret_arg0)
  {
    ret = -1UL;
    goto out;
  }
  copy_from_host(&ret_arg, ret_arg0, sizeof(struct call_enclave_arg_t));

  caller_enclave = __get_enclave(enclave->caller_eid);
  top_caller_enclave = __get_enclave(enclave->top_caller_eid);
  __enclave_return(regs, enclave, caller_enclave, top_caller_enclave);
  unmap((uintptr_t*)(enclave->root_page_table), ENCLAVE_DEFAULT_KBUFFER, top_caller_enclave->kbuffer_size);

  //restore caller_enclave's req arg
  //there is no need to check call_arg's validity again as it is already checked when executing call_enclave()
  struct call_enclave_arg_t *call_arg = va_to_pa((uintptr_t*)(caller_enclave->root_page_table), (void*)(regs[11]));

  //restore req_vaddr
restore_req_addr:
  if(!call_arg->req_vaddr || !ret_arg.req_vaddr || ret_arg.req_vaddr & (RISCV_PGSIZE-1)
      || ret_arg.req_size < call_arg->req_size || ret_arg.req_size & (RISCV_PGSIZE-1))
  {
    call_arg->req_vaddr = 0;
    //call_arg->req_size = 0;
    goto restore_resp_addr;
  }
  vma = find_vma(enclave->mmap_vma, ret_arg.req_vaddr, ret_arg.req_size);
  if(!vma)
  {
    //enclave return even when the shared mem return failed
    call_arg->req_vaddr = 0;
    //call_arg->req_size = 0;
    goto restore_resp_addr;
  }
  pma = vma->pma;
  delete_vma(&(enclave->mmap_vma), vma);
  delete_pma(&(enclave->pma_list), pma);
  unmap((uintptr_t*)(enclave->root_page_table), vma->va_start, vma->va_end - vma->va_start);
  vma->va_start = call_arg->req_vaddr;
  vma->va_end = vma->va_start + pma->size - RISCV_PGSIZE;
  vma->vm_next = NULL;
  pma->pm_next = NULL;
  if(insert_vma(&(caller_enclave->mmap_vma), vma, ENCLAVE_DEFAULT_MMAP_BASE) < 0)
  {
    vma->va_end = caller_enclave->mmap_vma->va_start;
    vma->va_start = vma->va_end - (pma->size - RISCV_PGSIZE);
    vma->vm_next = caller_enclave->mmap_vma;
    caller_enclave->mmap_vma = vma;
  }
  insert_pma(&(caller_enclave->pma_list), pma);
  mmap((uintptr_t*)(caller_enclave->root_page_table), &(caller_enclave->free_pages), vma->va_start, pma->paddr + RISCV_PGSIZE, pma->size - RISCV_PGSIZE);
  call_arg->req_vaddr = vma->va_start;
  //call_arg->req_size = vma->va_end - vma->va_start;

restore_resp_addr:
  if(!ret_arg.resp_vaddr || ret_arg.resp_vaddr & (RISCV_PGSIZE-1)
      || ret_arg.resp_size < RISCV_PGSIZE || ret_arg.resp_size & (RISCV_PGSIZE-1))
  {
    call_arg->resp_vaddr = 0;
    call_arg->resp_size = 0;
    goto restore_return_val;
  }
  vma = find_vma(enclave->mmap_vma, ret_arg.resp_vaddr, ret_arg.resp_size);
  if(!vma)
  {
    //enclave return even when the shared mem return failed
    call_arg->resp_vaddr = 0;
    call_arg->resp_size = 0;
    goto restore_return_val;
  }
  pma = vma->pma;
  delete_vma(&(enclave->mmap_vma), vma);
  delete_pma(&(enclave->pma_list), pma);
  unmap((uintptr_t*)(enclave->root_page_table), vma->va_start, vma->va_end - vma->va_start);
  vma->vm_next = NULL;
  pma->pm_next = NULL;
  if(caller_enclave->mmap_vma)
    vma->va_end = caller_enclave->mmap_vma->va_start;
  else
    vma->va_end = ENCLAVE_DEFAULT_MMAP_BASE;
  vma->va_start = vma->va_end - (pma->size - RISCV_PGSIZE);
  vma->vm_next = caller_enclave->mmap_vma;
  caller_enclave->mmap_vma = vma;
  insert_pma(&(caller_enclave->pma_list), pma);
  mmap((uintptr_t*)(caller_enclave->root_page_table), &(caller_enclave->free_pages), vma->va_start, pma->paddr + RISCV_PGSIZE, pma->size - RISCV_PGSIZE);
  call_arg->resp_vaddr = vma->va_start;
  call_arg->resp_size = ret_arg.resp_size;

  //pass return value of server
restore_return_val:
  call_arg->resp_val = ret_arg.resp_val;
  enclave->state = RUNNABLE;
  ret = 0;
out:
  release_enclave_metadata_lock();

  return ret;
}

/**************************************************************/
/*                   called when irq                          */
/**************************************************************/
uintptr_t do_timer_irq(uintptr_t *regs, uintptr_t mcause, uintptr_t mepc)
{
  uintptr_t retval = 0;
  unsigned int eid = get_curr_enclave_id();
  struct enclave_t *enclave = NULL;

  acquire_enclave_metadata_lock();

  enclave = __get_enclave(eid);
  if(!enclave || enclave->state != RUNNING)
  {
    printm("M mode: something is wrong with enclave%d, state: %d\r\n", eid, enclave->state);
    retval = -1UL;
    goto timer_irq_out;
  }
   swap_from_enclave_to_host(regs, enclave);
   enclave->state = RUNNABLE;

timer_irq_out:
  release_enclave_metadata_lock();

  clear_csr(mie, MIP_MTIP);
  set_csr(mip, MIP_STIP);
  regs[10] = ENCLAVE_TIMER_IRQ;
  return retval;
}

uintptr_t do_yield(uintptr_t *regs, uintptr_t mcause, uintptr_t mepc)
{
  uintptr_t retval = 0;
  unsigned int eid = get_curr_enclave_id();
  struct enclave_t *enclave = NULL;

  acquire_enclave_metadata_lock();

  enclave = __get_enclave(eid);
  if(!enclave || enclave->state != RUNNING)
  {
    printm("M mode: something is wrong with enclave%d\r\n", eid);
    retval = -1UL;
    goto timer_irq_out;
  }

  swap_from_enclave_to_host(regs, enclave);
  enclave->state = RUNNABLE;

timer_irq_out:
  release_enclave_metadata_lock();
  // clear_csr(mie, MIP_MTIP);
  // set_csr(mip, MIP_STIP);
  retval = ENCLAVE_YIELD;
  return retval;
}

//called when other hart send IPI with IPI_STOP_ENCLAVE call to current hart
uintptr_t ipi_stop_enclave(uintptr_t *regs, uintptr_t host_ptbr, int eid)
{
  uintptr_t ret = 0;
  struct enclave_t* enclave = NULL;

  acquire_enclave_metadata_lock();
  //printm("M mode: ipi_stop_enclave %d\r\n", eid);

  enclave = __get_enclave(eid);

  //enclave may have exited or even assigned to other host
  //after ipi sender release the enclave_metadata_lock
  if(!enclave || enclave->state <= FRESH || enclave->host_ptbr != host_ptbr || enclave->state == OCALLING)
  {
    ret = -1UL;
    goto ipi_stop_enclave_out;
  }

  //this situation should never happen
  if(enclave->state == RUNNING
      && (check_in_enclave_world() < 0 || cpus[read_csr(mhartid)].eid != eid))
  {
    printm("M mode: ipi_stop_enclave: this situation should never happen!\r\n");
    ret = -1;
    goto ipi_stop_enclave_out;
  }

  if(enclave->state == RUNNING)
  {
    swap_from_enclave_to_host(regs, enclave);
    regs[10] = ENCLAVE_TIMER_IRQ;
  }
  enclave->state = STOPPED;

ipi_stop_enclave_out:
  release_enclave_metadata_lock();
  return ret;
}

//called when other hart send IPI with IPI_DESTROY_ENCLAVE call to current hart
uintptr_t ipi_destroy_enclave(uintptr_t *regs, uintptr_t host_ptbr, int eid)
{
  uintptr_t ret = 0;
  struct enclave_t* enclave = NULL;
  struct pm_area_struct* pma = NULL;
  int need_free_enclave_memory = 0;

  acquire_enclave_metadata_lock();
  //printm("M mode: ipi_destroy_enclave %d\r\n", eid);

  enclave = __get_enclave(eid);

  //enclave may have exited or even assigned to other host
  //after ipi sender release the enclave_metadata_lock
  if(!enclave || enclave->state < FRESH)
  {
    ret = -1;
    goto ipi_stop_enclave_out;
  }

  //this situation should never happen
  if(enclave->state == RUNNING
      && (check_in_enclave_world() < 0 || cpus[read_csr(mhartid)].eid != eid))
  {
    printm("M mode: ipi_stop_enclave: this situation should never happen!\r\n");
    ret = -1;
    goto ipi_stop_enclave_out;
  }

  if(enclave->state == RUNNING)
  {
    swap_from_enclave_to_host(regs, enclave);
    //regs[10] = ENCLAVE_DESTROYED;
    regs[10] = 0;
  }
  pma = enclave->pma_list;
  need_free_enclave_memory = 1;
  __free_enclave(eid);

ipi_stop_enclave_out:
  release_enclave_metadata_lock();

  if(need_free_enclave_memory)
    free_enclave_memory(pma);

  return ret;
}

uintptr_t enclave_read_sec(uintptr_t *regs, uintptr_t sec){
  uintptr_t ret = 0;
  int eid = get_curr_enclave_id();
  struct enclave_t *enclave = NULL;
  if(check_in_enclave_world() < 0){
    return -1;
  }
  acquire_enclave_metadata_lock();
  enclave = __get_enclave(eid);
  if(!enclave || check_enclave_authentication(enclave) != 0 || enclave->state != RUNNING){
    ret = -1;
    goto out;
  }
  copy_dword_to_host((uintptr_t*)enclave->ocall_func_id, OCALL_READ_SECT);
  copy_dword_to_host((uintptr_t*)enclave->ocall_arg0, sec);
  swap_from_enclave_to_host(regs, enclave);
  enclave->state = OCALLING;
  ret = ENCLAVE_OCALL;

out:
  release_enclave_metadata_lock();
  return ret;

}

uintptr_t enclave_write_sec(uintptr_t *regs, uintptr_t sec){
  uintptr_t ret = 0;
  int eid = get_curr_enclave_id();
  struct enclave_t *enclave = NULL;
  if(check_in_enclave_world() < 0){
    return -1;
  }
  acquire_enclave_metadata_lock();
  enclave = __get_enclave(eid);
  if(!enclave || check_enclave_authentication(enclave) != 0|| enclave->state != RUNNING){
    ret = -1;
    goto out;
  }
  copy_dword_to_host((uintptr_t*)enclave->ocall_func_id, OCALL_WRITE_SECT);
  copy_dword_to_host((uintptr_t*)enclave->ocall_arg0,sec);
  swap_from_enclave_to_host(regs, enclave);
  enclave->state = OCALLING;
  ret = ENCLAVE_OCALL;

out:
  release_enclave_metadata_lock();
  return ret;
}
