/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// IPEnumeration.h ///////////////////////////////////////////////////////////////
// Class for enumerating IP addresses
// Author: Matthew D. Campbell, October 2001

#pragma once

#include "GameNetwork/Transport.h"

/**
 * IP wrapper class.
 */
class EnumeratedIP : public MemoryPoolObject
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(EnumeratedIP, "EnumeratedIP")
public:
	EnumeratedIP() { m_IPstring = ""; m_next = nullptr; m_IP = 0; }

	// Access functions
	AsciiString getIPstring() { return m_IPstring; }
	void setIPstring( AsciiString name ) { m_IPstring = name; }
	UnsignedInt getIP() { return m_IP; }
	void setIP( UnsignedInt IP ) { m_IP = IP; }
	EnumeratedIP *getNext() { return m_next; }
	void setNext( EnumeratedIP *next ) { m_next = next; }

protected:
	AsciiString m_IPstring;
	UnsignedInt m_IP;
	EnumeratedIP *m_next;
};
EMPTY_DTOR(EnumeratedIP)


/**
 * The IPEnumeration class is used to obtain a list of IP addresses on the
 * local machine.
 */
class IPEnumeration
{
public:

	IPEnumeration();
	~IPEnumeration();

	EnumeratedIP * getAddresses();		///< Return a linked list of local IP addresses
	AsciiString getMachineName();			///< Return the Network Neighborhood machine name

protected:
	void addNewIP( UnsignedByte a, UnsignedByte b, UnsignedByte c, UnsignedByte d );

	EnumeratedIP *m_IPlist;
	Bool m_isWinsockInitialized;
};

/**
 * Pick the local address the LAN code should call its own.
 *
 * GeneralsX @bugfix Claude 19/08/2026 (issue #86) The menus used to fall back to the head of the
 * enumerated list, which IPEnumeration keeps sorted by numeric address. That is arbitrary with
 * respect to reachability: a docker-compose bridge on 172.18.x or a container network on 10.x
 * sorts below a 192.168.x home LAN and wins. The address is not only used for discovery - it is
 * what a host advertises as slot 0, and what every client then connects to for the actual game
 * session - so a wrong choice survives the lobby and breaks the match instead.
 *
 * @param ipList          the enumerated candidates, in list order
 * @param preferredIP     the configured address, or 0; honoured whenever it is still a candidate
 * @return the chosen address, or 0 if there are no candidates at all
 */
UnsignedInt SelectLANLocalAddress( EnumeratedIP *ipList, UnsignedInt preferredIP );
