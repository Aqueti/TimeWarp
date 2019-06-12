#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <string>
#include <string.h>
#include <system_error>
#include <math.h>
#include "TimeWarpSockets.hpp"

//--------------------------------------------------------------
// Ensures that someone calls WSAStartup on Windows before using
// any socket code.
#if defined(TW_USE_WINSOCK_SOCKETS)
class WSAStart {
public:
	WSAStart() {
		WSADATA wsaData;
		int winStatus;

		winStatus = WSAStartup(MAKEWORD(1, 1), &wsaData);
		if (winStatus) {
			fprintf(stderr, "TimeWarpSockets: Failed to set up sockets.\n");
			fprintf(stderr, "WSAStartup failed with error code %d\n", winStatus);
		}
	}
};
static WSAStart startUp;
#endif

#if defined(TW_USE_WINSOCK_SOCKETS)
/* from HP-UX */
struct timezone {
	int tz_minuteswest; /* minutes west of Greenwich */
	int tz_dsttime;     /* type of dst correction */
};
#endif

//--------------------------------------------------------------
// gettimeofday() defines.  These are a bit hairy.  The basic problem is
// that Windows doesn't implement gettimeofday(), nor does it
// define "struct timezone", although Winsock.h does define
// "struct timeval".  The painful solution has been to define a
// TW_gettimeofday() function that takes a void * as a second
// argument (the timezone) and have all TimeWarp code call this function
// rather than gettimeofday().  On non-WINSOCK implementations,
// we alias TW_gettimeofday() right back to gettimeofday(), so
// that we are calling the system routine.  On Windows, we will
// be using TW_gettimofday().

#if (!defined(TW_USE_WINSOCK_SOCKETS))
// If we're using std::chrono, then we implement a new
// TW_gettimeofday() on top of it in a platform-independent
// manner.  Otherwise, we just use the system call.
#ifndef USE_STD_CHRONO
#define TW_gettimeofday gettimeofday
#else
int TW_gettimeofday(struct timeval* tp,
	void* tzp = NULL);
#endif
#else // winsock sockets

#include <chrono>
#include <ctime>

///////////////////////////////////////////////////////////////
// With Visual Studio 2013 64-bit, the hires clock produces a clock that has a
// tick interval of around 15.6 MILLIseconds, repeating the same
// time between them.
///////////////////////////////////////////////////////////////
// With Visual Studio 2015 64-bit, the hires clock produces a good, high-
// resolution clock with no blips.  However, its epoch seems to
// restart when the machine boots, whereas the system clock epoch
// starts at the standard midnight January 1, 1970.
///////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////
// Helper function to convert from the high-resolution clock
// time to the equivalent system clock time (assuming no clock
// adjustment on the system clock since program start).
//  To make this thread safe, we semaphore the determination of
// the offset to be applied.  To handle a slow-ticking system
// clock, we repeatedly sample it until we get a change.
//  This assumes that the high-resolution clock on different
// threads has the same epoch.
///////////////////////////////////////////////////////////////

static bool hr_offset_determined = false;
#include <mutex>
static std::mutex hr_offset_mutex;
static struct timeval hr_offset;

static struct timeval high_resolution_time_to_system_time(
	struct timeval hi_res_time //< Time computed from high-resolution clock
)
{
	// If we haven't yet determined the offset between the high-resolution
	// clock and the system clock, do so now.  Avoid a race between threads
	// using the semaphore and checking the boolean both before and after
	// grabbing the semaphore (in case someone beat us to it).
	if (!hr_offset_determined) {
		std::lock_guard<std::mutex> lock(hr_offset_mutex);
		// Someone else who had the semaphore may have beaten us to this.
		if (!hr_offset_determined) {
			// Watch the system clock until it changes; this will put us
			// at a tick boundary.  On many systems, this will change right
			// away, but on Windows 8 it will only tick every 16ms or so.
			std::chrono::system_clock::time_point pre =
				std::chrono::system_clock::now();
			std::chrono::system_clock::time_point post;
			// On Windows 8.1, this took from 1-16 ticks, and seemed to
			// get offsets to the epoch that were consistent to within
			// around 1ms.
			do {
				post = std::chrono::system_clock::now();
			} while (pre == post);

			// Now read the high-resolution timer to find out the time
			// equivalent to the post time on the system clock.
			std::chrono::high_resolution_clock::time_point high =
				std::chrono::high_resolution_clock::now();

			// Now convert both the hi-resolution clock time and the
			// post-tick system clock time into struct timevals and
			// store the difference between them as the offset.
			std::time_t high_secs =
				std::chrono::duration_cast<std::chrono::seconds>(
					high.time_since_epoch())
				.count();
			std::chrono::high_resolution_clock::time_point
				fractional_high_secs = high - std::chrono::seconds(high_secs);
			struct timeval high_time;
			high_time.tv_sec = static_cast<unsigned long>(high_secs);
			high_time.tv_usec = static_cast<unsigned long>(
				std::chrono::duration_cast<std::chrono::microseconds>(
					fractional_high_secs.time_since_epoch())
				.count());

			std::time_t post_secs =
				std::chrono::duration_cast<std::chrono::seconds>(
					post.time_since_epoch())
				.count();
			std::chrono::system_clock::time_point fractional_post_secs =
				post - std::chrono::seconds(post_secs);
			struct timeval post_time;
			post_time.tv_sec = static_cast<unsigned long>(post_secs);
			post_time.tv_usec = static_cast<unsigned long>(
				std::chrono::duration_cast<std::chrono::microseconds>(
					fractional_post_secs.time_since_epoch())
				.count());

			hr_offset = atl::TimeWarp::Sockets::TimevalDiff(post_time, high_time);

			// We've found our offset ... re-use it from here on.
			hr_offset_determined = true;
		}
	}

	// The offset has been determined, by us or someone else.  Apply it.
	return atl::TimeWarp::Sockets::TimevalSum(hi_res_time, hr_offset);
}

