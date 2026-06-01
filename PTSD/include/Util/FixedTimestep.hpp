#ifndef UTIL_FIXED_TIMESTEP_HPP
#define UTIL_FIXED_TIMESTEP_HPP

namespace Util {
/**
 * @class FixedTimestep
 * @brief Accumulator that converts variable frame times into fixed update steps.
 *
 * Deterministic gameplay/physics wants a constant @c dt regardless of frame
 * rate. Feed each frame's elapsed time to @ref Advance; it returns how many
 * fixed steps of size @ref StepMs should run this frame, keeping any remainder
 * for next time. @ref Alpha exposes the leftover fraction for render-time
 * interpolation.
 *
 * A per-frame step cap guards against the "spiral of death": if the simulation
 * falls too far behind (e.g. after a breakpoint or a long stall), the backlog is
 * dropped instead of trying to catch up forever.
 *
 * @note Pure value type: no global state, no time source, no randomness — fully
 * deterministic and unit-testable.
 */
class FixedTimestep {
public:
    /**
     * @brief Constructs a fixed-timestep accumulator.
     *
     * @param stepMs The fixed step size in milliseconds (defaults to 60 Hz).
     *               Non-positive values fall back to 60 Hz.
     * @param maxStepsPerFrame The maximum number of steps run per Advance() call
     *               before the backlog is dropped. Values below 1 clamp to 1.
     */
    explicit FixedTimestep(float stepMs = 1000.0F / 60.0F,
                           int maxStepsPerFrame = 5)
        : m_StepMs(stepMs > 0.0F ? stepMs : 1000.0F / 60.0F),
          m_MaxSteps(maxStepsPerFrame > 0 ? maxStepsPerFrame : 1) {}

    /**
     * @brief Adds a frame's elapsed time and reports how many steps to run.
     *
     * @param frameDtMs The elapsed time since the last frame, in milliseconds.
     *                  Non-positive values are ignored.
     * @return The number of fixed steps to run now (0..maxStepsPerFrame).
     */
    int Advance(float frameDtMs) {
        if (frameDtMs > 0.0F) {
            m_Accumulator += frameDtMs;
        }
        int steps = 0;
        while (m_Accumulator >= m_StepMs && steps < m_MaxSteps) {
            m_Accumulator -= m_StepMs;
            ++steps;
        }
        // Spiral-of-death guard: drop a backlog we will never catch up on.
        if (m_Accumulator > m_StepMs * static_cast<float>(m_MaxSteps)) {
            m_Accumulator = 0.0F;
        }
        return steps;
    }

    /**
     * @brief The fixed step size in milliseconds.
     */
    float StepMs() const { return m_StepMs; }

    /**
     * @brief The unconsumed time remaining in the accumulator, in milliseconds.
     */
    float Accumulator() const { return m_Accumulator; }

    /**
     * @brief Interpolation factor in [0, 1) between the last and next step.
     *
     * Useful for smoothing rendering between fixed simulation states.
     */
    float Alpha() const { return m_Accumulator / m_StepMs; }

    /**
     * @brief Clears the accumulated time.
     */
    void Reset() { m_Accumulator = 0.0F; }

private:
    float m_StepMs;
    int m_MaxSteps;
    float m_Accumulator = 0.0F;
};
} // namespace Util

#endif /* UTIL_FIXED_TIMESTEP_HPP */
