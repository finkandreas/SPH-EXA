/*
 * MIT License
 *
 * Copyright (c) 2021 CSCS, ETH Zurich
 *               2021 University of Basel
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*! @file
 * @brief Utility functions for determining the layout of particle buffers on a given rank
 *
 * @author Sebastian Keller <sebastian.f.keller@gmail.com>
 *
 * Each rank will be assigned a part of the SFC, equating to one or multiple ranges of
 * node indices of the global cornerstone octree. In a addition to the assigned nodes,
 * each rank must also store particle data for those nodes in the global octree which are
 * halos of the assigned nodes. Both types of nodes present on the rank are stored in the same
 * particle array (x,y,z,h,...) according to increasing node index, which is the same
 * as increasing Morton code.
 *
 * Given
 *  - the global cornerstone tree
 *  - its assignment to ranks
 *  - lists of in/outgoing halo nodes (global indices) per rank,
 * the utility functions in this file determine the position and size of each node (halo or assigned node)
 * in the particle buffers. The resulting layout is valid for all particle buffers, such as x,y,z,h,d,p,...
 *
 * Note:
 * If a node of the global cornerstone octree has index i, this means its Morton code range is tree[i] - tree[i+1]
 */

#pragma once

#include <vector>

#include "cstone/domain/domaindecomp.hpp"
#include "cstone/util/traits.hpp"

