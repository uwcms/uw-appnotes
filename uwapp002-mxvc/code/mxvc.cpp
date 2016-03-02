/* MXVC - Multitarget XVC
 * 
 * University of Wisconsin
 *
 * Compile with: g++ -std=c++0x -o mxvc mxvc.cpp
 *
 */


#include <arpa/inet.h>
#include <byteswap.h>
#include <errno.h>
#include <limits.h>
#include <map>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define debugf(...)
//#define debugf(...) printf(__VA_ARGS__)
#undef	NOISY

static ssize_t dowrite(int fd, const void *buf, size_t count) {
	unsigned char *outbuf = (unsigned char*)buf;
	int written = 0;

	while (count > 0) {
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		int num = select(fd+1, NULL, &fds, NULL, NULL);
		if (num < 0)
			return -1;
		int just_written = write(fd, outbuf, count);
		if (just_written < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				continue;
			return -1;
		}
		written += just_written;
		outbuf += just_written;
		count -= just_written;
	}
	return written;
}

static ssize_t doread(int fd, void *buf, size_t count) {
	unsigned char *inbuf = (unsigned char*)buf;
	int bytesread = 0;

	while (count > 0) {
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		int num = select(fd+1, &fds, NULL, NULL, NULL);
		if (num < 0)
			return -1;
		int just_read = read(fd, inbuf, count);
		if (just_read < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				continue;
			return -1;
		}
		else if (just_read == 0) {
			return bytesread;
		}
		bytesread += just_read;
		inbuf += just_read;
		count -= just_read;
	}
	return bytesread;
}

void handle_getinfo(int clientfd, std::map<std::string, int> &targets)
{
	debugf("REQ: getinfo\n");

	int infomax = INT_MAX;
	for (auto it = targets.begin(), eit = targets.end(); it != eit; it++) {
		debugf("Processing Target %s\n", it->first.c_str());
		if (dowrite(it->second, "getinfo:", 8) != 8) {
			printf("Error writing getinfo request to slave %s\n", it->first.c_str());
			exit(1);
		}
		char slvrsp[16];
		if (doread(it->second, slvrsp, 15) != 15) {
			printf("Error reading getinfo response from slave %s\n", it->first.c_str());
			exit(1);
		}
		if (strcmp(slvrsp, "xvcServer_v1.0:") != 0) {
			printf("Error, invalid getinfo response from slave %s\n", it->first.c_str());
			exit(1);
		}
		int buflen = 0;
		do {
			if (buflen >= 16 || doread(it->second, slvrsp+buflen, 1) != 1) {
				printf("Error reading getinfo response from slave %s\n", it->first.c_str());
				exit(1);
			}
			buflen++;
		} while (slvrsp[buflen-1] != '\n');
		slvrsp[buflen-1] = '\0';
		char *endptr = NULL;
		long int slvmax = strtol(slvrsp, &endptr, 10);
		if (!endptr || *endptr != '\0') {
			printf("Error reading getinfo response from slave %s\n", it->first.c_str());
			exit(1);
		}
		if (infomax > slvmax)
			infomax = slvmax;
	}

	char rsp[32];
	snprintf(rsp, 32, "xvcServer_v1.0:%d\n", infomax);
	if (dowrite(clientfd, rsp, strlen(rsp)) != static_cast<ssize_t>(strlen(rsp))) {
		printf("Error writing getinfo response to master\n");
		exit(1);
	}
}

