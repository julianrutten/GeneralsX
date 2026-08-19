# LAN Networking: How Discovery and Direct-IP Join Actually Work

Reference for anyone working on the LAN lobby (issue #86 and friends). This
describes the code as it exists on `fix/lan-multiplayer`, not as it is supposed
to work. Read this before changing anything in `Core/GameEngine/*/GameNetwork/`.

Everything below was derived by reading the source. Sections that were verified
by running code are marked **[verified]**; sections that are reasoning about
code that was not executed are marked **[inferred]**.

## 1. The pieces

| Layer | File | Responsibility |
|-------|------|----------------|
| `UDP` | `Core/GameEngine/Source/GameNetwork/udp.cpp` | Thin BSD-socket wrapper. One `SOCK_DGRAM` fd, non-blocking, optional `SO_BROADCAST`. |
| `Transport` | `Core/GameEngine/Source/GameNetwork/Transport.cpp` | Owns one `UDP`. Fixed-size in/out ring of `TransportMessage`. Adds a 6-byte header, a CRC and an XOR/byte-swap "encryption" pass. |
| `LANAPI` | `Core/GameEngine/Source/GameNetwork/LANAPI.cpp` | Lobby protocol state machine. Owns one `Transport`. |
| `LANAPI` handlers | `Core/GameEngine/Source/GameNetwork/LANAPIhandlers.cpp` | One `handleXxx()` per inbound message type. |
| `IPEnumeration` | `Core/GameEngine/Source/GameNetwork/IPEnumeration.cpp` | Lists candidate local IPv4 addresses. |
| Lobby UI | `GeneralsMD/.../Menus/LanLobbyMenu.cpp`, `.../NetworkDirectConnect.cpp` | Chooses which local address `LANAPI` will call its own, pumps `LANAPI::update()`. |

The in-game (post-lobby) session uses a *different* socket on
`NETWORK_BASE_PORT_NUMBER` (8088) via `ConnectionManager`/`Network`. Everything
in this document is about the lobby only.

## 2. Sockets and ports

* There is exactly **one** UDP socket for the whole lobby, port **8086**
  (`lobbyPort`, `LANAPI.cpp`). Both the announce/discovery traffic and every
  unicast reply share it.
* Bind address:
  * Windows: `m_transport->init(m_localIP, lobbyPort)` — bound to one address.
  * POSIX: `m_transport->init(INADDR_ANY, lobbyPort)` — bound to `0.0.0.0`.
    (`LANAPI::init()` and `LANAPI::SetLocalIP()`.)
* `SO_BROADCAST` is set immediately after bind via
  `Transport::allowBroadcasts(true)` → `UDP::AllowBroadcasts`. **[verified]**
* `SO_REUSEADDR` is **never** set. Two instances of the game on one host
  therefore cannot both open the lobby.
* The socket is set non-blocking (`UDP::SetBlocking(FALSE)`); `Transport::doRecv`
  drains it with `recvfrom` until `EWOULDBLOCK`.
* Every reply is sent to `senderIP:8086` — i.e. the *fixed* lobby port, never the
  observed source port. This is fine only because every participant binds 8086.

## 3. Wire format

```
+--------------------------------------------------+
| TransportMessageHeader (packed, 6 bytes)          |
|   UnsignedInt   crc      (4)                      |
|   UnsignedShort magic    (2)  == 0xF00D           |
+--------------------------------------------------+
| LANMessage (packed, 471 bytes)                    |
+--------------------------------------------------+
```

Total datagram: **477 bytes**. `sizeof(LANMessage) == 471` on the Linux x86_64
build **[verified — read out of the DWARF in the linked `GeneralsXZH`]**:

```
offset  size  field
     0     4  LANMessage::Type messageType      (enum, 4 bytes)
     4    26  WideCharWindows name[13]          (uint16_t, so 2 bytes/char)
    30     2  char userName[2]
    32     2  char hostName[2]
    34   437  union { ... }                     (largest member: GameInfo)
```

The union's largest member is `GameInfo` (437 bytes: `gameName[17]` +
`inProgress` + `options[401]` + `isDirectConnect`).

Notes on portability of this layout:

* `LANMessage` and `TransportMessage*` are `#pragma pack(1)`, so there is no
  padding to disagree about.
* Wide characters on the wire are `WideCharWindows` (`uint16_t`), *not*
  `wchar_t`, precisely because `wchar_t` is 4 bytes on Linux/macOS and 2 on
  Windows. Conversion happens in `CopyWcharToWindowsWideChar` /
  `GetWindowsWideCharAsWchar`.
