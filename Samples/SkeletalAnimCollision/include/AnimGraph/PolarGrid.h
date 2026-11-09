#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace RAnimation
{
    // Sample lookup of BlendSpace2D, free of nodes so it can be unit-tested. Samples sit on a grid of
    // "rows" (distinct y, e.g. speed) and, inside a row, "columns" (x, e.g. direction in degrees). The
    // input first picks the two bracketing rows (clamped outside), then the two bracketing columns in each
    // row; with wrapX the columns form a ring of `period` (359 blends with 1). A row with one sample is
    // taken as is. At most four (sample, weight) pairs come out and their weights sum to 1.
    struct GridSample
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    struct GridWeight
    {
        int index = -1;
        float weight = 0.0f;
    };

    inline int ResolvePolarGrid(const std::vector<GridSample>& samples,
                                float x,
                                float y,
                                bool wrapX,
                                float period,
                                GridWeight out[4])
    {
        constexpr float kRowEps = 1e-4f;
        int count = 0;
        if (samples.empty())
        {
            return 0;
        }

        // Rows: distinct y values, ascending.
        std::vector<float> rows;
        for (const GridSample& s : samples)
        {
            bool known = false;
            for (float r : rows)
            {
                known = known || std::fabs(r - s.y) <= kRowEps;
            }
            if (!known)
            {
                rows.push_back(s.y);
            }
        }
        std::sort(rows.begin(), rows.end());

        int rowLow = 0, rowHigh = 0;
        float fy = 0.0f;
        if (y <= rows.front())
        {
            rowLow = rowHigh = 0;
        }
        else if (y >= rows.back())
        {
            rowLow = rowHigh = static_cast<int>(rows.size()) - 1;
        }
        else
        {
            for (size_t r = 0; r + 1 < rows.size(); ++r)
            {
                if (y >= rows[r] && y <= rows[r + 1])
                {
                    rowLow = static_cast<int>(r);
                    rowHigh = static_cast<int>(r) + 1;
                    fy = rows[r + 1] > rows[r] ? (y - rows[r]) / (rows[r + 1] - rows[r]) : 0.0f;
                    break;
                }
            }
        }

        auto emit = [&](int index, float weight)
        {
            if (weight <= 0.0f || index < 0)
            {
                return;
            }
            for (int i = 0; i < count; ++i)
            {
                if (out[i].index == index)
                {
                    out[i].weight += weight;
                    return;
                }
            }
            if (count < 4)
            {
                out[count++] = {index, weight};
            }
        };

        auto resolveRow = [&](float rowY, float rowWeight)
        {
            if (rowWeight <= 0.0f)
            {
                return;
            }
            // Columns of this row, sorted by x.
            std::vector<int> cols;
            for (size_t i = 0; i < samples.size(); ++i)
            {
                if (std::fabs(samples[i].y - rowY) <= kRowEps)
                {
                    cols.push_back(static_cast<int>(i));
                }
            }
            std::sort(cols.begin(), cols.end(), [&](int a, int b) { return samples[static_cast<size_t>(a)].x < samples[static_cast<size_t>(b)].x; });
            if (cols.size() == 1)
            {
                emit(cols.front(), rowWeight);
                return;
            }
            const float first = samples[static_cast<size_t>(cols.front())].x;
            const float last = samples[static_cast<size_t>(cols.back())].x;
            float xx = x;
            if (wrapX && period > 0.0f)
            {
                xx = std::fmod(xx - first, period);
                if (xx < 0.0f)
                {
                    xx += period;
                }
                xx += first; // now in [first, first + period)
            }
            if (xx <= first)
            {
                emit(cols.front(), rowWeight);
                return;
            }
            if (xx >= last)
            {
                if (!wrapX || period <= 0.0f)
                {
                    emit(cols.back(), rowWeight);
                    return;
                }
                // between the last column and the first one again, across the seam
                const float span = first + period - last;
                const float fx = span > 0.0f ? (xx - last) / span : 0.0f;
                emit(cols.back(), rowWeight * (1.0f - fx));
                emit(cols.front(), rowWeight * fx);
                return;
            }
            for (size_t c = 0; c + 1 < cols.size(); ++c)
            {
                const float x0 = samples[static_cast<size_t>(cols[c])].x;
                const float x1 = samples[static_cast<size_t>(cols[c + 1])].x;
                if (xx >= x0 && xx <= x1)
                {
                    const float fx = x1 > x0 ? (xx - x0) / (x1 - x0) : 0.0f;
                    emit(cols[c], rowWeight * (1.0f - fx));
                    emit(cols[c + 1], rowWeight * fx);
                    return;
                }
            }
        };

        resolveRow(rows[static_cast<size_t>(rowLow)], rowLow == rowHigh ? 1.0f : 1.0f - fy);
        if (rowHigh != rowLow)
        {
            resolveRow(rows[static_cast<size_t>(rowHigh)], fy);
        }
        return count;
    }
} // namespace RAnimation
