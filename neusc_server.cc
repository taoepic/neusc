#include "neusc_server.h"
#include <fcntl.h>
#include <signal.h>

using namespace neusc;
using namespace std;

Request::Request(Server* s, int h) : 
		server(s), response(nullptr), data(nullptr), handle(h), 
		matured(false), discard(false),
		body_has_read(0), length_has_read(0) {
	reserved_size = RESERVED_SIZE;
	data = new char[reserved_size];
	assert(data);
	memset(length_buf, 0, 4);
}

Request::~Request() {
	if (data != nullptr)
		delete[] data;
	if (response != nullptr)
		delete response;
}

bool Request::reserve_size(int new_size) {
	if (new_size <= reserved_size)
		return true;
	int prepare_size = reserved_size;
	while (new_size > prepare_size)
		prepare_size *= 2;
	char* alloc_data = new char[prepare_size];
	if (alloc_data == nullptr)
		return false;
	memcpy(alloc_data, data, body_has_read);
	delete[] data;
	data = alloc_data;
	reserved_size = prepare_size;
	return true;
}

void Request::append_data(const char* src, int size, int handle, Server* server) {
	int copy_len;
	if (size == 0)
		return;

	if (length_has_read < 4) {
		copy_len = min(size, 4 - length_has_read);
		memcpy(length_buf + length_has_read, src, copy_len);
		length_has_read += copy_len;
		src += copy_len;
		size -= copy_len;
	}
	if (size == 0)
		return;
	int body_length = get_length();
	int remain_body_size = body_length - body_has_read;
	copy_len = min(size, remain_body_size);
	reserve_size(body_length);
	memcpy(data + body_has_read, src, copy_len);
	body_has_read += copy_len;
	src += copy_len;
	size -= copy_len;

	if (body_has_read == body_length) {
		/* read complete */
		server->move_premature_request(handle);
		server->notify_working();
	}
	if (size > 0) {
		Request* request = server->get_handle_request(handle);
		request->append_data(src, size, handle, server);
	}
}

void Request::clone_response(int size, const char* buf) {
	Response *r = response;
	assert (size > 0);
	if (r->data)
		delete [](r->data);
	r->data = new char[size];
	assert(r->data);
	memcpy(r->data, buf, size);
	r->set_length(size);
}

/* 
 * end_response make request matured 
 * don't need to lock mature_list, because it won't be touch if request->matured is false
*/
void Request::end_response() {
	matured = true;
	if (!discard)
		server->epoll_modify_socket(handle, EPOLLIN | EPOLLOUT | EPOLLET);
}

void Request::release_request_data() {
	if (data) {
		delete[] data;
		data = nullptr;
		memset(length_buf, 0, 4);
		reserved_size = 0;
		body_has_read = 0;
		length_has_read = 0;
	}
}

Response::Response(Request *r) : 
		request(r), data(nullptr), body_has_written(0), length_has_written(0) {
	memset(length_buf, 0, 4);
}

Response::~Response() {
	if (data)
		delete[] data;
}

void Response::write_data(int handle, Server* server) {
	int write_num, has_remain;
	if (length_has_written < 4) {
		has_remain = 4 - length_has_written;
		write_num = write(handle, length_buf + length_has_written, has_remain);
		if (write_num < 0) {
			if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
				return;
			/* network fail, maybe peer abort, like as EPIPE */
			perror("write<0");
			server->close_connection(handle);
			server->epoll_delete_socket(handle);
			server->clear_handle(handle);
			if (server->server_events.onPeerReset)
				server->server_events.onPeerReset(handle);
			return;
		}
		length_has_written += write_num;
		if (has_remain != write_num)
			return;
	}
	has_remain = get_length() - body_has_written;
	write_num = write(handle, data + body_has_written, has_remain);
	if (write_num < 0) {
		if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
			return;
		perror("write<0");
		server->close_connection(handle);
		server->epoll_delete_socket(handle);
		server->clear_handle(handle);
		if (server->server_events.onPeerReset)
			server->server_events.onPeerReset(handle);
		return;
	}
	body_has_written += write_num;
	if (write_num == has_remain) {
		/* send complete */
		server->sending_map.erase(handle);
		delete this->request;
		Request *request = server->move_sending_request(handle);
		if (request != nullptr) {
			request->response->write_data(handle, server);
		}
	}
}

bool volatile Server::exit_flag = false;

Server::Server() : listen_address("0.0.0.0") {
	work_thread_count = sysconf(_SC_NPROCESSORS_ONLN) * 2;
}

void Server::thread_process() {
	Request *request;

	while (!exit_flag) {
		do {
			std::unique_lock<std::mutex> in_lock(pending_list.mutex);
			pending_list.cond.wait(in_lock, [this] {
				return !(this->pending_list.list.empty()) || exit_flag;
			});
			if (exit_flag)
				return;
			request = pending_list.list.front();
			pending_list.list.pop_front();

			mature_list.lock();
			mature_list.list.push_back(request);
			mature_list.unlock();
		} while (false);

		request->response = new Response(request);
		assert(request->response);
		if (server_events.onRequest) {
			server_events.onRequest(request);
		} else {
			default_request_process(request);
		}
	}
}

