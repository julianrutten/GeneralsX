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
#include <cwchar>

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

// The real interface gather LANAPI uses lives in LANAPI.cpp as a static, so it
// is re-derived here from getifaddrs with the same rules. The two functions it
// feeds - LANSelectBroadcastDestinations and LANIsLocalAddress - are the real
// shared ones out of NetworkUtil.cpp, which is what the selftest exercises.
static Int gatherLocalInterfaces(LANLocalInterface *out, Int maxCount)
{
	Int count = 0;
	struct ifaddrs *ifaddr = nullptr;
	if (getifaddrs(&ifaddr) != 0) return 0;
	for (struct ifaddrs *ifa = ifaddr; ifa != nullptr && count < maxCount; ifa = ifa->ifa_next)
	{
		if (ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_INET) continue;
		if ((ifa->ifa_flags & IFF_UP) == 0) continue;
		const UnsignedInt hostAddr = ntohl(reinterpret_cast<const sockaddr_in *>(ifa->ifa_addr)->sin_addr.s_addr);
		if (hostAddr == 0) continue;
		LANLocalInterface &e = out[count];
		e.address = hostAddr;
		e.broadcast = 0;
		e.canBroadcast = ((ifa->ifa_flags & IFF_LOOPBACK) == 0)
			&& ((ifa->ifa_flags & IFF_POINTOPOINT) == 0)
			&& ((ifa->ifa_flags & IFF_BROADCAST) != 0);
		if (e.canBroadcast) {
			if (ifa->ifa_broadaddr != nullptr && ifa->ifa_broadaddr->sa_family == AF_INET)
				e.broadcast = ntohl(reinterpret_cast<const sockaddr_in *>(ifa->ifa_broadaddr)->sin_addr.s_addr);
			else if (ifa->ifa_netmask != nullptr && ifa->ifa_netmask->sa_family == AF_INET) {
				const UnsignedInt mask = ntohl(reinterpret_cast<const sockaddr_in *>(ifa->ifa_netmask)->sin_addr.s_addr);
				e.broadcast = (hostAddr & mask) | (~mask);
			}
		}
		++count;
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
	printf("--- local interfaces seen by the LAN code ---\n");
	LANLocalInterface ifaces[MAX_LAN_LOCAL_INTERFACES];
	const Int ifaceCount = gatherLocalInterfaces(ifaces, ARRAY_SIZE(ifaces));
	for (Int i = 0; i < ifaceCount; ++i) {
		char a[32], b[32];
		fmtIP(ifaces[i].address, a); fmtIP(ifaces[i].broadcast, b);
		printf("  %-16s broadcast=%-16s canBroadcast=%d\n", a, b, (int)ifaces[i].canBroadcast);
	}
	printf("--- LANSelectBroadcastDestinations() ---\n");
	UnsignedInt dsts[MAX_LAN_LOCAL_INTERFACES + 1];
	const Int dstCount = LANSelectBroadcastDestinations(ifaces, ifaceCount, dsts, ARRAY_SIZE(dsts));
	for (Int i = 0; i < dstCount; ++i) { char d[32]; fmtIP(dsts[i], d); printf("  %s\n", d); }
	printf("--- GetLocalAddressForPeer() ---\n");
	const UnsignedInt probes[] = { 0x08080808u /*8.8.8.8*/, 0x7F000001u /*127.0.0.1*/ };
	for (size_t i = 0; i < ARRAY_SIZE(probes); ++i) {
		char p[32], r[32];
		fmtIP(probes[i], p); fmtIP(GetLocalAddressForPeer(probes[i]), r);
		printf("  peer %-16s -> local %s\n", p, r);
	}
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


// ------------------------------------------------------------- selftest ----
// Regression tests for the pure address logic in NetworkUtil.cpp. These need no
// socket and no second machine, so they cover the multi-homed cases that cannot
// be reproduced by running the harness on a single-interface host.

static int s_failures = 0;

static void check(Bool cond, const char *what)
{
	printf("  %-4s %s\n", cond ? "ok" : "FAIL", what);
	if (!cond) ++s_failures;
}

static LANLocalInterface mkIface(const char *addr, const char *bcast, Bool canBroadcast)
{
	LANLocalInterface i;
	i.address = parseIP(addr);
	i.broadcast = bcast ? parseIP(bcast) : 0;
	i.canBroadcast = canBroadcast;
	return i;
}

static int modeSelfTest()
{
	UnsignedInt out[MAX_LAN_LOCAL_INTERFACES + 1];

	printf("LANSelectBroadcastDestinations\n");
	{
		// Windows and any other caller with no interface list must keep the
		// retail behaviour: one limited broadcast.
		Int n = LANSelectBroadcastDestinations(nullptr, 0, out, ARRAY_SIZE(out));
		check(n == 1 && out[0] == INADDR_BROADCAST, "empty interface list yields 255.255.255.255");
	}
	{
		// The issue #86 shape: a developer/container box where the numerically
		// lowest address is a bridge and the real LAN is somewhere else. Both
		// broadcast domains have to be announced on, in any order.
		LANLocalInterface ifaces[] = {
			mkIface("172.18.0.1", "172.18.255.255", TRUE),   // docker-compose br-xxxx
			mkIface("192.168.1.10", "192.168.1.255", TRUE),  // the actual LAN
		};
		Int n = LANSelectBroadcastDestinations(ifaces, 2, out, ARRAY_SIZE(out));
		Bool sawLAN = FALSE, sawBridge = FALSE;
		for (Int i = 0; i < n; ++i) {
			if (out[i] == parseIP("192.168.1.255")) sawLAN = TRUE;
			if (out[i] == parseIP("172.18.255.255")) sawBridge = TRUE;
		}
		check(n == 2 && sawLAN && sawBridge, "announces on every broadcast domain, not just one");
	}
	{
		// A zero broadcast address must never be queued: UDP::Write rejects it
		// before sendto and the transport slot would be wedged for good.
		LANLocalInterface ifaces[] = { mkIface("10.0.0.5", nullptr, TRUE) };
		Int n = LANSelectBroadcastDestinations(ifaces, 1, out, ARRAY_SIZE(out));
		check(n == 1 && out[0] == INADDR_BROADCAST, "interface with no broadcast address falls back, never sends to 0.0.0.0");
	}
	{
		// Loopback and point-to-point tunnels are not broadcast domains.
		LANLocalInterface ifaces[] = {
			mkIface("127.0.0.1", "127.255.255.255", FALSE),
			mkIface("10.8.0.2", "10.8.0.255", FALSE),
		};
		Int n = LANSelectBroadcastDestinations(ifaces, 2, out, ARRAY_SIZE(out));
		check(n == 1 && out[0] == INADDR_BROADCAST, "non-broadcast interfaces are skipped");
	}
	{
		// Two addresses on one subnet must not produce two identical sends.
		LANLocalInterface ifaces[] = {
			mkIface("192.168.1.10", "192.168.1.255", TRUE),
			mkIface("192.168.1.11", "192.168.1.255", TRUE),
		};
		Int n = LANSelectBroadcastDestinations(ifaces, 2, out, ARRAY_SIZE(out));
		check(n == 1 && out[0] == parseIP("192.168.1.255"), "duplicate broadcast addresses collapse");
	}
	{
		LANLocalInterface ifaces[] = {
			mkIface("192.168.1.10", "192.168.1.255", TRUE),
			mkIface("172.18.0.1", "172.18.255.255", TRUE),
		};
		Int n = LANSelectBroadcastDestinations(ifaces, 2, out, 1);
		check(n == 1, "respects maxAddrs");
	}

	printf("LANIsLocalAddress\n");
	{
		LANLocalInterface ifaces[] = {
			mkIface("172.18.0.1", "172.18.255.255", TRUE),
			mkIface("192.168.1.10", "192.168.1.255", TRUE),
		};
		// The self-echo test used to compare only against the one selected
		// address, so a client whose broadcast went out of the other interface
		// listed its own announce as a remote player.
		check(LANIsLocalAddress(ifaces, 2, parseIP("172.18.0.1"), parseIP("192.168.1.10")),
			"an address on another local interface is recognised as ours");
		check(LANIsLocalAddress(ifaces, 2, parseIP("172.18.0.1"), parseIP("172.18.0.1")),
			"the selected address is ours");
		check(!LANIsLocalAddress(ifaces, 2, parseIP("172.18.0.1"), parseIP("192.168.1.11")),
			"a peer address is not ours");
		check(!LANIsLocalAddress(ifaces, 2, parseIP("172.18.0.1"), 0),
			"0.0.0.0 is not ours");
		// With no interface list the behaviour degrades to plain equality,
		// which is what the Windows path relies on.
		check(LANIsLocalAddress(nullptr, 0, parseIP("192.168.1.10"), parseIP("192.168.1.10")),
			"with no interface list, equality with the selected address still holds");
		check(!LANIsLocalAddress(nullptr, 0, parseIP("192.168.1.10"), parseIP("192.168.1.11")),
			"with no interface list, other addresses are not ours");
	}

	printf("GetLocalAddressForPeer\n");
	{
		check(GetLocalAddressForPeer(0) == 0, "no answer for 0.0.0.0");
		check(GetLocalAddressForPeer(INADDR_BROADCAST) == 0, "no answer for the broadcast address");
		check(GetLocalAddressForPeer(parseIP("127.0.0.1")) == parseIP("127.0.0.1"),
			"loopback peer resolves to the loopback address");
		LANLocalInterface ifaces[MAX_LAN_LOCAL_INTERFACES];
		Int n = gatherLocalInterfaces(ifaces, ARRAY_SIZE(ifaces));
		UnsignedInt routed = GetLocalAddressForPeer(parseIP("8.8.8.8"));
		check(routed == 0 || LANIsLocalAddress(ifaces, n, 0, routed),
			"the routed source address is one of this machine's addresses");
	}


	printf("wide char wire conversion\n");
	{
		// LANAPI passes the capacity of the destination field, not the length of
		// the string. Copying a fixed count read past the end of the source, which
		// AddressSanitizer flags as a heap-buffer-overflow read and which put
		// adjacent heap into every outgoing packet.
		WideCharWindows dest[101];
		for (size_t i = 0; i < ARRAY_SIZE(dest); ++i) dest[i] = 0xBEEF;
		CopyWcharToWindowsWideChar(dest, L"hi", ARRAY_SIZE(dest) - 1);
		check(dest[0] == L'h' && dest[1] == L'i' && dest[2] == 0,
			"a short string is copied and terminated at its own length");
		check(dest[3] == 0xBEEF, "nothing is written past the terminator");

		// Exactly filling the field must still leave room for the terminator.
		WideCharWindows small[4];
		CopyWcharToWindowsWideChar(small, L"abcdef", ARRAY_SIZE(small) - 1);
		check(small[0] == L'a' && small[2] == L'c' && small[3] == 0,
			"an over-long string is truncated to the field capacity");

		CopyWcharToWindowsWideChar(small, L"", ARRAY_SIZE(small) - 1);
		check(small[0] == 0, "an empty string writes just a terminator");
	}
	{
		WideCharWindows name[13];
		CopyWcharToWindowsWideChar(name, L"Player", ARRAY_SIZE(name) - 1);
		check(wcscmp(GetWindowsWideCharAsWchar(name), L"Player") == 0, "round trips a name");

		// A field that arrives without a terminator used to send the length scan
		// off the end of the packet, and the bounds test was off by one, so a
		// string of exactly MAX_COMPUTERNAME_LENGTH wrote one past the static
		// buffer. Callers never checked the null it returned either.
		WideCharWindows unterminated[MAX_COMPUTERNAME_LENGTH + 8];
		for (size_t i = 0; i < ARRAY_SIZE(unterminated); ++i) unterminated[i] = L'x';
		wchar_t *out = GetWindowsWideCharAsWchar(unterminated);
		check(out != nullptr, "an unterminated field does not yield a null pointer");
		check(wcslen(out) == MAX_COMPUTERNAME_LENGTH - 1, "an unterminated field is truncated, not overrun");
	}

	printf("%s (%d failure%s)\n", s_failures ? "SELFTEST FAILED" : "selftest passed",
		s_failures, (s_failures == 1) ? "" : "s");
	return s_failures ? 1 : 0;
}

int main(int argc, char **argv)
{
	__argc = argc; __argv = argv;
	if (argc < 2) {
		fprintf(stderr,
			"usage:\n"
			"  lanprobe enumerate\n"
			"  lanprobe selftest\n"
			"  lanprobe send   <bindIP|any> <bindPort> <dstIP> <dstPort> <name> [repeats]\n"
			"  lanprobe listen <bindIP|any> <bindPort> <seconds> <claimedLocalIP>\n"
			"  lanprobe selfecho  <port> <broadcastDst> <claimedLocalIP>\n"
			"  lanprobe bindtwice <port>\n"
			"  lanprobe stucksend <port> <dstIP>\n");
		return 2;
	}
	if (!strcmp(argv[1], "enumerate")) return modeEnumerate();
	if (!strcmp(argv[1], "selftest")) return modeSelfTest();
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