void handle_shift(int clientfd, std::map<std::string, int> &targets)
{
#ifdef NOISY
	debugf("REQ: shift\n");
#endif

	/* TODO:
	 * This will require a buffer of TDO vectors.  TMS is passed immediately to
	 * all, but TDO needs to be shifted slowly along the chain.
	 *
	 * http://www.keil.com/support/man/docs/ulink2/ulink2_jtag_chain1.png
	 */

	uint32_t nbits = 0;
	if (doread(clientfd, &nbits, sizeof(nbits)) != sizeof(nbits)) {
		printf("Error reading shift request from master\n");
		exit(1);
	}

	// nbits is little endian.
	// __bswap_32 will turn it to big endian.
	// ntohl() will turn it from big endian to native endian.
	nbits = ntohl(__bswap_32(nbits));

#ifdef NOISY
	debugf("Reading %u bits for shift:\n", nbits);
#endif

	int nbytes = nbits/8 + (nbits%8 ? 1 : 0);
	int nwords = nbytes/4 + (nbytes%4 ? 1 : 0);

	uint8_t tms_vec[nbytes];
	memset(tms_vec, 0, sizeof(tms_vec));
	if (doread(clientfd, &tms_vec, nbytes) != nbytes) {
		printf("Error reading shift request from master\n");
		exit(1);
	}

	uint8_t tdi_vec[nbytes];
	memset(tdi_vec, 0, sizeof(tdi_vec));
	if (doread(clientfd, &tdi_vec, nbytes) != nbytes) {
		printf("Error reading shift request from master\n");
		exit(1);
	}

#ifdef NOISY
	debugf("    tms:");
	for (int i = 0; i < nbytes; i++) {
		if ((i % 16) == 0)
			debugf("\n        ");
		else if ((i % 8) == 0)
			debugf("   ");
		else if ((i % 4) == 0)
			debugf(" ");
		debugf(" %02x", tms_vec[i]);
	}
	debugf("\n");

	debugf("    tdi:");
	for (int i = 0; i < nbytes; i++) {
		if ((i % 16) == 0)
			debugf("\n        ");
		else if ((i % 8) == 0)
			debugf("   ");
		else if ((i % 4) == 0)
			debugf(" ");
		debugf(" %02x", tdi_vec[i]);
	}
	debugf("\n");
#endif

	for (auto it = targets.begin(), eit = targets.end(); it != eit; ++it) {
		if (dowrite(it->second, "shift:", 6) != 6) {
			printf("Error writing shift request to slave %s\n", it->first.c_str());
			exit(1);
		}
		// nbits has been converted to host endian.
		// htonl() will convert it to big endian.
		// __bswap_32() will convert it to little endian.
		uint32_t wire_nbits = __bswap_32(htonl(nbits));
		if (dowrite(it->second, &wire_nbits, sizeof(wire_nbits)) != sizeof(wire_nbits)) {
			printf("Error writing shift request to slave %s\n", it->first.c_str());
			exit(1);
		}
		if (dowrite(it->second, tms_vec, nbytes) != nbytes) {
			printf("Error writing shift request to slave %s\n", it->first.c_str());
			exit(1);
		}
		if (dowrite(it->second, tdi_vec, nbytes) != nbytes) {
			printf("Error writing shift request to slave %s\n", it->first.c_str());
			exit(1);
		}

		// Message sent.  Receiving Reply

		if (doread(it->second, tdi_vec, nbytes) != nbytes) {
			printf("Error reading shift response from slave %s\n", it->first.c_str());
			exit(1);
		}
	}

#ifdef NOISY
	debugf("    tdo:");
	for (int i = 0; i < nbytes; i++) {
		if ((i % 16) == 0)
			debugf("\n        ");
		else if ((i % 8) == 0)
			debugf("   ");
		else if ((i % 4) == 0)
			debugf(" ");
		debugf(" %02x", tdi_vec[i]);
	}
	debugf("\n");
#endif

	if (dowrite(clientfd, &tdi_vec, nbytes) != nbytes) {
		printf("Error writing shift result to master\n");
		exit(1);
	}
}

// From example project xvc_ml605
inline uint32_t byteToInteger(uint8_t *byteVector) {
	uint32_t uintRet;
	uint32_t uintRetTmp;

	uintRet     = (uint32_t) (byteVector[0] & 0xff);
	uintRetTmp  = (uint32_t) (byteVector[1] & 0xff);
	uintRet    |= uintRetTmp<<8;
	uintRetTmp  = (uint32_t) (byteVector[2] & 0xff);
	uintRet    |= uintRetTmp<<16;
	uintRetTmp  = (uint32_t) (byteVector[3] & 0xff);
	uintRet    |= uintRetTmp<<24;

	return uintRet;
}