void Server::set_non_blocking(int sock) {
	int opts;
	opts = fcntl(sock, F_GETFL);
	if (opts < 0) {
		perror("fcntl/GETFL");
		exit(1);
	}
	opts = opts | O_NONBLOCK;
	if (fcntl(sock, F_SETFL, opts) < 0) {
		perror("fcntl/SETFL");
		exit(2);
	}
}

/*
 * create premuture entry for specific handle and one request
 * should always keep premuture has a request entry until handle is closed
*/
void Server::create_premature_entry(int handle) {
	assert(premature_map.find(handle) == premature_map.end());
	Request* request = new Request(this, handle);
	assert(request);
	premature_map[handle] = request;
}

Request* Server::get_handle_request(int handle) {
	assert(premature_map.find(handle) != premature_map.end());
	return premature_map[handle];
}

Response* Server::get_handle_response(int handle) {
	Request *request;
	std::unordered_map<int,Request*>::iterator it = sending_map.find(handle);
	if (it == sending_map.end()) {
		request = move_sending_request(handle);
		if (request) {
			return request->response;
		}
		return nullptr;
	}
	request = it->second;
	return request->response;
}

/*
 * called from net thread, release or mark specific handle request in lists or maps
*/
void Server::clear_handle(int handle) {
	/* clear handle entry from premature_map */
	assert(premature_map.find(handle) != premature_map.end());
	Request *request = premature_map[handle];
	delete request;
	premature_map.erase(handle);

	/* clear handle in pending list yet not processing in work thread */
	pending_list.lock();
	std::list<Request*>::iterator it = pending_list.list.begin();
	while (it != pending_list.list.end()) {
		if ((*it)->handle == handle) {
			it = pending_list.list.erase(it);
			delete (*it);
		} else 
			++it;
	}
	pending_list.unlock();

	/* mark discard in mature list */
	mature_list.lock();
	it = mature_list.list.begin();
	while (it != mature_list.list.end()) {
		if ((*it)->handle == handle) {
			if ((*it)->matured) {
				/* delete immediate if mature */
				delete (*it);
				it = mature_list.list.erase(it);
			} else {
				/* mark discard at not mature request, will be delete 
					on sending list picking 
				*/
				(*it)->discard = true;
				++it;
			}
		} else {
			++it;
		}
	}
	mature_list.unlock();
}

/*
 * called from net thread, at EPOLLINT calling, 
 * after receive one complete request,
 * that request will be moved to pending list for work thread to pick up,
 * and premature map will create a new empty request for the handle
*/
void Server::move_premature_request(int handle) {
	assert(premature_map.find(handle) != premature_map.end());
	Request* request = premature_map[handle];

	pending_list.lock();
	pending_list.list.push_back(request);
	pending_list.unlock();
	
	request = new Request(this, handle);
	assert(request);
	premature_map[handle] = request;
}

/*
 * called from net thread, at EPOLLOUT calling
 * after complete sending previous response, it check mature list,
 * 1, skip un-matured request
 * 2, delete matured + discard request
 * 3, move one of specific handle request to sending_map from mature list
 * return nullptr indicate no eligible request to move
*/
Request* Server::move_sending_request(int handle) {
	Request *request;
	assert(sending_map.find(handle) == sending_map.end());
	
	mature_list.lock();
	std::list<Request*>::iterator it = mature_list.list.begin();
	while (it != mature_list.list.end()) {
		request = *it;
		if (!request->matured) {
			++it;
			continue;
		}
		if (request->discard) {
			it = mature_list.list.erase(it);
			delete request;
			continue;
		}
		if (request->handle != handle) {
			++it;
			continue;
		}
		mature_list.list.erase(it);
		sending_map[handle] = request;
		break;
	}
	if (it == mature_list.list.end())
		request = nullptr;
	mature_list.unlock();
	return request;
}

void Server::notify_working() {
	pending_list.cond.notify_one();
}

/*
 * called from net thread, close connection
*/
void Server::close_connection(int handle) {
	::close(handle);
}

/*
 * called from net thread, epoll add
*/
void Server::epoll_add_socket(int sock, int op) {
	struct epoll_event ev;
	ev.data.fd = sock;
	ev.events = op;
	::epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock, &ev);
}

/*
 * called from net thread, epoll delete
*/
void Server::epoll_delete_socket(int sock) {
	::epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sock, NULL);
}

/*
 * called from net thread or work thread, epoll modify
*/
void Server::epoll_modify_socket(int sock, int op) {
	struct epoll_event ev;
	ev.data.fd = sock;
	ev.events = op;
	::epoll_ctl(epoll_fd, EPOLL_CTL_MOD, sock, &ev);
}

/*
 * default_request_process, called from work thread
*/
void Server::default_request_process(Request* request) {
	char null_string = 0;
	request->clone_response(1, &null_string);
	request->end_response();
}

