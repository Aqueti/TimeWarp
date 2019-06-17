/** @file
	@brief Client and threaded server socket code to control a time offset.

	@copyright 2019 Aqueti

	@author ReliaSolve, working for Aqueti.
*/

#include "TimeWarp.hpp"
#include <Sockets.hpp>
#include <mutex>
#include <thread>
#include <map>
#include <memory>
#include <atomic>
#include <string.h>

// Versioned magic-cookie string to send and receive at connection initialization.
static std::string MagicCookie = "aqt::TimeWarp::Connection v01.00.00";

// Op codes for commands between the client and server
static const int64_t OP_SET_TIME = 1;

using namespace atl::TimeWarp;

class atl::TimeWarp::TimeWarpServer::TimeWarpServerPrivate {
public:
	// Mutex for all subthreads to use to avoid race conditions when
	// accessing data structures.
	std::mutex					m_mutex;

	TimeWarpServerCallback		m_callback = nullptr;
	void*						m_userData = nullptr;
	std::vector<std::string>	m_errors;

	SOCKET						m_listen = INVALID_SOCKET;
	std::thread					m_listenThread;

	// This structure keeps track of threads and the sockets that they should
	// be listening on.  Each thread is responsible for closing its own socket
	// before it exits.  There is a map from std::size to the infos to make it
	// easy for a thread to look up its entry and still allow each deletion
	// without changing the placement (as would happen in a vector).
	struct AcceptInfo {
		AcceptInfo(std::shared_ptr<std::thread> t, SOCKET s) : m_thread(t), m_sock(s), m_done(false) {};
		std::shared_ptr<std::thread>	m_thread;
		SOCKET							m_sock;
		std::atomic<bool>				m_done;
	};
	std::map<size_t, std::shared_ptr<AcceptInfo> > m_acceptThreads;
	size_t				m_nextMapEntry = 0;

	volatile bool				m_quit = false;		///< Time to shut down?
};

TimeWarpServer::TimeWarpServer(TimeWarpServerCallback callback, void* userData,
	uint16_t port, std::string cardIP)
{
	m_private.reset(new TimeWarpServerPrivate());

	// Check and store the paramters
	if (!callback) {
		m_private->m_errors.push_back("Null callback handler passed to constructor");
		return;
	}
	m_private->m_callback = callback;
	m_private->m_userData = userData;

	// Open the socket that we're going to listen on for new connections.
	const char* cardIPChar = nullptr;
	if (cardIP.size() > 0) {
		cardIPChar = cardIP.c_str();
	}
	m_private->m_listen = Sockets::open_tcp_socket(&port, cardIPChar);
	if (m_private->m_listen == INVALID_SOCKET) {
		m_private->m_errors.push_back("Could not open socket " + std::to_string(port) +
			" for listening");
		return;
	}
	if (listen(m_private->m_listen, 1)) {
		m_private->m_errors.push_back("get_a_TCP_socket: listen() failed.");
		Sockets::close_socket(m_private->m_listen);
		m_private->m_listen = INVALID_SOCKET;
		return;
	}

	// Start a thread to accept connections on the listening socket.
	m_private->m_listenThread = std::thread(ListenThread, m_private);
}

TimeWarpServer::~TimeWarpServer()
{
	// Tell all of our sub-threads it is time to quit.
	m_private->m_quit = true;

	// Wait for the listening thread to quit, which will have waited for
	// all of the accepting threads to have quit.
	m_private->m_listenThread.join();
}

