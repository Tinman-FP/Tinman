# Getting Started With Tinman

## Install On macOS

1. Download the macOS archive from the repository's Releases page.
2. Open the archive and drag Tinman.app into Applications.
3. Open Tinman.
4. If macOS asks for confirmation because the preview build is not notarized,
   Control-click Tinman, choose **Open**, and confirm.

Tinman stores its settings separately from TinManX1 and OrcaSlicer.

## Select A Printer

On first launch, complete the setup guide and select any manufacturers you use.
You can change the selection at any time:

1. Open the **Prepare** workspace.
2. Open the printer preset menu.
3. Choose **Select/Remove printers (system presets)**.
4. Select the manufacturer, model, and nozzle variants you want.
5. Finish the assistant.

To define an unsupported machine, choose **Create printer** in the printer
preset menu and enter the machine dimensions, firmware type, and extruder
details.

## Add Filaments

Open the filament preset menu and choose **Add/Remove filaments**. Tinman shows
materials compatible with the selected printer. Custom filament presets can be
created from the filament settings page.

## Existing TinManX1 Users

Tinman intentionally does not copy TinManX1 profiles or settings. Keeping the
applications separate makes it possible to test Tinman without changing a
working TinManX1 installation.

## Support Information

When reporting a problem, include:

- Tinman version and operating system
- printer model and nozzle diameter
- the selected printer, filament, and process preset names
- a project file or small reproduction model when it can be shared safely
- the relevant log from Tinman's log directory

Remove printer credentials, access codes, API keys, and private network
addresses before publishing logs.
