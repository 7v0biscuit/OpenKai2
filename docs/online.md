# Online play

Play versus against other players from the launcher's **Online** page (Ctrl+2).

## Profiles

Profiles are managed on the **Profile** page. Type a name in **Name for a new
profile** and press **Add**. The Online page only shows your active profile with
**Customize profile** and **Copy profile ID**. You need a profile to play
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

**[TTV]**, **[YT]** and **[ToB]** are name tags handed out by the developer as key
files. Type a name with the tag, such as `[TTV] Joint`, and the launcher asks for
that tag's key file once. After that any profile can carry the tag with any name.
**Rename** on the Profile page renames the active profile and keeps its tag:
`[TTV] Biscuit` can become `[TTV] Joint` or `[TTV] Slender` by typing only the new
name. Friends, picture and stats stay with the profile. Without the key the tag is
removed, and other players' games check each tag's signature. Each tag has its
own animated name effects (see **Profile appearance**).

The [TTV] and [YT] keys were replaced. Key files issued before this change no
longer work in new builds; ask the developer for the new file.

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
  address box joins that address, Enter in the Profile page's name box adds a
  profile, **Esc**
  stops waiting in a room and **Ctrl+R** refreshes the lists.

The game does not start while you wait. Both launchers first meet in the room,
check the version, password and connection, and measure the ping. Both
players need the same OpenKai2 version; a game from a different version is
refused with both version numbers so you know who needs to update. Only then do
both games start together. Press **Cancel** or **Close room** to stop waiting.

## Hosting

Fill in **Host a room** and press **Host room**:

| Setting | |
| --- | --- |
| Room name, password | Both optional. |
| Input delay | **Auto** picks it from the ping when someone joins. |
| Who can see it | **Public** lists the room. **Invite only** hides it from the list; invited players can still join. **Not listed** shares no room at all; give players your address. |

The host is player 1. There is nothing to set up and no port to forward. The
room uses TCP port 7350, or the next free one, and asks your router to open it
(UPnP). When a player can't reach the room that way, the two launchers meet
through the online service, then open a direct UDP connection through both
routers (hole punching): each asks a public STUN server for its own outside
address, they swap addresses, and both send to each other at once. That works
with most home routers and gives the same ping as a direct connection. Only if
the routers block it does the match run through the online service, at a
higher ping. The launcher log says which one a match used: "Connected directly
to the other player" or "Playing through the online service".

Lost packets on a direct connection are sent again after about one and a half
round trips instead of a fixed 0.4 seconds, so a dropped packet no longer
freezes the match for a moment.

## Friends and invites

- **Add friend:** pick a player and press **Add friend**. They get a notification
  and can accept. A request sent while they are offline waits until they come
  online. Friends are listed in quick access on Play, online ones first, and remain available in Online’s Players list.
  A green dot means online, yellow means in a match or hosting, and grey means
  offline. A friend who is hosting shows the room's name; double-click them to
  join it. Double-click any other friend to invite them, or pick a friend to
  remove them. Offline friends show when they were last seen, for example
  "Last seen 2 h ago". Friends are listed first in Online's Players list.
- **Friend alerts:** when a friend comes online, a Windows notification shows
  their name and card. Turn it off per profile with **Friends coming online** on
  the Profile page (Ctrl+P). Quiet mode, Do not disturb and a running game
  silence it.
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
the in-game bar shows **out of sync** and the host sends its full game state to
the other player, who continues from it, so the match carries on in sync. This
happens at most three times per match. The bar at the top shows the opponent,
ping and input delay. While the game is correcting late inputs it also shows
**rollback N**, the most frames it has had to replay in the last two seconds;
it disappears again when the connection is steady.

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
players' PCs whenever their routers allow it, and through the broker only when
they don't.

Room listings include the host's public IP address, so anyone using the service
can see it. Use **Not listed** to avoid that.

To use your own service instead, run `OpenKai2Lobby --port=1883` (built from
source) or any MQTT broker, and set `online_room ... "<host>:<port>"` in
`settings.ini`. Everyone who should see each other must use the same service.

## Profile appearance

Use **Customize profile** on Play to choose avatar initials, an accent, a
short tagline, an optional **Name color** and a **Main fighter**. Appearance is
saved separately for each profile on this PC and does not change the public
profile name, ID or private key.

