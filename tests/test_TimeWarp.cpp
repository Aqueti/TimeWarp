/** @file
	@brief Client and threaded server socket time offset test code.

	@copyright 2019 Aqueti

	@author ReliaSolve, working for Aqueti.
*/

#include <TimeWarp.hpp>
#include <iostream>
#include <thread>

struct STATE {
	volatile int64_t timeOffset = 0;
} g_state;

void CallbackHandler(void* userData, int64_t timeOffset)
{
	// Increment the count of messages received pointed to by userData
	if (userData == nullptr) {
		std::cerr << "Error: CallbackHandler() called with null userData" << std::endl;
		return;
	}
	static_cast<STATE*>(userData)->timeOffset = timeOffset;

	// Report the time update
	std::cout << "Got time update: " << timeOffset << std::endl;
}

int main(int argc, char* argv[])
{
	// Start a server listening on the default port and make sure it
	// is working.
	atl::TimeWarp::TimeWarpServer* svr =
		new atl::TimeWarp::TimeWarpServer(CallbackHandler, &g_state);
	std::vector<std::string> errs = svr->GetErrorMessages();
	if (errs.size()) {
		std::cerr << "Error(s) opening server:" << std::endl;
		for (size_t i = 0; i < errs.size(); i++) {
			std::cerr << "  " << errs[i] << std::endl;
		}
		return 1;
	}

	// Start a client to connect on the default port and make sure it is working.
	atl::TimeWarp::TimeWarpClient* cli = new atl::TimeWarp::TimeWarpClient("localhost");
	errs = cli->GetErrorMessages();
	if (errs.size()) {
		std::cerr << "Error(s) opening client:" << std::endl;
		for (size_t i = 0; i < errs.size(); i++) {
			std::cerr << "  " << errs[i] << std::endl;
		}
		return 2;
	}

	// Send a set of time adjustments to the server and wait for them to appear.
	// If they don't appear soon enough, then there is an error.
	for (int64_t to = -1000; to < 1000; to += 100) {
		if (!cli->SetTimeOffset(to)) {
			std::cerr << "Error(s) updating time to " << to << ":" << std::endl;
			for (size_t i = 0; i < errs.size(); i++) {
				std::cerr << "  " << errs[i] << std::endl;
			}
			return 3;
		}

		// Wait a bit and then see if the time is what we expect.
		std::this_thread::sleep_for(std::chrono::milliseconds(3));
		if (g_state.timeOffset != to) {
			std::cerr << "Time mismatch after update: "
				<< g_state.timeOffset << " != " << to << std::endl;
			return 4;
		}
	}

	// Done with all of our objects!
	delete cli;
	delete svr;
	std::cout << "Success!" << std::endl;
	return 0;
}
