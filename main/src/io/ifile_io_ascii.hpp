/*
 * MIT License
 *
 * Copyright (c) 2023 CSCS, ETH Zurich, University of Basel, University of Zurich
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
 * @brief ASCII file I/O
 *
 * @author Sebastian Keller <sebastian.f.keller@gmail.com>
 */

#pragma once

#include <mpi.h>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "file_utils.hpp"
#include "ifile_io.hpp"

namespace sphexa
{

class AsciiWriterNew : public IFileWriter
{
public:
    using Base      = IFileWriter;
    using FieldType = typename Base::FieldType;

    AsciiWriterNew(MPI_Comm comm)
        : comm_(comm)
    {
    }

    void constants(const std::map<std::string, double>& c, std::string path) const override {}

    std::string suffix() const override { return ""; }

    void addStep(size_t firstIndex, size_t lastIndex, std::string path) override
    {
        firstIndexStep_ = firstIndex;
        lastIndexStep_  = lastIndex;
        pathStep_       = path;
    }

    void stepAttribute(const std::string& key, FieldType val, int64_t /*size*/) override
    {
        if (key == "iteration") { iterationStep_ = *std::get<const int64_t*>(val); }
    }

    void writeField(const std::string& /*key*/, FieldType field, int col) override
    {
        columns_.push_back(col);
        std::visit(
            [this](auto arg)
            {
                std::vector<std::decay_t<decltype(*arg)>> vec(lastIndexStep_ - firstIndexStep_);
                std::copy_n(arg + firstIndexStep_, vec.size(), vec.data());
                stepBuffer_.push_back(std::move(vec));
            },
            field);
    }

    void closeStep() override
    {
        const char separator = ' ';
        pathStep_ += "." + std::to_string(iterationStep_) + ".txt";

        int rank, numRanks;
        MPI_Comm_rank(comm_, &rank);
        MPI_Comm_size(comm_, &numRanks);

        std::vector<FieldType> fieldPointers;
        for (const auto& v : stepBuffer_)
        {
            std::visit([&fieldPointers](const auto& arg) { fieldPointers.push_back(arg.data()); }, v);
        }

        cstone::sort_by_key(columns_.begin(), columns_.end(), fieldPointers.begin());

        for (int turn = 0; turn < numRanks; turn++)
        {
            if (turn == rank)
            {
                try
                {
                    bool append = rank != 0;
                    fileutils::writeAscii(firstIndexStep_, lastIndexStep_, pathStep_, append, fieldPointers, separator);
                }
                catch (std::runtime_error& ex)
                {
                    if (rank == 0) fprintf(stderr, "ERROR: %s Terminating\n", ex.what());
                    MPI_Abort(comm_, 1);
                }
            }

            MPI_Barrier(comm_);
        }
        columns_.clear();
        stepBuffer_.clear();
    }

private:
    MPI_Comm                       comm_;
    int64_t                        firstIndexStep_, lastIndexStep_;
    std::string                    pathStep_;
    std::vector<int>               columns_;
    std::vector<Base::FieldVector> stepBuffer_;
    int64_t                        iterationStep_;
};

} // namespace sphexa