Tagged profiles also get animated Dragon Ball Z name effects in the same list.
Each tag has its own set, and a profile can only use its own tag's effects. The
[DEV] key can use every effect, including the other tags' sets:

- **[DEV]:** Shenron wish, Fusion dance, Super Saiyan, Ki blast, Frost demon,
  Super Saiyan 2, Dragon Balls, Spirit Bomb, Powering up.
- **[TTV]:** Galick Gun, Majin Buu, Death Ball, Majin magic, Candy beam,
  Instant Transmission, Ginyu Force.
- **[YT]:** Kaio-ken, Kaio-ken x20, Red Ribbon, Champion belt, Cell Games,
  Dragon Fist, Dynamite Kick.
- **[ToB]:** Namek inferno, Kamehameha, Perfect Cell, Planet Vegeta, Big Bang
  Attack, Piccolo, Kid Buu rampage.

In Powering up, Ginyu Force, Dynamite Kick and Kid Buu rampage the letters of
the name move as well as change color. The **Name color** list previews every
color and effect in its own colors.

The effects appear only when that tag's key is installed and a profile with the
tag is active. The tag label is drawn in the tag's color and the avatar gets an
animated ring that blends smoothly through the effect's colors. Each effect is
signed with the tag's key and checked by every launcher before it is shown, so
other profiles cannot use or fake them. They always animate; with **Reduced
motion** on they move slowly instead. A player with a verified effect also gets
a **Verified** badge in their tag's color on their profile. Older launchers show
effects they do not know as a plain name.

Each tag has a profile theme: verified **[DEV]**, **[TTV]**, **[YT]** and
**[ToB]** players get a tinted card with a glowing edge and the theme name on
their player card. The **[TTV]**, **[YT]** and **[ToB]** labels shimmer in their
tag colors. A verified tag holder is recognized by other players even without
a name effect. In online matches the opponent's name on the in-game banner is
drawn with their name color, tag colors and name effect.

Tagged profiles can also add a **Channel link** (a Twitch, YouTube or Kick
`https://` address). It is signed with the tag's key, and other players see an
**Open channel** button on the player's profile. Right-click the player's name
for **Open channel** and **Copy channel link**.

Name colors come in Dragon Ball Z presets (Super Saiyan gold, Kamehameha blue,
Namekian green, Frieza purple, Kaio-ken red, Goku gi orange, Majin pink, Kami
white, Vegeta blue, Senzu green, Android teal, Ultra Instinct silver, Super
Saiyan Blue, Super Saiyan Rose, Hakai violet, Galick flare, Destructo amber,
Cell green, Capsule navy, Lava crimson, Frost cyan, Spirit Bomb aqua, Dragon
Radar lime and Shenron jade), or
choose **Custom color…** to pick any color. Preset colors shade softly into a
lighter tone across the name. Colors picked in older versions switch to the
refreshed tones automatically. Everyone sees your name color in the launcher
and on the in-game online banner. Very
dark or very light colors are adjusted so the name stays readable. **Theme
default** keeps the normal text color.

## Player cards

While you are online, other players see your player card: your picture, accent,
initials, name color, main fighter, tagline, **About me**, region, playstyle,
how many online matches you have played and when you first went online. It shows next to your name in
the Players, Friends and Rooms lists, for example "Online · Mains Goku ·
“Kamehameha all day”". **Player card online** chooses what is shared:

- **Picture and card** shares everything.
- **Card without the picture** shares everything except the picture.
- **Keep on this PC** shares nothing, and other players see your initials.

The shared picture is a small copy, up to 96 by 96 pixels, made from the first
frame of an animated GIF. Your original file never leaves your PC. Cards are
signed with the profile's key, so nobody else can change yours. Cards stay on
the service while you are offline, so friends still see your picture. Choosing
**Keep on this PC** removes the card from the service.

**Card look** adds more to your card:

- **Frame**: sixteen picture frames, from Saiyan gold to Time chamber.
- **Frame color**: keep the frame's own colors or tint it gold, silver, Super
  Saiyan Blue, Namek green and more.
- **Card design**: sixteen space backdrops with starfields and shooting stars,
  such as Midnight sky, Andromeda, Rose nebula, Emerald nebula, Milky Way, Twin
  suns, Supernova, Ringed planet, Starfall and Black hole.
