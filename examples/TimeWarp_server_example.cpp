/** @file
	@brief Threaded server socket time offset example.

	@copyright 2019 Aqueti

	@author ReliaSolve, working for Aqueti.
*/

#include <TimeWarp.hpp>
#include <iostream>
#include <thread>

void CallbackHandler(void* userData, int64_t timeOffset)
{
	// Report the time update
	std::cout << "Got time update: " << timeOffset << std::endl;
}

int main(int argc, char* argv[])
{
	// Start a server listening on the default port and make sure it
	// is working.
	atl::TimeWarp::TimeWarpServer* svr =
		new atl::TimeWarp::TimeWarpServer(CallbackHandler, nullptr);
	std::vector<std::string> errs = svr->GetErrorMessages();
	if (errs.size()) {
		std::cerr << "Error(s) opening server:" << std::endl;
		for (size_t i = 0; i < errs.size(); i++) {
			std::cerr << "  " << errs[i] << std::endl;
		}
		return 1;
	}

	// Wait forever; the callback handler will be called by the server
	// thread when it receives a message from any connection.
	while (true) {};

	// Done with all of our objects!
	delete svr;
	std::cout << "Success!" << std::endl;
	return 0;
}