namespace cstone
{

/*! @brief Layout description for particle buffers
 *
 * Common usage: storing the sub-range of locally owned/assigned particles within particle buffers
 *
 * 0       start              end      size
 * |-------|------------------|--------|
 *   halos   locally assigned   halos
 */
struct BufferDescription
{
    //! @brief subrange start
    LocalIndex start;
    //! @brief subrange end
    LocalIndex end;
    //! @brief total size of the buffer
    LocalIndex size;
};

/*! @brief calculates the complementary range of the input ranges
 *
 * Input:  │      ------    -----   --     ----     --  │
 * Output: -------      ----     ---  -----    -----  ---
 *         ^                                            ^
 *         │                                            │
 * @param first                                         │
 * @param ranges   size >= 1, must be sorted            │
 * @param last    ──────────────────────────────────────/
 * @return the output ranges that cover everything within [first:last]
 *         that the input ranges did not cover
 */
inline std::vector<IndexPair<TreeNodeIndex>>
invertRanges(TreeNodeIndex first, gsl::span<const IndexPair<TreeNodeIndex>> ranges, TreeNodeIndex last)
{
    std::vector<IndexPair<TreeNodeIndex>> invertedRanges;

    TreeNodeIndex currentIndex = first;
    for (auto range : ranges)
    {
        if (range.start() == range.end()) { continue; }

        assert(currentIndex <= range.start() && "non-empty ranges must be sorted\n");
        if (currentIndex < range.start()) { invertedRanges.emplace_back(currentIndex, range.start()); }
        currentIndex = range.end();
    }
    if (currentIndex < last) { invertedRanges.emplace_back(currentIndex, last); }

    return invertedRanges;
}

/*! @brief extract ranges of marked indices from a source array
 *
 * @tparam IntegralType  an integer type
 * @param source         array with quantities to extract, length N+1
 * @param flags          0 or 1 flags for index, length N
 * @param firstReqIdx    first index, permissible range: [0:N]
 * @param secondReqIdx   second index, permissible range: [0:N+1]
 * @return               vector (of pairs) of elements of @p source that span all
 *                       elements [firstReqIdx:secondReqIdx] of @p source that are
 *                       marked by @p flags
 *
 * Even indices mark the start of a range, uneven indices mark the end of the previous
 * range start. If two ranges are consecutive, they are fused into a single range.
 *
 * This is used to extract
 *  - SFC keys of cornerstone octree leaf nodes flagged as halos
 *  - Particle offsets from buffer layouts
 */
template<class IntegralType>
std::vector<IntegralType> extractMarkedElements(gsl::span<const IntegralType> source,
                                                gsl::span<const int> flags,
                                                TreeNodeIndex firstReqIdx,
                                                TreeNodeIndex secondReqIdx)
{
    std::vector<IntegralType> requestKeys;

    while (firstReqIdx != secondReqIdx)
    {
        // advance to first halo (or to secondReqIdx)
        while (firstReqIdx < secondReqIdx && flags[firstReqIdx] == 0)
        {
            firstReqIdx++;
        }

        // add one request key range
        if (firstReqIdx != secondReqIdx)
        {
            requestKeys.push_back(source[firstReqIdx]);
            // advance until not a halo or end of range
            while (firstReqIdx < secondReqIdx && flags[firstReqIdx] == 1)
            {
                firstReqIdx++;
            }
            requestKeys.push_back(source[firstReqIdx]);
        }
    }

    return requestKeys;
}

/*! @brief calculate the location (offset) of each focus tree leaf node in the particle arrays
 *
 * @param[in]  focusLeafCounts   node counts of the focus leaves, size N
 * @param[in]  haloFlags         flag for each node, with a non-zero value if present as halo node, size N
 * @param[in]  firstAssignedIdx  first focus leaf idx to treat as part of the assigned nodes on the executing rank
 * @param[in]  lastAssignedIdx   last focus leaf idx to treat as part of the assigned nodes on the executing rank
 * @param[out] layout            length N+1. The first element is zero, the last element is
 *                               equal to the sum of all all present (assigned+halo) node counts.
 */
inline void computeNodeLayout(gsl::span<const unsigned> focusLeafCounts,
                              gsl::span<const int> haloFlags,
                              TreeNodeIndex firstAssignedIdx,
                              TreeNodeIndex lastAssignedIdx,
                              gsl::span<LocalIndex> layout)
{
#pragma omp parallel for
    for (TreeNodeIndex i = 0; i < TreeNodeIndex(focusLeafCounts.size()); ++i)
    {
        bool haveParticles = (firstAssignedIdx <= i && i < lastAssignedIdx) || haloFlags[i];
        layout[i]          = -int(haveParticles) & focusLeafCounts[i];
    }

    exclusiveScan(layout.data(), layout.size());
}

/*! @brief computes a list which local array ranges are going to be filled with halo particles
 *
 * @param layout       prefix sum of leaf counts of locally present nodes (see computeNodeLayout)
 *                     length N+1
 * @param haloFlags    0 or 1 for each leaf, length N
 * @param assignment   assignment of leaf nodes to peer ranks
 * @param peerRanks    list of peer ranks
 * @return             list of array index ranges for the receiving part in exchangeHalos
 */
inline SendList computeHaloReceiveList(gsl::span<const LocalIndex> layout,
                                       gsl::span<const int> haloFlags,
                                       gsl::span<const TreeIndexPair> assignment,
                                       gsl::span<const int> peerRanks)
{
    SendList ret(assignment.size());

    for (int peer : peerRanks)
    {
        TreeNodeIndex peerStartIdx = assignment[peer].start();
        TreeNodeIndex peerEndIdx   = assignment[peer].end();

        std::vector<LocalIndex> receiveRanges =
            extractMarkedElements<LocalIndex>(layout, haloFlags, peerStartIdx, peerEndIdx);

        for (std::size_t i = 0; i < receiveRanges.size(); i += 2)
        {
            ret[peer].addRange(receiveRanges[i], receiveRanges[i + 1]);
        }
    }

    return ret;
}

template<class ReorderFunctor, class... Arrays1, class... Arrays2>
void reorderArrays(const ReorderFunctor& reorderFunctor,
                   size_t inputOffset,
                   size_t outputOffset,
                   std::tuple<Arrays1&...> arrays,
                   std::tuple<Arrays2&...> scratchBuffers)
{
    auto reorderArray = [inputOffset, outputOffset, &reorderFunctor, &scratchBuffers](auto& array)
    {
        auto& swapSpace = util::pickType<decltype(array)>(scratchBuffers);
        assert(swapSpace.size() == array.size());
        reorderFunctor(rawPtr(array) + inputOffset, rawPtr(swapSpace) + outputOffset);
        swap(swapSpace, array);
    };

    for_each_tuple(reorderArray, arrays);
}

} // namespace cstone