int TW_gettimeofday(timeval* tp, void* tzp)
{
	// If we have nothing to fill in, don't try.
	if (tp == NULL) {
		return 0;
	}
	struct timezone* timeZone = reinterpret_cast<struct timezone*>(tzp);

	// Find out the time, and how long it has been in seconds since the
	// epoch.
	std::chrono::high_resolution_clock::time_point now =
		std::chrono::high_resolution_clock::now();
	std::time_t secs =
		std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch())
		.count();

	// Subtract the time in seconds from the full time to get a
	// remainder that is a fraction of a second since the epoch.
	std::chrono::high_resolution_clock::time_point fractional_secs =
		now - std::chrono::seconds(secs);

	// Store the seconds and the fractional seconds as microseconds into
	// the timeval structure.  Then convert from the hi-res clock time
	// to system clock time.
	struct timeval hi_res_time;
	hi_res_time.tv_sec = static_cast<unsigned long>(secs);
	hi_res_time.tv_usec = static_cast<unsigned long>(
		std::chrono::duration_cast<std::chrono::microseconds>(
			fractional_secs.time_since_epoch())
		.count());
	*tp = high_resolution_time_to_system_time(hi_res_time);

	// @todo Fill in timezone structure with relevant info.
	if (timeZone != NULL) {
		timeZone->tz_minuteswest = 0;
		timeZone->tz_dsttime = 0;
	}

	return 0;
}

#endif

using namespace atl::TimeWarp::Sockets;

#ifdef TW_USE_WINSOCK_SOCKETS

// A socket in Windows can not be closed like it can in unix-land
#define closeSocket closesocket

// Socket errors don't set errno in Windows; they use their own
// custom error reporting methods.
#define socket_error WSAGetLastError()
static std::string WSA_number_to_string(int err)
{
	return std::system_category().message(err);
}
#define socket_error_to_chars(x) (WSA_number_to_string(x)).c_str()
#define TW_EINTR WSAEINTR

#else
#include <errno.h> // for errno, EINTR

#define closeSocket close

#define socket_error errno
#define socket_error_to_chars(x) strerror(x)
#define TW_EINTR EINTR

#include <arpa/inet.h>  // for inet_addr
#include <netinet/in.h> // for sockaddr_in, ntohl, in_addr, etc
#include <sys/socket.h> // for getsockname, send, AF_INET, etc
#include <unistd.h>     // for close, read, fork, etc
#ifdef _AIX
#define _USE_IRS
#endif
#include <netdb.h> // for hostent, gethostbyname, etc

#endif

#ifndef TW_USE_WINSOCK_SOCKETS
#include <sys/wait.h> // for waitpid, WNOHANG
#ifndef __CYGWIN__
#include <netinet/tcp.h> // for TCP_NODELAY
#endif                   /* __CYGWIN__ */
#endif                   /* TW_USE_WINSOCK_SOCKETS */

// cast fourth argument to setsockopt()
#ifdef TW_USE_WINSOCK_SOCKETS
#define SOCK_CAST (char *)
#else
#ifdef sparc
#define SOCK_CAST (const char *)
#else
#define SOCK_CAST
#endif
#endif

#if defined(_AIX) || defined(__APPLE__) || defined(ANDROID) || defined(__linux)
#define GSN_CAST (socklen_t *)
#else
#if defined(FreeBSD)
#define GSN_CAST (unsigned int *)
#else
#define GSN_CAST
#endif
#endif

//  NOT SUPPORTED ON SPARC_SOLARIS
//  gethostname() doesn't seem to want to link out of stdlib
#ifdef sparc
extern "C" {
	int gethostname(char*, int);
}
#endif

// perform normalization of a timeval
// XXX this still needs to be checked for errors if the timeval
// or the rate is negative
static inline void timevalNormalizeInPlace(timeval& in_tv)
{
	const long div_77777 = (in_tv.tv_usec / 1000000);
	in_tv.tv_sec += div_77777;
	in_tv.tv_usec -= (div_77777 * 1000000);
}
timeval atl::TimeWarp::Sockets::TimevalNormalize(const timeval& in_tv)
{
	timeval out_tv = in_tv;
	timevalNormalizeInPlace(out_tv);
	return out_tv;
}

// Calcs the sum of tv1 and tv2.  Returns the sum in a timeval struct.
// Calcs negative times properly, with the appropriate sign on both tv_sec
// and tv_usec (these signs will match unless one of them is 0).
// NOTE: both abs(tv_usec)'s must be < 1000000 (ie, normal timeval format)
timeval atl::TimeWarp::Sockets::TimevalSum(const timeval& tv1, const timeval& tv2)
{
	timeval tvSum = tv1;

	tvSum.tv_sec += tv2.tv_sec;
	tvSum.tv_usec += tv2.tv_usec;

	// do borrows, etc to get the time the way i want it: both signs the same,
	// and abs(usec) less than 1e6
	if (tvSum.tv_sec > 0) {
		if (tvSum.tv_usec < 0) {
			tvSum.tv_sec--;
			tvSum.tv_usec += 1000000;
		}
		else if (tvSum.tv_usec >= 1000000) {
			tvSum.tv_sec++;
			tvSum.tv_usec -= 1000000;
		}
	}
	else if (tvSum.tv_sec < 0) {
		if (tvSum.tv_usec > 0) {
			tvSum.tv_sec++;
			tvSum.tv_usec -= 1000000;
		}
		else if (tvSum.tv_usec <= -1000000) {
			tvSum.tv_sec--;
			tvSum.tv_usec += 1000000;
		}
	}
	else {
		// == 0, so just adjust usec
		if (tvSum.tv_usec >= 1000000) {
			tvSum.tv_sec++;
			tvSum.tv_usec -= 1000000;
		}
		else if (tvSum.tv_usec <= -1000000) {
			tvSum.tv_sec--;
			tvSum.tv_usec += 1000000;
		}
	}

	return tvSum;
}

