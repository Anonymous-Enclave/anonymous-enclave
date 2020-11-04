#include "eapp.h"
#include "print.h"
#include <stdlib.h>

int prime_loop(int num)
{
  unsigned long count = 0;
  int i;
  for(i = 2; i < num; i++)
  {
    if (num % i ==0)
      count++;
  }
  return count;
}

int prime(unsigned long * args)
{
  eapp_print("%s is running\n", "Prime");
  eapp_print("relay page address %lx, relay page size%lx \n", args[13], args[14]);
  char enclave_name[16];
  strcpy(enclave_name, "test");
  eapp_print("prime 1\n");
  struct call_enclave_arg_t call_arg;
  call_arg.req_vaddr = args[13];
  call_arg.req_size = args[14];
  eapp_print("prime 2\n");
  asyn_enclave_call(enclave_name,  &call_arg);
  eapp_print("prime 3\n");
  unsigned long ret;
  ret = prime_loop(10);
  eapp_print("prime 4\n");
  EAPP_RETURN(0);
}

int EAPP_ENTRY main(){
  unsigned long * args;
  EAPP_RESERVE_REG;
  prime(args);
}