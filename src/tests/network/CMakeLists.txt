cmake_minimum_required(VERSION 3.24)
project(WsprryPiNetworkInterop LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
# The executable embeds the ephemeral private key as well as its generated header.
file(CHMOD "${CMAKE_BINARY_DIR}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
# Share the immutable source pin with the CI checkout.
set(pico_pin_file "${CMAKE_CURRENT_SOURCE_DIR}/pico-reference-revision.txt")
set(PICO_COMPAT_PROFILE current CACHE STRING "Exact Pico API compatibility profile")
if(PICO_COMPAT_PROFILE STREQUAL "legacy")
  set(pico_pin_file "${CMAKE_CURRENT_SOURCE_DIR}/pico-legacy-revision.txt")
elseif(PICO_COMPAT_PROFILE STREQUAL "extended")
  set(pico_pin_file "${CMAKE_CURRENT_SOURCE_DIR}/pico-extended-revision.txt")
elseif(PICO_COMPAT_PROFILE STREQUAL "p152")
  set(pico_pin_file "${CMAKE_CURRENT_SOURCE_DIR}/pico-p152-revision.txt")
elseif(NOT PICO_COMPAT_PROFILE STREQUAL "current")
  message(FATAL_ERROR "Unknown Pico compatibility profile")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${pico_pin_file}")
file(READ "${pico_pin_file}" pico_pin)
string(STRIP "${pico_pin}" pico_pin)
string(LENGTH "${pico_pin}" pico_pin_length)
if(NOT pico_pin_length EQUAL 40 OR NOT pico_pin MATCHES "^[0-9a-f]+$")
  message(FATAL_ERROR "Pico reference revision must be a full lowercase commit SHA")
endif()
foreach(pair IN ITEMS "PICO_SOURCE|${pico_pin}" "MBEDTLS_SOURCE|0bebf8b8c7f07abe3571ded48a11aa907a1ffb20")
  string(REPLACE "|" ";" fields "${pair}")
  list(GET fields 0 var)
  list(GET fields 1 pin)
  execute_process(COMMAND git -C "${${var}}" rev-parse HEAD OUTPUT_VARIABLE revision OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  execute_process(COMMAND git -C "${${var}}" status --porcelain --untracked-files=normal OUTPUT_VARIABLE dirty OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  if(NOT revision STREQUAL pin OR NOT dirty STREQUAL "")
    message(FATAL_ERROR "${var} must be an unmodified checkout at ${pin}")
  endif()
endforeach()
set(MBEDTLS_DIR "${MBEDTLS_SOURCE}")
set(MBEDTLS_CONFIG_FILE "${PICO_SOURCE}/src/network/pico/mbedtls_config.h")
set(GEN_FILES OFF)
set(USE_STATIC_MBEDTLS_LIBRARY ON)
set(USE_SHARED_MBEDTLS_LIBRARY OFF)
set(DISABLE_PACKAGE_CONFIG_AND_INSTALL ON)
add_subdirectory("${MBEDTLS_SOURCE}/library" "${CMAKE_BINARY_DIR}/mbedtls")
include("${PICO_SOURCE}/cmake/mbedtls_alert_overlay.cmake")
wsprry_mbedtls_alert_overlay(mbedtls SOURCES "${MBEDTLS_SOURCE}")
set(credentials "${CMAKE_BINARY_DIR}/credentials-v3")
if(NOT EXISTS "${credentials}/server.key")
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${PICO_SOURCE}/scripts/generate_network_test_credentials.py" "${credentials}" COMMAND_ERROR_IS_FATAL ANY)
endif()
execute_process(COMMAND "${Python3_EXECUTABLE}" "${PICO_SOURCE}/scripts/network_certificates.py" validate --directory "${credentials}"
  OUTPUT_VARIABLE deployment COMMAND_ERROR_IS_FATAL ANY)
string(JSON WSPRRY_PICO_NETWORK_HOSTNAME GET "${deployment}" hostname)
string(JSON WSPRRY_PICO_NETWORK_DEVICE_ID GET "${deployment}" device_id)
file(READ "${credentials}/server.crt" WSPRRY_PICO_CERTIFICATE)
file(READ "${credentials}/server.key" WSPRRY_PICO_PRIVATE_KEY)
file(READ "${credentials}/client-ca.crt" WSPRRY_PICO_CLIENT_CA)
foreach(value WSPRRY_PICO_CERTIFICATE WSPRRY_PICO_PRIVATE_KEY WSPRRY_PICO_CLIENT_CA)
  if("${${value}}" MATCHES "[)]WP(CERT|KEY|CA)\"")
    message(FATAL_ERROR "Invalid credential encoding")
  endif()
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${credentials}/server.crt" "${credentials}/server.key" "${credentials}/client-ca.crt"
  "${credentials}/deployment.json" "${PICO_SOURCE}/scripts/network_certificates.py")
set(WSPRRY_PICO_NETWORK_PORT 18443)
configure_file("${PICO_SOURCE}/cmake/network_credentials.hpp.in" "${CMAKE_BINARY_DIR}/generated/network_credentials.hpp" @ONLY)
file(CHMOD "${CMAKE_BINARY_DIR}/generated/network_credentials.hpp" PERMISSIONS OWNER_READ OWNER_WRITE)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${PICO_SOURCE}/scripts/generate_web_assets.py" "${PICO_SOURCE}" "${CMAKE_BINARY_DIR}/generated/web_assets.cpp" COMMAND_ERROR_IS_FATAL ANY)
set(core network/http.cpp network/api.cpp encoding/wspr.cpp standalone/dry_run_engine.cpp
  standalone/config.cpp standalone/storage.cpp standalone/scheduler.cpp time/sntp.cpp
  time/utc_discipline.cpp time/usb_time_source.cpp wtp/json.cpp wtp/codec.cpp wtp/endpoint.cpp
  wtp/frame_parser.cpp wtp/inhibited_rf_engine.cpp wtp/job_service.cpp wtp/sha256.cpp)
if(NOT PICO_COMPAT_PROFILE STREQUAL "legacy")
  list(APPEND core application/resources.cpp application/frequency_policy.cpp hardware/pins.cpp
    provisioning/profile.cpp provisioning/network_profile.cpp provisioning/consumer_profile.cpp
    provisioning/consumer_claim.cpp provisioning/consumer_claim_commit.cpp provisioning/access.cpp
    provisioning/ble_session.cpp provisioning/field_runtime.cpp provisioning/gatt_framing.cpp
    provisioning/local_access.cpp provisioning/reset.cpp provisioning/reset_storage.cpp
    provisioning/softap_http.cpp provisioning/activation.cpp provisioning/storage.cpp
    provisioning/manager.cpp provisioning/command.cpp provisioning/runtime.cpp
    network/mdns.cpp network/bootstrap_http.cpp network/bootstrap_codec.cpp network/bootstrap_join.cpp
    network/bootstrap_slot.cpp network/bootstrap_wire.cpp network/owner_wire.cpp
    network/owner_challenge.cpp network/owner_claim_http.cpp network/consumer_radio.cpp network/softap_api.cpp
    standalone/frequency_policy_engine.cpp time/controller_time.cpp)
  if(PICO_COMPAT_PROFILE STREQUAL "current")
    list(APPEND core application/config_update.cpp network/management.cpp)
  elseif(PICO_COMPAT_PROFILE STREQUAL "p152")
    list(APPEND core application/config_update.cpp)
  endif()
endif()
list(TRANSFORM core PREPEND "${PICO_SOURCE}/src/")
add_executable(pico_tls_server pico_tls_server.cpp ${core}
  "${CMAKE_BINARY_DIR}/generated/web_assets.cpp" tcp.cpp
  "${PICO_SOURCE}/src/network/pico/server.cpp")
target_include_directories(pico_tls_server PRIVATE "${PICO_SOURCE}/src" "${PICO_SOURCE}/tests"
  "${PICO_SOURCE}/tests/network_mock" "${CMAKE_BINARY_DIR}/generated")
target_compile_definitions(pico_tls_server PRIVATE
  MBEDTLS_CONFIG_FILE="${MBEDTLS_CONFIG_FILE}"
  WSPRRY_PICO_STANDALONE_WSPR_BASE_FREQUENCY_HZ=3570100)
target_link_libraries(pico_tls_server PRIVATE mbedtls mbedx509 mbedcrypto)
target_compile_options(pico_tls_server PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(NOT PICO_COMPAT_PROFILE STREQUAL "legacy")
  target_sources(pico_tls_server PRIVATE
    "${PICO_SOURCE}/src/network/pico/psa_lifetime.cpp"
    "${PICO_SOURCE}/src/provisioning/pico/credential_validator.cpp")
  target_compile_definitions(pico_tls_server PRIVATE WSPRRY_PICO_EXTENDED_API=1)
endif()

# P15.3 genuine consumer path; historical compatibility profiles remain intact.
if(PICO_COMPAT_PROFILE STREQUAL "current")
  target_compile_definitions(pico_tls_server PRIVATE WSPRRY_PICO_MANAGEMENT_API=1)
  add_library(pico_consumer_fixture STATIC ${core}
    "${CMAKE_BINARY_DIR}/generated/web_assets.cpp"
    "${PICO_SOURCE}/src/network/pico/consumer_radio_crypto.cpp"
    "${PICO_SOURCE}/src/network/pico/psa_lifetime.cpp")
  target_include_directories(pico_consumer_fixture PRIVATE "${PICO_SOURCE}/src" "${PICO_SOURCE}/tests" "${MBEDTLS_SOURCE}/include")
  target_compile_definitions(pico_consumer_fixture PRIVATE MBEDTLS_CONFIG_FILE="${MBEDTLS_CONFIG_FILE}"
    WSPRRY_PICO_STANDALONE_WSPR_BASE_FREQUENCY_HZ=3570100)
  target_link_libraries(pico_consumer_fixture PRIVATE mbedcrypto)
  target_compile_options(pico_consumer_fixture PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
  add_executable(consumer_interop consumer_interop.cpp
    ../../wtp_integration/consumer_radio.cpp ../../wtp_integration/configuration.cpp
    ../../wtp_integration/browser_api.cpp ../../wtp_integration/network_http.cpp
    ../../wtp_integration/plain_tcp.cpp ../../wtp_integration/tls.cpp
    ../../wtp_integration/application.cpp ../../wtp_integration/status.cpp
    ../../wtp_integration/scheduler.cpp ../../wtp_integration/backend.cpp
    ../../wtp_integration/execution_plan.cpp ../../WSPR-Transmitter/src/execution_plan_compiler.cpp
    ../../WSPR-Transmitter/src/transmission_controller.cpp)
  target_compile_definitions(consumer_interop PRIVATE WSPRRYPI_CONSUMER_CRYPTO_TEST=1 MBEDTLS_CONFIG_FILE="${MBEDTLS_CONFIG_FILE}")
  target_include_directories(consumer_interop PRIVATE ../.. ../../WTP-Client/include
    "${PICO_SOURCE}/src" "${PICO_SOURCE}/tests" "${CMAKE_BINARY_DIR}/generated" "${MBEDTLS_SOURCE}/include")
  target_link_libraries(consumer_interop PRIVATE pico_consumer_fixture mbedcrypto ssl crypto
    "${CMAKE_BINARY_DIR}/client/libwtp_client.a" pthread)
  target_compile_options(consumer_interop PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
endif()
