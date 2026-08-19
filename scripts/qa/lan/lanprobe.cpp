// LAN lobby probe harness - drives the real Transport/UDP/IPEnumeration code.
#include "PreRTS.h"
#include "GameNetwork/Transport.h"
#include "GameNetwork/LANAPI.h"
#include "GameNetwork/IPEnumeration.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ifaddrs.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <unistd.h>

// --- globals the engine archives expect from the game's main TU ---
const char *g_csfFile = "data/%s/Generals.csf";
const char *g_strFile = "data/Generals.str";
int __argc = 0;
char **__argv = nullptr;
void *ApplicationHWnd = nullptr;
void *TheSDL3Window = nullptr;

static const UnsignedShort kLobbyPort = 8086;

static void fmtIP(UnsignedInt ip, char *out) {
	sprintf(out, "%d.%d.%d.%d", PRINTF_IP_AS_4_INTS(ip));
}

// Verbatim copy of LANAPI.cpp's static GatherSubnetBroadcastAddrs so we can
// observe what the shipping send path would choose for a given m_localIP.
static Int GatherSubnetBroadcastAddrs(UnsignedInt localIP, UnsignedInt *outAddrs, Int maxAddrs)
{
	if (outAddrs == nullptr || maxAddrs <= 0) return 0;
	Int count = 0;
	struct ifaddrs *ifaddr = nullptr;
	if (getifaddrs(&ifaddr) != 0) return 0;
	for (struct ifaddrs *ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next)
	{
		if (ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_INET) continue;
		if ((ifa->ifa_flags & IFF_UP) == 0 || (ifa->ifa_flags & IFF_LOOPBACK) != 0) continue;
		const sockaddr_in *addr = reinterpret_cast<const sockaddr_in *>(ifa->ifa_addr);
		const UnsignedInt hostAddr = ntohl(addr->sin_addr.s_addr);
		if (localIP != 0 && hostAddr != localIP) continue;
		UnsignedInt bcast = 0;
		if (ifa->ifa_broadaddr != nullptr && ifa->ifa_broadaddr->sa_family == AF_INET) {
			bcast = ntohl(reinterpret_cast<const sockaddr_in *>(ifa->ifa_broadaddr)->sin_addr.s_addr);
		} else if (ifa->ifa_netmask != nullptr && ifa->ifa_netmask->sa_family == AF_INET) {
			const UnsignedInt mask = ntohl(reinterpret_cast<const sockaddr_in *>(ifa->ifa_netmask)->sin_addr.s_addr);
			bcast = (hostAddr & mask) | (~mask);
		} else continue;
		Bool dup = FALSE;
		for (Int i = 0; i < count; ++i) if (outAddrs[i] == bcast) { dup = TRUE; break; }
		if (!dup && count < maxAddrs) outAddrs[count++] = bcast;
	}
	freeifaddrs(ifaddr);
	return count;
}

// ---------------------------------------------------------------- modes ----

static int modeEnumerate()
{
	printf("sizeof(LANMessage)      = %zu\n", sizeof(LANMessage));
	printf("sizeof(TransportMessage)= %zu\n", sizeof(TransportMessage));
	printf("sizeof(TransportHeader) = %zu\n", sizeof(TransportMessageHeader));
	printf("MAX_PACKET_SIZE         = %d\n", MAX_PACKET_SIZE);
	printf("MAX_NETWORK_MESSAGE_LEN = %d\n", MAX_NETWORK_MESSAGE_LEN);
	printf("--- IPEnumeration::getAddresses() (in list order) ---\n");
	IPEnumeration ips;
	EnumeratedIP *list = ips.getAddresses();
	int n = 0;
	for (EnumeratedIP *p = list; p != nullptr; p = p->getNext()) {
		printf("  [%d] %-16s 0x%08X%s\n", n, p->getIPstring().str(), p->getIP(),
			(n == 0) ? "   <-- LanLobbyMenuInit fallback picks this one" : "");
		++n;
	}
	if (n == 0) printf("  (none)\n");
	printf("--- GatherSubnetBroadcastAddrs() per candidate ---\n");
	for (EnumeratedIP *p = list; p != nullptr; p = p->getNext()) {
		UnsignedInt b[8];
		Int c = GatherSubnetBroadcastAddrs(p->getIP(), b, 8);
		char a[32]; fmtIP(p->getIP(), a);
		printf("  localIP=%-16s -> %d dst(s):", a, c);
		for (Int i = 0; i < c; ++i) { char d[32]; fmtIP(b[i], d); printf(" %s", d); }
		if (c == 0) printf(" (none -> falls back to 255.255.255.255)");
		printf("\n");
	}
	UnsignedInt b[8];
	Int c = GatherSubnetBroadcastAddrs(0xC0A8FE01u /* 192.168.254.1, not on this host */, b, 8);
	printf("  localIP=192.168.254.1  -> %d dst(s)%s\n", c,
		(c == 0) ? " (none -> falls back to 255.255.255.255)" : "");
	return 0;
}