// From example project xvc_ml605
inline void integerToByte(uint8_t *byteVector, uint32_t uintValue) {
	uint32_t uintTemp;

	uintTemp = uintValue;
	byteVector[0] = uintTemp & 0xff;
	uintTemp = uintTemp >> 8;
	byteVector[1] = uintTemp & 0xff;
	uintTemp = uintTemp >> 8;
	byteVector[2] = uintTemp & 0xff;
	uintTemp = uintTemp >> 8;
	byteVector[3] = uintTemp & 0xff;
}

void handle_settck(int clientfd, std::map<std::string, int> &targets)
{
	debugf("REQ: settck\n");

	uint32_t reqtck = 0;
	if (doread(clientfd, &reqtck, 4) != 4) {
		printf("Error reading settck request from master\n");
		exit(1);
	}

	// reqtck is in little endian, but since we are going to ignore and lie about it, no conversion is needed.

	// We can't know what the slave targets will all support, and it might not even be consistent.
	// It's best to not try to correct things here, and just allow chains to run at their default speeds.

	debugf("Claiming I set the clock to %u.  I'm lying.  But then, so does Xilinx.\n", ntohl(__bswap_32(reqtck)));

	if (dowrite(clientfd, &reqtck, 4) != 4) {
		printf("Error writing settck response to master\n");
		exit(1);
	}
}

void run_client(int clientfd, std::map<std::string, int> &targets)
{
	for (auto it = targets.begin(), eit = targets.end(); it != eit; ++it) {
		std::string target_addr_str = it->first;
		uint16_t target_port = 2542;

		size_t div_pos = it->first.find(':');
		if (div_pos != std::string::npos) {
			target_addr_str = it->first.substr(0, div_pos);
			std::string target_port_str = it->first.substr(div_pos+1);
			char *eptr = NULL;
			target_port = strtoul(target_port_str.c_str(), &eptr, 10);
			if (!eptr || *eptr != '\0') {
				printf("Invalid port: %s in %s\n", target_port_str.c_str(), it->first.c_str());
				exit(1);
			}
		}

		char *hostname;

		it->second = socket(AF_INET, SOCK_STREAM, 0);
		if (it->second < 0) {
			printf("Unable to create socket for target connection\n");
			exit(1);
		}

		struct hostent *target_hostent = gethostbyname(target_addr_str.c_str());
		if (target_hostent == NULL) {
			printf("Unable to resolve target hostname \"%s\"\n", target_addr_str.c_str());
			exit(1);
		}

		struct sockaddr_in target_addr;
		memset(&target_addr, 0, sizeof(target_addr));
		target_addr.sin_family = AF_INET;
		memcpy(&target_addr.sin_addr.s_addr, target_hostent->h_addr, target_hostent->h_length);                                                                                      
		target_addr.sin_port = htons(target_port);

		if (connect(it->second, reinterpret_cast<sockaddr*>(&target_addr), sizeof(target_addr)) < 0) {
			printf("Unable to connect to target %s\n", it->first.c_str());
			exit(1);
		}
	}

	char buf[10];
	memset(buf, 0, 10);
	int datalen = 0;
	int bytes;
	while ((bytes = doread(clientfd, buf+datalen, 1)) > 0) {
		datalen += bytes;
		if (datalen > 8)
			break;

		if (datalen == 8 && strncmp(buf, "getinfo:", 8) == 0) {
			handle_getinfo(clientfd, targets);

			memset(buf, 0, 10);
			datalen = 0;
			continue;
		}
		if (datalen == 6 && strncmp(buf, "shift:", 6) == 0) {
			handle_shift(clientfd, targets);

			memset(buf, 0, 10);
			datalen = 0;
			continue;
		}
		if (datalen == 7 && strncmp(buf, "settck:", 7) == 0) {
			handle_settck(clientfd, targets);

			memset(buf, 0, 10);
			datalen = 0;
			continue;
		}
	}
	for (auto it = targets.begin(), eit = targets.end(); it != eit; ++it) {
		close(it->second);
		it->second = -1;
	}
}

