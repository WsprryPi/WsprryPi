/**
 * @file wspr_reference_adapter.hpp
 * @brief Adapter types and helpers for wspr-reference integration.
 *
 * Copyright © 2025 - 2026 Lee C. Bussy (@LBussy). All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef WSPR_REFERENCE_ADAPTER_HPP
#define WSPR_REFERENCE_ADAPTER_HPP

#include "prepared_wspr_transmission.hpp"
#include "wspr_ref_api.hpp"

PreparedWsprTransmission build_prepared_wspr_transmission(
    const std::string& callsign,
    const std::string& locator,
    int power_dbm);

PreparedWsprTransmission build_prepared_wspr_transmission(
    const std::string& callsign,
    const std::string& locator,
    int power_dbm,
    wspr::TransmissionPlanPreference preference);

PreparedWsprTransmission build_prepared_wspr_transmission(
    const wspr::WsprEncodeResult& result);

#endif // WSPR_REFERENCE_ADAPTER_HPP