void Server::prepare_exit() {
	exit_flag = true;
}

/* release remain free all remain handle and request before server exit */
void Server::release_remain() {
	std::for_each(premature_map.begin(), premature_map.end(), [](std::pair<int,Request*> p) {
		/* close handle and release request */
		::close(p.first);	
		delete p.second;
	});
	std::for_each(sending_map.begin(), sending_map.end(), [](std::pair<int,Request*> p) {
		/* don't need to close handle, because premature should close all handle */
		delete p.second;
	});
	std::for_each(pending_list.list.begin(), pending_list.list.end(), [](Request* r) {
		delete r;
	});
	std::for_each(mature_list.list.begin(), mature_list.list.end(), [](Request* r) {
		delete r;
	});
}

static void server_interrupt(int) {
	Server::prepare_exit();
}

int Server::ready(int listen_port, const ServerEvents& on_events) {
	signal(SIGPIPE, SIG_IGN);
	signal(SIGINT, server_interrupt);
	signal(SIGQUIT, server_interrupt);
	server_events = on_events;

	if (on_events.onInit && !on_events.onInit()) {
		return 1;
	}
	int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	int on = 1;
	setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

	epoll_fd = epoll_create1(EPOLL_CLOEXEC);

	struct epoll_event ev;
	ev.data.fd = listen_fd;
	ev.events = EPOLLIN;
	epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev);

	bzero(&server_address, sizeof(server_address));
	server_address.sin_family = AF_INET;
	server_address.sin_addr.s_addr = inet_addr(listen_address.c_str());
	server_address.sin_port = htons(listen_port);

	if (-1 == bind(listen_fd, (sockaddr*)&server_address, sizeof(server_address))) {
		perror("bind");
		exit(1);
	}
	if (-1 == listen(listen_fd, LISTENQ)) {
		perror("listen");
		exit(1);
	}

	std::thread* t;
	for (int i = 0; i < work_thread_count; i++) {
		t = new std::thread(std::bind(&Server::thread_process, this));
		threads.push_back(t);
	}

	while (!exit_flag) {
		int nfds = epoll_wait(epoll_fd, events, EVENTSIZE, 300);

		for (int i = 0; i < nfds; i++) {
			if (events[i].data.fd == listen_fd) {
				/* connect request */
				struct sockaddr_in client_address;
				socklen_t clilen = sizeof(struct sockaddr);
				int connect_fd = accept(listen_fd, 
								(struct sockaddr*)&client_address,
								&clilen);
				if (connect_fd < 0) {
					perror("connect_fd");
					continue;
				}
				set_non_blocking(connect_fd);
				const char* client_ip = inet_ntoa(client_address.sin_addr);
				if (server_events.onConnected && 
						!server_events.onConnected(connect_fd, client_ip)) {
					close_connection(connect_fd);
					continue;
				}
				/* prepare for new request receive */
				create_premature_entry(connect_fd);
				epoll_add_socket(connect_fd, EPOLLIN | EPOLLOUT | EPOLLET);
			} else if (events[i].events & (EPOLLERR | EPOLLHUP)) {
				/* encounter error */
				int handle = events[i].data.fd;
				close_connection(handle);
				epoll_delete_socket(handle);
				clear_handle(handle);
			} else if (events[i].events & EPOLLIN) {
				/* request data incoming, must receive data until EAGAIN or ERROR
					because we use ET mode
				 */
				int handle = events[i].data.fd;
				assert(handle >= 0);
			again:
				int num_read = read(handle, buffer, BUFFERSIZE);
				if (num_read < 0) {
					if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
						continue;
					}
					/* maybe errno == ECONNREST ... */
					perror("read<0");
					close_connection(handle);
					epoll_delete_socket(handle);
					clear_handle(handle);
					if (server_events.onPeerReset)
						server_events.onPeerReset(handle);
					continue;
				} else if (num_read == 0) {
					close_connection(handle);
					epoll_delete_socket(handle);
					clear_handle(handle);
					if (server_events.onPeerClosed)
						server_events.onPeerClosed(handle);
					continue;
				}
				/* have valid data, fill the unmature request */
				Request* request = get_handle_request(handle);
				request->append_data(buffer, num_read, handle, this);
				goto again;
			} else if (events[i].events & EPOLLOUT) {
				/* active sending out response, must write out until EAGAIN or ERROR
					because ET mode, when one response complete, it will go through
					mature list to pick a new one.
					If fail to get one, the EPOLLOUT will be active again by the time 
					request->end_response called 
				*/
				int handle = events[i].data.fd;
				assert(handle >= 0);
				Response *response = get_handle_response(handle); 
				if (response == nullptr)
					continue;
				response->write_data(handle, this);
			}
		}
	}
	pending_list.cond.notify_all();
	std::for_each(threads.begin(), threads.end(), [](std::thread* th) {
		th->join();
		delete th;
	});
	release_remain();
	if (on_events.onEnd)
		on_events.onEnd();
	return 0;
}

