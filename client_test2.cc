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

#define MAXFILESIZE	(100*1024)
struct FileBatch {
	/* send out */
	const char *filename;
	char *buf;
	size_t size;
	/* recv back */
	string recv;
} batch [] = {
	{ .filename = "neusc_server.h" },
	{ .filename = "neusc_server.cc" },
	{ .filename = "neusc_clientsync.h" },
	{ .filename = "neusc_clientsync.cc" }
};

int batch_count = sizeof(batch) / sizeof(FileBatch);

ClientSync* client;

void help(const char *t) {
	cout << t << " -s server -p port" << endl;
	exit(1);
}

void read_file(int index) {
	const char* filename = batch[index].filename;
	batch[index].buf = new char[MAXFILESIZE];
	int fd = open(filename,  O_RDONLY);
	if (fd < 0) {
		cout << "cannot open file: " << filename << endl;
		return;
	}
	batch[index].buf[0] = index;
	batch[index].size = read(fd, batch[index].buf + 1, MAXFILESIZE-1) + 1;
	if (batch[index].size <= 0) {
		cout << "read file error" << endl;
	}
	close(fd);
}

void send_and_recv(int count) {
	/* send out files */
	cout << count << " send file: ";
	for (int i = 0; i < batch_count; i++) {
		client->out(batch[i].buf, batch[i].size);
		cout << batch[i].size << " ";
	}
	cout << endl;

	/* recv datas */
	cout << count << " recv file: ";
	for (int i = 0; i < batch_count; i++) {
		client->in(batch[i].recv);
		cout << batch[i].recv.size() << " ";
	}
	cout << endl;

	for (int i = 0; i < batch_count; i++) {
		const char* data = batch[i].recv.data();
		int idx = data[0];
		assert(idx >= 0 && idx < batch_count);
		if (batch[i].recv.size() != batch[idx].size ||
				memcmp(data + 1, batch[idx].buf + 1, batch[idx].size - 1)) { 
			cout << "DIFFERENT: " << "sent:" << strlen(batch[i].buf) << " "
				<< "recv:" << batch[i].recv.size() << endl;
		}
	}
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

	client = new ClientSync();
	client->set_remote(server, port);

	cout << "Start connecting " << endl;
	bool is_connected = client->connect_timeout(5);

	if (!is_connected) {
		cout << "connect fail" << endl;
		exit(1);
	}

	cout << "Connect success" << endl;

	for (int i = 0; i < batch_count; i++) {
		read_file(i);
	}

	for (int i = 0; i < 100; i++)
		send_and_recv(i + 1);

	for (int i = 0; i < batch_count; i++) {
		delete[] batch[i].buf;
	}

	delete client;
}