static void fillMessage(LANMessage *msg, LANMessage::Type type, const wchar_t *name)
{
	memset(msg, 0, sizeof(*msg));
	msg->messageType = type;
	CopyWcharToWindowsWideChar(msg->name, name, ARRAY_SIZE(msg->name) - 1);
	msg->userName[0] = 0;
	msg->hostName[0] = 0;
}

static int modeSend(UnsignedInt bindIP, UnsignedShort bindPort, UnsignedInt dstIP,
                    UnsignedShort dstPort, const wchar_t *name, int repeats)
{
	Transport t;
	if (!t.init(bindIP, bindPort)) { printf("BIND FAILED\n"); return 1; }
	if (!t.allowBroadcasts(true)) printf("WARNING: SO_BROADCAST not set\n");
	LANMessage msg;
	fillMessage(&msg, LANMessage::MSG_LOBBY_ANNOUNCE, name);
	char d[32]; fmtIP(dstIP, d);
	for (int i = 0; i < repeats; ++i) {
		Bool q = t.queueSend(dstIP, dstPort, (unsigned char *)&msg, sizeof(LANMessage));
		Bool u = t.update();
		printf("send #%d dst=%s:%d bytes=%zu queued=%d transportUpdate=%d\n",
			i, d, dstPort, sizeof(LANMessage) + sizeof(TransportMessageHeader), (int)q, (int)u);
		int stuck = 0;
		for (size_t k = 0; k < ARRAY_SIZE(t.m_outBuffer); ++k) if (t.m_outBuffer[k].length > 0) ++stuck;
		printf("        outBuffer slots still occupied after update: %d\n", stuck);
		usleep(200 * 1000);
	}
	return 0;
}

static int modeListen(UnsignedInt bindIP, UnsignedShort bindPort, int seconds, UnsignedInt claimedLocalIP)
{
	Transport t;
	if (!t.init(bindIP, bindPort)) { printf("BIND FAILED\n"); return 1; }
	if (!t.allowBroadcasts(true)) printf("WARNING: SO_BROADCAST not set\n");
	char cl[32]; fmtIP(claimedLocalIP, cl);
	printf("listening on port %d, pretending m_localIP == %s\n", bindPort, cl);
	fflush(stdout);
	int got = 0;
	for (int i = 0; i < seconds * 5; ++i) {
		t.update();
		for (size_t k = 0; k < ARRAY_SIZE(t.m_inBuffer); ++k) {
			if (t.m_inBuffer[k].length <= 0) break;
			LANMessage *m = (LANMessage *)(t.m_inBuffer[k].data);
			char s[32]; fmtIP(t.m_inBuffer[k].addr, s);
			printf("RECV len=%d from %s:%d type=%d name='%ls' selfEchoDropped=%s\n",
				t.m_inBuffer[k].length, s, t.m_inBuffer[k].port, (int)m->messageType,
				GetWindowsWideCharAsWchar(m->name),
				(t.m_inBuffer[k].addr == claimedLocalIP) ? "YES" : "no");
			fflush(stdout);
			t.m_inBuffer[k].length = 0;
			++got;
		}
		usleep(200 * 1000);
	}
	printf("total accepted datagrams: %d\n", got);
	return 0;
}


// One socket that both broadcasts and listens on the same port: reproduces what
// a real client sees of its own announce traffic.
static int modeSelfEcho(UnsignedShort port, UnsignedInt dstIP, UnsignedInt claimedLocalIP)
{
	Transport t;
	if (!t.init(0 /*INADDR_ANY*/, port)) { printf("BIND FAILED\n"); return 1; }
	if (!t.allowBroadcasts(true)) printf("WARNING: SO_BROADCAST not set\n");
	LANMessage msg;
	fillMessage(&msg, LANMessage::MSG_LOBBY_ANNOUNCE, L"Self");
	char d[32]; fmtIP(dstIP, d);
	char cl[32]; fmtIP(claimedLocalIP, cl);
	t.queueSend(dstIP, port, (unsigned char *)&msg, sizeof(LANMessage));
	t.update();
	printf("broadcast to %s:%d from a socket bound to 0.0.0.0:%d, m_localIP claimed as %s\n",
		d, port, port, cl);
	int got = 0;
	for (int i = 0; i < 10; ++i) {
		t.update();
		for (size_t k = 0; k < ARRAY_SIZE(t.m_inBuffer); ++k) {
			if (t.m_inBuffer[k].length <= 0) break;
			char s[32]; fmtIP(t.m_inBuffer[k].addr, s);
			printf("  own datagram came back from %s:%d -> LANAPI::update self-echo test %s\n",
				s, t.m_inBuffer[k].port,
				(t.m_inBuffer[k].addr == claimedLocalIP) ? "DROPS it (correct)"
				                                         : "does NOT drop it (treated as a remote peer)");
			t.m_inBuffer[k].length = 0;
			++got;
		}
		usleep(100 * 1000);
	}
	if (got == 0) printf("  own datagram did not come back\n");
	return 0;
}

