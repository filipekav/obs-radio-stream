# OBS Radio Stream

[![GitHub Release](https://img.shields.io/github/v/release/filipekav/obs-radio-stream)](https://github.com/filipekav/obs-radio-stream/releases)
[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](LICENSE)

**OBS Radio Stream** is a professional OBS Studio plugin designed to stream your master audio mix directly to **Icecast**, **AzuraCast**, and **SHOUTcast** servers. It provides a lightweight, native integration within the OBS interface, allowing for high-quality MP3 streaming without the need for external software or heavy dependencies.

## ✨ Features

- **Multi-Protocol Support:** Stream to Icecast/AzuraCast or SHOUTcast (v1) servers with a single click.
- **Protocol-Aware UI:** The interface dynamically adapts to the selected protocol, showing only relevant fields.
- **Isolated Settings:** Server credentials are stored separately per protocol, so switching between Icecast and SHOUTcast never overwrites your data.
- **Custom Bitrate Control:** High-quality MP3 encoding powered by `libmp3lame`.
- **Native OBS Dock:** Control your broadcast through a dedicated, dockable UI integrated directly into OBS.
- **Local MP3 Backup:** Option to save a local recording of your stream while broadcasting, with automatic timestamped filenames.
- **Live Connection Timer:** Real-time uptime display to monitor your broadcast duration.
- **Multi-language Support:** Fully localized UI available in 8 languages.

## 📡 Supported Protocols

| Protocol | Authentication | Port Behavior |
|---|---|---|
| **Icecast / AzuraCast** | HTTP Basic Auth (PUT) | Connects on the configured port |
| **SHOUTcast (v1)** | Password + ICY headers | Connects on configured port **+ 1** (DNAS standard) |

> [!NOTE]
> SHOUTcast v1 (DNAS) uses port+1 for source/DJ connections. If your server broadcasts on port `8000`, the plugin automatically connects on port `8001`. You should enter the **listener port** in the UI.

## 📥 Installation (For Users)

1. Go to the [Releases](../../releases) tab and download the latest `.zip` package for your system (Windows).
2. Extract the contents of the `.zip` file directly into your OBS Studio installation folder.
   - Typically: `C:\Program Files\obs-studio\`
3. Launch OBS Studio.
4. Navigate to the **Docks** menu at the top and enable **Radio Broadcast**.

## 🛠️ Building from Source (For Developers)

### Prerequisites
To build the plugin from source, you will need the following dependencies:
- **CMake** (3.28 or later)
- **OBS Studio SDK** (`libobs`)
- **Qt6** (Core, Gui, Widgets, Network)
- **libmp3lame** development headers

> [!IMPORTANT]
> **Network Stack:** This plugin implements the Icecast and SHOUTcast protocols manually using native **Qt6 Networking** (`QTcpSocket`). It **does not require** or use `libshout`.

### Build Steps
```bash
# Configure the project
cmake -B build

# Build the project
cmake --build build --config Release
```

## 🌐 Localization

The user interface currently supports 8 languages:
- **English** (en-US)
- **Portuguese** (pt-BR)
- **Spanish** (es-ES)
- **Russian** (ru-RU)
- **German** (de-DE)
- **French** (fr-FR)
- **Japanese** (ja-JP)
- **Chinese** (zh-CN)

> [!NOTE]
> Initial translations for languages other than English and Portuguese were generated with the help of AI. We welcome and encourage native speakers to submit a **Pull Request** to refine or improve these translations in the `data/locale/` directory.

## 📄 License & Credits

This project is licensed under the **GPLv2**. See the [LICENSE](LICENSE) file for more details.

Created and maintained by **Filipe Carvalho**.
