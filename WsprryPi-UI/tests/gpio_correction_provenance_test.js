const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname, '../data/site.js'), 'utf8');
const extract = (name, next) => source.slice(source.indexOf(`function ${name}(`), source.indexOf(`function ${next}(`));
const nodes = Object.fromEntries(['panel', 'value', 'detail'].map(key => [`gpio_frequency_correction_${key}`, {textContent: '', hidden: false}]));
const context = vm.createContext({document: {getElementById: id => nodes[id]}});
vm.runInContext(extract('normalizeRuntimeStatus', 'parseOperationFrequencyWithOptionalUnits') + extract('renderGpioFrequencyCorrection', 'applyRuntimeStatus'), context);
const provenance = {
    available: true, processor_profile: 'RP1', selected_parent: 'PLL_SYS',
    nominal_rate_hz: 200000000, intrinsic_ppm: -46.245,
    selected_component_ppm: 10, conducted_residual_ppm: 1.25, correction_ppm: 11.25, final_ppm: -34.995,
    correction_mode: 'qualified_estimate_plus_residual',
    provider_source_signature: '<source>', provider_snapshot_time: '2026-08-30T20:00:00Z'
};
function render(candidate = provenance, backend = 'rp1-gpclk') {
    context.renderGpioFrequencyCorrection(context.normalizeRuntimeStatus({
        transmit_backend: backend, gpio_correction_candidate: candidate,
        frequency_estimate_qualification: 'qualified', frequency_estimate_age_seconds: 2
    }));
}
render();
assert.match(nodes.gpio_frequency_correction_value.textContent, /11.250 PPM correction/);
assert.doesNotMatch(nodes.gpio_frequency_correction_value.textContent, /intrinsic|final|46\.245|34\.995|PLL_SYS/);
assert.match(nodes.gpio_frequency_correction_value.textContent, /1.250 residual ·/);
assert.match(nodes.gpio_frequency_correction_value.textContent, /sampled 2026/);
assert.match(nodes.gpio_frequency_correction_detail.textContent, /Committed: unavailable/);
assert.match(nodes.gpio_frequency_correction_detail.textContent, /Estimate: qualified · age 2 s/);
for (const age of [null, undefined, NaN, Infinity, -1, "5", false]) {
    context.renderGpioFrequencyCorrection(context.normalizeRuntimeStatus({
        transmit_backend: 'rp1-gpclk', frequency_estimate_qualification: 'unavailable',
        frequency_estimate_age_seconds: age
    }));
    assert.match(nodes.gpio_frequency_correction_detail.textContent, /age unknown/);
    assert.doesNotMatch(nodes.gpio_frequency_correction_detail.textContent, /age 0 s/);
}
context.renderGpioFrequencyCorrection(context.normalizeRuntimeStatus({
    transmit_backend: 'rp1-gpclk', frequency_estimate_age_seconds: 0
}));
assert.match(nodes.gpio_frequency_correction_detail.textContent, /age 0 s/);
render({...provenance, correction_mode: 'fixed_manual'});
assert.match(nodes.gpio_frequency_correction_value.textContent, /residual configured, not applied/);
render({...provenance, correction_ppm: null});
assert.equal(nodes.gpio_frequency_correction_value.textContent, 'Candidate: unavailable');
render({...provenance, correction_ppm: 0, selected_component_ppm: 0,
    correction_mode: 'uncalibrated'});
assert.match(nodes.gpio_frequency_correction_value.textContent, /0.000 PPM correction/);
assert.doesNotMatch(nodes.gpio_frequency_correction_value.textContent, /46\.245|34\.995/);
render(provenance, 'si5351');
assert.equal(nodes.gpio_frequency_correction_panel.hidden, true);
console.log('GPIO correction provenance UI tests passed');