// Calcs the diff between tv1 and tv2.  Returns the diff in a timeval struct.
// Calcs negative times properly, with the appropriate sign on both tv_sec
// and tv_usec (these signs will match unless one of them is 0)
timeval atl::TimeWarp::Sockets::TimevalDiff(const timeval& tv1, const timeval& tv2)
{
	timeval tv;

	tv.tv_sec = -tv2.tv_sec;
	tv.tv_usec = -tv2.tv_usec;

	return TimevalSum(tv1, tv);
}

timeval atl::TimeWarp::Sockets::TimevalScale(const timeval& tv, double scale)
{
	timeval result;
	result.tv_sec = (long)(tv.tv_sec * scale);
	result.tv_usec =
		(long)(tv.tv_usec * scale + fmod(tv.tv_sec * scale, 1.0) * 1000000.0);
	timevalNormalizeInPlace(result);
	return result;
}

// returns 1 if tv1 is greater than tv2;  0 otherwise
bool atl::TimeWarp::Sockets::TimevalGreater(const timeval& tv1, const timeval& tv2)
{
	if (tv1.tv_sec > tv2.tv_sec) return 1;
	if ((tv1.tv_sec == tv2.tv_sec) && (tv1.tv_usec > tv2.tv_usec)) return 1;
	return 0;
}

// return 1 if tv1 is equal to tv2; 0 otherwise
bool atl::TimeWarp::Sockets::TimevalEqual(const timeval& tv1, const timeval& tv2)
{
	if (tv1.tv_sec == tv2.tv_sec && tv1.tv_usec == tv2.tv_usec)
		return true;
	else
		return false;
}

unsigned long atl::TimeWarp::Sockets::TimevalDuration(struct timeval endT, struct timeval startT)
{
	return (endT.tv_usec - startT.tv_usec) +
		1000000L * (endT.tv_sec - startT.tv_sec);
}

double atl::TimeWarp::Sockets::TimevalDurationSeconds(struct timeval endT, struct timeval startT)
{
	return (endT.tv_usec - startT.tv_usec) / 1000000.0 +
		(endT.tv_sec - startT.tv_sec);
}

