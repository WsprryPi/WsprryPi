// Hardware-free native-host visual fixture. Every inventory reply is synthetic.
(function() {
    const identity = 'usb:04d8:00dd:path:6d61636f732d6c6f636174696f6e2d3132333430303030';
    const state = new URL(location.href).searchParams.get('fixture_state') || 'available';
    scheduleAutosave = () => {};
    hasUnsavedLocalConfigChanges = () => false;
    ajaxWithEndpointFallback = (endpoint, options = {}) => {
        const reply = {done(callback) {
            if (endpoint === SI5351_ADDRESSES_ENDPOINT) setTimeout(() => callback({
                'I2C Bus': options.data.bus, 'Adapter Identity': options.data.identity,
                Transport: 'mcp2221a-hid', Addresses: state === 'denied' ? [] : ['0x' + Number(options.data.address).toString(16).toUpperCase()],
                'Discovery Error': state === 'denied' ? 'HID access denied. Close the other application and retry.' : ''
            }), 30);
            return reply;
        }, fail() { return reply; }};
        return reply;
    };
    function initialize() {
        if (window.PicoFirmwareSafety?.locked) { setTimeout(initialize,25); return; }
        document.getElementById('transmit_backend').checked = true;
        window.WSPRRYPI_PLATFORM = {...(window.WSPRRYPI_PLATFORM || {}),
            si5351Transport: 'mcp2221a-hid', si5351AdapterDiscoveryError: '',
            si5351Adapters: state === 'missing' ? [] : Array.from({length: state === 'duplicate' ? 2 : 1}, () => ({Identity:identity,Name:'MCP2221A',Available:state !== 'duplicate'})),
            si5351Detected: null};
        clickTransmitBackend();
        document.getElementById('si5351_reference_frequency').value = '25000000';
        document.getElementById('si5351_reference_source').value = 'crystal';
        document.getElementById('si5351_reference_source').dispatchEvent(new Event('change',{bubbles:true}));
        document.getElementById('si5351_i2c_address').value = '0x60';
        populateI2cBuses(17, identity);
        if (state === 'available' || state === 'denied')
            populateSi5351Addresses(17,'0x60',state === 'available' ? ['0x60'] : [], state === 'denied' ? 'HID access denied. Close the other application and retry.' : '',17);
        syncBackendControlAvailability();
        updateMachinePowerCapabilities({reboot:false,power_off:false});
        document.getElementById('transmitter-hardware-tab').click();
        document.documentElement.setAttribute('data-bs-theme',new URL(location.href).searchParams.get('theme') === 'dark' ? 'dark' : 'light');
        document.documentElement.dataset.nativeHostFixture = state;
    }
    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded',()=>setTimeout(initialize,0));
    else setTimeout(initialize,0);
})();
