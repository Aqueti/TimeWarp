/** @file
	@brief Client and threaded server socket code to control a time offset.

	@copyright 2019 Aqueti

	@author ReliaSolve, working for Aqueti.
*/

#pragma once
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace atl { namespace TimeWarp {

	/// @brief Type definition for a TimeWarpServer callback function.
	/// @param [in] userData A (possibly Null) user-data pointer that was
	///             passed in when the callback was registered.
	/// @param [in] timeOffset The time offset to apply.  A negative value
	///             is in the past and a positive value is in the future.
	typedef void (*TimeWarpServerCallback)(void* userData, int64_t timeOffset);

	/// @brief Standard port for a TimeWarpServer
	static const uint16_t DefaultPort = 2984;

	class TimeWarpServer {
	public:

		/// @brief Constructor for a TimeWarpServer object.
		/// @param [in] port The port to listen to for connections on all interfaces.
		/// @param [in] callback Function to be called from a new thread when a
		///             time offset request is received from a connected client.
		/// @param [in] userData A (possibly-Null) pointer that is passed to the
		///             callback function when it is called.  This is useful for
		///             passing data that it will need to know to handle the request.
		/// @param [in] cardIP The string name of the IP address of the network
		///             card to use for the outgoing connection, empty string
		///             for "ANY".
		TimeWarpServer(TimeWarpServerCallback callback, void *userData,
			uint16_t port = DefaultPort, std::string cardIP = "");

		/// @brief Destructor for a TimeWarpServer object; stops all threads and connections.
		~TimeWarpServer();

		/// @brief Tells whether the object is doing okay.
		/// @return Empty vector if there have been no errors, descriptions of any
		///         errors if there have been any.
		std::vector<std::string> GetErrorMessages();

	protected:
		class TimeWarpServerPrivate;
		std::shared_ptr<TimeWarpServerPrivate> m_private;

		/// @brief Thread that will listen for incoming connections
		static void ListenThread(std::shared_ptr<TimeWarpServerPrivate> p);

		/// @brief Thread that will handle commands from an incoming connection
		static void AcceptThread(std::shared_ptr<TimeWarpServerPrivate> p, size_t i);
	};

	class TimeWarpClient {
	public:
		/// @brief Constructor for a TimeWarpClient object.
		/// @param [in] hostName The computer to connect to.
		/// @param [in] port The port to connect to.
		/// @param [in] cardIP The string name of the IP address of the network
		///             card to use for the outgoing connection, empty string
		///             for "ANY".
		TimeWarpClient(std::string hostName, uint16_t port = DefaultPort,
			std::string cardIP = "");

		/// @brief Destructor for a TimeWarpClient object; stops connection.
		~TimeWarpClient();

		/// @brief Send a new time offset to the connected server.
		/// @param [in] timeOffset New time offset; positive is in the future
		///             and negative is in the past.
		/// @return True on success, false on failure.  See GetErrorMessages()
		///         for details of the error(s) on failure.
		bool SetTimeOffset(int64_t timeOffset);

		/// @brief Tells whether the object is doing okay.
		/// @return Empty vector if there have been no errors, descriptions of any
		///         errors if there have been any.
		std::vector<std::string> GetErrorMessages();

	protected:
		class TimeWarpClientPrivate;
		std::unique_ptr<TimeWarpClientPrivate> m_private;
	};

}};

//=====================================================================================
// C API interface exported through the dynamic load library for the client side.

extern "C" {
	/// @brief Create a TimeWarpClient object.
	/// @param [in] hostName The computer to connect to.
	/// @param [in] port The port to connect to.  -1 for default.
	/// @param [in] cardIP The string name of the IP address of the network
	///             card to use for the outgoing connection, empty string "" for ANY.
	/// @return Pointer to the client object on success, NULL on failure.
	void* atl_TimeWarpClientCreate(char* hostName, int port, char* cardIP);

	/// @brief Set the offset on a TimeWarpClient object.
	/// @param [in] client A pointer returned by aqt_TimeWarpClientCreate().
	/// @param [in] timeOffset New time offset; positive is in the future
	///             and negative is in the past.
	/// @return True on success, false on failure.
	bool atl_TimeWarpClientSetTimeOffset(void* client, int64_t offset);

	/// @brief Destroy a TimeWarpClient object.
	/// @return True on success, false on failure.
	bool atl_TimeWarpClientDestroy(void* client);
}
