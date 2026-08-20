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

#pragma once

#include "GameNetwork/NetworkDefs.h"
#include "GameNetwork/NetworkInterface.h"

UnsignedInt AssembleIp(UnsignedByte a, UnsignedByte b, UnsignedByte c, UnsignedByte d);
UnsignedInt ResolveIP(AsciiString host);
UnsignedShort GenerateNextCommandID();
Bool DoesCommandRequireACommandID(NetCommandType type);
Bool CommandRequiresAck(NetCommandMsg *msg);
Bool CommandRequiresDirectSend(NetCommandMsg *msg);
Bool IsCommandSynchronized(NetCommandType type);
const char* GetNetCommandTypeAsString(NetCommandType type);

// GeneralsX @bugfix Claude 19/08/2026 Shared, testable helpers for LAN lobby address handling (issue #86).

/// The most local IPv4 interfaces the LAN lobby will look at in one pass.
static const Int MAX_LAN_LOCAL_INTERFACES = 32;

/**
 * One local IPv4 interface, as the LAN lobby cares about it.
 * All addresses are in host byte order, matching the rest of the LAN code.
 */
struct LANLocalInterface
{
	UnsignedInt address;			///< the interface's own address
	UnsignedInt broadcast;			///< its broadcast address, or 0 if it has none
	Bool				canBroadcast;		///< up, not loopback, not point-to-point, and broadcast capable
};

/**
 * Collect the destinations a LAN discovery/announce packet has to be sent to so
 * that every broadcast domain this machine sits on hears it.
 *
 * Passing an empty interface list (which is what the Windows path does, since it
 * has no getifaddrs) yields exactly INADDR_BROADCAST, i.e. the retail behaviour.
 *
 * @return the number of addresses written to outAddrs; never 0, never writes 0.
 */
Int LANSelectBroadcastDestinations( const LANLocalInterface *ifaces, Int ifaceCount,
																		UnsignedInt *outAddrs, Int maxAddrs );

/**
 * TRUE when addr is an address of this machine. selectedLocalIP is the address
 * LANAPI is currently calling its own; it is always treated as local, so an
 * empty interface list degrades to the plain equality test.
 */
Bool LANIsLocalAddress( const LANLocalInterface *ifaces, Int ifaceCount,
												UnsignedInt selectedLocalIP, UnsignedInt addr );

/**
 * Ask the routing table which local address this machine would send from when
 * talking to peerIP. Opens a UDP socket and connects it, which transmits
 * nothing, then reads the address the kernel bound.
 *
 * @return the local address in host byte order, or 0 if it could not be determined.
 */
UnsignedInt GetLocalAddressForPeer( UnsignedInt peerIP );

#ifdef DEBUG_LOGGING
extern "C" {
void dumpBufferToLog(const void *vBuf, Int len, const char *fname, Int line);
};
#define LOGBUFFER(buf, len) dumpBufferToLog(buf, len, __FILE__, __LINE__)
#else
#define LOGBUFFER(buf, len)
#endif // DEBUG_LOGGING

inline UnsignedInt AssembleIp(UnsignedByte a, UnsignedByte b, UnsignedByte c, UnsignedByte d)
{
    return ((UnsignedInt)(a) << 24) |
           ((UnsignedInt)(b) << 16) |
           ((UnsignedInt)(c) << 8) |
           ((UnsignedInt)(d));
}
