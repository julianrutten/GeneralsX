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


#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameNetwork/networkutil.h"

// TheSuperHackers @tweak Mauller 26/08/2025 reduce the minimum runahead from 10
// This lets network games run at latencies down to 133ms when the network conditions allow
Int MIN_LOGIC_FRAMES = 5;
Int MAX_FRAMES_AHEAD = 128;
Int MIN_RUNAHEAD = 4;
Int FRAME_DATA_LENGTH = (MAX_FRAMES_AHEAD+1)*2;
Int FRAMES_TO_KEEP = (MAX_FRAMES_AHEAD/2) + 1;

#ifdef DEBUG_LOGGING

void dumpBufferToLog(const void *vBuf, Int len, const char *fname, Int line)
{
	DEBUG_LOG(("======= dumpBufferToLog() %d bytes =======", len));
	DEBUG_LOG(("Source: %s:%d", fname, line));
	const char *buf = (const char *)vBuf;
	Int numLines = len / 8;
	if ((len % 8) != 0)
	{
		++numLines;
	}
	for (Int dumpindex = 0; dumpindex < numLines; ++dumpindex)
	{
		Int offset = dumpindex*8;
		DEBUG_LOG_RAW(("\t%5.5d\t", offset));
		Int dumpindex2;
		Int numBytesThisLine = min(8, len - offset);
		for (dumpindex2 = 0; dumpindex2 < numBytesThisLine; ++dumpindex2)
		{
			Int c = (buf[offset + dumpindex2] & 0xff);
			DEBUG_LOG_RAW(("%02X ", c));
		}
		for (; dumpindex2 < 8; ++dumpindex2)
		{
			DEBUG_LOG_RAW(("   "));
		}
		DEBUG_LOG_RAW((" | "));
		for (dumpindex2 = 0; dumpindex2 < numBytesThisLine; ++dumpindex2)
		{
			char c = buf[offset + dumpindex2];
			DEBUG_LOG_RAW(("%c", (isprint(c)?c:'.')));
		}
		DEBUG_LOG_RAW(("\n"));
	}
	DEBUG_LOG(("End of packet dump"));
}

#endif // DEBUG_LOGGING

/**
 * ResolveIP turns a string ("games2.westwood.com", or "192.168.0.1") into
 * a 32-bit unsigned integer.
 */
UnsignedInt ResolveIP(AsciiString host)
{
  struct hostent *hostStruct;
  struct in_addr *hostNode;

  if (host.isEmpty())
  {
	  DEBUG_LOG(("ResolveIP(): Can't resolve null"));
	  return 0;
  }

  // String such as "127.0.0.1"
  if (isdigit(host.getCharAt(0)))
  {
    return ( ntohl(inet_addr(host.str())) );
  }

  // String such as "localhost"
  hostStruct = gethostbyname(host.str());
  if (hostStruct == nullptr)
  {
	  DEBUG_LOG(("ResolveIP(): Can't resolve %s", host.str()));
	  return 0;
  }
  hostNode = (struct in_addr *) hostStruct->h_addr;
  return ( ntohl(hostNode->s_addr) );
}

// GeneralsX @bugfix Claude 19/08/2026 Shared, testable helpers for LAN lobby address handling (issue #86).

/**
 * Collect the destinations a LAN discovery/announce packet has to be sent to.
 *
 * The lobby used to send to the subnet broadcast of one hand-picked interface
 * and, when it found one, skip the global broadcast entirely. That makes the
 * whole of discovery depend on having guessed the right interface, and a
 * broadcast sent to a subnet this machine is not on is accepted by the socket
 * layer without any error, so a wrong guess is completely silent. Announce on
 * every broadcast domain we are actually attached to instead.
 */
Int LANSelectBroadcastDestinations( const LANLocalInterface *ifaces, Int ifaceCount,
																		UnsignedInt *outAddrs, Int maxAddrs )
{
	if (outAddrs == nullptr || maxAddrs <= 0)
	{
		return 0;
	}

	if (ifaces == nullptr)
	{
		ifaceCount = 0;
	}

	Int count = 0;

	for (Int i = 0; (i < ifaceCount) && (count < maxAddrs); ++i)
	{
		const UnsignedInt bcast = ifaces[i].broadcast;

		// A zero broadcast address is not a destination. Queuing one wedges a
		// transport out-buffer slot for good, because UDP::Write rejects it
		// before sendto and Transport::doSend only frees the slot on success.
		if (!ifaces[i].canBroadcast || bcast == 0)
		{
			continue;
		}

		Bool duplicate = FALSE;
		for (Int j = 0; j < count; ++j)
		{
			if (outAddrs[j] == bcast)
			{
				duplicate = TRUE;
				break;
			}
		}

		if (!duplicate)
		{
			outAddrs[count++] = bcast;
		}
	}

	// No usable interface broadcast address, so fall back to the limited
	// broadcast address. This is also the Windows path, which passes no
	// interfaces at all and therefore keeps its retail behaviour exactly.
	if (count == 0)
	{
		outAddrs[count++] = INADDR_BROADCAST;
	}

	return count;
}