void do_fork() {
	if (fork())
		exit(0);
	setsid();
	if (fork())
		exit(0);
}

int main(int argc, char *argv[])
{
	if (argc < 3) {
		printf("mxvc client_ip_address xvc_address_1[:port] [xvc_address_2[:port] ...]\n");
		return 1;
	}

	std::string authorized_client = argv[1];
	std::map<std::string, int> targets;
	for (int i = 2; i < argc; ++i)
		targets.insert(std::make_pair(std::string(argv[i]), -1));

	//int listenfd;
	struct sockaddr_in serv_addr;

	int listenfd = socket(AF_INET, SOCK_STREAM, 0);
	if (listenfd < 0) {
		printf("Unable to open listening socket: %d (%s)\n", errno, strerror(errno));
		return 1;
	}

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(2542);

	int optval = 1;
	setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,&optval,sizeof(int));

	if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
		printf("Unable to bind listening address: %d (%s)\n", errno, strerror(errno));
		return 1;
	}

	printf("Listening for clients...\n");
	listen(listenfd,5);

#ifdef DAEMON_MODE
	do_fork();
#endif

	while (true) {
		struct sockaddr_in client_addr;
		socklen_t client_addr_len = sizeof(client_addr);
		int clientfd = accept(listenfd, reinterpret_cast<struct sockaddr*>(&client_addr), &client_addr_len);
		if (clientfd >= 0) {
			if (argc >= 2) {
				char remoteip[INET_ADDRSTRLEN+1];
				memset(remoteip, 0, INET_ADDRSTRLEN+1);
				if (inet_ntop(AF_INET, &(client_addr.sin_addr), remoteip, client_addr_len) == NULL) { // We bound to AF_INET specifically, above.
					printf("Client dropped from unknown address.\n");
					close(clientfd);
					continue;
				}
				printf("Client connected from %s\n", remoteip);
				if (strncmp(remoteip, authorized_client.c_str(), INET_ADDRSTRLEN+1) != 0) {
					printf("Client rejected.  We will only accept connections from \"%s\"\n", authorized_client.c_str());
					close(clientfd);
					continue;
				}
			}

			int optval = 3; // tcp_keepalive_probes (max unacknowledged)
			if(setsockopt(clientfd, IPPROTO_TCP, TCP_KEEPCNT, &optval, sizeof(optval)) < 0) {
				printf("Failed to set TCP_KEEPCNT.  Dropping new client\n");
				close(clientfd);
				continue;
			}

			optval = 60; // tcp_keepalive_time (idle time before connection's first probe)
			if(setsockopt(clientfd, IPPROTO_TCP, TCP_KEEPIDLE, &optval, sizeof(optval)) < 0) {
				printf("Failed to set TCP_KEEPIDLE.  Dropping new client\n");
				close(clientfd);
				continue;
			}

			optval = 60; // tcp_keepalive_intvl (time between probes, data or not)
			if(setsockopt(clientfd, IPPROTO_TCP, TCP_KEEPINTVL, &optval, sizeof(optval)) < 0) {
				printf("Failed to set TCP_KEEPINTVL.  Dropping new client\n");
				close(clientfd);
				continue;
			}

			optval = 1; // enable tcp keepalive
			if(setsockopt(clientfd, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval)) < 0) {
				printf("Failed to set SO_KEEPALIVE.  Dropping new client\n");
				close(clientfd);
				continue;
			}

			run_client(clientfd, targets);
			printf("Client disconnected.\n");
			close(clientfd);
		}
	}
	close(listenfd);
	return 0;
}