- **Title**: forty-one titles. Most unlock with your level; some need an
  achievement, including the speedrun and skill titles. Four developer titles
  (OpenKai2 Developer, Lead Developer, Grand Kai of Code and Bug Slayer) are
  only for profiles with the developer key, which can also use every title and
  feature any achievement.
- **Featured achievement**: show one achievement you have unlocked under your
  name.

On the Wave home the Profile page shows your card as players see it at the top,
drawn as a Budokai 2 style panel with your name banner, title ribbon, level
badge and experience bar,
with **Card look**, **About you**, **Privacy & alerts** and the save buttons
below. Launchers older than v0.6.2 don't show cards from v0.6.2 or later.

**Preview my card** opens your player profile exactly as other players see it.
Online matches and the member-since date are counted automatically. **Copy
profile ID** copies the public ID for sharing. **Use defaults** resets
appearance without deleting the profile, its friends or its stats.

## Viewing other players

Right-click any name in Players, Friends or Rooms for a menu: **View profile**,
**Invite to play** (or **Join their room** while they host), **Add friend** or
**Remove friend**, **Copy name**, **Copy profile ID** and **Block**. The selected
player's **Profile** button on Online opens the same page, and Ctrl+K finds
players by name, or by a pasted profile ID.

A player profile shows their picture, colored name, tagline, main fighter,
status, about text, region, playstyle, online matches, member-since date, when
you last played them and their OpenKai2 version, with buttons to invite, join,
add or remove a friend, copy their ID or block them.

**Block** hides a player's invites and friend requests, removes them from your
friends and lists them last. **Unblock** from their profile or the right-click
menu. Players you face online are remembered as **Recent opponents**, up to 20
per profile.

On Play, the Friends panel shows **All friends**, **Online friends**,
**Everyone online** or **Recent opponents**. If no profile exists,
**Create profile** on Play or Online opens the Profile page to create one.

## Profile pictures and browsing

Choose **Customize profile** on Online or Play, then **Choose picture…** to
select a PNG, JPEG or animated GIF from your PC. You can also drop a picture
onto the Profile page. GIFs animate on Profile, Play and Online. Pictures are
displayed in a circle. **Picture framing** chooses **Fill the circle** (crop to
the center), **Fit the whole picture** (keep its edges), **Fill, keep the top**
(crop but keep the top, good for faces) or **Zoom in**. Changing it saves right
away and also updates the shared card. Initials are used when no picture is set.

Pictures may be up to 16 MB, with up to 300 GIF frames. The launcher copies
files to `%LOCALAPPDATA%\OpenKai2\profiles\images`. Removing or replacing a
picture keeps the original file. Each profile keeps its own picture, initials,
accent, tagline, name color and main fighter. See **Player cards** for what other
players see.

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
present without drawing or sound. It runs up to 10 frames ahead of the other
player before it waits. Three times a second the two games compare how far
ahead each one is; the one that is ahead slows down by about 2 ms a frame until
they are level, so it rarely has to stop and wait. Rollback keeps working through loading screens. In the
few frames the game cannot be saved, such as while it uses the memory card, it
waits for the other player's buttons instead of guessing. Set `OPENKAI2_NETPLAY_ROLLBACK=0` on both PCs to go
back to plain lockstep. `OPENKAI2_NETPLAY_LAG_MS` adds artificial delay to
received inputs for testing. When a match ends, `startup.log` reports
`[netplay] rollback predicted=... mispredicted=... rollbacks=...`.

It was built in steps:

1. Done: snapshots that store only the memory that changed each frame, and
   save states that replay exactly. `OPENKAI2_ROLLBACK_TEST=start,frames,depth`
   makes the game snapshot every frame, roll back `depth` frames every
   `3 * depth` frames, replay them, and report in `startup.log` whether the
   replayed frames matched (`[rollback] ... differing=0`). It also rolls back
   over every stretch of frames the game could not save, except movies, and
   logs each one (`[rollback] replayed N frames across a stretch ...`).
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
5. Done: recovery from going out of sync. When the memory check finds a
   difference, the host waits at its next frame for the other player's real
   inputs, then sends its full game state and memory card. The other game
   loads it and replays any frames it had already run ahead, exactly like a
   rollback. Memory checks made before the recovery are ignored. The
   end-of-match log line adds `resyncs=`.

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