/**
 * TRUE when addr belongs to this machine.
 *
 * LANAPI drops datagrams from itself by comparing the source address against
 * the single address it picked for itself. Broadcasts loop back to the sending
 * host, so on a machine with more than one address that test can fail and the
 * client ends up listing its own announce as a remote peer.
 */
Bool LANIsLocalAddress( const LANLocalInterface *ifaces, Int ifaceCount,
												UnsignedInt selectedLocalIP, UnsignedInt addr )
{
	if (addr == 0)
	{
		return FALSE;
	}

	if (addr == selectedLocalIP)
	{
		return TRUE;
	}

	if (ifaces == nullptr)
	{
		return FALSE;
	}

	for (Int i = 0; i < ifaceCount; ++i)
	{
		if (ifaces[i].address == addr)
		{
			return TRUE;
		}
	}

	return FALSE;
}

/**
 * Ask the routing table which local address we would send from when talking to
 * peerIP. connect() on a datagram socket transmits nothing; it just binds the
 * local end, which is exactly the address the peer will see as our source.
 */
UnsignedInt GetLocalAddressForPeer( UnsignedInt peerIP )
{
	if (peerIP == 0 || peerIP == INADDR_BROADCAST)
	{
		return 0;
	}

	const SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == INVALID_SOCKET)
	{
		return 0;
	}

	sockaddr_in peer;
	memset(&peer, 0, sizeof(peer));
	peer.sin_family = AF_INET;
	peer.sin_addr.s_addr = htonl(peerIP);
	peer.sin_port = htons(1);	// never used, connect() on UDP sends nothing

	UnsignedInt localIP = 0;
	if (connect(sock, (struct sockaddr *)&peer, sizeof(peer)) == 0)
	{
		sockaddr_in local;
		memset(&local, 0, sizeof(local));
		socklen_t localLen = sizeof(local);
		if (getsockname(sock, (struct sockaddr *)&local, &localLen) == 0
			&& local.sin_family == AF_INET)
		{
			localIP = ntohl(local.sin_addr.s_addr);
		}
	}

	closesocket(sock);

	// A connect() to an address the host has no route to yields nothing usable.
	if (localIP == INADDR_ANY || localIP == INADDR_NONE)
	{
		return 0;
	}

	return localIP;
}

/**
 * Returns the next network command ID.
 */
static UnsignedShort s_commandID = 0;
UnsignedShort GenerateNextCommandID()
{
	return s_commandID++;
}

/**
 * Returns true if this type of command requires a unique command ID.
 */
Bool DoesCommandRequireACommandID(NetCommandType type) {
	if ((type == NETCOMMANDTYPE_GAMECOMMAND) ||
			(type == NETCOMMANDTYPE_FRAMEINFO) ||
			(type == NETCOMMANDTYPE_PLAYERLEAVE) ||
			(type == NETCOMMANDTYPE_DESTROYPLAYER) ||
			(type == NETCOMMANDTYPE_RUNAHEADMETRICS) ||
			(type == NETCOMMANDTYPE_RUNAHEAD) ||
			(type == NETCOMMANDTYPE_CHAT) ||
			(type == NETCOMMANDTYPE_DISCONNECTVOTE) ||
			(type == NETCOMMANDTYPE_LOADCOMPLETE) ||
			(type == NETCOMMANDTYPE_TIMEOUTSTART) ||
			(type == NETCOMMANDTYPE_WRAPPER) ||
			(type == NETCOMMANDTYPE_FILE) ||
			(type == NETCOMMANDTYPE_FILEANNOUNCE) ||
			(type == NETCOMMANDTYPE_FILEPROGRESS) ||
			(type == NETCOMMANDTYPE_DISCONNECTPLAYER) ||
			(type == NETCOMMANDTYPE_DISCONNECTFRAME) ||
			(type == NETCOMMANDTYPE_DISCONNECTSCREENOFF) ||
			(type == NETCOMMANDTYPE_FRAMERESENDREQUEST))
	{
		return TRUE;
	}
	return FALSE;
}

/**
 * Returns true if this type of network command requires an ack.
 */
Bool CommandRequiresAck(NetCommandMsg *msg) {
	if ((msg->getNetCommandType() == NETCOMMANDTYPE_GAMECOMMAND) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_FRAMEINFO) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_PLAYERLEAVE) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_DESTROYPLAYER) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_RUNAHEADMETRICS) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_RUNAHEAD) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_CHAT) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_DISCONNECTVOTE) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_DISCONNECTPLAYER) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_LOADCOMPLETE) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_TIMEOUTSTART) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_WRAPPER) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_FILE) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_FILEANNOUNCE) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_FILEPROGRESS) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_DISCONNECTPLAYER) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_DISCONNECTFRAME) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_DISCONNECTSCREENOFF) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_FRAMERESENDREQUEST))
	{
		return TRUE;
	}
	return FALSE;
}

Bool IsCommandSynchronized(NetCommandType type) {
	if ((type == NETCOMMANDTYPE_GAMECOMMAND) ||
			(type == NETCOMMANDTYPE_FRAMEINFO) ||
			(type == NETCOMMANDTYPE_PLAYERLEAVE) ||
			(type == NETCOMMANDTYPE_DESTROYPLAYER) ||
			(type == NETCOMMANDTYPE_RUNAHEAD))
	{
		return TRUE;
	}
	return FALSE;
}

