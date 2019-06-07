#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <string>
#include <system_error>
#include "TimeWarpSockets.hpp"


using namespace atl::TimeWarp::Sockets;

#ifdef USE_WINSOCK_SOCKETS

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

#ifndef USE_WINSOCK_SOCKETS
#include <sys/wait.h> // for waitpid, WNOHANG
#ifndef __CYGWIN__
#include <netinet/tcp.h> // for TCP_NODELAY
#endif                   /* __CYGWIN__ */
#endif                   /* USE_WINSOCK_SOCKETS */

// cast fourth argument to setsockopt()
#ifdef USE_WINSOCK_SOCKETS
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

// On Win32, this constant is defined as ~0 (sockets are unsigned ints)
#ifndef USE_WINSOCK_SOCKETS
#define INVALID_SOCKET -1
#endif

/**
 *   This function returns the host IP address in string form.  For example,
 * the machine "ioglab.cs.unc.edu" becomes "152.2.130.90."  This is done
 * so that the remote host can connect back even if it can't resolve the
 * DNS name of this host.  This is especially useful at conferences, where
 * DNS may not even be running.
 *   If the NIC_IP name is passed in as NULL but the SOCKET passed in is
 * valid, then look up the address associated with that socket; this is so
 * that when a machine has multiple NICs, it will send the outgoing request
 * for UDP connections to the same place that its TCP connection is on.
 */

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

/**
 *	This routine will perform like a normal select() call, but it will
 * restart if it quit because of an interrupt.  This makes it more robust
 * in its function, and allows this code to perform properly on pxpl5, which
 * sends USER1 interrupts while rendering an image.
 */
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
		}
		else {
			FD_ZERO(&tmpread);
		}
		if (writefds != NULL) {
			tmpwrite = *writefds;
		}
		else {
			FD_ZERO(&tmpwrite);
		}
		if (exceptfds != NULL) {
			tmpexcept = *exceptfds;
		}
		else {
			FD_ZERO(&tmpexcept);
		}

		/* Do the select on the temporary sets of descriptors */
		ret = select(width, &tmpread, &tmpwrite, &tmpexcept, timeout2ptr);
		if (ret >= 0) { /* We are done if timeout or found some */
			done = 1;
		}
		else if (socket_error != TW_EINTR) { /* Done if non-intr error */
			done = 1;
		}
		else if ((timeout != NULL) &&
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

/**
 *      This routine will write a block to a file descriptor.  It acts just
 * like the write() system call does on files, but it will keep sending to
 * a socket until an error or all of the data has gone.
 *      This will also take care of problems caused by interrupted system
 * calls, retrying the write when they occur.  It will also work when
 * sending large blocks of data through socket connections, since it will
 * send all of the data before returning.
 *	This routine will either write the requested number of bytes and
 * return that or return -1 (in the case of an error) or 0 (in the case
 * of EOF being reached before all the data is sent).
 */

#ifndef USE_WINSOCK_SOCKETS

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

/**
 *      This routine will read in a block from the file descriptor.
 * It acts just like the read() routine does on normal files, so that
 * it hides the fact that the descriptor may point to a socket.
 *      This will also take care of problems caused by interrupted system
 * calls, retrying the read when they occur.
 *	This routine will either read the requested number of bytes and
 * return that or return -1 (in the case of an error) or 0 (in the case
 * of EOF being reached before all the data arrives).
 */

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

int atl::TimeWarp::Sockets::noint_block_write(SOCKET outsock, char* buffer, size_t length)
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

#endif /* USE_WINSOCK_SOCKETS */

/**
 *   This routine will read in a block from the file descriptor.
 * It acts just like the read() routine on normal files, except that
 * it will time out if the read takes too long.
 *   This will also take care of problems caused by interrupted system
 * calls, retrying the read when they occur.
 *   This routine will either read the requested number of bytes and
 * return that or return -1 (in the case of an error) or 0 (in the case
 * of EOF being reached before all the data arrives), or return the number
 * of characters read before timeout (in the case of a timeout).
 */

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
	}
	else {
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
			}
			else {
				timeout2 = TimevalDiff(stop, now);
			}
		}

		if (!FD_ISSET(infile, &readfds)) { /* No chars yet */
			ret = 0;
			continue;
		}

