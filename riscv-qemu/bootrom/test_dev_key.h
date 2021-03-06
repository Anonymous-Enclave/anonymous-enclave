/* These are known device TESTING keys, use them for testing on platforms/qemu */

#warning Using TEST device root key. No integrity guarantee.
static const unsigned char _penglai_dev_secret_key[] = {
  0x4d,  0x48,  0x63,  0x43,  0x41,  0x51,  0x45,  0x45,  0x49,  0x4c,  0x79,  0x4c,  
  0x6e,  0x56,  0x55,  0x67,  0x66,  0x49,  0x54,  0x57,  0x63,  0x41,  0x68,  0x55,  
  0x6e,  0x72,  0x7a,  0x47,  0x51,  0x56,  0x4c,  0x4d
};
static const size_t _penglai_dev_secret_key_len = 32;

static const unsigned char _penglai_dev_public_key[] = {
  0x4d,  0x46,  0x6b,  0x77,  0x45,  0x77,  0x59,  0x48,  0x4b,  0x6f,  0x5a,  0x49,  
  0x7a,  0x6a,  0x30,  0x43,  0x41,  0x51,  0x59,  0x49,  0x4b,  0x6f,  0x45,  0x63,  
  0x7a,  0x31,  0x55,  0x42,  0x67,  0x69,  0x30,  0x44,  0x51,  0x67,  0x41,  0x45,  
  0x59,  0x66,  0x32,  0x7a,  0x31,  0x55,  0x2f,  0x66,  0x4e,  0x5a,  0x45,  0x54,  
  0x2f,  0x33,  0x74,  0x50,  0x55,  0x77,  0x35,  0x73,  0x45,  0x75,  0x34,  0x51,  
  0x6e,  0x43,  0x61,  0x46
};
static const size_t _penglai_dev_public_key_len = 64;