/**
 * Returns true if this type of network command requires the ack to be sent directly to the player
 * rather than going through the packet router.  This should really only be used by commands
 * used on the disconnect screen.
 */
Bool CommandRequiresDirectSend(NetCommandMsg *msg) {
	if ((msg->getNetCommandType() == NETCOMMANDTYPE_DISCONNECTVOTE) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_DISCONNECTPLAYER) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_LOADCOMPLETE) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_TIMEOUTSTART) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_FILE) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_FILEANNOUNCE) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_FILEPROGRESS) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_DISCONNECTFRAME) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_DISCONNECTSCREENOFF) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_FRAMERESENDREQUEST)) {
		return TRUE;
	}
	return FALSE;
}

const char* GetNetCommandTypeAsString(NetCommandType type) {

	switch (type) {
	case NETCOMMANDTYPE_ACKBOTH:
		return "NETCOMMANDTYPE_ACKBOTH";
	case NETCOMMANDTYPE_ACKSTAGE1:
		return "NETCOMMANDTYPE_ACKSTAGE1";
	case NETCOMMANDTYPE_ACKSTAGE2:
		return "NETCOMMANDTYPE_ACKSTAGE2";
	case NETCOMMANDTYPE_FRAMEINFO:
		return "NETCOMMANDTYPE_FRAMEINFO";
	case NETCOMMANDTYPE_GAMECOMMAND:
		return "NETCOMMANDTYPE_GAMECOMMAND";
	case NETCOMMANDTYPE_PLAYERLEAVE:
		return "NETCOMMANDTYPE_PLAYERLEAVE";
	case NETCOMMANDTYPE_RUNAHEADMETRICS:
		return "NETCOMMANDTYPE_RUNAHEADMETRICS";
	case NETCOMMANDTYPE_RUNAHEAD:
		return "NETCOMMANDTYPE_RUNAHEAD";
	case NETCOMMANDTYPE_DESTROYPLAYER:
		return "NETCOMMANDTYPE_DESTROYPLAYER";
	case NETCOMMANDTYPE_KEEPALIVE:
		return "NETCOMMANDTYPE_KEEPALIVE";
	case NETCOMMANDTYPE_DISCONNECTCHAT:
		return "NETCOMMANDTYPE_DISCONNECTCHAT";
	case NETCOMMANDTYPE_CHAT:
		return "NETCOMMANDTYPE_CHAT";
	case NETCOMMANDTYPE_MANGLERQUERY:
		return "NETCOMMANDTYPE_MANGLERQUERY";
	case NETCOMMANDTYPE_MANGLERRESPONSE:
		return "NETCOMMANDTYPE_MANGLERRESPONSE";
	case NETCOMMANDTYPE_PROGRESS:
		return "NETCOMMANDTYPE_PROGRESS";
	case NETCOMMANDTYPE_LOADCOMPLETE:
		return "NETCOMMANDTYPE_LOADCOMPLETE";
	case NETCOMMANDTYPE_TIMEOUTSTART:
		return "NETCOMMANDTYPE_TIMEOUTSTART";
	case NETCOMMANDTYPE_WRAPPER:
		return "NETCOMMANDTYPE_WRAPPER";
	case NETCOMMANDTYPE_FILE:
		return "NETCOMMANDTYPE_FILE";
	case NETCOMMANDTYPE_FILEANNOUNCE:
		return "NETCOMMANDTYPE_FILEANNOUNCE";
	case NETCOMMANDTYPE_FILEPROGRESS:
		return "NETCOMMANDTYPE_FILEPROGRESS";
	case NETCOMMANDTYPE_DISCONNECTKEEPALIVE:
		return "NETCOMMANDTYPE_DISCONNECTKEEPALIVE";
	case NETCOMMANDTYPE_DISCONNECTPLAYER:
		return "NETCOMMANDTYPE_DISCONNECTPLAYER";
	case NETCOMMANDTYPE_PACKETROUTERQUERY:
		return "NETCOMMANDTYPE_PACKETROUTERQUERY";
	case NETCOMMANDTYPE_PACKETROUTERACK:
		return "NETCOMMANDTYPE_PACKETROUTERACK";
	case NETCOMMANDTYPE_DISCONNECTVOTE:
		return "NETCOMMANDTYPE_DISCONNECTVOTE";
	case NETCOMMANDTYPE_DISCONNECTFRAME:
		return "NETCOMMANDTYPE_DISCONNECTFRAME";
	case NETCOMMANDTYPE_DISCONNECTSCREENOFF:
		return "NETCOMMANDTYPE_DISCONNECTSCREENOFF";
	case NETCOMMANDTYPE_FRAMERESENDREQUEST:
		return "NETCOMMANDTYPE_FRAMERESENDREQUEST";
	default:
		DEBUG_CRASH(("Unknown NetCommandType in GetNetCommandTypeAsString"));
		return "UNKNOWN";
	}

}
