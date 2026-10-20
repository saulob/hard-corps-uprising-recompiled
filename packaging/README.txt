Hard Corps: Uprising - PC port
Version @VERSION@

Hard Corps: Uprising for Xbox Live Arcade, statically recompiled to run natively
on Windows with the ReXGlue SDK.

Source code and issue tracker:
https://github.com/saulob/hard-corps-uprising-recompiled


NO GAME FILES ARE INCLUDED
--------------------------
You need your own copy of the game: the Hard Corps: Uprising Xbox Live Arcade
package. This port only runs this exact release:

  Title ID:      58410A18
  Game version:  0.0.0.3 (Xbox Live Arcade)

Any other version is refused with "Different version of the game".


REQUIREMENTS
------------
- Windows 10 or 11, 64-bit
- A DirectX 12 capable graphics card
- The latest Microsoft Visual C++ Redistributable 2015-2022 (x64). Most PCs
  have it. If Windows reports a missing VCRUNTIME140.dll or MSVCP140.dll,
  install it from https://aka.ms/vs/17/release/vc_redist.x64.exe
- Your own Hard Corps: Uprising Xbox Live Arcade package (about 1 GB), plus
  about 1 GB of free disk space next to the program for the game files


INSTALL
-------
1. Extract the ZIP anywhere you can write to (not "Program Files").
2. Run hard_corps_uprising.exe. The launcher opens.
3. On the Play page, click "Install from package...".
4. Select your Hard Corps: Uprising Xbox Live Arcade package. It is one file
   with a long hexadecimal name and no extension. On an Xbox 360 hard drive it
   is in Content\0000000000000000\58410A18\000D0000\
5. Wait while the launcher checks the package and copies the game files into
   the "game" folder next to the program.
6. When the Play page says "Ready to play", press PLAY.

To repair the game files, use "Reinstall from package..." on the Play page.
Your settings and saves are kept.


UPDATES
-------
The launcher checks the project's GitHub releases once per start. When a newer
release exists, an update banner appears in the header; nothing is installed
until you choose UPDATE NOW and then RESTART TO UPDATE. The update replaces
only the port's own program files. Game files, user_data, saves, settings,
logs, caches and DLC are kept.


DLC (OPTIONAL)
--------------
The DLC characters are Harley Daniels, Sayuri and Leviathan. DLC packages are
not included with this program, and it does not download them. Use your own
Xbox 360 DLC package for Hard Corps: Uprising (Title ID 58410A18, content type
00000002, Marketplace).

Open the DLC page, click "Install DLC..." and select the package. The DLC page
shows each character as Installed or Not installed. "Remove" on an installed
character removes only that package, after a confirmation. You can install the
package again later from your own copy. DLC is stored in your user data folder
("user_data" next to hard_corps_uprising.exe), not in the game folder.


WINDOWS SMARTSCREEN
-------------------
The first time you run hard_corps_uprising.exe, Windows SmartScreen may show a
warning, because the program is not digitally signed. If you downloaded the
release from the project's GitHub releases page, choose "More info", then
"Run anyway".


PLAYING
-------
- F11 switches between Windowed and Borderless Fullscreen.
- F10 opens the cheat menu. F2 shows or hides the frame counter.
- Xbox controllers work without setup. Connect two for local 2-player.
- The keyboard works too; the Controls page lists the keys.
- Online multiplayer is not supported.
- To open the launcher again after turning it off, hold Shift while starting.


WHERE THINGS ARE
----------------
- Game files:     the "game" folder next to hard_corps_uprising.exe
- Logs:           the "logs" folder next to hard_corps_uprising.exe
- Settings, saves, DLC and shader cache:
                  the "user_data" folder next to hard_corps_uprising.exe

Upgrading from v0.3.1 or earlier: those versions kept this data in
Documents\hard_corps_uprising. The first start moves that folder into
"user_data" and removes the old one after the move is checked. If the move
fails, the old folder stays in use for that run and the move is tried again at
the next start. If "user_data" and the old folder both hold data, "user_data"
is used and the old folder is left alone. If the program folder is read-only,
the old folder is used for that run. To keep your data when updating, extract
the new version over the old one, or move "user_data" and "game" across.

If the game does not start or crashes, please attach the newest file from the
"logs" folder to a report at
https://github.com/saulob/hard-corps-uprising-recompiled/issues


LICENSE
-------
The original project code of this port is licensed under the GNU General
Public License v2.0 only (GPL-2.0-only). The full text is in LICENSE.

Third-party components keep their own licenses. Their license files and
notices are in the "licenses" folder: ReXGlue-LICENSE.txt and
THIRD-PARTY-LICENSES.txt.

Game files are not included and are not covered by this project's license.
This port is not affiliated with or endorsed by the makers or publishers of
Hard Corps: Uprising.
