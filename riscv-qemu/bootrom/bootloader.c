#include <stddef.h>
#include <string.h>
#include "gm/sm2.h"
#include "gm/sm3.h"
typedef unsigned char byte;

/* penglai header fields in DRAM */
#define DRAM_BASE 0x80000000
unsigned int penglai_sm_size = 0x1ff000;
extern byte penglai_dev_public_key[64];
extern byte penglai_dev_secret_key[32];
extern byte penglai_sm_public_key[64];
extern byte penglai_sm_secret_key[32];
extern byte penglai_sm_hash[32];
extern byte penglai_sm_signature[64];


void bootloader() {
  struct sm3_context ctx;

  /**
   * Measure SM.
   * You can just use this function
   * sm3((void*)DRAM_BASE, penglai_sm_size, penglai_sm_hash);
   * Or you can do it yourself.
   */
  sm3_init(&ctx);
  sm3_update(&ctx, (void*)DRAM_BASE, penglai_sm_size);
  sm3_final(&ctx, penglai_sm_hash);

  /**
   * TEST Device key.
   * The device keys are created by sm2 algorithm.
   */ 
  #include "use_test_keys.h"

  /**
   * TEST SM key.
   * The keypair is created by sm2 algorithm.
   */
  ecc_point pub;
  // sm2_make_keypair(penglai_sm_secret_key, &pub);
  // sm2_make_keypair(penglai_sm_secret_key, &pub);
  sm2_make_prikey(penglai_sm_secret_key);
  sm2_make_pubkey(penglai_sm_secret_key, &pub);
  
  memcpy(penglai_sm_public_key, &pub, 64);

  /**
   * Sign the SM
   */
  struct signature_t *signature = (struct signature_t*)penglai_sm_signature;
  sm2_sign((void*)(signature->r), (void*)(signature->s), (void*)penglai_sm_secret_key, (void*)penglai_sm_hash);
  //memcpy(penglai_sm_signature, &signature, 64);

  /**
   * Clean up.
   */
  //memset((void*)penglai_dev_secret_key, 0, sizeof(penglai_dev_secret_key));

  return;
}
