# Discord activity

The Windows launcher and game support Discord Rich Presence through the local
desktop client's IPC connection. No account login, bot token or Discord SDK is
required. The launcher reports its current page; the game reports an active
Budokai 2 session. Activity includes elapsed session time and the application's
icon. ISO paths, save names and hardware details are never included.

Use **Discord activity** in the launcher sidebar to turn sharing on or off.
The choice is saved and also applies to a running game within a few seconds.
Closing the application clears its activity. Discord being closed or
unresponsive does not prevent launching or playing.

The application ID is `1565006652468625510`. In that application's Discord
Developer Portal, upload the [current EXE icon](../packaging/windows/openkai2-discord.png) under **Rich Presence → Art
Assets** with the key **`openkai2`**. Discord hosts this image; an executable's
embedded icon cannot be sent over Rich Presence IPC. Asset changes may take
time to appear in the desktop client. Discord's activity privacy preferences
must also allow sharing.

Local IPC regression tests cover the handshake, icon key, fragmented messages,
ping responses, page changes, saved privacy choices and shutdown with a missing
or stalled client. These checks do not verify a live Discord account's artwork
configuration.
