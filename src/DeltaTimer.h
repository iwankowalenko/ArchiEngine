#pragma once

#include <chrono>

namespace archi
{
    class DeltaTimer final
    {
    public:
        using clock = std::chrono::high_resolution_clock;

        void Reset()
        {
            m_last = clock::now();
            m_hasLast = true;
        }

        double TickSeconds()
        {
            const auto now = clock::now();
            if (!m_hasLast)
            {
                m_last = now;
                m_hasLast = true;
                return 0.0;
            }

            const std::chrono::duration<double> dt = now - m_last;
            m_last = now;
            return dt.count();
        }

    private:
        clock::time_point m_last{};
        bool m_hasLast = false;
    };
}

