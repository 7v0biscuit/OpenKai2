# Online play

Play versus mode against a friend over the internet from the launcher's
**Online** page.

## Profiles

You need a profile to play online. Type a name under **New profile** and press
**Add**. The selected profile's name is shown to the other player. You can keep
several profiles and switch between them.

## Hosting a room

1. Optional: type a **room name** and a **password**.
2. Pick an **input delay** (2 frames suits most connections).
3. Set **Server browser** to **Listed** to show the room in the server browser.
   Otherwise, give the other player your IP address and port.
4. Press **Host room**. The game starts and waits for a player.

The host is player 1. The host may need to forward the TCP port (7350 by default)
on their router.

## Joining

- **Server browser:** enter the server address under **Server** and press
  **Refresh**. Choose a room and press **Join room**. Locked rooms need the room
  password.
- **Direct:** type the host's address (`IP` or `IP:port`) and press **Join
  address**.

The joining player is player 2. A room leaves the list once a player joins.

## How a match stays in sync

When a player joins, the host sends its memory card and the full game state, so
both games start the same. Each game then sends its button presses to the other,
a few frames ahead (the input delay). Both games run the same frames with the
same inputs. If one player's inputs are late, both games wait for them.

Every two seconds the games compare their memory. If they ever differ, the
overlay shows **out of sync**.

The in-game bar at the top shows the opponent, the ping and the input delay.

Both players need the same OpenKai2 version and the same Budokai 2 disc. Mods,
save states and speedrun mode are off online. Your own memory card is not
changed: online matches use a copy in `%LOCALAPPDATA%\OpenKai2\netplay\`.

## Running a server

The server browser needs a lobby server, which lists open rooms. Anyone can run
one; it does not carry game traffic, only the room list.

Build `OpenKai2Lobby` from source (see [building.md](building.md)) and run:

```
OpenKai2Lobby --port=7351
```

Open TCP port 7351 on the server. Players enter the server's address (for
example `lobby.example.org` or `203.0.113.5:7351`) in the launcher.

Each address can list up to four rooms, and the server lists up to 500.
