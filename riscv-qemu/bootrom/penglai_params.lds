. = 0x801ff000; /* the last page before the payload */

/* ## manufacturer_keys : */

/* 64 Bytes : manufacturer public key */
PROVIDE( penglai_m_public_key = . );
. += 0x40;

/* 64 Bytes : device public key */
PROVIDE( penglai_dev_public_key = . );
. += 0x40;

/* 32 Bytes : device secret key */
PROVIDE( penglai_dev_secret_key = . );
. += 0x20;

/* ## security_monitor_keys : */

/* 64 Bytes : security monitor public key */
PROVIDE( penglai_sm_public_key = . );
. += 0x40;

/* 32 Bytes : security monitor secret key */
PROVIDE( penglai_sm_secret_key = . );
. += 0x20;

/* 32 Bytes : security monitor hash */
PROVIDE( penglai_sm_hash = . );
. += 0x20;

/* 64 Bytes : security monitor's signature by device */
PROVIDE( penglai_sm_signature = . );
. += 0x40;