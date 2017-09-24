#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <error.h>
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
#include "neusc_clientsync.h"
#include "neusc_server.h"

using namespace neusc;

ClientSync::ClientSync() {
	signal(SIGPIPE, SIG_IGN);
	init();
}

ClientSync::~ClientSync() {
	if (handle > 0)
		close(handle);
}

void ClientSync::init() {
	handle = ::socket(AF_INET, SOCK_STREAM, 0);
	if (handle < 0) {
		::perror("socket");
	}
}

void ClientSync::close_handle() {
	if (handle > 0)
		close(handle);
	handle = -1;
}

void ClientSync::disconnect() {
	if (handle > 0) {
		::shutdown(handle, 2);
		::close(handle);
	}
	handle = -1;
}

bool ClientSync::reconnect() {
	// if handle is not -1, just return without reconnect
	if (handle < 0)
		return connect();
	return true;
}

bool ClientSync::set_remote(const char* name, int port) {
	bool is_ip = true;
	int name_len = strlen(name);
	for (int i = 0; i < name_len; i++) {
		if (name[i] != '.' && (name[i] < '0' || name[i] > '9')) {
			is_ip = false;
			break;
		}
	}
	if (!is_ip) {
		/* dns */
		struct hostent* h = gethostbyname(name);
		if (h == nullptr) {
			return false;
		}
		int i = 0;
		char** pptr = h->h_addr_list;
		for (; *pptr != nullptr; pptr++) {
			if (inet_ntop(h->h_addrtype, *pptr, 
					server_ip_address[i], IP_MAXSIZE) != nullptr) {
				i++;
				if (i >= IP_LIST_COUNT)
					break;
			}
		}
		server_ip_count = i;
	} else {
		server_ip_count = 1;
		strcpy(server_ip_address[0], name);
	}
	memset(&server_address, 0, sizeof(server_address));
	server_address.sin_family = AF_INET;
	server_address.sin_port = htons(port);
	server_address.sin_addr.s_addr = inet_addr(server_ip_address[0]);
	return true;
}

bool ClientSync::connect_timeout(int timeout) {
	if (handle < 0)
		init();
	int error = -1;
	int len = sizeof(int);
	timeval tm;
	fd_set set;
	unsigned long ul = 1;
	ioctl(handle, FIONBIO, &ul);
	bool ret = false;
	if (::connect(handle,(struct sockaddr*)&server_address,
				sizeof(server_address)) == -1) {
		tm.tv_sec = timeout;
		tm.tv_usec = 0;
		FD_ZERO(&set);
		FD_SET(handle, &set);
		if (::select(handle+1, NULL, &set, NULL, &tm) > 0) {
			::getsockopt(handle, SOL_SOCKET, SO_ERROR,
					&error, (socklen_t*)&len);
			ret = ((error == 0) ? true : false);
		} else
			ret = false;
	} else
		ret = true;
	ul = 0;
	ioctl(handle, FIONBIO, &ul);
	if (ret == false) {
		close_handle();
		return false;
	}

	return true;
}

bool ClientSync::connect() {
	if (handle < 0)
		init();
	int result = ::connect(handle, (struct sockaddr*)&server_address, 
			sizeof(server_address));
	if (result != 0) {
		close_handle();
		return false;
	}

	return true;
}


/* when return false, handle has been closed */
bool ClientSync::out(const char* message, unsigned int length) {
	if (handle < 0)
		return false;

	unsigned char len[4];
	len[0] = length >> 24;
	len[1] = (length & 0xFF0000U) >> 16;
	len[2] = (length & 0xFF00U) >> 8;
	len[3] = (length & 0xFFU);
	if (!write_socket_in_block(handle, (char*)len, 4)) {
		close_handle();
		return false;
	}
	if (!write_socket_in_block(handle, message, length)) {
		close_handle();
		return false;
	}
	return true;
}

/* when return false, handle has been closed */
bool ClientSync::in(std::string& str) {
	char *buf;
	unsigned int length;
	if (in(buf, length)) {
		str = std::string(buf, buf + length);
		delete[] buf;
		return true;
	} else 
		return false;
}

bool ClientSync::in(char*& message, unsigned int& length) {
	if (handle < 0)
		return false;

	unsigned char len[4];
	char* buf;
	if (!read_socket_in_block(handle, (char*)len, 4)) {
		close_handle();
		return false;
	}
	length = (len[0] << 24) | (len[1] << 16) | (len[2] << 8) | len[3];
	if(length <= 0)
		return false;
	buf = new char[length];
	if (buf == nullptr) 
		return false;
	if (!read_socket_in_block(handle, buf, length)) {
		delete[] buf;
		close_handle();
		return false;
	}
	message = buf;
	return true;
}

bool ClientSync::write_socket_in_block(int fd, const char* buf, int len) {
	if (fd < 0)
		return false;
	int write_num;
	while (len > 0) {
		write_num = ::write(fd, buf, len);
		if (write_num < 0) {
			if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
				continue;
			return false;
		} else if (write_num == 0) {
			return false;
		}
		len -= write_num;
		buf += write_num;
	}
	return true;
}

bool ClientSync::read_socket_in_block(int fd, char* buf, int len) {
	if (fd < 0)
		return false;
	int read_num;
	while (len > 0) {
		read_num = ::read(fd, buf, len);
		if (read_num < 0) {
			if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
				continue;
			return false;
		} else if (read_num == 0) {
			return false;
		}
		len -= read_num;
		buf += read_num;
	}
	return true;
}