// Can two LANAPI instances share the lobby port? (SO_REUSEADDR is never set.)
static int modeBindTwice(UnsignedShort port)
{
	Transport a, b;
	Bool ra = a.init(0, port);
	Bool rb = b.init(0, port);
	printf("first  bind 0.0.0.0:%d -> %s\n", port, ra ? "ok" : "FAILED");
	printf("second bind 0.0.0.0:%d -> %s\n", port, rb ? "ok" : "FAILED");
	return 0;
}

// A destination the socket layer rejects: does the queued message ever leave
// m_outBuffer, or does it wedge the slot forever?
static int modeStuckSend(UnsignedShort port, UnsignedInt dstIP)
{
	Transport t;
	if (!t.init(0, port)) { printf("BIND FAILED\n"); return 1; }
	t.allowBroadcasts(true);
	LANMessage msg;
	fillMessage(&msg, LANMessage::MSG_LOBBY_ANNOUNCE, L"Stuck");
	char d[32]; fmtIP(dstIP, d);
	t.queueSend(dstIP, port, (unsigned char *)&msg, sizeof(LANMessage));
	for (int pass = 0; pass < 3; ++pass) {
		Bool u = t.update();
		int occupied = 0;
		for (size_t k = 0; k < ARRAY_SIZE(t.m_outBuffer); ++k) if (t.m_outBuffer[k].length > 0) ++occupied;
		printf("dst=%s pass=%d transportUpdate=%d outBuffer occupied=%d\n", d, pass, (int)u, occupied);
	}
	return 0;
}

static UnsignedInt parseIP(const char *s)
{
	unsigned a, b, c, d;
	if (sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return 0;
	return (a << 24) | (b << 16) | (c << 8) | d;
}

int main(int argc, char **argv)
{
	__argc = argc; __argv = argv;
	if (argc < 2) {
		fprintf(stderr,
			"usage:\n"
			"  lanprobe enumerate\n"
			"  lanprobe send   <bindIP|any> <bindPort> <dstIP> <dstPort> <name> [repeats]\n"
			"  lanprobe listen <bindIP|any> <bindPort> <seconds> <claimedLocalIP>\n"
			"  lanprobe selfecho  <port> <broadcastDst> <claimedLocalIP>\n"
			"  lanprobe bindtwice <port>\n"
			"  lanprobe stucksend <port> <dstIP>\n");
		return 2;
	}
	if (!strcmp(argv[1], "enumerate")) return modeEnumerate();
	if (!strcmp(argv[1], "send") && argc >= 7) {
		UnsignedInt bind = strcmp(argv[2], "any") ? parseIP(argv[2]) : 0;
		wchar_t wname[64];
		mbstowcs(wname, argv[6], 63); wname[63] = 0;
		return modeSend(bind, (UnsignedShort)atoi(argv[3]), parseIP(argv[4]),
			(UnsignedShort)atoi(argv[5]), wname, (argc > 7) ? atoi(argv[7]) : 1);
	}
	if (!strcmp(argv[1], "listen") && argc >= 6) {
		UnsignedInt bind = strcmp(argv[2], "any") ? parseIP(argv[2]) : 0;
		return modeListen(bind, (UnsignedShort)atoi(argv[3]), atoi(argv[4]), parseIP(argv[5]));
	}
	if (!strcmp(argv[1], "selfecho") && argc >= 5)
		return modeSelfEcho((UnsignedShort)atoi(argv[2]), parseIP(argv[3]), parseIP(argv[4]));
	if (!strcmp(argv[1], "bindtwice") && argc >= 3)
		return modeBindTwice((UnsignedShort)atoi(argv[2]));
	if (!strcmp(argv[1], "stucksend") && argc >= 4)
		return modeStuckSend((UnsignedShort)atoi(argv[2]), parseIP(argv[3]));
	fprintf(stderr, "bad arguments\n");
	return 2;
}
