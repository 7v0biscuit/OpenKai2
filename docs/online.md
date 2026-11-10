# Online play

Play versus against other players from the launcher's **Online** page (Ctrl+2).

## Profiles

Type a name under **New profile** and press **Add**. You need a profile to play
online, and you can keep several. Each profile has its own key, created on your
PC, which proves to other players that you are you. Friends and invites are tied
to the profile, not to the name.

Profile names are unique across OpenKai2. When you add a profile the launcher
checks the online service, and a name another profile already holds is refused.
Case, spaces and symbols do not count, so "Joint", "joint" and "J-O-I-N-T" are
the same name. The first profile to go online with a name keeps it. If a
profile's name is already held by someone else, the Online page shows
**Name taken** and the profile stays hidden until you add one with a free name.
Players whose name does not match its holder are marked "(name not verified)" in
the Players list, invites and friend requests.

On the public broker the claim is signed but not enforced by the server, so a
modified client could still copy a name; OpenKai2 clients flag it. A self-hosted
**OpenKai2Lobby** refuses the copy outright.

The **[DEV]** tag is reserved for the OpenKai2 developer, whose profile is
**[DEV] Biscuit**. Adding a [DEV] name asks for the developer key; without it the
tag is removed, and other players' games check the key's signature too.

## Finding a match

The launcher connects to the online service on its own. The **Rooms** list and
the **Players** list update live; there is nothing to refresh.

- **Join a room:** pick it and press **Join room**, or double-click it. When the
  room has a password, a password box appears below the list. Each room shows
  its ping in green, yellow or red; **RELAY** means the host can't be reached
  directly, and the match will go through the online service instead. Rooms on
  your version with the lowest ping are listed first, and rooms that need
  another OpenKai2 version show it in red.
- **Join by address:** type the host's address and press **Join address**. Add
  `:port` only if the host's room shows a port other than 7350.
- **Keyboard:** Enter in the password box joins the selected room, Enter in the
  address box joins that address, Enter in **New profile** adds it, **Esc**
  stops waiting in a room and **Ctrl+R** refreshes the lists.

The game does not start while you wait. Both launchers first meet in the room,
check the version, password and connection, and measure the ping. Only then do
both games start together. Press **Cancel** or **Close room** to stop waiting.

## Hosting

Fill in **Host a room** and press **Host room**:

| Setting | |
| --- | --- |
| Room name, password | Both optional. |
| Input delay | **Auto** picks it from the ping when someone joins. |
| Who can see it | **Public** lists the room. **Invite only** hides it from the list; invited players can still join. **Not listed** shares no room at all; give players your address. |

The host is player 1. There is nothing to set up. The room uses TCP port 7350,
or the next free one, and asks your router to open it (UPnP). When a player
can't reach the room directly, because of the router or a firewall, both
launchers connect through the online service instead and the match runs
through it automatically. A direct connection has a lower ping, so it is always
tried first.

## Friends and invites

- **Add friend:** pick a player and press **Add friend**. They get a notification
  and can accept. A request sent while they are offline waits until they come
  online. Friends are listed in the sidebar on every page, online ones first.
  A green dot means online, yellow means in a match or hosting, and grey means
  offline. A friend who is hosting shows the room's name; double-click them to
  join it. Double-click any other friend to invite them, or pick a friend to
  remove them.
- **Invite:** pick a player and press **Invite**. Your room opens and the invite
  is sent. The invited player gets a Windows notification and an **Accept** /
  **Decline** card at the top of the launcher, and joins without needing the
  room password.
- **Notifications:** the card shows who sent the invite or friend request, how
  long ago, which room, and how many more are waiting. Repeat invites from the
  same player are merged into one. The **Online** tab shows how many are
  waiting.

## In the match

When both games start, they connect again with a one-time key from the room, and
the host sends its memory card and full game state, so both games start the
same. Your controls work while the games link up. The games then exchange button presses a few frames ahead
and run in lockstep. Every two seconds they compare memory; if they ever differ,
the in-game bar shows **out of sync**. The bar at the top shows the opponent,
ping and input delay.

Both players need the same OpenKai2 version and Budokai 2 disc. Mods, save states
and speedrun mode are off online. Your own memory card is not changed: matches
use a copy in `%LOCALAPPDATA%\OpenKai2\netplay\`.

## The online service

Rooms, player lists, friend requests and invites go through a public MQTT broker
(`broker.emqx.io:1883` by default). Match traffic goes directly between the two
players' PCs, not through the broker.

Room listings include the host's public IP address, so anyone using the service
can see it. Use **Not listed** to avoid that.

To use your own service instead, run `OpenKai2Lobby --port=1883` (built from
source) or any MQTT broker, and set `online_room ... "<host>:<port>"` in
`settings.ini`. Everyone who should see each other must use the same service.
