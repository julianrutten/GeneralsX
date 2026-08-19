# LAN lobby probe harness

`lanprobe` drives the shipping LAN networking code — `Transport`, `UDP` and
`IPEnumeration` — outside the game, so the socket behaviour can be observed
directly instead of reasoned about. It is linked against the real objects from a
`GeneralsXZH` build (it reuses that build's own compile and link lines), so
there is no second copy of the protocol to drift out of sync.

```bash
cmake --preset linux64-deploy
cmake --build build/linux64-deploy --target GeneralsXZH
scripts/qa/lan/build-lanprobe.sh
build/linux64-deploy/lanprobe enumerate
```

## Modes

| Mode | What it does |
|---|---|
| `enumerate` | Prints the wire sizes, the real `IPEnumeration` candidate list in list order, the interfaces the LAN code sees, the destinations `LANSelectBroadcastDestinations()` picks, and what `GetLocalAddressForPeer()` answers. |
| `selftest` | Regression tests for the pure address logic in `NetworkUtil.cpp`. Needs no socket and no second machine, so it covers the multi-homed cases a single-interface host cannot reproduce. Exit code is non-zero on failure. |
| `send <bindIP\|any> <bindPort> <dstIP> <dstPort> <name> [repeats]` | Queues a real `LANMessage` through a real `Transport` and pumps it. Reports whether the out buffer drained. |
| `listen <bindIP\|any> <bindPort> <seconds> <claimedLocalIP>` | Receives through a real `Transport` and decodes each datagram, showing the source address and whether `LANAPI::update()`'s self-echo test would have dropped it. |
| `selfecho <port> <broadcastDst> <claimedLocalIP>` | Broadcasts and listens on one socket, i.e. what a real client sees of its own announce. |
| `discovery <bindPort> <dstPort> <addr/bcast,...> <legacyLocalIP>` | Sends one announce under the pre-fix destination policy and one under the current policy, over real sockets, against a synthetic interface inventory. A `listen` on another port shows which arrived. |
| `bindtwice <port>` | Whether two lobby sockets can share the port. |
| `stucksend <port> <dstIP>` | Whether a send the socket layer rejects ever leaves the out buffer. |

Two "machines" are simulated with two processes on different ports, since a
single host cannot present two lobby endpoints on port 8086 (see the
`bindtwice` result below).

## Observations, 19/08/2026, Linux x86_64, single `eth0` at 10.192.90.5/24

Everything here was produced by running the harness, not by reading code.

1. **The wire format is fine on this platform.** `sizeof(LANMessage) == 471`,
   plus the 6-byte transport header, gives a 477-byte datagram. A message sent
   by one process is decoded by another with the right type, the right name and
   the right source address. Framing, CRC, the XOR/`htonl` pass and the
   `WideCharWindows` conversion all round-trip.

   ```
   send #0 dst=10.192.90.255:9002 bytes=477 queued=1 transportUpdate=1
   RECV len=471 from 10.192.90.5:9001 type=2 name='HostA'
   ```

2. **A socket bound to a unicast address receives no broadcasts at all on
   Linux.** Bound to `10.192.90.5:9004`, the listener received the unicast
   datagram and neither the subnet-directed nor the global broadcast:

   ```
   send  10.192.90.255:9004  -> not received
   send 255.255.255.255:9004 -> not received
   send    10.192.90.5:9004  -> received
   ```

   This is why the POSIX bind was moved to `INADDR_ANY`, and it means the
   upstream `m_transport->init(m_localIP, lobbyPort)` cannot work on Linux.

3. **A client's own broadcast comes back to its own socket**, so the
   `senderIP == m_localIP` self-echo test in `LANAPI::update()` is load bearing.
   With a correct `m_localIP` the echo is dropped; with a wrong one the client
   treats *its own* announce as a remote peer:

   ```
   claimed m_localIP 10.192.90.5 -> own datagram from 10.192.90.5: DROPS it
   claimed m_localIP 172.17.0.1  -> own datagram from 10.192.90.5: does NOT drop it
   ```

4. **Two lobby sockets cannot share a port.** `SO_REUSEADDR` is never set, so
   the second bind fails. Two instances on one machine cannot both enter the LAN
   lobby, which is a large part of why this bug is hard to reproduce.

5. **A send to a subnet the host is not on succeeds silently.** Broadcasting to
   `192.168.254.255` from a host that only has `10.192.90.0/24` returns success
   and drains the out buffer. Nothing anywhere reports that the announce went
   nowhere. So picking the wrong local interface produces a completely silent
   black hole, not an error.

6. **A send to `0.0.0.0` used to wedge an out-buffer slot forever.** `UDP::Write`
   returns `ADDRNOTAVAIL` without ever calling `sendto`, `Transport::doSend`
   leaves `length` non-zero so the slot is never reused, and
   `Transport::update()` still returns `TRUE`, so nothing upstream notices:

   ```
   dst=0.0.0.0 pass=0 transportUpdate=1 outBuffer occupied=1
   dst=0.0.0.0 pass=1 transportUpdate=1 outBuffer occupied=1
   dst=0.0.0.0 pass=2 transportUpdate=1 outBuffer occupied=1
   ```

   `Transport::doSend` now discards a message whose destination address or port
   is zero, so the same run reports `occupied=0`.

## What this container could not test

* No `CAP_NET_ADMIN` and no permission to unshare a network namespace, so a
  second address or a second interface could not be created. The multi-homed
  case — the one where the local address the game picks differs from the source
  address a peer observes — **could not be reproduced end to end here**. The
  address-selection logic is covered by unit tests instead.
* No macOS, no Flatpak sandbox, no second physical machine, no game UI.

## The discovery failure, reproduced

The container cannot create a second interface, but the interface *inventory*
can be supplied by hand while the sends stay real. This is a machine whose
numerically lowest address is a container bridge, which is what the lobby used
to pick, with the real LAN on the second entry:

```
$ lanprobe listen any 9032 4 0.0.0.0 &
$ lanprobe discovery 9031 9032 "172.18.0.1/172.18.255.255,10.192.90.5/10.192.90.255" 172.18.0.1
legacy policy (m_localIP = 172.18.0.1):
  sends to 172.18.255.255 only
fixed policy:
  sends to 172.18.255.255
  sends to 10.192.90.255
out buffer slots still occupied: 0

# what the peer on the real LAN received:
RECV len=471 from 10.192.90.5:9031 type=2 name='fixed'
```

The legacy announce never reaches the peer, and nothing anywhere reports a
problem: `sendto` returns success and the out buffer drains. The fixed policy
announces on both broadcast domains and arrives.

## Which assertions encode which bug

`selftest` is written so that the assertions fail against the code as it was
before the issue #86 work:

* *"announces on every broadcast domain, not just one"* — the old
  `GatherSubnetBroadcastAddrs()` filtered the interface list down to the one
  whose address equalled `m_localIP` and, having found it, suppressed the
  `255.255.255.255` fallback.
* *"never sends to 0.0.0.0"* — the old code passed `ifa_broadaddr` through
  unchecked, and a zero destination wedges a transport slot (observation 6).
* *"an address on another local interface is recognised as ours"* — the old
  self-echo test in `LANAPI::update()` was `senderIP == m_localIP` (observation 3).