double atl::TimeWarp::Sockets::TimevalMsecs(const timeval& tv)
{
	return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

timeval atl::TimeWarp::Sockets::MsecsTimeval(const double dMsecs)
{
	timeval tv;
	tv.tv_sec = (long)floor(dMsecs / 1000.0);
	tv.tv_usec = (long)((dMsecs / 1000.0 - tv.tv_sec) * 1e6);
	return tv;
}

int atl::TimeWarp::Sockets::getmyIP(char* myIPchar, unsigned maxlen,
	const char* NIC_IP,
	SOCKET incoming_socket)
{
	char myname[100];     // Host name of this host
	struct hostent* host; // Encoded host IP address, etc.
	char myIPstring[100]; // Hold "152.2.130.90" or whatever

	if (myIPchar == NULL) {
		fprintf(stderr, "getmyIP: NULL pointer passed in\n");
		return -1;
	}

	// If we have a specified NIC_IP address, fill it in and return it.
	if (NIC_IP) {
		if (strlen(NIC_IP) > maxlen) {
			fprintf(stderr, "getmyIP: Name too long to return\n");
			return -1;
		}
#ifdef VERBOSE
		fprintf(stderr, "Was given IP address of %s so returning that.\n",
			NIC_IP);
#endif
		strncpy(myIPchar, NIC_IP, maxlen);
		myIPchar[maxlen - 1] = '\0';
		return 0;
	}

	// If we have a valid specified SOCKET, then look up its address and
	// return it.
	if (incoming_socket != INVALID_SOCKET) {
		struct sockaddr_in socket_name;
		int socket_namelen = sizeof(socket_name);

		if (getsockname(incoming_socket, (struct sockaddr*) & socket_name,
			GSN_CAST & socket_namelen)) {
			fprintf(stderr, "getmyIP: cannot get socket name.\n");
			return -1;
		}

		sprintf(myIPstring, "%u.%u.%u.%u",
			ntohl(socket_name.sin_addr.s_addr) >> 24,
			(ntohl(socket_name.sin_addr.s_addr) >> 16) & 0xff,
			(ntohl(socket_name.sin_addr.s_addr) >> 8) & 0xff,
			ntohl(socket_name.sin_addr.s_addr) & 0xff);

		// Copy this to the output
		if ((unsigned)strlen(myIPstring) > maxlen) {
			fprintf(stderr, "getmyIP: Name too long to return\n");
			return -1;
		}

		strcpy(myIPchar, myIPstring);

#ifdef VERBOSE
		fprintf(stderr, "Decided on IP address of %s.\n", myIPchar);
#endif
		return 0;
	}

	// Find out what my name is
	// gethostname() is guaranteed to produce something gethostbyname() can
	// parse.
	if (gethostname(myname, sizeof(myname))) {
		fprintf(stderr, "getmyIP: Error finding local hostname\n");
		return -1;
	}

	// Find out what my IP address is
	host = gethostbyname(myname);
	if (host == NULL) {
		fprintf(stderr, "getmyIP: error finding host by name (%s)\n",
			myname);
		return -1;
	}

	// Convert this back into a string
#ifndef CRAY
	if (host->h_length != 4) {
		fprintf(stderr, "getmyIP: Host length not 4\n");
		return -1;
	}
#endif
	sprintf(myIPstring, "%u.%u.%u.%u",
		(unsigned int)(unsigned char)host->h_addr_list[0][0],
		(unsigned int)(unsigned char)host->h_addr_list[0][1],
		(unsigned int)(unsigned char)host->h_addr_list[0][2],
		(unsigned int)(unsigned char)host->h_addr_list[0][3]);

	// Copy this to the output
	if ((unsigned)strlen(myIPstring) > maxlen) {
		fprintf(stderr, "getmyIP: Name too long to return\n");
		return -1;
	}

	strcpy(myIPchar, myIPstring);
#ifdef VERBOSE
	fprintf(stderr, "Decided on IP address of %s.\n", myIPchar);
#endif
	return 0;
}

int atl::TimeWarp::Sockets::noint_select(int width, fd_set* readfds, fd_set* writefds,
	fd_set* exceptfds, struct timeval* timeout)
{
	fd_set tmpread, tmpwrite, tmpexcept;
	int ret;
	int done = 0;
	struct timeval timeout2;
	struct timeval* timeout2ptr;
	struct timeval start, stop, now;

	/* If the timeout parameter is non-NULL and non-zero, then we
	 * may have to adjust it due to an interrupt.  In these cases,
	 * we will copy the timeout to timeout2, which will be used
	 * to keep track.  Also, the stop time is calculated so that
		 * we can know when it is time to bail. */
	if ((timeout != NULL) &&
		((timeout->tv_sec != 0) || (timeout->tv_usec != 0))) {
		timeout2 = *timeout;
		timeout2ptr = &timeout2;
		TW_gettimeofday(&start, NULL);         /* Find start time */
		stop = TimevalSum(start, *timeout); /* Find stop time */
	}
	else {
		timeout2ptr = timeout;
		stop.tv_sec = 0;
		stop.tv_usec = 0;
	}

	/* Repeat selects until it returns for a reason other than interrupt */
	do {
		/* Set the temp file descriptor sets to match parameters each time
		 * through. */
		if (readfds != NULL) {
			tmpread = *readfds;
		} else {
			FD_ZERO(&tmpread);
		}
		if (writefds != NULL) {
			tmpwrite = *writefds;
		} else {
			FD_ZERO(&tmpwrite);
		}
		if (exceptfds != NULL) {
			tmpexcept = *exceptfds;
		} else {
			FD_ZERO(&tmpexcept);
		}

		/* Do the select on the temporary sets of descriptors */
		ret = select(width, &tmpread, &tmpwrite, &tmpexcept, timeout2ptr);
		if (ret >= 0) { /* We are done if timeout or found some */
			done = 1;
		} else if (socket_error != TW_EINTR) { /* Done if non-intr error */
			done = 1;
		} else if ((timeout != NULL) &&
			((timeout->tv_sec != 0) || (timeout->tv_usec != 0))) {

			/* Interrupt happened.  Find new time timeout value */
			TW_gettimeofday(&now, NULL);
			if (TimevalGreater(now, stop)) { /* Past stop time */
				done = 1;
			}
			else { /* Still time to go. */
				unsigned long usec_left;
				usec_left = (stop.tv_sec - now.tv_sec) * 1000000L;
				usec_left += stop.tv_usec - now.tv_usec;
				timeout2.tv_sec = usec_left / 1000000L;
				timeout2.tv_usec = usec_left % 1000000L;
			}
		}
	} while (!done);

	/* Copy the temporary sets back to the parameter sets */
	if (readfds != NULL) {
		*readfds = tmpread;
	}
	if (writefds != NULL) {
		*writefds = tmpwrite;
	}
	if (exceptfds != NULL) {
		*exceptfds = tmpexcept;
	}

	return (ret);
}

#ifndef TW_USE_WINSOCK_SOCKETS

int atl::TimeWarp::Sockets::noint_block_write(int outfile, const char buffer[], size_t length)
{
	int sofar = 0; /* How many characters sent so far */
	int ret;       /* Return value from write() */

	do {
		/* Try to write the remaining data */
		ret = write(outfile, buffer + sofar, length - sofar);
		sofar += ret;

		/* Ignore interrupted system calls - retry */
		if ((ret == -1) && (socket_error == TW_EINTR)) {
			ret = 1;    /* So we go around the loop again */
			sofar += 1; /* Restoring it from above -1 */
		}

	} while ((ret > 0) && (static_cast<size_t>(sofar) < length));

	if (ret == -1) return (-1); /* Error during write */
	if (ret == 0) return (0);   /* EOF reached */

	return (sofar); /* All bytes written */
}

int atl::TimeWarp::Sockets::noint_block_read(int infile, char buffer[], size_t length)
{
	int sofar; /* How many we read so far */
	int ret;   /* Return value from the read() */

	// TCH 4 Jan 2000 - hackish - Cygwin will block forever on a 0-length
	// read(), and from the man pages this is close enough to in-spec that
	// other OS may do the same thing.

	if (!length) {
		return 0;
	}
	sofar = 0;
	do {
		/* Try to read all remaining data */
		ret = read(infile, buffer + sofar, length - sofar);
		sofar += ret;

		/* Ignore interrupted system calls - retry */
		if ((ret == -1) && (socket_error == TW_EINTR)) {
			ret = 1;    /* So we go around the loop again */
			sofar += 1; /* Restoring it from above -1 */
		}
	} while ((ret > 0) && (static_cast<size_t>(sofar) < length));

	if (ret == -1) return (-1); /* Error during read */
	if (ret == 0) return (0);   /* EOF reached */

	return (sofar); /* All bytes read */
}

#else /* winsock sockets */

int atl::TimeWarp::Sockets::noint_block_write(SOCKET outsock, const char* buffer, size_t length)
{
	int nwritten;
	size_t sofar = 0;
	do {
		/* Try to write the remaining data */
		nwritten =
			send(outsock, buffer + sofar, static_cast<int>(length - sofar), 0);

		if (nwritten == SOCKET_ERROR) {
			return -1;
		}

		sofar += nwritten;
	} while (sofar < length);

	return static_cast<int>(sofar); /* All bytes written */
}

int atl::TimeWarp::Sockets::noint_block_read(SOCKET insock, char* buffer, size_t length)
{
	int nread;
	size_t sofar = 0;

	// TCH 4 Jan 2000 - hackish - Cygwin will block forever on a 0-length
	// read(), and from the man pages this is close enough to in-spec that
	// other OS may do the same thing.

	if (!length) {
		return 0;
	}

	do {
		/* Try to read all remaining data */
		nread =
			recv(insock, buffer + sofar, static_cast<int>(length - sofar), 0);

		if (nread == SOCKET_ERROR) {
			return -1;
		}
		if (nread == 0) { /* socket closed */
			return 0;
		}

		sofar += nread;
	} while (sofar < length);

	return static_cast<int>(sofar); /* All bytes read */
}

#endif /* TW_USE_WINSOCK_SOCKETS */

int atl::TimeWarp::Sockets::noint_block_read_timeout(SOCKET infile, char buffer[], size_t length,
	struct timeval* timeout)
{
	int ret; /* Return value from the read() */
	struct timeval timeout2;
	struct timeval* timeout2ptr;
	struct timeval start, stop, now;

	// TCH 4 Jan 2000 - hackish - Cygwin will block forever on a 0-length
	// read(), and from the man pages this is close enough to in-spec that
	// other OS may do the same thing.

	if (!length) {
		return 0;
	}

	/* If the timeout parameter is non-NULL and non-zero, then we
	 * may have to adjust it due to an interrupt.  In these cases,
	 * we will copy the timeout to timeout2, which will be used
	 * to keep track.  Also, the current time is found so that we
	 * can track elapsed time. */
	if ((timeout != NULL) &&
		((timeout->tv_sec != 0) || (timeout->tv_usec != 0))) {
		timeout2 = *timeout;
		timeout2ptr = &timeout2;
		TW_gettimeofday(&start, NULL);         /* Find start time */
		stop = TimevalSum(start, *timeout); /* Find stop time */
	} else {
		timeout2ptr = timeout;
	}

	size_t sofar = 0;/* How many we read so far */
	do {
		int sel_ret;
		fd_set readfds, exceptfds;

		/* See if there is a character ready for read */
		FD_ZERO(&readfds);
		FD_SET(infile, &readfds);
		FD_ZERO(&exceptfds);
		FD_SET(infile, &exceptfds);
		sel_ret = noint_select(static_cast<int>(infile) + 1, &readfds,
			NULL, &exceptfds, timeout2ptr);
		if (sel_ret == -1) { /* Some sort of error on select() */
			return -1;
		}
		if (FD_ISSET(infile, &exceptfds)) { /* Exception */
			return -1;
		}
		if (!FD_ISSET(infile, &readfds)) { /* No characters */
			if ((timeout != NULL) && (timeout->tv_sec == 0) &&
				(timeout->tv_usec == 0)) {      /* Quick poll */
				return static_cast<int>(sofar); /* Timeout! */
			}
		}

		/* See what time it is now and how long we have to go */
		if (timeout2ptr) {
			TW_gettimeofday(&now, NULL);
			if (TimevalGreater(now, stop)) { /* Timeout! */
				return static_cast<int>(sofar);
			} else {
				timeout2 = TimevalDiff(stop, now);
			}
		}

		if (!FD_ISSET(infile, &readfds)) { /* No chars yet */
			ret = 0;
			continue;
		}

#ifndef TW_USE_WINSOCK_SOCKETS
		ret = read(infile, buffer + sofar, length - sofar);
		sofar += ret;

		/* Ignore interrupted system calls - retry */
		if ((ret == -1) && (socket_error == TW_EINTR)) {
			ret = 1;    /* So we go around the loop again */
			sofar += 1; /* Restoring it from above -1 */
		}
#else
		{
			int nread = recv(infile, buffer + sofar,
				static_cast<int>(length - sofar), 0);
			sofar += nread;
			ret = nread;
		}
#endif

	} while ((ret > 0) && (sofar < length));
#ifndef TW_USE_WINSOCK_SOCKETS
	if (ret == -1) return (-1); /* Error during read */
#endif
	if (ret == 0) return (-1); /* EOF reached */

	return static_cast<int>(sofar); /* All bytes read */
}

SOCKET atl::TimeWarp::Sockets::open_socket(int type, unsigned short* portno,
	const char* IPaddress)
{
	struct sockaddr_in name;
	struct hostent* phe; /* pointer to host information entry   */
	int namelen;

	// create an Internet socket of the appropriate type
	SOCKET sock = socket(AF_INET, type, 0);
	if (sock == INVALID_SOCKET) {
		fprintf(stderr, "open_socket: can't open socket.\n");
#ifndef _WIN32_WCE
		fprintf(stderr, "  -- Error %d (%s).\n", socket_error,
			socket_error_to_chars(socket_error));
#endif
		return INVALID_SOCKET;
	}

	// Added by Eric Boren to address socket reconnectivity on the Android
#ifdef __ANDROID__
	int32_t optval = 1;
	int32_t sockoptsuccess =
		setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval);
	// fprintf(stderr, "setsockopt returned %i, optval: %i\n", sockoptsuccess,
	//        optval);
#endif

	namelen = sizeof(name);

	// bind to local address
	memset((void*)& name, 0, namelen);
	name.sin_family = AF_INET;
	if (portno) {
		name.sin_port = htons(*portno);
	} else {
		name.sin_port = htons(0);
	}

	// Map our host name to our IP address, allowing for dotted decimal
	if (!IPaddress) {
		name.sin_addr.s_addr = INADDR_ANY;
	}
	else if ((name.sin_addr.s_addr = inet_addr(IPaddress)) == INADDR_NONE) {
		if ((phe = gethostbyname(IPaddress)) != NULL) {
			memcpy((void*)& name.sin_addr, (const void*)phe->h_addr,
				phe->h_length);
		}
		else {
			closeSocket(sock);
			fprintf(stderr, "open_socket:  can't get %s host entry\n",
				IPaddress);
			return INVALID_SOCKET;
		}
	}

#ifdef VERBOSE3
	// NIC will be 0.0.0.0 if we use INADDR_ANY
	fprintf(stderr, "open_socket:  request port %d, using NIC %d %d %d %d.\n",
		portno ? *portno : 0, ntohl(name.sin_addr.s_addr) >> 24,
		(ntohl(name.sin_addr.s_addr) >> 16) & 0xff,
		(ntohl(name.sin_addr.s_addr) >> 8) & 0xff,
		ntohl(name.sin_addr.s_addr) & 0xff);
#endif

	if (bind(sock, (struct sockaddr*) & name, namelen) < 0) {
		fprintf(stderr, "open_socket:  can't bind address");
		if (portno) {
			fprintf(stderr, " %d", *portno);
		}
#ifndef _WIN32_WCE
		fprintf(stderr, "  --  %d  --  %s\n", socket_error,
			socket_error_to_chars(socket_error));
#endif
		fprintf(stderr, "  (This probably means that another application has "
			"the port open already)\n");
		closeSocket(sock);
		return INVALID_SOCKET;
	}

	// Find out which port was actually bound
	if (getsockname(sock, (struct sockaddr*) & name, GSN_CAST & namelen)) {
		fprintf(stderr, "open_socket: cannot get socket name.\n");
		closeSocket(sock);
		return INVALID_SOCKET;
	}
	if (portno) {
		*portno = ntohs(name.sin_port);
	}

#ifdef VERBOSE3
	// NIC will be 0.0.0.0 if we use INADDR_ANY
	fprintf(stderr, "open_socket:  got port %d, using NIC %d %d %d %d.\n",
		portno ? *portno : ntohs(name.sin_port),
		ntohl(name.sin_addr.s_addr) >> 24,
		(ntohl(name.sin_addr.s_addr) >> 16) & 0xff,
		(ntohl(name.sin_addr.s_addr) >> 8) & 0xff,
		ntohl(name.sin_addr.s_addr) & 0xff);
#endif

	return sock;
}

