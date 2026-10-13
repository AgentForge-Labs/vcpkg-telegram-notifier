# AgentForge Telegram Notifier C++20 vcpkg overlay

Public isolated SDK source snapshot (v0.2.0). **The private monorepo is not exposed.** Licensed AGPL-3.0-only.

```bash
git clone https://github.com/AgentForge-Labs/vcpkg-telegram-notifier
git clone https://github.com/microsoft/vcpkg
./vcpkg/bootstrap-vcpkg.sh
./vcpkg/vcpkg install agentforge-telegram-notifier --overlay-ports="$PWD/vcpkg-telegram-notifier/ports"
```

Self-owned public vcpkg overlay, **not** an official Microsoft vcpkg registry listing.