/* Static */
void TimeWarpServer::ListenThread(std::shared_ptr<TimeWarpServerPrivate> p)
{
	if (!p) { return; }

	// Keep listening for connections.  When we get one, add it to the list.
	while (!p->m_quit) {
		SOCKET acceptSock;
		int ret = Sockets::poll_for_accept(p->m_listen, &acceptSock, 0.01);
		switch (ret) {
			case 0:
				break;
			case 1:
				{	std::lock_guard<std::mutex> lock(p->m_mutex);
					p->m_acceptThreads[p->m_nextMapEntry] =
						std::make_shared<TimeWarpServerPrivate::AcceptInfo>(
							nullptr,
							acceptSock);
					// Start the thread only after the map entry is made to avoid
					// having the thread running before its data is available.
					p->m_acceptThreads[p->m_nextMapEntry]->m_thread =
						std::make_shared<std::thread>(AcceptThread, p, p->m_nextMapEntry);
					p->m_nextMapEntry++;
				}
				break;

			default:
				{	std::lock_guard<std::mutex> lock(p->m_mutex);
					p->m_errors.push_back("Failure listenting on socket");
				}
				break;
		}

		// If any of the accept threads have completed, remove them from the map.
		auto i = p->m_acceptThreads.begin();
		{
			std::lock_guard<std::mutex> lock(p->m_mutex);
			while (i != p->m_acceptThreads.end()) {
				if (i->second->m_done) {
					i->second->m_thread->join();
					// Delete the entry from the map and advance to the next entry
					auto victim = i;
					i++;
					p->m_acceptThreads.erase(victim);
				}
				else {
					i++;
				}
			}
		}
	}

	// Wait for all of the accept threads to quit and remove them from the map.
	while (p->m_acceptThreads.size()) {
		p->m_acceptThreads.begin()->second->m_thread->join();
		p->m_acceptThreads.erase(p->m_acceptThreads.begin());
	}
}

void TimeWarpServer::AcceptThread(std::shared_ptr<TimeWarpServerPrivate> p, size_t i)
{
	if (!p) { return; }
	std::shared_ptr<TimeWarpServerPrivate::AcceptInfo> info = p->m_acceptThreads[i];

	{
		// Try to send the magic cookie, telling the client our version.
		size_t len = MagicCookie.size();
		if (len != Sockets::noint_block_write(info->m_sock, MagicCookie.c_str(), len)) {
			std::lock_guard<std::mutex> lock(p->m_mutex);
			p->m_errors.push_back("Could not write magic cookie");
			Sockets::close_socket(info->m_sock);
			info->m_sock = INVALID_SOCKET;
			info->m_done = true;
			return;
		}

		// Try to read the magic cookie from the client and see if it matches what
		// we're expecting.  Time out if we don't hear back within half a second.
		std::vector<char> cookie(len);
		struct timeval timeout = { 0, 500000 };
		if (len != Sockets::noint_block_read_timeout(info->m_sock, cookie.data(), len, &timeout)) {
			std::lock_guard<std::mutex> lock(p->m_mutex);
			p->m_errors.push_back("Could not read magic cookie");
			Sockets::close_socket(info->m_sock);
			info->m_sock = INVALID_SOCKET;
			info->m_done = true;
			return;
		}
		if (0 != memcmp(cookie.data(), MagicCookie.c_str(), len)) {
			std::lock_guard<std::mutex> lock(p->m_mutex);
			p->m_errors.push_back("Bad magic cookie from server");
			Sockets::close_socket(info->m_sock);
			info->m_sock = INVALID_SOCKET;
			info->m_done = true;
			return;
		}
	}

	// Keep reading until it is time to quit or we get an error.
	size_t numRead = 0;
	int64_t opLocal;
	int64_t offLocal;
	size_t len = sizeof(opLocal) + sizeof(offLocal);
	std::vector<char> buffer(len);
	while (!p->m_quit) {
		// Poll to see if we can read another request until we get one or get an error.
		struct timeval timeout = { 1, 1000 };
		int got = Sockets::noint_block_read_timeout(info->m_sock, &(buffer.data()[numRead]),
			len - numRead, &timeout);

		// If it was an error, we're done.  This is not a global error, just a closed connection.
		if (got == -1) {
			break;
		}

		// If we got a complete report, handle it and reset for the next one
		// Otherwise, we just go around and read some more.
		if (numRead + got == len) {
			std::lock_guard<std::mutex> lock(p->m_mutex);
			opLocal = Sockets::ntoh(*reinterpret_cast<int64_t*>(&buffer.data()[0]));
			offLocal = Sockets::ntoh(*reinterpret_cast<int64_t*>(&buffer.data()[sizeof(opLocal)]));
			p->m_callback(p->m_userData, offLocal);
			numRead = 0;
		}
	}
	
	// Close my socket before quitting
	Sockets::close_socket(info->m_sock);
	info->m_done = true;
}

std::vector<std::string> TimeWarpServer::GetErrorMessages()
{
	if (m_private) {
		return m_private->m_errors;
	}
	std::vector<std::string> errs = { "NULL private pointer in call to GetErrorMessages" };
	return errs;
}