SOCKET atl::TimeWarp::Sockets::open_udp_socket(unsigned short* portno, const char* IPaddress)
{
	return open_socket(SOCK_DGRAM, portno, IPaddress);
}

SOCKET atl::TimeWarp::Sockets::open_tcp_socket(unsigned short* portno,
	const char* NIC_IP)
{
	return open_socket(SOCK_STREAM, portno, NIC_IP);
}

SOCKET atl::TimeWarp::Sockets::connect_udp_port(const char* machineName, int remotePort,
	const char* NIC_IP)
{
	SOCKET udp_socket;
	struct sockaddr_in udp_name;
	struct hostent* remoteHost;
	int udp_namelen;

	udp_socket = open_udp_socket(NULL, NIC_IP);

	udp_namelen = sizeof(udp_name);

	memset((void*)& udp_name, 0, udp_namelen);
	udp_name.sin_family = AF_INET;

	// gethostbyname() fails on SOME Windows NT boxes, but not all,
	// if given an IP octet string rather than a true name.
	// MS Documentation says it will always fail and inet_addr should
	// be called first. Avoids a 30+ second wait for
	// gethostbyname() to fail.

	if ((udp_name.sin_addr.s_addr = inet_addr(machineName)) == INADDR_NONE) {
		remoteHost = gethostbyname(machineName);
		if (remoteHost) {

#ifdef CRAY
			int i;
			u_long foo_mark = 0L;
			for (i = 0; i < 4; i++) {
				u_long one_char = remoteHost->h_addr_list[0][i];
				foo_mark = (foo_mark << 8) | one_char;
			}
			udp_name.sin_addr.s_addr = foo_mark;
#else
			memcpy(&(udp_name.sin_addr.s_addr), remoteHost->h_addr,
				remoteHost->h_length);
#endif
		}
		else {
			closeSocket(udp_socket);
			fprintf(stderr,
				"connect_udp_port: error finding host by name (%s).\n",
				machineName);
			return INVALID_SOCKET;
		}
	}
#ifndef TW_USE_WINSOCK_SOCKETS
	udp_name.sin_port = htons(remotePort);
#else
	udp_name.sin_port = htons((u_short)remotePort);
#endif

	if (connect(udp_socket, (struct sockaddr*) & udp_name, udp_namelen)) {
		fprintf(stderr, "connect_udp_port: can't bind udp socket.\n");
		closeSocket(udp_socket);
		return INVALID_SOCKET;
	}

	// Find out which port was actually bound
	udp_namelen = sizeof(udp_name);
	if (getsockname(udp_socket, (struct sockaddr*) & udp_name,
		GSN_CAST & udp_namelen)) {
		fprintf(stderr, "connect_udp_port: cannot get socket name.\n");
		closeSocket(udp_socket);
		return INVALID_SOCKET;
	}

#ifdef VERBOSE3
	// NOTE NIC will be 0.0.0.0 if we listen on all NICs.
	fprintf(stderr,
		"connect_udp_port:  got port %d, using NIC %d %d %d %d.\n",
		ntohs(udp_name.sin_port), ntohl(udp_name.sin_addr.s_addr) >> 24,
		(ntohl(udp_name.sin_addr.s_addr) >> 16) & 0xff,
		(ntohl(udp_name.sin_addr.s_addr) >> 8) & 0xff,
		ntohl(udp_name.sin_addr.s_addr) & 0xff);
#endif

	return udp_socket;
}

