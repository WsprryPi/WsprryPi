(function (root, factory) {
    const api = factory();
    if (typeof module === "object" && module.exports) {
        module.exports = api;
    } else {
        root.CwTimingState = api;
    }
}(typeof globalThis !== "undefined" ? globalThis : this, function () {
    "use strict";

    const SPEEDS = Object.freeze({ QRSS1: 1, QRSS3: 3, QRSS6: 6 });
    const CONVENTIONAL_STANDARD = Object.freeze({
        intraElement: 1,
        interCharacter: 3,
        interWord: 7,
    });
    const DFCW_STANDARD = Object.freeze({
        intraElement: 0.333333,
        interCharacter: 1,
        interWord: 3,
    });
    const MORSE_TABLE = Object.freeze({
        A: ".-", B: "-...", C: "-.-.", D: "-..", E: ".", F: "..-.",
        G: "--.", H: "....", I: "..", J: ".---", K: "-.-", L: ".-..",
        M: "--", N: "-.", O: "---", P: ".--.", Q: "--.-", R: ".-.",
        S: "...", T: "-", U: "..-", V: "...-", W: ".--", X: "-..-",
        Y: "-.--", Z: "--..", 0: "-----", 1: ".----", 2: "..---",
        3: "...--", 4: "....-", 5: ".....", 6: "-....", 7: "--...",
        8: "---..", 9: "----.", "/": "-..-.", "?": "..--..",
        ".": ".-.-.-", ",": "--..--", "-": "-....-", "+": ".-.-.",
        "=": "-...-",
    });

    function positiveFinite(value) {
        const text = typeof value === "string" ? value.trim() : value;
        const parsed = text === "" ? NaN : Number(text);
        return Number.isFinite(parsed) && parsed > 0 ? parsed : null;
    }

    function inferSpeed(value) {
        const parsed = positiveFinite(value);
        if (parsed === 1) return "QRSS1";
        if (parsed === 3) return "QRSS3";
        if (parsed === 6) return "QRSS6";
        return parsed === null ? "QRSS3" : "Advanced";
    }

    function activeGroup(mode) {
        return String(mode).toUpperCase() === "DFCW" ? "dfcw" : "conventional";
    }

    function standardForGroup(group) {
        return group === "dfcw" ? DFCW_STANDARD : CONVENTIONAL_STANDARD;
    }

    function tripletIsValid(triplet) {
        return !!triplet && ["intraElement", "interCharacter", "interWord"]
            .every((key) => positiveFinite(triplet[key]) !== null);
    }

    function inferSpacing(group, triplet) {
        if (!tripletIsValid(triplet)) return "Advanced";
        const standard = standardForGroup(group);
        return triplet.intraElement === standard.intraElement &&
            triplet.interCharacter === standard.interCharacter &&
            triplet.interWord === standard.interWord
            ? "Standard"
            : "Advanced";
    }

    function cloneTriplet(triplet) {
        return {
            intraElement: triplet.intraElement,
            interCharacter: triplet.interCharacter,
            interWord: triplet.interWord,
        };
    }

    function cloneState(state) {
        return {
            dotSeconds: state.dotSeconds,
            conventional: cloneTriplet(state.conventional),
            dfcw: cloneTriplet(state.dfcw),
        };
    }

    function applySpeed(state, speed) {
        const next = cloneState(state);
        if (Object.prototype.hasOwnProperty.call(SPEEDS, speed)) {
            next.dotSeconds = SPEEDS[speed];
        }
        return next;
    }

    function applySpacing(state, mode, spacing) {
        const next = cloneState(state);
        const group = activeGroup(mode);
        if (spacing === "Standard") {
            next[group] = cloneTriplet(standardForGroup(group));
        }
        return next;
    }

    function gapDurations(dotSeconds, triplet) {
        const dot = positiveFinite(dotSeconds);
        if (dot === null || !tripletIsValid(triplet)) return null;
        return {
            intraElement: dot * triplet.intraElement,
            interCharacter: dot * triplet.interCharacter,
            interWord: dot * triplet.interWord,
        };
    }

    function invalidFields(state) {
        const invalid = [];
        if (positiveFinite(state.dotSeconds) === null) invalid.push("dotSeconds");
        ["conventional", "dfcw"].forEach((group) => {
            ["intraElement", "interCharacter", "interWord"].forEach((key) => {
                if (positiveFinite(state[group][key]) === null) invalid.push(`${group}.${key}`);
            });
        });
        return invalid;
    }

    function isValid(state) {
        return invalidFields(state).length === 0;
    }

    function serialize(state) {
        return {
            "Dot Seconds": state.dotSeconds,
            "Intra Element Gap": state.conventional.intraElement,
            "Inter Character Gap": state.conventional.interCharacter,
            "Inter Word Gap": state.conventional.interWord,
            "DFCW Intra Element Gap": state.dfcw.intraElement,
            "DFCW Inter Character Gap": state.dfcw.interCharacter,
            "DFCW Inter Word Gap": state.dfcw.interWord,
        };
    }

    // Mirrors ExecutionPlanCompiler::expand_morse_message and its QRSS,
    // FSKCW, and DFCW mark/gap selection. Keep this as the browser's single
    // duration estimator so display and duration-policy validation cannot drift.
    function estimateMessageSeconds(message, mode, timing) {
        if (!timing || positiveFinite(timing.dotSeconds) === null) {
            return { ok: false, reason: "unavailable" };
        }

        const normalizedMode = String(mode || "").toUpperCase();
        if (!["QRSS", "FSKCW", "DFCW"].includes(normalizedMode)) {
            return { ok: false, reason: "not applicable" };
        }

        const dashSeconds = normalizedMode === "DFCW"
            ? Number(timing.dotSeconds)
            : Number(timing.dotSeconds) * 3;
        const required = [
            dashSeconds,
            timing.intraElementGapSeconds,
            timing.interCharacterGapSeconds,
            timing.interWordGapSeconds,
        ];
        if (!required.every((value) => positiveFinite(value) !== null)) {
            return { ok: false, reason: "unavailable" };
        }

        let totalSeconds = 0;
        let emittedCharacter = false;
        let pendingWordGap = false;
        for (const ch of String(message || "")) {
            if (/\s/.test(ch)) {
                if (emittedCharacter) pendingWordGap = true;
                continue;
            }

            const morse = MORSE_TABLE[ch.toUpperCase()];
            if (!morse) {
                return { ok: false, reason: `unavailable: unsupported character ${ch}` };
            }

            if (emittedCharacter) {
                totalSeconds += pendingWordGap
                    ? Number(timing.interWordGapSeconds)
                    : Number(timing.interCharacterGapSeconds);
            }
            for (let index = 0; index < morse.length; ++index) {
                totalSeconds += morse[index] === "."
                    ? Number(timing.dotSeconds)
                    : dashSeconds;
                if (index + 1 < morse.length) {
                    totalSeconds += Number(timing.intraElementGapSeconds);
                }
            }
            emittedCharacter = true;
            pendingWordGap = false;
        }

        return emittedCharacter
            ? { ok: true, seconds: totalSeconds }
            : { ok: false, reason: "unavailable" };
    }

    return Object.freeze({
        SPEEDS,
        CONVENTIONAL_STANDARD,
        DFCW_STANDARD,
        MORSE_TABLE,
        positiveFinite,
        inferSpeed,
        activeGroup,
        standardForGroup,
        tripletIsValid,
        inferSpacing,
        cloneState,
        applySpeed,
        applySpacing,
        gapDurations,
        invalidFields,
        isValid,
        serialize,
        estimateMessageSeconds,
    });
}));
