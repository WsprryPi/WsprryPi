/**
 * @file wspr_transmit_backend.hpp
 * @brief Generic backend interface for WSPR transmission hardware.
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

#ifndef WSPR_TRANSMIT_BACKEND_HPP
#define WSPR_TRANSMIT_BACKEND_HPP

#include <cstdint>

#include "wspr_transmit_types.hpp"

/**
 * @class WsprTransmitBackend
 * @brief Abstract backend interface for platform- or device-specific RF output.
 *
 * @details
 * A backend implements the hardware-specific portion of transmission while
 * the controller owns scheduling, configuration policy, and the high-level
 * transmission state machine. The interface is intentionally expressed in
 * generic transmission terms rather than Raspberry Pi DMA details so that
 * alternative backends, such as clock-generator-based implementations, can
 * fit the same controller lifecycle.
 *
 * Expected lifecycle for a configured transmission:
 * 1. `prepareTransmission()`
 * 2. `configureTransmission(plan)`
 * 3. `beginTransmissionOutput(plan)`
 * 4. Repeated `emitSymbol(plan, sym_num, tsym, symbol_index)`
 * 5. `endTransmissionOutput()`
 * 6. `cleanupTransmission()`
 *
 * Fault monitoring and recovery controls are part of the backend contract so
 * a backend can manage hardware-specific health checks privately.
 */
class WsprTransmitBackend
{
public:
    /**
     * @brief Virtual destructor for polymorphic backend ownership.
     */
    virtual ~WsprTransmitBackend() = default;

    /**
     * @brief Start backend-specific fault monitoring for an active emission.
     */
    virtual void startFaultMonitoring() = 0;

    /**
     * @brief Stop backend-specific fault monitoring.
     */
    virtual void stopFaultMonitoring() = 0;

    /**
     * @brief Allocate or initialize hardware resources needed for transmission.
     *
     * @details
     * This step prepares backend-private runtime state before configuration is
     * applied. It should not begin RF output.
     */
    virtual void prepareTransmission() = 0;

    /**
     * @brief Apply hardware configuration for the supplied transmission plan.
     *
     * @param plan Backend-neutral transmission snapshot.
     * @return Backend-neutral summary of the applied configuration.
     */
    virtual WsprTransmissionConfigureResult configureTransmission(
        const WsprTransmissionPlan &plan) = 0;

    /**
     * @brief Release backend resources associated with transmission setup.
     */
    virtual void cleanupTransmission() = 0;

    /**
     * @brief Convert a logical power level into an estimated output in
     *        milliwatts (mW).
     *
     * @param level Backend-defined power level index.
     * @return Estimated output power in milliwatts (mW).
     */
    virtual int getOutputPowerMilliwatts(int level) = 0;

    /**
     * @brief Enable RF output for the configured transmission.
     *
     * @param plan Backend-neutral transmission snapshot.
     */
    virtual void beginTransmissionOutput(const WsprTransmissionPlan &plan) = 0;

    /**
     * @brief Disable RF output and stop any active hardware emission.
     */
    virtual void endTransmissionOutput() = 0;

    /**
     * @brief Emit one controller-scheduled symbol or continuous-tone update.
     *
     * @param plan Backend-neutral transmission snapshot.
     * @param sym_num Symbol value to emit.
     * @param tsym Symbol duration in seconds. A value of `0.0` represents
     *             continuous-tone mode.
     * @param symbol_index Zero-based symbol index for logging/progress, or
     *                     `-1` when not applicable.
     */
    virtual void emitSymbol(
        const WsprTransmissionPlan &plan,
        const std::uint32_t &sym_num,
        const double &tsym,
        int symbol_index) = 0;

    /**
     * @brief Best-effort emergency reset of active transmission output.
     *
     * @details
     * This method is intended for fault handling paths and must not throw.
     */
    virtual void resetTransmissionOutput() noexcept = 0;

    /**
     * @brief Report whether the backend currently holds a latched fault.
     */
    virtual bool faulted() const noexcept = 0;

    /**
     * @brief Clear a latched backend fault condition.
     */
    virtual void clearFault() noexcept = 0;

    /**
     * @brief Enable or disable backend-managed automatic fault recovery.
     *
     * @param enable True to enable automatic recovery.
     */
    virtual void setAutoRecover(bool enable) noexcept = 0;

    /**
     * @brief Return whether backend-managed automatic recovery is enabled.
     */
    virtual bool autoRecoverEnabled() const noexcept = 0;

    /**
     * @brief Attempt synchronous recovery from a backend fault.
     *
     * @return True on successful recovery, false otherwise.
     */
    virtual bool recoverFromFault() = 0;

    /**
     * @brief Return whether backend-managed recovery is currently active.
     */
    virtual bool recoveryInProgress() const noexcept = 0;
};

#endif
