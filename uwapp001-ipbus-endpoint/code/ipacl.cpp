#include "ipacl.h"
#include <stdio.h>
#include <stdint.h>

#define IP_ACL_PATH "/mnt/persistent/config/rpcsvc.acl"

int ip2string(uint32_t ip, char *str, int strsize) // str must be at least 16 bytes for an address
{
	return snprintf(str, strsize, "%hhu.%hhu.%hhu.%hhu",
			(ip >> 24) & 0xff,
			(ip >> 16) & 0xff,
			(ip >>  8) & 0xff,
			(ip      ) & 0xff
			);
}

bool authorize_ip(uint32_t ip) 
{
	FILE *ipacls = fopen(IP_ACL_PATH, "r");
	if (!ipacls) {
		return false; // This is a strict allow set.  Missing file = empty set.
	}

	char linebuf[1024];

	while (fgets(linebuf, 1024, ipacls) != NULL) {
		// Ignore comments
		for (int i = 0; i < 1024; i++) {
			if (linebuf[i] == '#' || linebuf[i] == '\n') {
				linebuf[i] = '\0';
				break;
			}
		}
		// Ignore blank lines
		if (linebuf[0] == '\0')
			continue;

		uint8_t ipdata[4];
		uint8_t cidrlen;
		if (sscanf(linebuf, "%hhu.%hhu.%hhu.%hhu/%hhu", ipdata, ipdata+1, ipdata+2, ipdata+3, &cidrlen) == 5) {
			// We have a valid allow line.
			uint32_t allowip = (ipdata[0]<<24)|(ipdata[1]<<16)|(ipdata[2]<<8)|(ipdata[3]);
			uint32_t cidrmask = 0xFFFFFFFF << (32-cidrlen);

			// Is the current IP allowed?
			if ((ip & cidrmask) == (allowip & cidrmask)) {
				fclose(ipacls);
				return true;
			}
		}
	}
	fclose(ipacls);
	return false;
}
