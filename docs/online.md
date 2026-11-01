# Online play

Play versus against other players from the launcher's **Online** page (Ctrl+2).

## Profiles

Type a name under **New profile** and press **Add**. You need a profile to play
online, and you can keep several. Each profile has its own key, created on your
PC, which proves to other players that you are you. Friends and invites are tied
to the profile, not to the name.

The **[DEV]** tag is reserved for the OpenKai2 developer, whose profile is
**[DEV] Biscuit**. Adding a [DEV] name asks for the developer key; without it the
tag is removed, and other players' games check the key's signature too.

## Finding a match

The launcher connects to the online service on its own. The **Rooms** list and
the **Players** list update live; there is nothing to refresh.

- **Join a room:** pick it and press **Join room**, or double-click it. Rooms
  with a password need it typed in the box below the list. The list shows each
  room's ping; "no reply" means the host's port can't be reached from your PC.
- **Join by address:** type the host's address (`IP` or `IP:port`) and press
  **Join address**.

## Hosting

Fill in **Host a room** and press **Host room**:

| Setting | |
| --- | --- |
| Room name, password | Both optional. |
| Port | TCP port to host on (7350). |
| Input delay | **Auto** picks it from the ping when someone joins. |
| Who can see it | **Public** lists the room. **Invite only** hides it from the list; invited players can still join. **Not listed** shares no room at all; give players your address. |

The host is player 1. The launcher asks your router to open the port (UPnP). If
your router doesn't allow that and nobody can join, forward TCP port 7350 to your
PC in the router's settings.

## Friends and invites

- **Add friend:** pick a player and press **Add friend**. They get a notification
  and can accept. A request sent while they are offline waits until they come
  online. Friends appear at the top of the Players list, with their status.
- **Invite:** pick a player and press **Invite**. Your room opens and the invite
  is sent. The invited player gets a Windows notification and an **Accept** /
  **Decline** banner, and joins without needing the room password.

## In the match

When a player joins, the host sends its memory card and full game state, so both
games start the same. The games then exchange button presses a few frames ahead
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