* Multi-byte scalars (`UnsignedInt ip`, `crc`, `mapCRC`, …) are written in
  **host byte order**, not network byte order. The XOR "encryption" pass does a
  `htonl()` on each 4-byte word of the whole datagram, which byte-swaps
  everything on a little-endian host and is undone symmetrically on receive.
  Two little-endian hosts therefore agree; a big-endian host would not. All
  currently supported targets (x86_64, arm64 Apple) are little-endian, so this
  is not a live problem **[inferred]**.
* `enum` members inside the packed struct (`messageType`, `ChatType`,
  `ReturnType`) are 4 bytes on every supported ABI **[verified for
  linux-x86_64]**.

### Encryption and CRC

`Transport::queueSend`:

1. copies the payload into `m_outBuffer[i].data`, records `length`, `addr`, `port`
2. sets `header.magic = 0xF00D`
3. computes the CRC over `&header.magic` for `length + sizeof(header) - 4` bytes
   (i.e. magic + payload) and stores it in `header.crc`
4. `encryptBuf(&m_outBuffer[i], length + sizeof(header))` — XOR with a rolling
   mask plus `htonl` per 4-byte word. Bytes past the last whole word are left in
   the clear.

`Transport::doRecv` mirrors this: `decryptBuf(buf, len)` where `len` is the
number of bytes actually received, then `isGeneralsPacket()` re-computes the CRC
and checks the magic. Since the sender encrypts exactly the number of bytes it
transmits, the word counts match. **[inferred — arithmetic checked by hand]**

## 4. The local-address choice, and why it matters

`LANAPI::m_localIP` is not derived from the socket. It is chosen by the UI and
pushed in with `LANAPI::SetLocalIP()`:

* `LanLobbyMenuInit` (`LanLobbyMenu.cpp:419-500`): take
  `OptionPreferences::getLANIPAddress()`, else `TheGlobalData->m_defaultIP`. Use
  it **only if it still appears in the enumerated list**; otherwise fall back to
  `IPlist->getIP()`.
* `NetworkDirectConnectInit` (`NetworkDirectConnect.cpp:300-338`): same shape,
  but it reads `OptionPreferences::getOnlineIPAddress()` — the *GameSpy/online*
  preference, not the LAN one.

`IPEnumeration::addNewIP` keeps the list sorted **ascending by numeric IP**. The
menus used to take the head of that list, i.e. the *numerically lowest*
candidate; they now call `SelectLANLocalAddress()`, which honours a configured
address while it is still enumerated, otherwise prefers the address the default
route uses, otherwise falls back to the head of the list. On POSIX the candidate set is every `AF_INET` address
that is `IFF_UP`, not `IFF_LOOPBACK`, is `IFF_BROADCAST`, is not
`IFF_POINTOPOINT`, and whose interface name does not start with `docker`,
`veth`, `virbr`, `awdl`, `llw` or `utun`.

That name blacklist misses, among others: `br-<hash>` (docker-compose bridges),
`lxdbr0`, `podman0`/`cni-podman0`, `zt<id>` (ZeroTier), `ham0` (Hamachi),
`tap*`/`tun*` bridged VPNs and libvirt bridges that were renamed. All of those
are `IFF_UP`, non-loopback and `IFF_BROADCAST`, and all of them typically carry
`10.*` or `172.16-31.*` addresses, which sort **below** a `192.168.*` home LAN.

`m_localIP` is then used for three different jobs, and it is not obviously the
right value for all three:

1. **Choosing where broadcasts go.** This *used* to depend on `m_localIP`:
   `LANAPI::sendMessage` called `GatherSubnetBroadcastAddrs(m_localIP, …)`,
   which returned only the subnet broadcast of the interface whose address
   equalled `m_localIP`, and skipped the `255.255.255.255` fallback whenever it
   found one. It no longer does — `LANSelectBroadcastDestinations()` announces
   on every broadcast domain the machine is attached to.
2. **Deciding "is this packet mine?".** `LANAPI::update()` drops a datagram from
   this machine as a self-echo. Broadcasts *do* loop back to the sending host on
   Linux **[verified]**, so this check is load bearing. It now compares against
   every local address rather than only `m_localIP`.
