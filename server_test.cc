#include "neusc_server.h"
#include <iostream>
#include <sstream>

using namespace neusc;
using namespace std;

const int PORT = 23456;

int main () {
	Server* server = new Server();
	server->set_listen_address("0.0.0.0");

	//server->set_work_thread_count(4);
	//server->set_config_on(Server::RESPONSE_ORDERLY);

	ServerEvents events = {
		.onInit = [](Server* s) {
			cout << "server init" << endl;
			return true;
		},
		.onEnd = [](Server* s) {
			cout << "server end" << endl;
		},
		.onConnected = [](int handle, const char* ipaddr) {
			cout << "connection establish (" << handle << ") ip: " << ipaddr << endl;
			return true;
		},
		.onPeerReset = [](int handle) {
			cout << "connection abort (" << handle << ")" << endl;
		},
		.onPeerClosed = [](int handle) {
			cout << "connection closed (" << handle << ")" << endl;
		},
		.onRequest = [](Request *request) -> bool {
			int request_size = request->get_size();
			const char *request_ptr = request->get_ptr();

			cout << "(" << request_size << ")" << endl;
			request->clone_response(request_size, request_ptr);

			/* now you can release request buffer */
			request->release_request_data();

			/* prepare response for sending out */
			request->end_response();

			/* must return true if it has response to send back */
			return true;
		}
	};

	cout << "server start @" << PORT << endl;
	server->ready(PORT, events);
}

