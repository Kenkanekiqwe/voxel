# Voxel Voice

Native Windows voice-chat prototype.

Current MVP:
- Native Win32 UI, no WebView.
- WASAPI-backed audio through miniaudio.
- Opus 48 kHz mono at 32 kbps.
- Separate UDP voice server, so users do not host rooms.
- Mute/unmute.
- Native Windows screen capture through GDI + WIC. The current button captures a JPEG to the temp directory.

Build with Visual Studio 2022 C++ Desktop and CMake 3.20+.

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel

The first configure downloads Opus and miniaudio.

Start VoxelServer.exe and expose UDP port 40000. Then run VoxelClient.exe and enter the server IP.

Next layer: stream captured screen frames to a room and add a proper SFU/jitter buffer while keeping this native client architecture.