3. **Identity in the protocol.** `m_localIP` is written into the host's slot 0,
   into the generated game name (`"%8.8X%8.8X"` of `m_localIP` and the seed), is
   compared against the `playerIP`/`gameIP` fields of incoming messages, and is
   what `LANGameSlot::isLocalPlayer()` — and hence `getLocalSlotNum()` — uses to
   find our own slot in the host's slot list. It is also the address every other
   client will connect to on port 8088 for the actual game session, so a bad
   choice survives the lobby and breaks the match instead.

## 5. Sequence: lobby discovery

Host and joiner both sit in `LanLobbyMenu`, which calls `TheLAN->update()` every
frame (`LanLobbyMenu.cpp:666`). `LANAPI::update()` rate-limits itself to one
pass per 200 ms and re-announces every `s_resendDelta` = 10 s.

```
A (any client)                                 B (any client)
--------------                                 --------------
LanLobbyMenuInit
  IPEnumeration -> pick m_localIP
  SetLocalIP    -> bind 0.0.0.0:8086, SO_BROADCAST
  RequestSetName -> MSG_LOBBY_ANNOUNCE  --bcast-->  handleLobbyAnnounce
  RequestLocations -> MSG_REQUEST_LOCATIONS --bcast--> handleRequestLocations
                                                     adds A to player list,
                                                     replies MSG_LOBBY_ANNOUNCE
                                                     (broadcast, not unicast)
handleLobbyAnnounce  <--bcast--------------------
  adds B to player list
```

A host that has created a game answers `MSG_REQUEST_LOCATIONS` with
`MSG_GAME_ANNOUNCE` instead, and re-broadcasts `MSG_GAME_ANNOUNCE` +
`MSG_GAME_OPTIONS` every 10 s. `handleGameAnnounce` on the other side parses the
options blob (`ParseGameOptionsString`) and adds the game to the list — **and
silently drops the game if the options string fails to parse**, which includes
the case where the announced map cannot be resolved locally
(`ParseAsciiStringToGameInfo`, "saw bogus map name"). That is a second, distinct
way for a game to never appear in someone's lobby list.

Games and players are pruned when not heard from for `2 * s_resendDelta` = 20 s.

## 6. Sequence: join (both lobby-join and direct-IP join)

```
Joiner J                                        Host H
--------                                        ------
(direct connect only)
RequestGameJoinDirectConnect(H)
  MSG_REQUEST_GAME_INFO ---unicast to H------>  handleRequestGameInfo
    PlayerInfo.ip = J.m_localIP                   replies MSG_GAME_ANNOUNCE
                                                  unicast to observed source
handleGameAnnounce  <---------------------------
  senderIP == m_directConnectRemoteIP
  ParseGameOptionsString -> slot0.ip = H.m_localIP
  RequestGameJoin(game, H)

RequestGameJoin
  MSG_REQUEST_JOIN ------unicast to H-------->  handleRequestJoin
    GameToJoin.gameIP = slot0.ip                  if (GameToJoin.gameIP
    m_pendingAction = ACT_JOIN                        != m_localIP) return;
    m_expiration = now + 5000 ms                  find open slot,
                                                  slot.ip = observed source IP
                                                  >>> OnPlayerJoin(): the host's
                                                  >>> lobby now shows J's name
                                                  reply MSG_JOIN_ACCEPT with
                                                    GameJoined.playerIP
                                                      = observed source IP
                                                    GameJoined.gameIP
                                                      = H.m_localIP
handleJoinAccept  <----unicast to observed src--
  if (GameJoined.playerIP != m_localIP) return;   <<< silent drop
  if (m_pendingAction != ACT_JOIN) return;        <<< silent drop
  m_currentGame = LookupGame(GameJoined.gameName) <<< NULL -> RET_UNKNOWN
  OnGameJoin(RET_OK)

...5 s later, if none of the above fired:
LANAPI::update() -> OnGameJoin(RET_TIMEOUT)
```

`handleJoinDeny` has the same `GameJoined.playerIP != m_localIP` gate.

### The asymmetry in issue #86 falls straight out of this diagram

The report is: on a direct-IP connect the **host** sees the joining player's
name, but the **joiner** times out.

`OnPlayerJoin()` on the host fires *before* the reply is sent, and it is driven
purely by the inbound `MSG_REQUEST_JOIN`. The joiner's acceptance of the reply is
gated on `GameJoined.playerIP == m_localIP`, where `playerIP` is the source
address **the host observed** and `m_localIP` is the address **the joiner picked
out of its own interface list**. Those are two independent quantities. Whenever
they disagree, you get exactly the reported behaviour: host shows the player,
joiner times out after 5 s, no error message anywhere because the drop is
silent. **[inferred — consistent with the report, not yet reproduced]**

