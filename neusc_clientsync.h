#ifndef __NEUSC_CLIENTSYNC_H_
#define __NEUSC_CLIENTSYNC_H_

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <iostream>
#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <netdb.h>
#include <signal.h>
#include "neusc_server.h"

namespace neusc {
class ClientSync {
public:
	ClientSync();
	~ClientSync();
	void init();
	void close_handle();
	bool set_remote(const char* name, int port);
	void disconnect();

	bool connect_timeout(int timeout);
	bool connect();
	bool reconnect();
	bool is_connected() const {
		return handle > 0;
	}

	/* when return false, handle has been closed */
	bool out(const char* message, unsigned int length);
	bool out(const std::string& msg) {
		return out(msg.data(), msg.size() + 1);
	}

	/* when return false, handle has been closed */
	bool in(std::string& str);

	/* return bool if success, message and length will be updated
	   if success, you must delete[] message by yourself 
	*/
	bool in(char*& message, unsigned int &length);

protected:
	bool write_socket_in_block(int fd, const char* buf, int len);
	bool read_socket_in_block(int fd, char* buf, int len);

	static const int IP_LIST_COUNT = 4;
	static const int IP_MAXSIZE = 32;
	int handle = -1;
	struct sockaddr_in server_address;
	bool exception_on;
	char server_ip_address[IP_LIST_COUNT][IP_MAXSIZE];
	int server_ip_count = 0;
};
} // namespace neusc

#endif

