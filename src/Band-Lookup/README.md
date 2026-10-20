# Band Lookup Catalog

This component defines WsprryPi's shared frequency-to-band correlation catalog.
It contains correlation envelopes only: it does not determine operating authority,
WSPR dial-frequency presets, or backend qualification.

The parent `BandLookup` facade owns the separate WSPR preset convenience layer,
including qualified preset identities. Those presets consume this catalog for
band identity but are not part of this component's correlation data.
