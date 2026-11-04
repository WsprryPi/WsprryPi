"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

// Execute the named production lookup helpers without entering the HTTP handler.
// PHP tokens retain function bodies exactly; the response boundary throws locally.
const { spawnSync } = require("node:child_process");
const lookup = String.raw`
function respond(int $status, array $payload): never { throw new InvalidArgumentException($payload['error']); }
$wanted = ['chooseLookupBaseCallsignSegment', 'normalizeLookupBaseCallsign', 'buildLookupCallsignCandidates'];
$tokens = token_get_all(file_get_contents($argv[1]));
for ($i = 0; $i < count($tokens); ++$i) {
    if (!is_array($tokens[$i]) || $tokens[$i][0] !== T_FUNCTION) continue;
    $start = $i; $name = '';
    for ($j = $i + 1; $j < count($tokens); ++$j) {
        if (is_array($tokens[$j]) && $tokens[$j][0] === T_STRING) { $name = $tokens[$j][1]; break; }
        if ($tokens[$j] === '(') break;
    }
    if (!in_array($name, $wanted, true)) continue;
    $body = ''; $depth = 0; $opened = false;
    for (; $i < count($tokens); ++$i) {
        $token = $tokens[$i]; $body .= is_array($token) ? $token[1] : $token;
        if ($token === '{') { ++$depth; $opened = true; }
        if ($token === '}' && --$depth === 0 && $opened) break;
    }
    eval($body);
}
foreach ([' aa0nt ', 'AA0NT/P', 'W0/AA0NT', '<AA0NT>', 'AA0NT/12'] as $input) {
    $actual = buildLookupCallsignCandidates($input);
    $expected = ['base' => 'AA0NT', 'downloader' => ['AA0NT', '%/AA0NT', 'AA0NT/%', '<AA0NT>'],
                 'clickhouse_regexes' => ['^AA0NT$', '^<AA0NT>$', '^[A-Z0-9]+/AA0NT$', '^AA0NT/[A-Z0-9]+$']];
    if ($actual !== $expected) throw new RuntimeException('compound callsign lookup candidates changed');
}
try { buildLookupCallsignCandidates(''); throw new RuntimeException('empty callsign admitted'); }
catch (InvalidArgumentException $expected) {}
echo 'Production compound callsign lookup candidates passed';
`;
const lookupResult = spawnSync("php", ["-r", lookup, path.resolve(__dirname, "../data/fetch_spots.php")], {encoding: "utf8", timeout: 5000});
assert.equal(lookupResult.status, 0, lookupResult.stderr);
console.log(lookupResult.stdout);
