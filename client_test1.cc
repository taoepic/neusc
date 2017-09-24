#include <sys/socket.h>
#include <sys/types.h>
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
#include "neusc_clientsync.h"

using namespace std;
using namespace neusc;

void help(const char *t) {
	cout << t << " -s server -p port" << endl;
	exit(1);
}
	
void read_file(const char* filename, char* buffer, int& size) {
	int fd = open(filename,  O_RDONLY);
	if (fd < 0) {
		cout << "cannot open file: " << filename << endl;
	}
	size = read(fd, buffer, 300*1024);
	if (size < 0) {
		cout << "read file error" << endl;
	}
	close(fd);
}

int main(int ac, char* av[]) {
	char server[128];
	int port;
	if (ac <= 1) {
		help(av[0]);
	}
	for (int i = 1; i < ac; i++) {
		if (!strcmp(av[i], "-s")) {
			if (++i < ac)
				strcpy(server, av[i]);
			else
				help(av[0]); 
		} else if (!strcmp(av[i], "-p")) {
			if (++i < ac)
				port = atoi(av[i]);
			else
				help(av[0]);
		}
	}

	ClientSync* client = new ClientSync();
	client->set_remote(server, port);

	cout << "Start connecting " << endl;
	bool is_connected = client->connect_timeout(5);

	if (!is_connected) {
		cout << "connect fail" << endl;
		exit(1);
	}

	cout << "Connect success" << endl;
	char * buffer = new char[300 * 1024];
	if (buffer == nullptr) {
		cout << "allocate memory fail" << endl;
	}
	int size;

	/* sending file1 to server */
	read_file("neusc_server.h", buffer, size);
	cout << "sent size " << size << endl;
	client->out(buffer, size);

	/* wait reply */
	std::string got;
	client->in(got);
	cout << "recv size " << got.size() << endl;

	if (got != buffer) {
		cout << "DIFFERENT: " << endl;
		cout << "sent: " << strlen(buffer) << endl;
		cout << "recv: " << got.size() << endl;
		return 1;
	}
	//cout << "server reply (1): " << got << endl;

	/*sending file2 to server */
	read_file("neusc_server.cc", buffer, size);
	cout << "send size " << size << endl;
	client->out(buffer, size);

	/* wait reply */
	client->in(got);
	cout << "recv size " << got.size() << endl;
	if (got != buffer) {
		cout << "DIFFERENT: " << endl;
		cout << "sent: " << strlen(buffer) << endl;
		cout << "recv: " << got.size() << endl;
		return 1;
	}
	//cout << "server reply (2): " << got << endl;

	delete[] buffer;
	delete client;
}

