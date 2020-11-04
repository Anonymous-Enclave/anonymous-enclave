. = 0x801ff000; /* the last page before the payload */

/* ## manufacturer_keys : */

/* 64 Bytes : manufacturer public key */
PROVIDE( manu_public_key = . );
. += 0x40;

/* 64 Bytes : device public key */
PROVIDE( dev_public_key = . );
. += 0x40;

/* 32 Bytes : device secret key */
PROVIDE( dev_secret_key = . );
. += 0x20;

/* ## security_monitor_keys : */

/* 64 Bytes : security monitor public key */
PROVIDE( sm_public_key = . );
. += 0x40;

/* 32 Bytes : security monitor secret key */
PROVIDE( sm_secret_key = . );
. += 0x20;

/* 32 Bytes : security monitor hash */
PROVIDE( sm_hash = . );
. += 0x20;

/* 64 Bytes : security monitor's signature by device */
PROVIDE( sm_signature = . );
. += 0x40;
