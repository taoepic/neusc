#ifndef __NEUSC_SERVER_H_
#define __NEUSC_SERVER_H_

#include <cassert>
#include <iostream>
#include <functional>
#include <vector>
#include <list>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <map>
#include <unordered_map>
#include <deque>
#include <cerrno>
#include <pthread.h>
#include <sys/time.h>
#include <algorithm>

namespace neusc {

class Server;
class Request;
class Response;

struct ServerEvents {
	/* onInit return return false to stop server starting process */
	std::function<bool(Server*)> onInit = nullptr;
	std::function<void(Server*)> onEnd = nullptr;

	/* onConnected return false to stop connection */
	std::function<bool(int, const char*)> onConnected = nullptr;
	std::function<void(int)> onPeerReset = nullptr;
	std::function<void(int)> onPeerClosed = nullptr;

	/* onRequest return false indicates nothing to reponse to client 
	 * must return true if you have response to send back.
	*/
	std::function<bool(Request*)> onRequest = nullptr;

	/* onPick gives a pending_list for choosing a request for work thread to process,
	 * the list must not lock again because it has locked. the function needs to
	 * return the select request's iterator. you can delete the item which you don't
	 * want to process, but you should be careful that will make a result of 
	 * no response to client.
	 * If onPick set to nullptr, default process will select the first item to continue
	*/
	std::function<std::list<Request*>::iterator(std::list<Request*>)> onPick = nullptr;
};

class Response {
	friend class Server;
	friend class Request;
public:
	Response(Request *request);
	~Response();
	const char * get_ptr() { return data; }
	int get_size() { return get_length(); }
protected:
	inline int get_length() const {
		return length_buf[0] << 24 |
			length_buf[1] << 16 | length_buf[2] << 8 | length_buf[3];
	}
	inline void set_length(unsigned int len) {
		length_buf[0] = len >> 24;
		length_buf[1] = (len & 0x00FFFFFFU) >> 16;
		length_buf[2] = (len & 0x00FFFFU) >> 8;
		length_buf[3] = len & 0x00FFU;
	}
	void write_data(int handle, Server* server);
	Request *request;
	char* data;
	int body_has_written;
	int length_has_written;
	unsigned char length_buf[4];
};

class Request {
	friend class Server;
	friend class Response;
public:
	static const int RESERVED_SIZE = 1024;
	Request(Server* server, int handle);
	~Request();

	/* request buffer start ptr and size */
	const char* get_ptr() { return data; }
	int get_size() { return get_length(); }

	/* copy data to reponse buffer */
	void clone_response(int size, const char* buf);

	/* NOTE: refer data can eliminate copy of data, 
	 *	but will be released by response as delete[] data 
    */
	void refer_response(int size, const char* buf);

	/* set response mature for reply out */
	void end_response();

	/* release request data, only can be used when have got 
	 *	the request data in word queue 
	*/
	void release_request_data();

	/* get response ptr */
	Response *res() { return response; }
protected:
	inline int get_length() const {
		return length_buf[0] << 24 |
			length_buf[1] << 16 | length_buf[2] << 8 | length_buf[3];
	}

	bool reserve_size(int new_size);
	void append_data(const char* src, int size, int handle, Server* server);

	Server *server;
	Response *response;
	char* data;
	int handle;

	/* the request in mature_list, if matured & discard, 
	 *	request will be deleted immediate 
	*/
	volatile bool matured;
	bool discard;
	int reserved_size;
	int body_has_read;
	int length_has_read;
	unsigned char length_buf[4];
};

/* when a request has filled completely, it's moved to pending request list
 * but it's not complete with response 
*/
struct PendingList {
	std::mutex mutex;
	std::condition_variable cond;
	std::list<Request*> list;
	void lock() { mutex.lock(); }
	void unlock() { mutex.unlock(); }
};

/* All requests which have end response will be move to this list
*/
struct MatureList {
	std::mutex mutex;
	std::list<Request*> list;
	void lock() { mutex.lock(); }
	void unlock() { mutex.unlock(); }
};

class Server {
	friend class Request;
	friend class Response;
public:
	enum : unsigned char {
		RESPONSE_ORDERLY = 1,
	};
	Server();
	int ready(int listen_port, const ServerEvents& on_event);
	void set_work_thread_count(int c) { work_thread_count = c; }
	void set_listen_address(const std::string& a) { listen_address = a; }
	void set_config_on(unsigned char c) { config |= c; }
	void set_config_off(unsigned char c) { config &= ~c; }
	void dump_state();
	static void prepare_exit();

protected:
	void thread_process();
	void create_premature_entry(int handle);
	Request* get_handle_request(int handle);
	Response* get_handle_response(int handle);
	void clear_handle(int handle);
	void move_premature_request(int handle);
	Request* move_sending_request(int handle);
	void notify_working();
	void release_remain();

	void set_non_blocking(int);
	void close_connection(int handle);
	void epoll_add_socket(int sock, int op);
	void epoll_delete_socket(int sock);
	void epoll_modify_socket(int sock, int op);

	constexpr static const int LISTENQ = 20;
	constexpr static const int EVENTSIZE = 1000;
	constexpr static const int BUFFERSIZE = 64 * 1024;

	static volatile bool exit_flag;
	int epoll_fd;
	struct epoll_event events[EVENTSIZE];
	struct sockaddr_in server_address;
	char buffer[BUFFERSIZE];
	ServerEvents server_events;
	int work_thread_count;
	std::string listen_address;
	unsigned char config;

	std::unordered_map<int,Request*> premature_map;
	std::unordered_map<int,Request*> sending_map;
	PendingList pending_list;
	MatureList mature_list;

	std::vector<std::thread*> threads;
};
} // namespace neusc

#endif