int atl::TimeWarp::Sockets::get_local_socket_name(char* local_host, size_t max_length,
	const char* remote_host)
{
	const int remote_port = 3883;	// Quasi-random port number...
	struct sockaddr_in udp_name;
	int udp_namelen = sizeof(udp_name);

	SOCKET udp_socket = connect_udp_port(remote_host, remote_port, NULL);
	if (udp_socket == INVALID_SOCKET) {
		fprintf(stderr,
			"get_local_socket_name: cannot connect_udp_port to %s.\n",
			remote_host);
		fprintf(stderr, " (returning 0.0.0.0 so we listen on all ports).\n");
		udp_name.sin_addr.s_addr = 0;
	}
	else {
		if (getsockname(udp_socket, (struct sockaddr*) & udp_name,
			GSN_CAST & udp_namelen)) {
			fprintf(stderr, "get_local_socket_name: cannot get socket name.\n");
			closeSocket(udp_socket);
			return -1;
		}
	}

	// NOTE NIC will be 0.0.0.0 if we listen on all NICs.
	char myIPstring[100];
	int ret = sprintf(myIPstring, "%d.%d.%d.%d",
		ntohl(udp_name.sin_addr.s_addr) >> 24,
		(ntohl(udp_name.sin_addr.s_addr) >> 16) & 0xff,
		(ntohl(udp_name.sin_addr.s_addr) >> 8) & 0xff,
		ntohl(udp_name.sin_addr.s_addr) & 0xff);

	// Copy this to the output
	if ((unsigned)strlen(myIPstring) > max_length) {
		fprintf(stderr, "get_local_socket_name: Name too long to return\n");
		closeSocket(udp_socket);
		return -1;
	}

	strcpy(local_host, myIPstring);
	closeSocket(udp_socket);
	return ret;
}

