#define MAX_CLIENTS 50
#define IP_ACL_FILE "/mnt/persistent/config/rpcsvc.acl"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#include "ipacl.h"
#include "Client.h"

int main(int argc, char *argv[])
{
	int listenfd;
	struct sockaddr_in serv_addr;

	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	if (listenfd < 0) {
		printf("Unable to open socket: %d (%s)\n", errno, strerror(errno));
		return 1;
	}

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(60002);

	if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
		printf("Unable to bind address: %d (%s)\n", errno, strerror(errno));
		return 1;
	}

	listen(listenfd,5);

	if (fork())
		exit(0);
	setsid();
	if (fork())
		exit(0);

	std::vector<Client> clients;

	while (true) {
		fd_set rfds, wfds;
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		int maxfd = 0;
		if (clients.size() < MAX_CLIENTS) {
			FD_SET(listenfd, &rfds);
			maxfd = listenfd;
		}
		for (auto it = clients.begin(), eit = clients.end(); it != eit; ++it) {
			if (it->write_ready()) {
				FD_SET(it->fd, &wfds);
				if (maxfd < it->fd)
					maxfd = it->fd;
			}
			if (it->read_ready()) {
				FD_SET(it->fd, &rfds);
				if (maxfd < it->fd)
					maxfd = it->fd;
			}
		}

		if (select(maxfd+1, &rfds, &wfds, NULL, NULL) < 0) {
			if (errno == EINTR)
				continue;

			syslog(LOG_ERR, "Error in select(): %m");
			break;
		}

		// Process any existing connections.
		for (auto it = clients.begin(), eit = clients.end(); it != eit; ++it) {
			if (FD_ISSET(it->fd, &rfds) || FD_ISSET(it->fd, &wfds))
				it->run_io();
		}

		// Accept New Connections
		if (FD_ISSET(listenfd, &rfds)) {
			struct sockaddr_in client_addr;
			socklen_t client_addr_len = sizeof(client_addr);
			int clientfd = accept(listenfd, reinterpret_cast<struct sockaddr*>(&client_addr), &client_addr_len);

#ifdef IP_ACL_FILE
			if (authorize_ip(ntohl(client_addr.sin_addr.s_addr))) {
#endif
				clients.emplace_back(clientfd);
#ifdef IP_ACL_FILE
			}
			else {
				close(clientfd);
				char ipaddr[16];
				ip2string(ntohl(client_addr.sin_addr.s_addr), ipaddr, 16);
				syslog(LOG_NOTICE, "Connection attempted from unauthorized IP: %s", ipaddr);
			}
#endif
		}
	}
	return 1;
}
