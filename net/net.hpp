/*
 *  Flo's Open libRary (floor)
 *  Copyright (C) 2004 - 2013 Florian Ziesche
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#ifndef __FLOOR_NET_HPP__
#define __FLOOR_NET_HPP__

#include "core/platform.hpp"
#include "net/net_protocol.hpp"
#include "net/net_tcp.hpp"
#include "core/logger.hpp"
#include "threading/thread_base.hpp"

template <class protocol_policy> class net : public thread_base {
public:
	net();
	virtual ~net();
	
	virtual void run();
	virtual bool connect_to_server(const string& server_name,
								   const unsigned short int port,
								   const unsigned short int local_port = 65535);
	
	virtual bool is_received_data() const;
	virtual deque<vector<char>> get_and_clear_received_data();
	virtual void clear_received_data();
	
	// multi-packet send
	virtual void send_data(const vector<vector<char>>& packets_data);
	// single-packet send
	virtual void send_data(const vector<char>& packet_data);
	virtual void send_data(const string& packet_data);
	virtual void send_data(const char* packet_data, const size_t length);
	
	virtual boost::asio::ip::address get_local_address() const;
	virtual unsigned short int get_local_port() const;
	virtual boost::asio::ip::address get_remote_address() const;
	virtual unsigned short int get_remote_port() const;
	
	virtual const protocol_policy& get_protocol() const;
	virtual protocol_policy& get_protocol();
	
	virtual void set_max_packets_per_second(const size_t& packets_per_second);
	virtual const size_t& get_max_packets_per_second() const;
	
	virtual void invalidate();

protected:
	protocol_policy protocol;
	atomic<bool> connected { false };
	
	string last_packet_remains { "" }; // TODO: remove?
	size_t received_length { 0 };
	size_t packets_per_second { 0 };
	size_t last_packet_send { 0 };
	deque<vector<char>> receive_store;
	deque<vector<char>> send_store;
	
	static constexpr size_t packet_max_len { 16384 };
	array<char, packet_max_len> receive_data;
	virtual size_t receive_packet(char* data, const size_t max_len);
	virtual void send_packet(const char* data, const size_t len);
	virtual size_t process_packet(const string& data, const size_t max_len);
	
};

template <class protocol_policy> net<protocol_policy>::net() : thread_base(), protocol() {
	this->start(); // start thread
}

template <class protocol_policy> net<protocol_policy>::~net() {
}

template <class protocol_policy>
bool net<protocol_policy>::connect_to_server(const string& server_name,
											 const unsigned short int port,
											 const unsigned short int local_port floor_unused) {
	lock(); // we need to lock the net class, so run() isn't called while we're connecting
	
	try {
		if(!protocol.is_valid()) throw exception();
		
		// open socket to the server
		if(!protocol.open_socket(server_name, port)) throw exception();
		
		// connection created - data transfer is now possible
		connected = true;
	}
	catch(...) {
		log_error("failed to connect to server!");
		unlock();
		set_thread_should_finish(); // and quit ...
		return false;
	}
	
	unlock();
	return true;
}

template <class protocol_policy> void net<protocol_policy>::run() {
	if(!connected) return;
	
	// receive data - if possible
	size_t len = 0, used = 0;
	try {
		if(protocol.is_valid()) {
			if(protocol.ready()) {
				receive_data.fill(0);
				len = receive_packet(&receive_data[0], packet_max_len);
				if(len == 0 || len > packet_max_len) {
					// failure, kill this object/thread
					throw exception();
				}
				else {
					string data(receive_data.data());
					if(last_packet_remains.length() > 0) {
						data = last_packet_remains + data;
						len += last_packet_remains.length();
						last_packet_remains = "";
					}
					
					used = process_packet(data, len);
					
					len -= used;
					if(used == 0 || len > 0) {
						last_packet_remains = data.substr(used, len);
					}
				}
			}
		}
		else throw runtime_error("invalid protocol");
	}
	catch(exception& e) {
		log_error("net error: %s", e.what());
		set_thread_should_finish();
		return;
	}
	catch(...) {
		// something is wrong, finsh and return
		log_error("unknown net error, exiting ...");
		set_thread_should_finish();
		return;
	}
	
	// send data - if possible
	if(!send_store.empty()) {
		if(packets_per_second != 0 && last_packet_send > (SDL_GetTicks() - 1000u)) {
			// wait
		}
		else {
			const auto send_begin = send_store.begin();
			auto send_end = send_store.end();
			if(packets_per_second != 0 && send_store.size() > packets_per_second) {
				send_end = send_begin + packets_per_second;
			}
			
			for(auto send_iter = send_begin; send_iter != send_end; send_iter++) {
				send_packet(send_iter->data(), send_iter->size());
			}
			if(packets_per_second != 0) last_packet_send = SDL_GetTicks();
			
			send_store.erase(send_begin, send_end);
		}
	}
}

template <class protocol_policy> size_t net<protocol_policy>::receive_packet(char* data, const size_t max_len) {
	if(!protocol.is_valid()) {
		log_error("invalid protocol and/or sockets!");
		return -1;
	}
	
	// receive the package
	size_t len = protocol.receive(data, max_len);
	// received packet length is equal or less than zero, return -1
	if(len == 0 || len > packet_max_len) {
		log_error("invalid data received!");
		return -1;
	}
	
	return len;
}

template <class protocol_policy> size_t net<protocol_policy>::process_packet(const string& data, const size_t max_len floor_unused) {
	size_t old_pos = 0, pos = 0;
	size_t lb_offset = 1;
	const size_t len = data.length();
	for(;;) {
		// handle \n and \r\n newlines
		if((pos = data.find("\r", old_pos)) == string::npos) {
			if((pos = data.find("\n", old_pos)) == string::npos) {
				break;
			}
			else lb_offset = 1;
		}
		else {
			if(pos+1 >= len) {
				// \n not received yet
				break;
			}
			if(data[pos+1] != '\n') {
				// \r must be followed by \n!
				break;
			}
			pos++;
			lb_offset = 2;
		}
		
		const auto recv_data = data.substr(old_pos, pos - old_pos - lb_offset + 1);
		receive_store.emplace_back(begin(recv_data), end(recv_data));
		old_pos = pos + 1;
	}
	
	received_length += old_pos;
	return old_pos;
}

template <class protocol_policy> void net<protocol_policy>::send_packet(const char* data, const size_t len) {
	// TODO: add to send store instead
	if(!protocol.send(data, len)) {
		log_error("couldn't send packet!");
	}
}

template <class protocol_policy> bool net<protocol_policy>::is_received_data() const {
	return !receive_store.empty();
}

template <class protocol_policy> deque<vector<char>> net<protocol_policy>::get_and_clear_received_data() {
	decltype(receive_store) ret;
	this->lock();
	receive_store.swap(ret);
	this->unlock();
	return ret;
}

template <class protocol_policy> void net<protocol_policy>::clear_received_data() {
	receive_store.clear();
}

template <class protocol_policy> void net<protocol_policy>::send_data(const vector<vector<char>>& packets_data) {
	this->lock();
	send_store.insert(end(send_store), cbegin(packets_data), cend(packets_data));
	this->unlock();
}

template <class protocol_policy> void net<protocol_policy>::send_data(const vector<char>& packet_data) {
	this->lock();
	send_store.emplace_back(cbegin(packet_data), cend(packet_data));
	this->unlock();
}

template <class protocol_policy> void net<protocol_policy>::send_data(const string& packet_data) {
	this->lock();
	send_store.emplace_back(cbegin(packet_data), cend(packet_data));
	this->unlock();
}

template <class protocol_policy> void net<protocol_policy>::send_data(const char* packet_data, const size_t length) {
	this->lock();
	send_store.emplace_back(packet_data, packet_data + length);
	this->unlock();
}

template <class protocol_policy> boost::asio::ip::address net<protocol_policy>::get_local_address() const {
	return protocol.get_local_address();
}
template <class protocol_policy> unsigned short int net<protocol_policy>::get_local_port() const {
	return protocol.get_local_port();
}

template <class protocol_policy> boost::asio::ip::address net<protocol_policy>::get_remote_address() const {
	return protocol.get_remote_address();
}
template <class protocol_policy> unsigned short int net<protocol_policy>::get_remote_port() const {
	return protocol.get_remote_port();
}

template <class protocol_policy> const protocol_policy& net<protocol_policy>::get_protocol() const {
	return protocol;
}

template <class protocol_policy> protocol_policy& net<protocol_policy>::get_protocol() {
	return protocol;
}

template <class protocol_policy> void net<protocol_policy>::set_max_packets_per_second(const size_t& packets_per_second_) {
	packets_per_second = packets_per_second_;
}

template <class protocol_policy> const size_t& net<protocol_policy>::get_max_packets_per_second() const {
	return packets_per_second;
}

template <class protocol_policy> void net<protocol_policy>::invalidate() {
	protocol.invalidate();
}

#endif
