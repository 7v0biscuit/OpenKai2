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
the **Players** list update live. **Refresh** also rechecks room latency.

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
  online. Friends are listed in quick access on Play, online ones first, and remain available in Online’s Players list.
  A green dot means online, yellow means in a match or hosting, and grey means
  offline. A friend who is hosting shows the room's name; double-click them to
  join it. Double-click any other friend to invite them, or pick a friend to
  remove them.
- **Invite:** pick a player and press **Invite**. Your room opens and the invite
  is sent. The invited player gets a Windows notification and an **Accept** /
  **Decline** card in **Notifications** (the bell, or Ctrl+Shift+N), and joins without needing the
  room password.
- **Notifications:** open the bell in the header to review requests. The card shows who sent the invite or friend request, how
  long ago, which room, and how many more are waiting. Repeat invites from the
  same player are merged into one. The **Online** tab shows how many are
  waiting.

## In the match

When both games start, they connect again with a one-time key from the room, and
the host sends its memory card and full game state, so both games start the
same. Your controls work while the games link up. The games then exchange button presses a few frames ahead
and use rollback to hide the delay (see below). Every two seconds they compare memory; if they ever differ,
the in-game bar shows **out of sync**. The bar at the top shows the opponent,
ping and input delay.

Both players need the same OpenKai2 version and Budokai 2 disc. Mods, save states,
the randomizer and speedrun mode are off online. **Music off** is ignored online,
because it changes the music data the game reads and the two games would differ. Online matches always use PS2 disc timing,
whatever **PS2 load times** is set to, so both games load at the same moments.
The disc drive's position and the game's random numbers travel with the
starting state, so both games begin from exactly the same point. Your own memory card is not changed: matches
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

## Profile appearance

Use **Customize profile** on Play to choose avatar initials, an accent and a
short tagline. Appearance is saved separately for each profile on this PC.
It does not change the public profile name, ID or private key, and is not sent
to other players.

**Copy profile ID** copies the public ID for sharing. **Use defaults** resets
appearance without deleting the profile or its friends. If no profile exists,
**Create profile** on Play opens the Online page to create one.

## Profile pictures and browsing

Choose **Customize profile** on Online or Play, then **Choose picture…** to
select a PNG, JPEG or animated GIF from your PC. You can also drop a picture
onto the Profile page. GIFs animate on Profile, Play and Online. Pictures are
displayed in a circle; choose **Fill the circle** to crop, or **Fit the whole picture** to keep its edges. Initials are used when no picture is set.

Pictures may be up to 16 MB, with up to 300 GIF frames. The launcher copies
files to `%LOCALAPPDATA%\OpenKai2\profiles\images`. Removing or replacing a
picture keeps the original file. Each profile keeps its own picture, initials,
accent and tagline. These appearance settings stay on this PC and are not
uploaded to the online service.

Use **Search rooms or players** to find a room, host or player by name. **Clear**
restores the lists. **Ctrl+F** focuses the current page’s search. On Play and Online, it searches rooms and players. Room filters
show compatible versions by default, all versions, or compatible rooms without
a password. Player filters show everyone or just friends who are online.
Friends remain available in Online's Players list after being added.
Filters are remembered; search text is cleared when the launcher restarts.

**Reconnect** retries the service when offline; **Refresh** updates room pings
when connected. Join and invite buttons become available when their required
ISO, connection and selection are ready. List positions stay in place while
room pings and player presence update.

## Rollback

Online matches use rollback netcode. Input delay is capped at 2 frames; when
the other player's buttons for a frame have not arrived yet, the game repeats
their last known buttons and keeps running. If the real buttons turn out
different, the game returns to the last correct frame and replays up to the
present without drawing or sound. It runs up to 8 frames ahead of the other
player before it waits. Set `OPENKAI2_NETPLAY_ROLLBACK=0` on both PCs to go
back to plain lockstep. `OPENKAI2_NETPLAY_LAG_MS` adds artificial delay to
received inputs for testing. When a match ends, `startup.log` reports
`[netplay] rollback predicted=... mispredicted=... rollbacks=...`.

It was built in steps:

1. Done: snapshots that store only the memory that changed each frame, and
   save states that replay exactly. `OPENKAI2_ROLLBACK_TEST=start,frames,depth`
   makes the game snapshot every frame, roll back `depth` frames every
   `3 * depth` frames, replay them, and report in `startup.log` whether the
   replayed frames matched (`[rollback] ... differing=0`).
2. Done: rollback snapshots leave the picture out and compare memory on
   several CPU cores (about 1.4 ms per snapshot on an idle four-core test
   machine). Rolled-back frames replay without waiting for real time, without
   sound and without drawing; the frame after a replay draws normally.
3. Done: predicted inputs and corrections in the match itself. Hash checks
   only cover frames whose inputs are confirmed on both sides.
4. Done: time sync. Every half second each game tells the other how far ahead
   of the other player's inputs it is running. When one game has drifted 1.5
   frames or more ahead, for example because its PC keeps time slightly
   faster, it waits one to three frames so both stay in step instead of the
   faster one repeatedly running out of inputs. The end-of-match log line adds
   `sync_waits=` and `sync_frames=`.

## Managing profiles on this PC

The Profile page has an **Active profile** switcher. **Manage online profiles**
opens name creation and removal on Online. Switching and removal are disabled
while a game or room is active so a match keeps the identity it started with.

Use **Copy appearance** to reuse another local profile’s picture, framing,
initials, accent and tagline. It keeps the destination profile’s identity,
friends and notification preference. **Save profile** saves the text, framing,
accent and alert settings; choosing or removing a picture also saves them.

**Online availability** lets each profile appear as Available, Away or Do not
disturb. Hosting, joining and active matches take priority automatically. Do
not disturb also silences desktop alerts; your online status stays visible.

Each profile can use **Quiet — launcher inbox only** to turn off invitation
sounds, taskbar flashing and Windows alerts. Requests still appear in the
launcher’s Notifications inbox. This preference does not hide online presence.

**Remove** asks for confirmation and defaults to keeping the profile. Before
removal, the launcher saves its identity, friend list and appearance in
`%LOCALAPPDATA%\OpenKai2\profiles\recovery\last-removed.ini`.
**Undo removal** restores that profile without replacing game settings or
other profiles. This recovery file stores a private identity key, just like the
local settings file. It stays on this PC and is replaced on the next removal.
The original picture files and game saves are kept.

Launcher motion and hover hints can be changed under **Settings → Launcher**.
Reduced motion keeps the dock, animated pictures and cycling profile accents
still. Smooth mode also respects Windows’ animation preference. Ctrl+K, Ctrl+F
and the dock shortcuts keep working when hover hints are off.
