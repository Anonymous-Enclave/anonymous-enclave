#include <string.h>
#include <stdint.h>

#include "random.h"

int vli_get_random(u8 *data, u32 len)
{
  int ret = 0;

  //TODO: optimize it with real entropy machine
  u8 hash[32] = "1f9dac1a61a2e0af09a5147bb92d1d4e59a320c2fd6de7f551e319084c64b700";
  int i=0;
  for(i=0; i < 32; ++i)
  {
    data[i] = hash[i];
  }

  return ret;
}
