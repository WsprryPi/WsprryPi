// SPDX-License-Identifier: MIT
// Production-shaped fixture from WsprryPico/tests/network_diagnostics_fixture.hpp.
// Synthetic production-shaped network diagnostics for the USB endpoint fixture.
#pragma once
#include <string>
#include <optional>

inline std::string network_diagnostics_fixture(bool enabled, std::optional<bool> requested = {}) {
    auto result = std::string("{\"enabled\":") + (enabled ? "true" : "false") +
           R"DIAG(,
  "ntp_observations": {
    "source": "microchrony",
    "packet_accepted": false,
    "arbiter_accepted_this_packet": false,
    "last_accepted_measurement": null
  },
  "ntp_rate": {
    "qualified": false,
    "utc_rate_ppb": 0,
    "uncertainty_ppb": 0,
    "generation": "0",
    "sampled_raw_ns": "0",
    "span_ns": "0",
    "samples": 0,
    "reason": "startup"
  },
  "ntp_learning": {
    "schema": "microchrony-status/1",
    "synchronized": false,
    "phase_settled": false,
    "rate_settled": false,
    "utc_frequency_ppb": 0,
    "skew_ppb": 0,
    "remaining_correction_ns": "0",
    "clock_error_ns": "0",
    "rate_ppb": 0,
    "rf_candidate": {
      "qualified": false,
      "utc_rate_ppb": 0,
      "uncertainty_ppb": 0,
      "generation": "0",
      "sampled_raw_ns": "0",
      "span_ns": "0",
      "samples": 0,
      "reason": "startup"
    }
  },
  "ntp_pool": {
    "schema": "microchrony-sources/1",
    "enabled": false,
    "selected_slot": null,
    "combined_sources": 0,
    "utc_updates": 0,
    "peers": [
      {
        "slot": 0,
        "hostname": "",
        "endpoint": "",
        "queries": 0,
        "accepted": 0,
        "rejected": 0,
        "selection_score_milli": 0,
        "reach": 0,
        "max_process_us": 0,
        "poll_seconds": 0,
        "selected": false,
        "combined": false,
        "reason": "startup"
      }
    ]
  },
  "initialized": false,
  "link_status": 0,
  "ipv4": "",
  "softap_netif": {
    "up": false,
    "link_up": false,
    "ipv4": ""
  },
  "requested_enabled": null,
  "withdrawal_pending": false,
  "resume_after_withdrawal": false,
  "control_configured": false,
  "control_listening": false,
  "consumer_web_ready": false,
  "consumer_web_port": 80,
  "deployment_identity_matches": false,
  "station_mac": "",
  "stable_hostname": "",
  "configured_hostname": "pico-usb.local",
  "advertised_hostname": "",
  "mdns_state": "unconfigured",
  "mdns_reason": "",
  "mdns_registrations": 0,
  "mdns_conflicts": 0,
  "mdns_failures": 0,
  "mdns_address_changes": 0,
  "mdns_goodbye_attempts": 0,
  "mdns_goodbye_failures": 0,
  "mdns_rejected_packets": 0,
  "power_save": null,
  "packets": {
    "arp": {
      "received": 0,
      "sent": 0,
      "dropped": 0
    },
    "ipv4": {
      "received": 0,
      "sent": 0,
      "dropped": 0
    },
    "tcp": {
      "received": 0,
      "sent": 0,
      "dropped": 0
    },
    "udp": {
      "received": 0,
      "sent": 0,
      "dropped": 0
    }
  },
  "memory": {
    "statistics_enabled": true,
    "heap": null,
    "tcp_pcbs": null,
    "tcp_listeners": null,
    "udp_pcbs": null,
    "timeouts": null,
    "igmp_groups": null,
    "tcp_segments": null,
    "packet_pool": null
  },
  "tx_credit": {
    "waits": 0,
    "preserved": 0,
    "timeouts": 0
  },
  "ntp_server": "",
  "ntp_address": "",
  "ntp_resolution": "unresolved",
  "ntp_resolution_failures": 0,
  "queries": 0,
  "accepted": 0,
  "rejected": 0,
  "last_rtt_ns": 0,
  "last_sample_uncertainty_ns": null
})DIAG";
    if (requested) {
        const std::string key = "\"requested_enabled\": null";
        result.replace(result.find(key), key.size(), std::string("\"requested_enabled\": ") + (*requested ? "true" : "false"));
        if (enabled && !*requested) {
            const std::string withdrawal = "\"withdrawal_pending\": false";
            result.replace(result.find(withdrawal), withdrawal.size(), "\"withdrawal_pending\": true");
        }
    }
    return result;
}
