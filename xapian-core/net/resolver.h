/** @file
 * @brief Resolve hostnames and ip addresses
 */
/* Copyright (C) 2017,2018 Olly Betts
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef XAPIAN_INCLUDED_RESOLVER_H
#define XAPIAN_INCLUDED_RESOLVER_H

#include <cstring>
#include "safenetdb.h"
#include "safesyssocket.h"
#include "str.h"
#include "xapian/error.h"

class Resolver {
    struct addrinfo* result = NULL;

    int eai_to_xapian(int e) {
	// Under WIN32, the EAI_* constants are defined to be WSA_* constants
	// with roughly equivalent meanings, so we can just let them be handled
	// as any other WSA_* error codes would be.
#ifndef __WIN32__
	// Ensure they all have the same sign - this switch will fail to
	// compile if we bitwise-or some 1 and some 2 bits to get 3.
#define C(X) ((X) < 0 ? 2 : 1)
	// Switch on a value there is a case for, to avoid clang warning: "no
	// case matching constant switch condition '0'"
	switch (3) {
	    case
		C(EAI_AGAIN)|
		C(EAI_BADFLAGS)|
		C(EAI_FAIL)|
		C(EAI_FAMILY)|
		C(EAI_MEMORY)|
		C(EAI_NONAME)|
		C(EAI_SERVICE)|
		C(EAI_SOCKTYPE)|
		C(EAI_SYSTEM)|
#ifdef EAI_ADDRFAMILY
		// In RFC 2553 but not RFC 3493 or POSIX:
		C(EAI_ADDRFAMILY)|
#endif
#ifdef EAI_NODATA
		// In RFC 2553 but not RFC 3493 or POSIX:
		C(EAI_NODATA)|
#endif
#ifdef EAI_OVERFLOW
		// In RFC 3493 and POSIX but not RFC 2553:
		C(EAI_OVERFLOW)|
#endif
		0: break;
	    case 3: break;
	}
#undef C

	// EAI_SYSTEM means "look at errno".
	if (e == EAI_SYSTEM)
	    return errno;
	// POSIX only says that EAI_* constants are "non-zero".  On Linux they
	// are negative, but allow for them being positive too.
	if (EAI_FAIL > 0)
	    return -e;
#endif
	return e;
    }

  public:
    class const_iterator {
	struct addrinfo* p;
      public:
	explicit const_iterator(struct addrinfo* p_) : p(p_) { }

	struct addrinfo& operator*() const {
	    return *p;
	}

	void operator++() {
	    p = p->ai_next;
	}

	const_iterator operator++(int) {
	    struct addrinfo* old_p = p;
	    operator++();
	    return const_iterator(old_p);
	}

	bool operator==(const const_iterator& o) {
	    return p == o.p;
	}

	bool operator!=(const const_iterator& o) {
	    return !(*this == o);
	}
    };

    Resolver(const std::string& host, int port, int flags = 0) {
	// RFC 3493 has an extra sentence in its definition of
	// AI_ADDRCONFIG which POSIX doesn't:
	//
	//   "The loopback address is not considered for this case as valid
	//   as a configured address."
	//
	// Some platforms implement this version rather than POSIX (notably
	// glibc on Linux).  Others implement POSIX (from looking at the
	// man pages, these include FreeBSD 11.0).
	//
	// In most cases, this extra sentence is arguably helpful - e.g. it
	// means that you won't get IPv6 addresses just because the system
	// has IPv6 loopback (and similarly for IPv4).
	//
	// However, it behaves unhelpfully if the *only* interface
	// configured is loopback - in this situation, AI_ADDRCONFIG means
	// that you won't get an IPv4 address (as there's no IPv4 address
	// configured ignoring loopback) and you won't get an IPv6 address
	// (as there's no IPv6 address configured ignoring loopback).
	//
	// It's generally rare that systems with only loopback would want
	// to use the remote backend, but a real example is testsuites
	// (including our own) running on autobuilders which deliberately
	// close off network access.
	//
	// To allow such cases to work on Linux (and other platforms which
	// follow the RFC rather than POSIX in this detail) we avoid using
	// AI_ADDRCONFIG for 127.0.0.1, ::1 and localhost.  There are
	// other ways to write these IP addresses and other hostnames may
	// map onto them, but this just needs to work for the standard
	// cases which a testsuite might use.
	if (host != "::1" && host != "127.0.0.1" && host != "localhost") {
	    flags |= AI_ADDRCONFIG;
	}
	flags |= AI_NUMERICSERV;

	struct addrinfo hints;
	std::memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = flags;
	hints.ai_protocol = 0;

	const char * node = host.empty() ? NULL : host.c_str();
	int r = getaddrinfo(node, str(port).c_str(), &hints, &result);
	if (r != 0) {
	    throw Xapian::NetworkError("Couldn't resolve host " + host,
				       eai_to_xapian(r));
	}
    }

    ~Resolver() {
	if (result) freeaddrinfo(result);
    }

    const_iterator begin() const {
	return const_iterator(result);
    }

    const_iterator end() const {
	return const_iterator(NULL);
    }
};

#endif // XAPIAN_INCLUDED_RESOLVER_H
