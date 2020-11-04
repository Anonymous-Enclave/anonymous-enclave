#include "eapp.h"
#include <stdlib.h>
#include <stdio.h>
#include "print.h"
#include <openssl/aes.h>

int prime()
{
    char userkey[AES_BLOCK_SIZE];
    unsigned char *date = malloc(AES_BLOCK_SIZE*3);
    unsigned char *encrypt = malloc(AES_BLOCK_SIZE*3 + 4);
    unsigned char *plain = malloc(AES_BLOCK_SIZE*3);
    AES_KEY key;

    memset((void*)userkey, 'k', AES_BLOCK_SIZE);
    memset((void*)date, 'p', AES_BLOCK_SIZE*3);
    memset((void*)encrypt, 0, AES_BLOCK_SIZE*6);
    memset((void*)plain, 0, AES_BLOCK_SIZE*3);

    /*设置加密key及密钥长度*/
    AES_set_encrypt_key(userkey, AES_BLOCK_SIZE*8, &key);

    int len = 0;
    /*循环加密，每次只能加密AES_BLOCK_SIZE长度的数据*/
while(len < AES_BLOCK_SIZE*3) {
        AES_encrypt(date+len, encrypt+len, &key);    
        len += AES_BLOCK_SIZE;
    }
    /*设置解密key及密钥长度*/    
    AES_set_decrypt_key(userkey, AES_BLOCK_SIZE*8, &key);

    len = 0;
    /*循环解密*/
while(len < AES_BLOCK_SIZE*3) {
        AES_decrypt(encrypt+len, plain+len, &key);    
        len += AES_BLOCK_SIZE;
    }
    /*解密后与原数据是否一致*/
    if(!memcmp(plain, date, AES_BLOCK_SIZE*3)){
        eapp_print("test success\n");    
    }else{
        eapp_print("test failed\n");    
    }

    eapp_print("encrypt: ");
    int i = 0;
    for(i = 0; i < AES_BLOCK_SIZE*3 + 4; i++){
        eapp_print("%.2x ", encrypt[i]);
        if((i+1) % 32 == 0){
            eapp_print("\n");    
        }
    }
    eapp_print("\n");    
    EAPP_RETURN(0);
}

int EAPP_ENTRY main(){
  unsigned long * args;
  EAPP_RESERVE_REG;
  prime(args);
}