The same disagreement also breaks discovery in the other direction, because
`GatherSubnetBroadcastAddrs(m_localIP, …)` will have sent the announce to the
subnet broadcast of the *wrong* interface and suppressed the `255.255.255.255`
fallback.

## 7. Divergences from TheSuperHackers upstream worth knowing about

* `LANAPI::init()` / `SetLocalIP()` bind `INADDR_ANY` on POSIX; upstream binds
  `m_localIP`. (Deliberate, from PR #201 — a socket bound to a unicast address
  does not receive broadcasts on Linux.)
* `LANAPI::sendMessage()` upstream is `if (ip) … else if (directConnect) … else
  broadcast`. The first branch had lost its `else`, so a unicast message was
  *also* broadcast to the whole subnet: every join accept/deny, every
  direct-connect game announce and every targeted game-options update went out
  twice. Restored.
* POSIX `IPEnumeration` uses `getifaddrs()` with the filters described in §4;
  upstream uses `gethostbyname(gethostname())`.
* The subnet-directed broadcast path is GeneralsX-only.
* `NetworkDirectConnectInit()` reads `OptionPreferences::getOnlineIPAddress()`
  where the LAN lobby reads `getLANIPAddress()`. That is retail behaviour, not a
  GeneralsX change, but it does mean the two screens can pick different local
  addresses on the same machine. Left alone; worth revisiting.

## 8. What a failure looks like from the outside

Because every mismatch above is a silent `return`, the symptoms are:

* lobby lists that stay empty although packets are arriving,
* a host that shows a player who never shows the host,
* `RET_TIMEOUT` five seconds after pressing Join, with nothing logged.

`Core/GameEngine/Source/GameNetwork/*.cpp` still carries a full set of
commented-out `[LAN86]` `fprintf(stderr, …)` traces from the previous
investigation. Uncommenting them is the fastest way to see which `return` a
given packet died on.

## 9. Status after the 19/08/2026 work

Fixed, with the evidence in `scripts/qa/lan/README.md`:

* Discovery announces on every broadcast domain rather than one guessed
  interface, and never queues a zero destination.
* The self-echo test uses every local address.
* A join request now carries the address the routing table says reaches that
  host, and a join accept/deny is accepted if it names any address of this
  machine while a join is pending; the address the host observed is adopted, and
  the cached local address on every `LANGameInfo` moves with it.
* The host accepts a join request naming any of its own addresses.
* The menus choose the local address by reachability rather than numeric order,
  and no longer dereference an empty enumeration list.
* `CopyWcharToWindowsWideChar()` no longer reads past the end of the source
  string (an AddressSanitizer heap-buffer-overflow read that also put adjacent
  heap into every packet), `GetWindowsWideCharAsWchar()` bounds its scan and
  never returns null, and `LANMessage` is zero-initialised instead of sending
  several hundred bytes of uninitialised stack.
* `Transport::doSend()` no longer wedges an out-buffer slot on a destination the
  socket layer rejects outright.

Still unknown, and not claimed:

* Whether a real match between two machines now works. Nobody ran one.
* macOS. Nothing here was compiled or run on macOS. The POSIX paths are shared
  with Linux, and `getifaddrs`, `IFF_BROADCAST` and `connect`/`getsockname` on a
  datagram socket all behave the same way there, but that is reasoning, not
  evidence.
* Flatpak's network sandbox. `--share=network` in
  `flatpak/com.fbraz3.GeneralsX*.yml` should give the app the host network
  namespace, so broadcast ought to work unchanged. Untested.
* The in-game lobby UI. Nothing here was observed through the game.
* Whether the reported lobby failure between two *single-interface* Linux
  machines is explained. It is not, by any of the above: on a machine with one
  usable address the old code picked it correctly. The one thing that does
  explain it is the pre-#201 bind to a unicast address, which receives no
  broadcasts at all (**[verified]**, observation 2 in the harness README) — if
  the reporters were on a Flatpak built before 2026-07-12 that is a complete
  explanation, and it is already fixed. If they were not, the fault is still
  open and the `[LAN86]` traces are the way in.
* Whether `ParseGameOptionsString()` failing on an unresolvable map is dropping
  games from lobby lists in the field. The code path is real
  (`LANAPI::handleGameAnnounce` deletes the game when the options string does
  not parse, and `ParseAsciiStringToGameInfo` rejects a map it cannot resolve),
  but nothing here shows it happening.
