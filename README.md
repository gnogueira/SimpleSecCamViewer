# SimpleSecCamViewer

A Windows XP application for viewing compatible NVR cameras over a local network. It supports up to 16 channels, selectable layouts, fullscreen viewing and automatic reconnection. Actual capacity depends on the NVR, network and computer.

The application uses the RTSP substream format of Xiongmai/XMeye NVRs, including some Giga Security models. It is not a universal RTSP viewer and does not record video. Compatibility with other devices must be tested.

## Setup

Windows XP SP3 and a 32-bit installation of VLC 3 are required. Copy `windows/iniciar.bat.example` to `iniciar.bat` next to the executable and edit the NVR address, username, password and VLC path. The application reads the file as text; it does not need to be run. The password is stored in plain text: protect this file and do not publish it.

Channels and layout are saved in `camxp.ini`. Press F11 to toggle fullscreen and Esc to return to the window. See [windows/README.md](windows/README.md) for build instructions.

## Capacity and security

Each camera uses an NVR session, and other applications share the device's connection limit. Connections are spaced out, with increasing delays after repeated failures. Recovery monitors frame progress and may not detect an NVR retransmitting a frozen picture as new frames.

RTSP and credentials in the URL do not provide confidentiality by themselves. Use a trusted network and do not expose the NVR directly to the internet. Local settings, passwords, logs and recordings are not included in this repository.

## License

This project's own code is available under the [MIT License](LICENSE). Third-party components have their own licenses. The application loads LibVLC from a separate VLC installation. Check the applicable conditions before redistributing VLC components; see [THIRD_PARTY.md](THIRD_PARTY.md).
