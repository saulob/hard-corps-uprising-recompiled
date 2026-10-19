# Launcher header art

`header-art.svg` is the source of truth. Edit it first.

`header-art.png` is the runtime asset, generated from the SVG at 3840x432 with a transparent
background. Do not edit it by hand; regenerate it after every SVG change.

From the repository root on Windows, with Edge installed:

```powershell
& 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe' `
  --headless=new --disable-gpu --hide-scrollbars --default-background-color=00000000 `
  --window-size=3840,432 --screenshot=assets/launcher/header-art.png `
  "file:///$((Resolve-Path assets/launcher/header-art.svg).Path -replace '\\','/')"
```
