//===- FuzzerDataFlowTrace.h - Internal header for the Fuzzer ---*- C++ -* ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// fuzzer::DataFlowTrace; reads and handles a data-flow trace.
//
// A data flow trace is generated by e.g. dataflow/DataFlow.cpp
// and is stored on disk in a separate directory.
//
// The trace dir contains a file 'functions.txt' which lists function names,
// oner per line, e.g.
// ==> functions.txt <==
// Func2
// LLVMFuzzerTestOneInput
// Func1
//
// All other files in the dir are the traces, see dataflow/DataFlow.cpp.
// The name of the file is sha1 of the input used to generate the trace.
//
// Current status:
//   the data is parsed and the summary is printed, but the data is not yet
//   used in any other way.
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZER_DATA_FLOW_TRACE
#define LLVM_FUZZER_DATA_FLOW_TRACE

#include "FuzzerDefs.h"
#include "FuzzerIO.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

namespace fuzzer {

int CollectDataFlow(const std::string &DFTBinary, const std::string &DirPath,
                    const Vector<SizedFile> &CorporaFiles);

class BlockCoverage {
 public:
  bool AppendCoverage(std::istream &IN);
  bool AppendCoverage(const std::string &S);

  size_t NumCoveredFunctions() const {
    return Functions.size();
  }

  uint32_t GetCounter(size_t FunctionId, size_t BasicBlockId) {
    auto It = Functions.find(FunctionId);
    if (It == Functions.end()) return 0;
    const auto &Counters = It->second;
    if (BasicBlockId < Counters.size()) return Counters[BasicBlockId];
    return 0;
  }

  uint32_t GetNumberOfBlocks(size_t FunctionId) {
    auto It = Functions.find(FunctionId);
    if (It == Functions.end()) return 0;
    const auto &Counters = It->second;
    return Counters.size();
  }

  uint32_t GetNumberOfCoveredBlocks(size_t FunctionId) {
    auto It = Functions.find(FunctionId);
    if (It == Functions.end()) return 0;
    const auto &Counters = It->second;
    uint32_t    Result = 0;
    for (auto Cnt : Counters)
      if (Cnt) Result++;
    return Result;
  }

  Vector<double> FunctionWeights(size_t NumFunctions) const;
  void           clear() {
    Functions.clear();
  }

 private:
  typedef Vector<uint32_t> CoverageVector;

  uint32_t NumberOfCoveredBlocks(const CoverageVector &Counters) const {
    uint32_t Res = 0;
    for (auto Cnt : Counters)
      if (Cnt) Res++;
    return Res;
  }

  uint32_t NumberOfUncoveredBlocks(const CoverageVector &Counters) const {
    return Counters.size() - NumberOfCoveredBlocks(Counters);
  }

  uint32_t SmallestNonZeroCounter(const CoverageVector &Counters) const {
    assert(!Counters.empty());
    uint32_t Res = Counters[0];
    for (auto Cnt : Counters)
      if (Cnt) Res = Min(Res, Cnt);
    assert(Res);
    return Res;
  }

  // Function ID => vector of counters.
  // Each counter represents how many input files trigger the given basic block.
  std::unordered_map<size_t, CoverageVector> Functions;
  // Functions that have DFT entry.
  std::unordered_set<size_t> FunctionsWithDFT;
};

class DataFlowTrace {
 public:
  void ReadCoverage(const std::string &DirPath);
  bool Init(const std::string &DirPath, std::string *FocusFunction,
            Vector<SizedFile> &CorporaFiles, Random &Rand);
  void Clear() {
    Traces.clear();
  }
  const Vector<uint8_t> *Get(const std::string &InputSha1) const {
    auto It = Traces.find(InputSha1);
    if (It != Traces.end()) return &It->second;
    return nullptr;
  }

 private:
  // Input's sha1 => DFT for the FocusFunction.
  std::unordered_map<std::string, Vector<uint8_t> > Traces;
  BlockCoverage                                     Coverage;
  std::unordered_set<std::string>                   CorporaHashes;
};
}  // namespace fuzzer

#endif  // LLVM_FUZZER_DATA_FLOW_TRACE
