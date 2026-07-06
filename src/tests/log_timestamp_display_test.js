const fs = require("fs");
const path = require("path");
const vm = require("vm");

function requireCondition(condition, message) {
    if (!condition) {
        console.error(`FAIL: ${message}`);
        process.exit(1);
    }
}

const sourcePath = path.resolve(__dirname, "../../WsprryPi-UI/data/view_logs.js");
const source = fs.readFileSync(sourcePath, "utf8");

const noop = () => {};
const fakeClassList = { add: noop, remove: noop, toggle: noop };
const fakeElement = {
    classList: fakeClassList,
    appendChild: noop,
    addEventListener: noop,
    removeEventListener: noop,
    setAttribute: noop,
    removeAttribute: noop,
    querySelector: () => null,
    querySelectorAll: () => [],
    contains: () => false,
    textContent: "",
    title: "",
    hidden: false,
    scrollTop: 0,
    scrollHeight: 0,
    clientHeight: 0
};

const fakeDocument = {
    hidden: false,
    body: fakeElement,
    documentElement: { style: {} },
    getElementById: () => null,
    createElement: () => ({ ...fakeElement, classList: { ...fakeClassList } }),
    createTextNode: (text) => ({ textContent: String(text) }),
    addEventListener: noop,
    removeEventListener: noop,
    querySelector: () => null,
    querySelectorAll: () => []
};

const fakeWindow = {
    WSPRRYPI_ENABLE_LOG_TEST_HOOKS: true,
    location: new URL("http://localhost/view_logs.php"),
    addEventListener: noop,
    removeEventListener: noop,
    setTimeout,
    clearTimeout,
    setInterval,
    clearInterval,
    localStorage: {
        getItem: () => null,
        setItem: noop,
        removeItem: noop
    }
};

const context = vm.createContext({
    window: fakeWindow,
    document: fakeDocument,
    console,
    URL,
    EventSource: function EventSource() {},
    requestAnimationFrame: (cb) => setTimeout(cb, 0),
    cancelAnimationFrame: clearTimeout
});

vm.runInContext(source, context, { filename: sourcePath });

const hooks = fakeWindow.WSPRRYPI_LOG_TEST_HOOKS;
requireCondition(hooks, "view_logs.js must expose gated log test hooks");

const samples = [
    {
        __REALTIME_TIMESTAMP: "1783296000123000",
        _SYSTEMD_UNIT: "wsprrypi.service",
        PRIORITY: "6",
        MESSAGE: "Scheduling path selected: WSPR.",
        expected: "2026-07-06T00:00:00.123Z"
    },
    {
        __REALTIME_TIMESTAMP: "1783296001456000",
        _SYSTEMD_UNIT: "wsprrypi.service",
        PRIORITY: "6",
        MESSAGE: "Waiting for next transmission window.",
        expected: "2026-07-06T00:00:01.456Z"
    },
    {
        __REALTIME_TIMESTAMP: "1783296010789000",
        _SYSTEMD_UNIT: "wsprrypi.service",
        PRIORITY: "6",
        MESSAGE: "Started transmission: 14.097100 MHz.",
        expected: "2026-07-06T00:00:10.789Z"
    },
    {
        __REALTIME_TIMESTAMP: "1783296121412000",
        _SYSTEMD_UNIT: "wsprrypi.service",
        PRIORITY: "6",
        MESSAGE: "Completed transmission: 110.629798 seconds.",
        expected: "2026-07-06T00:02:01.412Z"
    },
    {
        __REALTIME_TIMESTAMP: "1783296122000000",
        _SYSTEMD_UNIT: "init.scope",
        PRIORITY: "6",
        MESSAGE: "wsprrypi.service: Deactivated successfully.",
        expected: "2026-07-06T00:02:02.000Z"
    }
];

const rendered = samples.map((sample) => hooks.formatTimestampUTC(sample));
for (let i = 0; i < samples.length; i += 1) {
    requireCondition(
        rendered[i] === samples[i].expected,
        `${samples[i]._SYSTEMD_UNIT} sample ${i} should render ${samples[i].expected}, got ${rendered[i]}`
    );
    requireCondition(
        !rendered[i].startsWith("1970-01-01"),
        `${samples[i]._SYSTEMD_UNIT} sample ${i} must not render as Unix epoch`
    );
}

requireCondition(
    new Set(rendered).size === rendered.length,
    "multiple distinct source timestamps must remain distinct after rendering"
);

const invalidSamples = [
    {},
    { __REALTIME_TIMESTAMP: null },
    { __REALTIME_TIMESTAMP: "" },
    { __REALTIME_TIMESTAMP: "not-a-timestamp" },
    { __REALTIME_TIMESTAMP: "0" },
    { __REALTIME_TIMESTAMP: "2147483647" }
];

for (const sample of invalidSamples) {
    requireCondition(
        hooks.formatTimestampUTC(sample) === "",
        `invalid timestamp ${JSON.stringify(sample.__REALTIME_TIMESTAMP)} must render blank`
    );
}

console.log("PASS: log timestamp display regression tests");
