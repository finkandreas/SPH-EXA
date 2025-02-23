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
 * @brief Density i-loop GPU driver
 *
 * @author Sebastian Keller <sebastian.f.keller@gmail.com>
 */

#include "cstone/cuda/cuda_utils.cuh"
#include "cstone/findneighbors.hpp"
#include "cstone/traversal/find_neighbors.cuh"

#include "sph/sph_gpu.hpp"
#include "sph/particles_data.hpp"
#include "sph/hydro_ve/av_switches_kern.hpp"

namespace sph::cuda
{

using cstone::GpuConfig;
using cstone::LocalIndex;
using cstone::TravConfig;
using cstone::TreeNodeIndex;

template<class Tc, class T, class KeyType>
__global__ void AVswitchesGpu(T sincIndex, T K, unsigned ngmax, const cstone::Box<T> box, size_t first, size_t last,
                              const cstone::OctreeNsView<Tc, KeyType> tree, const Tc* x, const Tc* y, const Tc* z,
                              const T* vx, const T* vy, const T* vz, const T* h, const T* c, const T* c11, const T* c12,
                              const T* c13, const T* c22, const T* c23, const T* c33, const T* wh, const T* whd,
                              const T* kx, const T* xm, const T* divv, T minDt, T alphamin, T alphamax,
                              T decay_constant, T* alpha, LocalIndex* nidx, TreeNodeIndex* globalPool)
{
    unsigned laneIdx     = threadIdx.x & (GpuConfig::warpSize - 1);
    unsigned numTargets  = (last - first - 1) / TravConfig::targetSize + 1;
    unsigned targetIdx   = 0;
    unsigned warpIdxGrid = (blockDim.x * blockIdx.x + threadIdx.x) >> GpuConfig::warpSizeLog2;

    cstone::LocalIndex* neighborsWarp = nidx + ngmax * TravConfig::targetSize * warpIdxGrid;

    while (true)
    {
        // first thread in warp grabs next target
        if (laneIdx == 0) { targetIdx = atomicAdd(&cstone::targetCounterGlob, 1); }
        targetIdx = cstone::shflSync(targetIdx, 0);

        if (targetIdx >= numTargets) return;

        cstone::LocalIndex bodyBegin = first + targetIdx * TravConfig::targetSize;
        cstone::LocalIndex bodyEnd   = cstone::imin(bodyBegin + TravConfig::targetSize, last);
        cstone::LocalIndex i         = bodyBegin + laneIdx;

        auto ncTrue = traverseNeighbors(bodyBegin, bodyEnd, x, y, z, h, tree, box, neighborsWarp, ngmax, globalPool);

        if (i >= last) continue;

        unsigned ncCapped = stl::min(ncTrue[0], ngmax);
        alpha[i]          = AVswitchesJLoop<TravConfig::targetSize>(
            i, sincIndex, K, box, neighborsWarp + laneIdx, ncCapped, x, y, z, vx, vy, vz, h, c, c11, c12, c13, c22, c23,
            c33, wh, whd, kx, xm, divv, minDt, alphamin, alphamax, decay_constant, alpha[i]);
    }
}

template<class Dataset>
void computeAVswitches(size_t startIndex, size_t endIndex, Dataset& d,
                       const cstone::Box<typename Dataset::RealType>& box)
{
    unsigned numBodies = endIndex - startIndex;
    unsigned numBlocks = TravConfig::numBlocks(numBodies);

    auto [traversalPool, nidxPool] = cstone::allocateNcStacks(d.devData.traversalStack, numBodies, d.ngmax);
    cstone::resetTraversalCounters<<<1, 1>>>();

    AVswitchesGpu<<<numBlocks, TravConfig::numThreads>>>(
        d.sincIndex, d.K, d.ngmax, box, startIndex, endIndex, d.treeView.nsView(), rawPtr(d.devData.x),
        rawPtr(d.devData.y), rawPtr(d.devData.z), rawPtr(d.devData.vx), rawPtr(d.devData.vy), rawPtr(d.devData.vz),
        rawPtr(d.devData.h), rawPtr(d.devData.c), rawPtr(d.devData.c11), rawPtr(d.devData.c12), rawPtr(d.devData.c13),
        rawPtr(d.devData.c22), rawPtr(d.devData.c23), rawPtr(d.devData.c33), rawPtr(d.devData.wh),
        rawPtr(d.devData.whd), rawPtr(d.devData.kx), rawPtr(d.devData.xm), rawPtr(d.devData.divv), d.minDt, d.alphamin,
        d.alphamax, d.decay_constant, rawPtr(d.devData.alpha), nidxPool, traversalPool);
    checkGpuErrors(cudaDeviceSynchronize());
}

template void computeAVswitches(size_t, size_t, sphexa::ParticlesData<double, unsigned, cstone::GpuTag>& d,
                                const cstone::Box<double>&);
template void computeAVswitches(size_t, size_t, sphexa::ParticlesData<double, uint64_t, cstone::GpuTag>& d,
                                const cstone::Box<double>&);
template void computeAVswitches(size_t, size_t, sphexa::ParticlesData<float, unsigned, cstone::GpuTag>& d,
                                const cstone::Box<float>&);
template void computeAVswitches(size_t, size_t, sphexa::ParticlesData<float, uint64_t, cstone::GpuTag>& d,
                                const cstone::Box<float>&);

} // namespace sph::cuda