#ifndef USE_WINSOCK_SOCKETS
		int ret = read(infile, buffer + sofar, length - sofar);
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
#ifndef USE_WINSOCK_SOCKETS
	if (ret == -1) return (-1); /* Error during read */
#endif
	if (ret == 0) return (0); /* EOF reached */

	return static_cast<int>(sofar); /* All bytes read */
}

/**
 * This routine opens a socket with the requested port number.
 * The routine returns -1 on failure and the file descriptor on success.
 * The portno parameter is filled in with the actual port that is opened
 * (this is useful when the address INADDR_ANY is used, since it tells
 * what port was opened).
 * To select between multiple NICs, we can specify the IP address of the
 * socket to open;  NULL selects the default NIC.
 */

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
	}
	else {
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

/**
 * Create a UDP socket and bind it to its local address.
 */

SOCKET atl::TimeWarp::Sockets::open_udp_socket(unsigned short* portno, const char* IPaddress)
{
	return open_socket(SOCK_DGRAM, portno, IPaddress);
}

/**
 * Create a TCP socket and bind it to its local address.
 */

SOCKET atl::TimeWarp::Sockets::open_tcp_socket(unsigned short* portno,
	const char* NIC_IP)
{
	return open_socket(SOCK_STREAM, portno, NIC_IP);
}

/**
 * Create a UDP socket and connect it to a specified port.
 */

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
#ifndef USE_WINSOCK_SOCKETS
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

/**
 * Retrieves the IP address or hostname of the local interface used to connect
 * to the specified remote host.
 * XXX: This does not always work.  See the Github issue with the report from
 * Isop W. Alexander showing that a machine with two ports (172.28.0.10 and
 * 192.168.191.130) sent a connection request that came from the 172 IP address
 * but that had the name of the 192 interface in the message as the host to
 * call back.  This turned out to be unroutable, so the server failed to call
 * back on the correct IP address.  Presumably, this happens when the gateway
 * is configured to be a single outgoing NIC.  This was on a Linux box.  We
 * need a more reliable way to select the outgoing NIC.  XXX Actually, the
 * problem may be that we aren't listening on the incorrect port -- the UDP
 * receipt code may use the IP address the message came from rather than the
 * machine name in the message.
 *
 * @param local_host A buffer of size 64 that will contain the name of the local
 * interface.
 * @param max_length The maximum length of the local_host buffer.
 * @param remote_host The name of the remote host.
 *
 * @return Returns -1 on getsockname() error, or the output of sprintf
 * building the local_host string.
 */
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

/**
 * This section deals with implementing a method of connection termed a
 * UDP request.  This works by having the client open a TCP socket that
 * it listens on. It then lobs datagrams to the server asking to be
 * called back at the socket. This allows it to timeout on waiting for
 * a connection request, resend datagrams in case some got lost, or give
 * up at any time. The whole algorithm is implemented in the
 * udp_request_call() function; the functions before that are helper
 * functions that have been broken out to allow a subset of the algorithm
 * to be run by a connection whose server has dropped and they want to
 * re-establish it.
 *
 * This routine will lob a datagram to the given port on the given
 * machine asking it to call back at the port on this machine that
 * is also specified. It returns 0 on success and -1 on failure.
 */

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

/**
 * This routine will get a TCP socket that is ready to accept connections.
 * That is, listen() has already been called on it.
 * It will get whatever socket is available from the system. It returns
 * 0 on success and -1 on failure. On success, it fills in the pointers to
 * the socket and the port number of the socket that it obtained.
 * To select between multiple network interfaces, we can specify an IPaddress;
 * the default value is NULL, which uses the default NIC.
 */

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

	// fprintf(stderr, "Listening on port %d, address %d %d %d %d.\n",
	//*listen_portnum, listen_name.sin_addr.s_addr >> 24,
	//(listen_name.sin_addr.s_addr >> 16) & 0xff,
	//(listen_name.sin_addr.s_addr >> 8) & 0xff,
	// listen_name.sin_addr.s_addr & 0xff);

	return 0;
}

/**
 * This routine will check the listen socket to see if there has been a
 * connection request. If so, it will accept a connection on the accept
 * socket and set TCP_NODELAY on that socket. The attempt will timeout
 * in the amount of time specified.  If the accept and set are
 * successful, it returns 1. If there is nothing asking for a connection,
 * it returns 0. If there is an error along the way, it returns -1.
 */

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
