/** @file
	@brief Client time offset example code.

	@copyright 2019 Aqueti

	@author ReliaSolve, working for Aqueti.
*/

#include <TimeWarp.hpp>
#include <iostream>
#include <thread>

int main(int argc, char* argv[])
{
	// Start a client to connect on the default port and make sure it is working.
	atl::TimeWarp::TimeWarpClient* cli = new atl::TimeWarp::TimeWarpClient("localhost");
	std::vector<std::string> errs = cli->GetErrorMessages();
	if (errs.size()) {
		std::cerr << "Error(s) opening client:" << std::endl;
		for (size_t i = 0; i < errs.size(); i++) {
			std::cerr << "  " << errs[i] << std::endl;
		}
		return 2;
	}

	// Send a set of time adjustments to the server, waiting in between sends
	for (int64_t to = -1000; to <= 1000; to += 100) {
		if (!cli->SetTimeOffset(to)) {
			std::cerr << "Error(s) updating time to " << to << ":" << std::endl;
			for (size_t i = 0; i < errs.size(); i++) {
				std::cerr << "  " << errs[i] << std::endl;
			}
			return 3;
		}

		// Wait a bit between sends.
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	// Done with all of our objects!
	delete cli;
	std::cout << "Success!" << std::endl;
	return 0;
}
