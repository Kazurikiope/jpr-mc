# pack-extract (Windows)

Takes the behavior packs and resource packs out of a Bedrock server or world on
your PC and saves them as `.mcpack` files, plus one `.mcaddon` with all of them.
You don't need to install anything. It runs on the PowerShell that comes with
Windows 10/11.

## Use

Put `Extract-Packs.bat` and `Extract-Packs.ps1` in the same folder, then do one
of these:

- **Drag your server folder** (the one with `bedrock_server.exe`) or a world
  folder onto `Extract-Packs.bat`.
- **Double-click** `Extract-Packs.bat`, then either drag the folder into the
  window and press Enter, or just press Enter to take the resource packs the
  game downloaded when you joined a server.
- If you put the two files inside the server folder, double-clicking is enough.

The output goes into `extracted\<date>\` next to the script. To import a pack,
double-click the `.mcpack` or `.mcaddon`.

From a PowerShell prompt:

```powershell
.\Extract-Packs.ps1 -ServerPath "C:\bedrock-server"          # packs the world uses
.\Extract-Packs.ps1 -ServerPath "C:\bedrock-server" -All     # every pack in the folder
.\Extract-Packs.ps1 -ServerPath "...\minecraftWorlds\abc="   # a singleplayer world
.\Extract-Packs.ps1 -FromCache                               # packs downloaded from servers
.\Extract-Packs.ps1 -CachePath "D:\somewhere"                # scan a folder you choose
```

`-Folders` also saves each pack as an unzipped folder. `-OutDir` changes where
the output goes.

## What it can and can't get

| Source | Behavior packs | Resource packs |
|---|---|---|
| Server or world folder (`-ServerPath`) | yes | yes |
| Game download cache (`-FromCache`) | **no, never** | yes, unless encrypted |

- **Behavior packs stay on the server.** The server runs them and only sends
  players the results. They are never downloaded to the game, so the only way
  to get one is from the server's own folder. For a server running on your PC
  (localhost), that's the folder you started `bedrock_server.exe` from.
- **Which packs the world uses.** The script reads `level-name` from
  `server.properties`, then `world_behavior_packs.json` and
  `world_resource_packs.json` in that world. It finds each listed pack by UUID
  in `worlds\<level>\behavior_packs`, `resource_packs`, the server's
  `behavior_packs`, `resource_packs`, and the `development_*` folders. Packs can
  be folders or `.mcpack`/`.zip` files. The built-in vanilla packs are skipped.
- **Encrypted packs are skipped.** Some servers encrypt their packs. The
  script finds these by checking `contents.json` and lists them as skipped. It
  does not try to decrypt them.
- **The download cache location.** It searches the cache folders under
  `%APPDATA%\Minecraft Bedrock` (the current launcher) and
  `%LOCALAPPDATA%\Packages\Microsoft.MinecraftUWP_8wekyb3d8bbwe` (older Store
  versions), plus the Preview versions of both. Packs you installed yourself are
  not included. If nothing shows up, join the server once so the game downloads
  the packs. If there's still nothing, point `-CachePath` at the folder.

If Windows blocks the script because it was downloaded, right-click
`Extract-Packs.ps1` → Properties → **Unblock**. The `.bat` already runs it with
`-ExecutionPolicy Bypass`.
