/** @file
	@brief Client and threaded server socket code to control a time offset.

	@copyright 2019 Aqueti

	@author ReliaSolve, working for Aqueti.
*/

#include "TimeWarp.hpp"
#include "TimeWarpSockets.hpp"

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

	friend class TimeWarpClient;
};

std::vector<std::string> TimeWarpClient::GetErrorMessages()
{
	if (m_private) {
		return m_private->m_errors;
	}
	std::vector<std::string> errs = { "NULL private pointer in call to GetErrorMessages" };
	return errs;
}
