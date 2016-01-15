#ifndef __IPACL_H
#define __IPACL_H

#include <stdint.h>

int ip2string(uint32_t ip, char *str, int strsize); // str must be at least 16 bytes for an address
bool authorize_ip(uint32_t ip);

#endif
