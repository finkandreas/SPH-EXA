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
 * @brief SPH density kernel tests
 *
 * @author Ruben Cabezon <ruben.cabezon@unibas.ch>
 * @author Sebastian Keller <sebastian.f.keller@gmail.com>
 */

#include <vector>

#include "gtest/gtest.h"

#include "sph/hydro_ve/divv_curlv_kern.hpp"
#include "sph/tables.hpp"
#include "../../../main/src/io/file_utils.hpp"

using namespace sph;

TEST(Divv_Curlv, JLoop)
{
    using T = double;

    T sincIndex = 6.0;
    T K         = compute_3d_k(sincIndex);
    T mpart     = 3.781038064465603e26;

    std::array<double, lt::size> wh  = lt::createWharmonicLookupTable<double, lt::size>();
    std::array<double, lt::size> whd = lt::createWharmonicDerivativeLookupTable<double, lt::size>();

    cstone::Box<T> box(-1.e9, 1.e9, cstone::BoundaryType::open);

    size_t   npart          = 99;
    unsigned neighborsCount = npart - 1, i;

    std::vector<cstone::LocalIndex> neighbors(neighborsCount - 1);

    for (i = 0; i < neighborsCount; i++)
    {
        neighbors[i] = i + 1;
    }

    std::vector<T> x(npart);
    std::vector<T> y(npart);
    std::vector<T> z(npart);
    std::vector<T> h(npart);
    std::vector<T> m(npart);
    std::vector<T> gradh(npart);
    std::vector<T> rho0(npart);
    std::vector<T> sumwhrho0(npart);
    std::vector<T> vx(npart);
    std::vector<T> vy(npart);
    std::vector<T> vz(npart);
    std::vector<T> c(npart);
    std::vector<T> p(npart);
    std::vector<T> u(npart);
    std::vector<T> divvp(npart);
    std::vector<T> alpha(npart);
    std::vector<T> c11(npart);
    std::vector<T> c12(npart);
    std::vector<T> c13(npart);
    std::vector<T> c22(npart);
    std::vector<T> c23(npart);
    std::vector<T> c33(npart);
    std::vector<T> dvxdxp(npart);
    std::vector<T> dvxdyp(npart);
    std::vector<T> dvxdzp(npart);
    std::vector<T> dvydxp(npart);
    std::vector<T> dvydyp(npart);
    std::vector<T> dvydzp(npart);
    std::vector<T> dvzdxp(npart);
    std::vector<T> dvzdyp(npart);
    std::vector<T> dvzdzp(npart);
    std::vector<T> sumwh(npart);
    std::vector<T> xm(npart);
    std::vector<T> kx(npart);

    std::vector<T*> fields{x.data(),      y.data(),      z.data(),      vx.data(),     vy.data(),     vz.data(),
                           h.data(),      c.data(),      c11.data(),    c12.data(),    c13.data(),    c22.data(),
                           c23.data(),    c33.data(),    p.data(),      gradh.data(),  rho0.data(),   sumwhrho0.data(),
                           sumwh.data(),  dvxdxp.data(), dvxdyp.data(), dvxdzp.data(), dvydxp.data(), dvydyp.data(),
                           dvydzp.data(), dvzdxp.data(), dvzdyp.data(), dvzdzp.data(), alpha.data(),  u.data(),
                           divvp.data()};

    sphexa::fileutils::readAscii("example_data.txt", npart, fields);

    std::fill(m.begin(), m.end(), mpart);

    for (i = 0; i < neighborsCount + 1; i++)
    {
        xm[i] = mpart / rho0[i];
        kx[i] = K * xm[i] / math::pow(h[i], 3);
    }

    // fill with invalid initial value to make sure that the kernel overwrites it instead of add to it
    T divv  = -1;
    T curlv = -1;
    T dV11  = -1;
    T dV12  = -1;
    T dV13  = -1;
    T dV22  = -1;
    T dV23  = -1;
    T dV33  = -1;

    // compute gradient for for particle 0
    divV_curlVJLoop(0, sincIndex, K, box, neighbors.data(), neighborsCount, x.data(), y.data(), z.data(), vx.data(),
                    vy.data(), vz.data(), h.data(), c11.data(), c12.data(), c13.data(), c22.data(), c23.data(),
                    c33.data(), wh.data(), whd.data(), kx.data(), xm.data(), &divv, &curlv, &dV11, &dV12, &dV13, &dV22,
                    &dV23, &dV33, true);

    T dvxdxRef = 1.3578325800572969e-3;
    T dvxdyRef = 3.0002215820712448e-2;
    T dvxdzRef = -9.0001569692540768e-3;
    T dvydxRef = -5.3495470097538571e-3;
    T dvydyRef = 2.2556439156962826e-2;
    T dvydzRef = 5.8741778655137782e-3;
    T dvzdxRef = 4.3397397877829496e-3;
    T dvzdyRef = 3.8963123805324977e-3;
    T dvzdzRef = 9.8460822552287348e-3;

    EXPECT_NEAR(divv, 3.3760353992248873e-2, 1e-10);
    EXPECT_NEAR(curlv, 3.783664800939196e-2, 1e-10);
    EXPECT_NEAR(dV11, dvxdxRef, 1e-10);
    EXPECT_NEAR(dV12, dvxdyRef + dvydxRef, 1e-10);
    EXPECT_NEAR(dV13, dvxdzRef + dvzdxRef, 1e-10);
    EXPECT_NEAR(dV22, dvydyRef, 1e-10);
    EXPECT_NEAR(dV23, dvydzRef + dvzdyRef, 1e-10);
    EXPECT_NEAR(dV33, dvzdzRef, 1e-10);
}