int atl::TimeWarp::Sockets::udp_request_lob_packet(
	SOCKET udp_sock,      // Socket to use to send
	const char*,         // Name of the machine to call
	const int,            // UDP port on remote machine
	const int local_port, // TCP port on this machine
	const char* NIC_IP)
{
	char msg[150];      /* Message to send */
	int32_t msglen;  /* How long it is (including \0) */
	char myIPchar[100]; /* IP decription this host */

	/* Fill in the request message, telling the machine and port that
	 * the remote server should connect to.  These are ASCII, separated
	 * by a space.  getmyIP returns the NIC_IP if it is not null,
	 * or the host name of this machine using gethostname() if it is
	 * NULL.  If the NIC_IP is NULL but we have a socket (as we do here),
	 * then it returns the address associated with the socket.
	 */
	if (getmyIP(myIPchar, sizeof(myIPchar), NIC_IP, udp_sock)) {
		fprintf(stderr,
			"udp_request_lob_packet: Error finding local hostIP\n");
		closeSocket(udp_sock);
		return (-1);
	}
	sprintf(msg, "%s %d", myIPchar, local_port);
	msglen = static_cast<int32_t>(strlen(msg) +
		1); /* Include the terminating 0 char */

// Lob the message
	if (send(udp_sock, msg, msglen, 0) == -1) {
		perror("udp_request_lob_packet: send() failed");
		closeSocket(udp_sock);
		return -1;
	}

	return 0;
}

int atl::TimeWarp::Sockets::get_a_TCP_socket(SOCKET* listen_sock, int* listen_portnum,
	const char* NIC_IP)
{
	struct sockaddr_in listen_name; /* The listen socket binding name */
	int listen_namelen;

	listen_namelen = sizeof(listen_name);

	/* Create a TCP socket to listen for incoming connections from the
	 * remote server. */

	*listen_sock = open_tcp_socket(NULL, NIC_IP);
	if (*listen_sock < 0) {
		fprintf(stderr, "get_a_TCP_socket:  socket didn't open.\n");
		return -1;
	}

	if (listen(*listen_sock, 1)) {
		fprintf(stderr, "get_a_TCP_socket: listen() failed.\n");
		closeSocket(*listen_sock);
		return (-1);
	}

	if (getsockname(*listen_sock, (struct sockaddr*) & listen_name,
		GSN_CAST & listen_namelen)) {
		fprintf(stderr, "get_a_TCP_socket: cannot get socket name.\n");
		closeSocket(*listen_sock);
		return (-1);
	}

	*listen_portnum = ntohs(listen_name.sin_port);
	return 0;
}

int atl::TimeWarp::Sockets::poll_for_accept(SOCKET listen_sock, SOCKET* accept_sock,
	double timeout)
{
	fd_set rfds;
	struct timeval t;

	// See if we have a connection attempt within the timeout
	FD_ZERO(&rfds);
	FD_SET(listen_sock, &rfds); /* Check for read (connect) */
	t.tv_sec = (long)(timeout);
	t.tv_usec = (long)((timeout - t.tv_sec) * 1000000L);
	if (noint_select(static_cast<int>(listen_sock) + 1, &rfds, NULL, NULL,
		&t) == -1) {
		perror("poll_for_accept: select() failed");
		return -1;
	}
	if (FD_ISSET(listen_sock, &rfds)) { /* Got one! */
		/* Accept the connection from the remote machine and set TCP_NODELAY
		* on the socket. */
		if ((*accept_sock = accept(listen_sock, 0, 0)) == -1) {
			perror("poll_for_accept: accept() failed");
			return -1;
		}
#if !defined(_WIN32_WCE) && !defined(__ANDROID__)
		{
			struct protoent* p_entry;
			int nonzero = 1;

			if ((p_entry = getprotobyname("TCP")) == NULL) {
				fprintf(stderr,
					"poll_for_accept: getprotobyname() failed.\n");
				closeSocket(*accept_sock);
				return (-1);
			}

			if (setsockopt(*accept_sock, p_entry->p_proto, TCP_NODELAY,
				SOCK_CAST & nonzero, sizeof(nonzero)) == -1) {
				perror("poll_for_accept: setsockopt() failed");
				closeSocket(*accept_sock);
				return (-1);
			}
		}
#endif
		return 1; // Got one!
	}

	return 0; // Nobody called
}

