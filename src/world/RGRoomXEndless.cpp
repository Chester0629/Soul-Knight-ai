#include "world/RGRoomXEndless.hpp"

namespace Game {

namespace {

// Per-axis interval-overlap predicate, reconstructed from the inlined
// UnityEngine.Rect.Overlaps branch structure in the decompilation.
//
// For one axis the query interval is [qMin, qMax] and the placed interval is
// [rMin, rMin+rLen]. The decomp (game_full.c:429346-429372 for x,
// 429381-429405 for y) walks the standard containment/overlap cascade:
//   - if qMin < rMin: overlap unless the placed interval ends strictly before
//     the query begins (rMin+rLen < qMax keeps them overlapping);
//   - else (qMin >= rMin): the query start sits inside or past the placed start,
//     so they overlap while qMin <= rMin+rLen (placed reaches the query start)
//     and the placed start does not exceed the query end (rMin <= qMax).
// The combined effect is the classic open/closed interval-overlap test
//   qMin <= rMin+rLen && rMin <= qMax
// with the decomp's boundary handling. No RNG, no state writes.
bool AxisOverlap(float qMin, float qMax, float rMin, float rLen) {
    const float rMax = rMin + rLen;
    return (qMin <= rMax) && (rMin <= qMax);
}

} // namespace

// FAITHFUL: RGRoomXEndless__IsOccupied @ game_full.c:429286
bool RGRoomXEndless::IsOccupied(int col, int row, int w, int h,
                                const std::vector<PlacedRect> &placed) {
    // Build the padded query Rect (col-1, row-1, w+2, h+2): a 1-cell margin on
    // every side (decomp lines 429313-429327). xMin/yMin are the padded min
    // corner; xMax/yMax reconstruct (x+width) / (y+height) of that query.
    const float queryXMin = static_cast<float>(col - 1);
    const float queryYMin = static_cast<float>(row - 1);
    const float queryXMax = static_cast<float>((w + 2) + (col - 1)); // col + w + 1
    const float queryYMax = static_cast<float>((h + 2) + (row - 1)); // row + h + 1

    // Scan the runtime placed-obstacle List<Rect> (decomp this+0x7c). Return on
    // the FIRST rect that overlaps the padded query on BOTH axes.
    for (const PlacedRect &rect : placed) {
        const bool overlapX =
            AxisOverlap(queryXMin, queryXMax, rect.x, rect.width);
        const bool overlapY =
            AxisOverlap(queryYMin, queryYMax, rect.y, rect.height);

        // Decomp: `if (!bVar3) bVar2 = bVar3;` then `if (bVar2) return 1;`
        // i.e. final overlap = overlapX && overlapY.
        if (overlapX && overlapY) {
            return true; // decomp: return 1
        }
    }
    return false; // decomp: return 0 (list exhausted, no overlap)
}

} // namespace Game
