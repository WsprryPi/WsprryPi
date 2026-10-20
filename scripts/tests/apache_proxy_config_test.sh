#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPOSITORY_ROOT=$(cd "${SCRIPT_DIR}/../.." && pwd)
CONFIG="${REPOSITORY_ROOT}/config/wsprrypi.conf"
INSTALLER="${REPOSITORY_ROOT}/scripts/install.sh"

mapping='ProxyPass        /wsprrypi/api/support-bundles http://127.0.0.1:31415/api/support-bundles'
reverse_mapping='ProxyPassReverse /wsprrypi/api/support-bundles http://127.0.0.1:31415/api/support-bundles'
intake_mapping='ProxyPass        /wsprrypi/api/support-intake http://127.0.0.1:31415/api/support-intake'
intake_reverse_mapping='ProxyPassReverse /wsprrypi/api/support-intake http://127.0.0.1:31415/api/support-intake'
network_safety_mapping='ProxyPass        /wsprrypi/api/network-safety http://127.0.0.1:31415/api/network-safety'
network_safety_reverse_mapping='ProxyPassReverse /wsprrypi/api/network-safety http://127.0.0.1:31415/api/network-safety'
rp1_route_mapping='ProxyPass        /wsprrypi/api/rp1-gpclk-route http://127.0.0.1:31415/api/rp1-gpclk-route'
rp1_route_reverse_mapping='ProxyPassReverse /wsprrypi/api/rp1-gpclk-route http://127.0.0.1:31415/api/rp1-gpclk-route'
installer_mapping="ProxyPass        /wsprrypi/api/support-bundles http://127.0.0.1:\${WSPRRYPI_WEB_PORT}/api/support-bundles"
installer_reverse_mapping="ProxyPassReverse /wsprrypi/api/support-bundles http://127.0.0.1:\${WSPRRYPI_WEB_PORT}/api/support-bundles"
installer_intake_mapping="ProxyPass        /wsprrypi/api/support-intake http://127.0.0.1:\${WSPRRYPI_WEB_PORT}/api/support-intake"
installer_intake_reverse_mapping="ProxyPassReverse /wsprrypi/api/support-intake http://127.0.0.1:\${WSPRRYPI_WEB_PORT}/api/support-intake"
installer_network_safety_mapping="ProxyPass        /wsprrypi/api/network-safety http://127.0.0.1:\${WSPRRYPI_WEB_PORT}/api/network-safety"
installer_network_safety_reverse_mapping="ProxyPassReverse /wsprrypi/api/network-safety http://127.0.0.1:\${WSPRRYPI_WEB_PORT}/api/network-safety"
installer_rp1_route_mapping="ProxyPass        /wsprrypi/api/rp1-gpclk-route http://127.0.0.1:\${WSPRRYPI_WEB_PORT}/api/rp1-gpclk-route"
installer_rp1_route_reverse_mapping="ProxyPassReverse /wsprrypi/api/rp1-gpclk-route http://127.0.0.1:\${WSPRRYPI_WEB_PORT}/api/rp1-gpclk-route"

grep -Fqx '    ProxyPreserveHost On' "$CONFIG"
grep -Fqx '    Include /usr/local/etc/wsprrypi-apache-network-policy.conf' "$CONFIG"
grep -Fqx '    Include /usr/local/etc/wsprrypi-apache-network-policy.conf' "$INSTALLER"
grep -Fq 'render-apache-privileged-network-policy.py' "$INSTALLER"
grep -Fq 'a2enmod headers proxy proxy_http proxy_wstunnel' "$INSTALLER"
grep -Fqx "    ${mapping}" "$CONFIG"
grep -Fqx "    ${reverse_mapping}" "$CONFIG"
grep -Fqx "    ${intake_mapping}" "$CONFIG"
grep -Fqx "    ${intake_reverse_mapping}" "$CONFIG"
grep -Fqx "    ${network_safety_mapping}" "$CONFIG"
grep -Fqx "    ${network_safety_reverse_mapping}" "$CONFIG"
grep -Fqx "    ${rp1_route_mapping}" "$CONFIG"
grep -Fqx "    ${rp1_route_reverse_mapping}" "$CONFIG"
grep -Fqx "    ${installer_mapping}" "$INSTALLER"
grep -Fqx "    ${installer_reverse_mapping}" "$INSTALLER"
grep -Fqx "    ${installer_intake_mapping}" "$INSTALLER"
grep -Fqx "    ${installer_intake_reverse_mapping}" "$INSTALLER"
grep -Fqx "    ${installer_network_safety_mapping}" "$INSTALLER"
grep -Fqx "    ${installer_network_safety_reverse_mapping}" "$INSTALLER"
grep -Fqx "    ${installer_rp1_route_mapping}" "$INSTALLER"
grep -Fqx "    ${installer_rp1_route_reverse_mapping}" "$INSTALLER"
grep -Fq 'Support bundle API family and signed private-intake availability.' "$CONFIG"
grep -Fq 'Support bundle API family and signed private-intake availability.' "$INSTALLER"

! grep -Eq 'ProxyPass[[:space:]]+/wsprrypi/api([[:space:]]|/)' "$CONFIG"
! grep -Eq 'ProxyPass[[:space:]]+/wsprrypi/api([[:space:]]|/)' "$INSTALLER"
! grep -Eq 'RequestHeader[[:space:]].*(Host|Origin)' "$CONFIG"
! grep -Eq 'RequestHeader[[:space:]].*(Host|Origin)' "$INSTALLER"
! grep -Eq 'Access-Control-Allow-Origin[[:space:]]+\*|Header[[:space:]]+set[[:space:]]+Access-Control-Allow-Origin' "$CONFIG"
! grep -Eq 'Access-Control-Allow-Origin[[:space:]]+\*|Header[[:space:]]+set[[:space:]]+Access-Control-Allow-Origin' "$INSTALLER"

grep -Fq 'install_wsprrypi_proxy_block()' "$INSTALLER"
grep -Fq 'debug_start "$@"' "$INSTALLER"
grep -Fq 'if [[ "$DRY_RUN" == "true" ]]' "$INSTALLER"

echo "apache proxy configuration tests: PASS"
