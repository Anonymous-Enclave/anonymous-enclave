#include "eapp.h"
#include <stdlib.h>
#include <stdio.h>

int ocall(unsigned long * args)
{
  EAPP_OCALL(1);
  EAPP_RETURN(1);
}

int EAPP_ENTRY main(){
  unsigned long * args;
  EAPP_RESERVE_REG;
  ocall(args);
}
