# Hard Corps: Uprising v0.7.1

A focused maintenance release after v0.7.0. It reduces antivirus false positives on Windows, makes the launcher's self-update more reliable, and hardens how the release packages are built and checked. Gameplay and the v0.7.0 feature set are unchanged.

## Highlights

- **Fewer antivirus false positives.** The Windows update helper is now built separately as a small self-contained program, and the version handling in the Windows executable was reworked. Both final release packages pass VirusTotal.
- **More reliable updates.** Existing v0.7.0 installations can update to v0.7.1 from the launcher, and your settings, saves, DLC, game files, logs and cache are kept.
- **Stricter release packaging.** Every package is checked for files that must never ship before it is published.

## What's Changed

### Windows / Updater

- The update helper (`trg-updater.exe`) is now built separately with a static runtime. It depends only on Windows system libraries, and its debug symbols stay outside the release ZIP.
- Logic that only the update helper needs is now built into the helper alone; the game executable keeps its normal launcher behavior.
- The update helper now logs each step of an update: the files it checks, the backup, the file replacement, verification, rollback when something fails, and the restart.
- More robust version handling: the port version comes from a single project setting and is assembled at runtime.
- The release ZIP is extracted again and verified after it is created.
- The release check now also rejects debug and symbol files, leftover updater folders, debugging tools, Vulkan layers, local file paths, development markers and Xbox 360 game data.

### Linux / AppImage

- The AppImage is rebuilt from the final v0.7.1 source.
- It remains compatible with the Ubuntu 20.04 / glibc 2.31 baseline; the highest glibc version it requires is still 2.30.
- The extract-and-run fallback for systems where FUSE cannot mount the AppImage is retained.

## Downloads

- `HardCorpsUprising-v0.7.1-windows-x64.zip`
  SHA-256: `197e5e2b9f75525f39e5db32a6d6294097b7b280e65cea804e899462129c4710`
- `HardCorpsUprisingRecompiled-v0.7.1-x86_64.AppImage`
  SHA-256: `98842755d58e1e313fbc012b51dc8752fa9f45aac27ec13a2e39266f6e406112`

## VirusTotal

- Windows: https://www.virustotal.com/gui/file/197e5e2b9f75525f39e5db32a6d6294097b7b280e65cea804e899462129c4710
- Linux AppImage: https://www.virustotal.com/gui/file/98842755d58e1e313fbc012b51dc8752fa9f45aac27ec13a2e39266f6e406112

## Notes

- If you are on v0.7.0, the launcher can install this update for you.
- Linux remains experimental and has not been broadly validated on real Linux PCs or Steam Deck hardware.
- No game files are included. You need your own legally obtained copy of Hard Corps: Uprising.
