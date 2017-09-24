#include "neusc_server.h"
#include <iostream>
#include <sstream>

using namespace neusc;
using namespace std;

const int PORT = 23456;

void onRequest(Request* request) {
	int request_size = request->get_size();
	const char *request_ptr = request->get_ptr();

	cout << "(" << request_size << ")" << endl;
	request->clone_response(request_size, request_ptr);
	request->end_response();
}

int main () {
	Server* server = new Server();
	ServerEvents events;

	//server->set_work_thread_count(4);
	server->set_listen_address("0.0.0.0");
	events.onInit = []() {
		cout << "server init" << endl;
		return true;
	};
	events.onEnd = []() {
		cout << "server end" << endl;
	};
	events.onConnected = [](int handle, const char* ipaddr) {
		cout << "connection establish (" << handle << ") ip: " << ipaddr << endl;
		return true;
	};
	events.onPeerReset = [](int handle) {
		cout << "connection abort (" << handle << ")" << endl;
	};
	events.onPeerClosed = [](int handle) {
		cout << "connection closed (" << handle << ")" << endl;
	};
	events.onRequest = onRequest;

	cout << "server start @" << PORT << endl;
	server->ready(PORT, events);
}

