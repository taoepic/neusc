## A high concurrent multi queue depth req/res C++ framework

example:  
server: ./server_test   
client: ./client_test2 -s 127.0.0.1 -p 23456  
  
Server side example:  
```{cpp}
	using namespace neusc;
	Server *server = new Server();
	ServerEvents events;
	events.onConnect = [](int handle, const char* ipaddr) {
		std::cout << "connection establish, from "  << ipaddr << std::endl;
		return true;
	};
	events.onPeerReset = [](int handle) {
		std::cout << "connection abort." << std::endl;
	};
	events.onPeerClose = [](int handle) {
		std::cout << "connection closed." << std::endl;
	};
	events.onRequest = [](Request* request) {
		int req_size = request->get_size();
		const char* req_ptr = request->get_ptr();

		request->clone_response(req_size, req_ptr);
		request->end_response();
	};

	server->ready(LISTEN_PORT, events);
```

Client side (SYNC mode) example:  
```{cpp}
	using namespace neusc;
	ClientSync *client = new ClientSync();
	client->set_remote(server_name_or_ip, port);
	if (!client->connect_timeout(10)) {
		std::cout << "connect fail" << std::endl;
		return;
	}
/* send out request 1 */
	if (!client->out(std::string("the client request or data here"))) {
		std::cout << "send request fail" << std::endl;
		return;
	}

/* send out request 2 */
/* send out request 3 */
/* .... */

/* receive response I */
	std::string reply;
	if (!client->in(reply)) {
		std::cout << "recv response fail" << std::endl;
		return;
	}
	std::cout << "recv response: " << reply << std::endl;
/* receive response II */
/* receive response III */
/* ... */

	client->disconnect();
	delete client;
```

Request & Response size could up to 2^32  

