"use strict";

const assert = require("node:assert/strict");
const timing = require("../data/cw_timing_state.js");

function baseState() {
    return {
        dotSeconds: 3,
        conventional: { intraElement: 1, interCharacter: 3, interWord: 7 },
        dfcw: { intraElement: 0.333333, interCharacter: 1, interWord: 3 },
    };
}

assert.equal(timing.inferSpeed(undefined), "QRSS3");
assert.equal(timing.inferSpeed(1), "QRSS1");
assert.equal(timing.inferSpeed(3), "QRSS3");
assert.equal(timing.inferSpeed(6), "QRSS6");
assert.equal(timing.inferSpeed(3.0000001), "Advanced");
assert.equal(timing.inferSpacing("conventional", baseState().conventional), "Standard");
assert.equal(timing.inferSpacing("dfcw", baseState().dfcw), "Standard");
assert.equal(timing.inferSpacing("dfcw", { intraElement: 1 / 3, interCharacter: 1, interWord: 3 }), "Advanced");

const custom = baseState();
custom.conventional = { intraElement: 1.5, interCharacter: 4, interWord: 8 };
custom.dfcw = { intraElement: 0.5, interCharacter: 2, interWord: 4 };
const speedChanged = timing.applySpeed(custom, "QRSS6");
assert.equal(speedChanged.dotSeconds, 6);
assert.deepEqual(speedChanged.conventional, custom.conventional);
assert.deepEqual(speedChanged.dfcw, custom.dfcw);
const conventionalStandard = timing.applySpacing(custom, "QRSS", "Standard");
assert.deepEqual(conventionalStandard.conventional, timing.CONVENTIONAL_STANDARD);
assert.deepEqual(conventionalStandard.dfcw, custom.dfcw);
const dfcwStandard = timing.applySpacing(custom, "DFCW", "Standard");
assert.deepEqual(dfcwStandard.dfcw, timing.DFCW_STANDARD);
assert.deepEqual(dfcwStandard.conventional, custom.conventional);
assert.deepEqual(timing.applySpacing(custom, "DFCW", "Advanced"), custom);

[
    [1, [1, 3, 7], [1, 3, 7]],
    [3, [1, 3, 7], [3, 9, 21]],
    [6, [1, 3, 7], [6, 18, 42]],
    [2.5, [1, 3, 7], [2.5, 7.5, 17.5]],
    [3, [1.5, 4, 8], [4.5, 12, 24]],
    [1, [0.333333, 1, 3], [0.333333, 1, 3]],
    [3, [0.333333, 1, 3], [0.999999, 3, 9]],
    [6, [0.333333, 1, 3], [1.999998, 6, 18]],
    [2.5, [0.333333, 1, 3], [0.8333325, 2.5, 7.5]],
    [3, [0.5, 2, 4], [1.5, 6, 12]],
].forEach(([dot, values, expected]) => {
    const actual = timing.gapDurations(dot, {
        intraElement: values[0], interCharacter: values[1], interWord: values[2],
    });
    assert.ok(Math.abs(actual.intraElement - expected[0]) < 1e-12);
    assert.ok(Math.abs(actual.interCharacter - expected[1]) < 1e-12);
    assert.ok(Math.abs(actual.interWord - expected[2]) < 1e-12);
});

const serialized = timing.serialize(custom);
assert.deepEqual(Object.keys(serialized), [
    "Dot Seconds", "Intra Element Gap", "Inter Character Gap", "Inter Word Gap",
    "DFCW Intra Element Gap", "DFCW Inter Character Gap", "DFCW Inter Word Gap",
]);
assert.equal(timing.isValid(custom), true);
[
    ["dotSeconds"],
    ["conventional", "intraElement"],
    ["conventional", "interCharacter"],
    ["conventional", "interWord"],
    ["dfcw", "intraElement"],
    ["dfcw", "interCharacter"],
    ["dfcw", "interWord"],
].forEach((path) => {
    [0, -1, Number.NaN, Number.POSITIVE_INFINITY, "", "1 second"].forEach((value) => {
        const invalid = baseState();
        if (path.length === 1) invalid[path[0]] = value;
        else invalid[path[0]][path[1]] = value;
        assert.equal(timing.isValid(invalid), false, `${path.join(".")} must reject ${String(value)}`);
    });
});

function estimate(message, mode, dotSeconds, gaps) {
    return timing.estimateMessageSeconds(message, mode, {
        dotSeconds,
        intraElementGapSeconds: dotSeconds * gaps.intraElement,
        interCharacterGapSeconds: dotSeconds * gaps.interCharacter,
        interWordGapSeconds: dotSeconds * gaps.interWord,
    });
}

const conventional = timing.CONVENTIONAL_STANDARD;
const dfcw = timing.DFCW_STANDARD;
for (const mode of ["QRSS", "FSKCW", "DFCW"]) {
    const gaps = mode === "DFCW" ? dfcw : conventional;
    assert.equal(
        estimate("E", mode, 61, gaps).seconds > 60,
        true,
        `${mode} must detect an overlong message`
    );
    assert.equal(
        estimate("E", mode, 60, gaps).seconds,
        60,
        `${mode} must accept equality with the repeat interval`
    );
}

const overlong = estimate("EE", "QRSS", 20, conventional);
assert.equal(overlong.seconds > 60, true);
assert.equal(estimate("E", "QRSS", 20, conventional).seconds <= 60, true,
    "shortening the message must recover");
assert.equal(overlong.seconds <= 120, true,
    "increasing the repeat interval must recover");
assert.equal(estimate("EE", "QRSS", 10, conventional).seconds <= 60, true,
    "shortening dot length must recover");
assert.equal(
    estimate("EE", "QRSS", 20, { intraElement: 1, interCharacter: 0.5, interWord: 7 }).seconds <
        overlong.seconds,
    true,
    "changing active spacing must reevaluate duration"
);
assert.equal(timing.estimateMessageSeconds("", "QRSS", {
    dotSeconds: 1,
    intraElementGapSeconds: 1,
    interCharacterGapSeconds: 3,
    interWordGapSeconds: 7,
}).ok, false, "empty messages must remain invalid");
assert.match(timing.estimateMessageSeconds("E@", "QRSS", {
    dotSeconds: 1,
    intraElementGapSeconds: 1,
    interCharacterGapSeconds: 3,
    interWordGapSeconds: 7,
}).reason, /unsupported character @/, "unsupported messages must remain invalid");

console.log("cw_timing_state_test passed");
