#include "apache_privileged_network_policy.hpp"

#include <sstream>

ApachePrivilegedNetworkPolicyResult render_apache_privileged_network_policy(
    PrivilegedNetworkMode mode) {
    std::ostringstream output;
    output << "# Managed by WsprryPi. Manual edits will be replaced.\n"
           << "# Apache overwrites the dedicated backend identity with its "
              "actual connection peer.\n";
    if (mode == PrivilegedNetworkMode::insecure_disabled) {
        output << "# NETWORK SAFETY OFF\n";
    } else {
        output << "# Current-network authorization is enforced by WsprryPi "
                  "for each request.\n";
    }
    output << "<LocationMatch \"^/wsprrypi/(?:config(?:/|$)|control(?:/|$)|version$|api(?:/|$)|socket(?:/|$))\">\n"
           << "    RequestHeader unset "
           << WSPRRYPI_TRUSTED_PROXY_IDENTITY_HEADER << "\n"
           << "    RequestHeader set "
           << WSPRRYPI_TRUSTED_PROXY_IDENTITY_HEADER
           << " \"expr=%{CONN_REMOTE_ADDR}\"\n"
           << "</LocationMatch>\n";
    return {output.str(), {}};
}
