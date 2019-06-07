/** @file
	@brief Client and threaded server socket code to control a time offset.

	@copyright 2019 Aqueti

	@author ReliaSolve, working for Aqueti.
*/

#include "TimeWarp.hpp"
#include "TimeWarpSockets.hpp"

// Versioned magic-cookie string to send and receive at connection initialization.
static std::string MagicCookie = "aqt::TimeWarp::Connection v01.00.00";

// Op codes for commands between the client and server
static const int64_t OP_SET_TIME = 1;

using namespace atl::TimeWarp;

class atl::TimeWarp::TimeWarpServer::TimeWarpServerPrivate {
public:
	std::vector<std::string> m_errors;

	friend class TimeWarpServer;
};

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
	struct timeval timeout = { 0, 5000000 };
	if (len != Sockets::noint_block_read_timeout(m_private->m_socket, cookie.data(), len, &timeout)) {
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
	size_t len = sizeof(double) + sizeof(int64_t);
	std::vector<char> buffer(len);
	int64_t opNet = Sockets::hton(OP_SET_TIME);
	int64_t offNet = Sockets::hton(timeOffset);
	memcpy(&buffer[0], &opNet, sizeof(int64_t));
	memcpy(&buffer[4], &offNet, sizeof(int64_t));

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
