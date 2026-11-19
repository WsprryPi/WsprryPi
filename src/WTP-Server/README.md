# Native execution core

This portable component retains bounded typed execution, ownership and lease
policy, replay identities and receipts, clock admission, cancellation, terminal
observations, and an inhibited engine for non-RF tests. It contains no external
WTP frame decoder, endpoint, listener, or response serializer.

The `wsprrypico::wtp` namespace and current internal request types remain stable.
The parent supplies native engines and arbitration through `IJobService`.
The shared [native execution safety rules](../../docs/native-execution-safety.md)
explain frequency placement, timing, cleanup, and ownership. Disconnecting a
management client does not revoke native execution authority.

Run `make -C src/WTP-Server test` to build the independent archive and exercise
the typed service with an inhibited engine and injected clock. No parent
application or hardware dependency is required. See [provenance](PROVENANCE.json)
and the retained [MIT license](LICENSE.md).