bool atl::TimeWarp::Sockets::connect_tcp_to(const char* addr, int port,
	const char* NICaddress, SOCKET *s)
{
	if (s == nullptr) {
		fprintf(stderr, "connect_tcp_to: Null socket pointer\n");
		return false;
	}

	struct sockaddr_in client; /* The name of the client */
	struct hostent* host;      /* The host to connect to */

	/* set up the socket */
	*s = open_tcp_socket(NULL, NICaddress);
	if (*s < 0) {
		fprintf(stderr, "connect_tcp_to: can't open socket\n");
		return false;
	}
	client.sin_family = AF_INET;

	// gethostbyname() fails on SOME Windows NT boxes, but not all,
	// if given an IP octet string rather than a true name.
	// MS Documentation says it will always fail and inet_addr should
	// be called first. Avoids a 30+ second wait for
	// gethostbyname() to fail.

	if ((client.sin_addr.s_addr = inet_addr(addr)) == INADDR_NONE) {
		host = gethostbyname(addr);
		if (host) {

#ifdef CRAY
			{
				int i;
				u_long foo_mark = 0;
				for (i = 0; i < 4; i++) {
					u_long one_char = host->h_addr_list[0][i];
					foo_mark = (foo_mark << 8) | one_char;
				}
				client.sin_addr.s_addr = foo_mark;
			}
#else
			memcpy(&(client.sin_addr.s_addr), host->h_addr, host->h_length);
#endif
		}
		else {

#if !defined(hpux) && !defined(__hpux) && !defined(_WIN32) && !defined(sparc)
			herror("gethostbyname error:");
#else
			perror("gethostbyname error:");
#endif
			fprintf(stderr, "connect_tcp_to: error finding host by name (%s)\n",
				addr);
			return false;
		}
	}

#ifndef USE_WINSOCK_SOCKETS
	client.sin_port = htons(port);
#else
	client.sin_port = htons((u_short)port);
#endif

	if (connect(*s, (struct sockaddr*) & client, sizeof(client)) < 0) {
#ifdef USE_WINSOCK_SOCKETS
		if (!d_tcp_only) {
			fprintf(stderr, "connect_tcp_to: Could not connect "
				"to machine %d.%d.%d.%d port %d\n",
				(int)(client.sin_addr.S_un.S_un_b.s_b1),
				(int)(client.sin_addr.S_un.S_un_b.s_b2),
				(int)(client.sin_addr.S_un.S_un_b.s_b3),
				(int)(client.sin_addr.S_un.S_un_b.s_b4),
				(int)(ntohs(client.sin_port)));
			int error = WSAGetLastError();
			fprintf(stderr, "Winsock error: %d\n", error);
		}
#else
		fprintf(stderr, "connect_tcp_to: Could not connect to "
			"machine %d.%d.%d.%d port %d\n",
			(int)((client.sin_addr.s_addr >> 24) & 0xff),
			(int)((client.sin_addr.s_addr >> 16) & 0xff),
			(int)((client.sin_addr.s_addr >> 8) & 0xff),
			(int)((client.sin_addr.s_addr >> 0) & 0xff),
			(int)(ntohs(client.sin_port)));
#endif
		closeSocket(*s);
		return false;
	}

	/* Set the socket for TCP_NODELAY */
#if !defined(_WIN32_WCE) && !defined(__ANDROID__)
	{
		struct protoent* p_entry;
		int nonzero = 1;

		if ((p_entry = getprotobyname("TCP")) == NULL) {
			fprintf(
				stderr,
				"connect_tcp_to: getprotobyname() failed.\n");
			closeSocket(*s);
			return false;
		}

		if (setsockopt(*s, p_entry->p_proto, TCP_NODELAY,
			SOCK_CAST & nonzero, sizeof(nonzero)) == -1) {
			perror("connect_tcp_to: setsockopt() failed");
			closeSocket(*s);
			return false;
		}
	}
#endif
	return true;
}

int atl::TimeWarp::Sockets::close_socket(SOCKET sock)
{
	return closeSocket(sock);
}

// From this we get the variable "TW_big_endian" set to true if the machine we
// are
// on is big endian and to false if it is little endian.

static const int TW_int_data_for_endian_test = 1;
static const char* TW_char_data_for_endian_test =
static_cast<const char*>(static_cast<const void*>((&TW_int_data_for_endian_test)));
static const bool TW_big_endian = (TW_char_data_for_endian_test[0] != 1);

// convert double to/from network order
// I have chosen big endian as the network order for double
// to match the standard for htons() and htonl().
// NOTE: There is an added complexity when we are using an ARM
// processor in mixed-endian mode for the doubles, whereby we need
// to not just swap all of the bytes but also swap the two 4-byte
// words to get things in the right order.
#if defined(__arm__)
#include <endian.h>
#endif

double atl::TimeWarp::Sockets::hton(double d)
{
	if (!TW_big_endian) {
		double dSwapped;
		char* pchSwapped = (char*)& dSwapped;
		char* pchOrig = (char*)& d;

		// swap to big-endian order.
		unsigned i;
		for (i = 0; i < sizeof(double); i++) {
			pchSwapped[i] = pchOrig[sizeof(double) - i - 1];
		}

#if defined(__arm__) && !defined(__ANDROID__)
		// On ARM processor, see if we're in mixed mode.  If so,
		// we need to swap the two words after doing the total
		// swap of bytes.
#if __FLOAT_WORD_ORDER != __BYTE_ORDER
		{
			/* Fixup mixed endian floating point machines */
			uint32_t* pwSwapped = (uint32_t*)& dSwapped;
			uint32_t scratch = pwSwapped[0];
			pwSwapped[0] = pwSwapped[1];
			pwSwapped[1] = scratch;
		}
#endif
#endif

		return dSwapped;
	}
	else {
		return d;
	}
}

// they are their own inverses, so ...
double atl::TimeWarp::Sockets::ntoh(double d) { return hton(d); }

// convert int64_t to/from network order
// I have chosen big endian as the network order for double
// to match the standard for htons() and htonl().
// NOTE: There is an added complexity when we are using an ARM
// processor in mixed-endian mode for the doubles, whereby we need
// to not just swap all of the bytes but also swap the two 4-byte
// words to get things in the right order.

int64_t atl::TimeWarp::Sockets::hton(int64_t d)
{
	if (!TW_big_endian) {
		int64_t dSwapped;
		char* pchSwapped = (char*)& dSwapped;
		char* pchOrig = (char*)& d;

		// swap to big-endian order.
		unsigned i;
		for (i = 0; i < sizeof(int64_t); i++) {
			pchSwapped[i] = pchOrig[sizeof(int64_t) - i - 1];
		}

#if defined(__arm__) && !defined(__ANDROID__)
		// On ARM processor, see if we're in mixed mode.  If so,
		// we need to swap the two words after doing the total
		// swap of bytes.
#if __FLOAT_WORD_ORDER != __BYTE_ORDER
		{
			/* Fixup mixed endian floating point machines */
			uint32_t* pwSwapped = (uint32_t*)& dSwapped;
			uint32_t scratch = pwSwapped[0];
			pwSwapped[0] = pwSwapped[1];
			pwSwapped[1] = scratch;
		}
#endif
#endif

		return dSwapped;
	}
	else {
		return d;
	}
}

// they are their own inverses, so ...
int64_t atl::TimeWarp::Sockets::ntoh(int64_t d) { return hton(d); }