class atl::TimeWarp::TimeWarpClient::TimeWarpClientPrivate {
public:
	std::vector<std::string> m_errors;
	SOCKET m_socket = INVALID_SOCKET;
};

TimeWarpClient::TimeWarpClient(std::string hostName, uint16_t port, std::string cardIP)
{
	m_private.reset(new TimeWarpClientPrivate());

	// Connect to the requested socket.
	const char* nicName = nullptr;
	if (cardIP.size() > 0) { nicName = cardIP.c_str(); }
	if (!Sockets::connect_tcp_to(hostName.c_str(), port, nicName, &m_private->m_socket)) {
		m_private->m_errors.push_back("Could not connect to requested TCP port");
		return;
	}

	// Try to send the magic cookie, telling the server our version.
	size_t len = MagicCookie.size();
	if (len != Sockets::noint_block_write(m_private->m_socket, MagicCookie.c_str(), len)) {
		m_private->m_errors.push_back("Could not write magic cookie");
		Sockets::close_socket(m_private->m_socket);
		m_private->m_socket = INVALID_SOCKET;
		return;
	}

	// Try to read the magic cookie from the server and see if it matches what
	// we're expecting.  Time out if we don't hear back within half a second.
	std::vector<char> cookie(len);
	struct timeval timeout = { 0, 500000 };
	int ret;
	if (len != (ret = Sockets::noint_block_read_timeout(m_private->m_socket, cookie.data(), len, &timeout))) {
		m_private->m_errors.push_back("Could not read magic cookie");
		Sockets::close_socket(m_private->m_socket);
		m_private->m_socket = INVALID_SOCKET;
		return;
	}
	if (0 != memcmp(cookie.data(), MagicCookie.c_str(), len)) {
		m_private->m_errors.push_back("Bad magic cookie from server");
		Sockets::close_socket(m_private->m_socket);
		m_private->m_socket = INVALID_SOCKET;
		return;
	}
}

TimeWarpClient::~TimeWarpClient()
{
	if (m_private->m_socket != INVALID_SOCKET) {
		Sockets::close_socket(m_private->m_socket);
	}
	m_private.reset();
}

bool TimeWarpClient::SetTimeOffset(int64_t timeOffset)
{
	if (!m_private) {
		return false;
	}
	if (m_private->m_socket == INVALID_SOCKET) {
		m_private->m_errors.push_back("Attempted to set time on unconnected object");
		return false;
	}

	// Pack a 64-bit op-code to set the time offset followed by
	// the 64-bit time offset into a buffer and send it.
	int64_t opNet = Sockets::hton(OP_SET_TIME);
	int64_t offNet = Sockets::hton(timeOffset);
	size_t len = sizeof(opNet) + sizeof(offNet);
	std::vector<char> buffer(len);
	memcpy(&buffer[0], &opNet, sizeof(int64_t));
	memcpy(&buffer[sizeof(opNet)], &offNet, sizeof(int64_t));

	// Send the command
	if (len != Sockets::noint_block_write(m_private->m_socket, buffer.data(), len)) {
		m_private->m_errors.push_back("Could not send command on socket");
		return false;
	}

	return true;
}

std::vector<std::string> TimeWarpClient::GetErrorMessages()
{
	if (m_private) {
		return m_private->m_errors;
	}
	std::vector<std::string> errs = { "NULL private pointer in call to GetErrorMessages" };
	return errs;
}


void* atl_TimeWarpClientCreate(char* hostName, int port, char* cardIP)
{
	if (port == -1) { port = DefaultPort; }
	TimeWarpClient* client = new TimeWarpClient(hostName, static_cast<uint16_t>(port), cardIP);
	if (client->GetErrorMessages().size()) {
		delete client;
		return nullptr;
	}
	return client;
}

bool atl_TimeWarpClientSetTimeOffset(void* client, int64_t offset)
{
	if (!client) {
		return false;
	}
	TimeWarpClient* me = static_cast<TimeWarpClient*>(client);
	me->SetTimeOffset(offset);
	if (me->GetErrorMessages().size()) {
		return false;
	}
	return true;
}

bool atl_TimeWarpClientDestroy(void* client)
{
	if (!client) {
		return false;
	}
	TimeWarpClient* me = static_cast<TimeWarpClient*>(client);
	delete me;
	return true;
}
