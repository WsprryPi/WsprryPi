# Byte-stream contract

This component retains the header-only `wsprrypi::wtp::ByteStream` contract used
by the independent TLS, HTTP, and USB management adapters. It contains no WTP
codec, framing, session, execution, or reconnect client. The historical component
root and namespace remain stable for these consumers.

Adapters return promptly and never retain caller spans. `Progress` transfers
1 through the supplied span size; other results transfer zero bytes. A failed
write may have an unknown effect. Trust, framing, deadlines, authentication,
and uncertain mutation recovery belong to the consuming management adapter.

Run `make -C src/WTP-Client test` for the standalone C++20 API check. The
component depends only on the standard library and retains its
[MIT license](LICENSE.md). WsprryPi owns this interface; removed upstream wire
sources and their obsolete fixtures are no longer part of the component.
