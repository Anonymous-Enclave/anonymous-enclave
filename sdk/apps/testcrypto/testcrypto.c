#include<wolfcrypt/aes.h>
#include<wolfcrypt/rsa.h>
#include<eapp.h>
#include<print.h>
#include<string.h>
#include<wolfcrypt/dh.h>
#include<wolfcrypt/sha256.h>
int test_crypt(){
    eapp_print("begin test aes\n");
    Aes enc;
    Aes dec;
    byte key[24];
    byte iv[16];
    for(int i = 0; i < 21; i++){
        key[i] = (byte)i;
    }
    for(int i = 0; i < 16;i++){
        iv[i] = (byte)i;
    }
    byte plain[32];
    byte cipher[32];
    for(int i = 0; i< 32; i++){
        plain[i] = (byte)i;
    }
    wc_AesSetKey(&enc,key,sizeof(key),iv, AES_ENCRYPTION);
    wc_AesCbcEncrypt(&enc, cipher, plain, sizeof(plain));
    wc_AesSetKey(&dec, key, sizeof(key), iv, AES_DECRYPTION);
    wc_AesCbcDecrypt(&dec, plain, cipher, sizeof(cipher));
    for(int i = 0; i < 32; i++){
        if(plain[i] != (byte)i){
            eapp_print("failed\n");
        }
    }
    eapp_print("test aes succeed\n");
    eapp_print("begin test rsa\n");
    RsaKey rsa_key; 
    // this key can be used as public key 
    // and private key both for test
    WC_RNG rng;
    int ret = 0;
    long e = 65537;
    byte msg[] = "test_rsa";
    byte rsa_cipher[256];
    wc_InitRsaKey(&rsa_key, NULL);
    wc_InitRng(&rng);
    ret = wc_MakeRsaKey(&rsa_key, 2048, e, &rng);
    if(ret != 0){
        eapp_print("make rsa private key failed, ret: %d\n",ret);
        return 0;
    }
    ret = wc_RsaPublicEncrypt(msg,sizeof(msg),rsa_cipher, 
                              sizeof(rsa_cipher), &rsa_key,&rng);
    if( ret < 0){
        eapp_print("rsa public encrypt failed\n");
        return 0;
    }
    ret = wc_RsaPrivateDecrypt(rsa_cipher,ret,msg,sizeof(msg),&rsa_key);
    if(ret < 0 || strcmp(msg, "test_rsa")!= 0){
        eapp_print("rsa decrypt failed\n");
    }
    eapp_print("test rsa succeed\n");
    eapp_print("begin test Diffie-Hellman\n");
    DhKey dh_key;
    byte dh_priv[256];
    byte dh_pub[256];
    WC_RNG dh_rng;
    word32 dh_privSz, dh_pubSz;
    wc_InitDhKey(&dh_key);
    wc_InitRng(&dh_rng);
    ret = wc_DhGenerateKeyPair(&dh_key, &rng,dh_priv,&dh_privSz,dh_pub,&dh_pubSz);
    if(ret != 0){
        eapp_print("DH generate key pair failed, ret: %d\n",ret);
        return 0;
    }
    eapp_print("begin test sha-256\n");
    byte shaSum[SHA256_DIGEST_SIZE];
    byte buffer[1024];
    for(int i = 0; i < 1024; i++){
        buffer[i] = i % 256;
    }
    Sha256 sha;
    ret = wc_InitSha256(&sha);
    if(ret != 0){
        eapp_print("init Sha-256 failed\n");
        return 0;
    }
    ret = wc_Sha256Update(&sha, buffer,sizeof(buffer));
    if(ret != 0){
        eapp_print("sha-256 update failed\n");
        return 0;
    }
    ret = wc_Sha256Final(&sha,shaSum);
    if(ret != 0){
        eapp_print("sha-256 final failed\n");
        return 0;
    }
    eapp_print("test sha-256 succeed\n");
    return 0;
}

int EAPP_ENTRY main(){
    unsigned long *args;
    EAPP_RESERVE_REG;
    test_crypt();
    EAPP_RETURN(0);
}