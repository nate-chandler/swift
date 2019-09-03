//===------------ CombinerBase.cpp ----------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "combine-base"
#include "CombinerBase.h"
#include "llvm/Support/Debug.h"

using namespace swift;

//===----------------------------------------------------------------------===//
//                     CombinerWorklist Implementation
//===----------------------------------------------------------------------===//

void CombinerWorklist::add(SILInstruction *I) {
  if (!worklistMap.insert(std::make_pair(I, worklist.size())).second)
    return;

  LLVM_DEBUG(llvm::dbgs() << "SC: ADD: " << *I << '\n');
  worklist.push_back(I);
}

void CombinerWorklist::addInitialGroup(ArrayRef<SILInstruction *> List) {
  assert(worklist.empty() && "worklist must be empty to add initial group");
  worklist.reserve(List.size()+16);
  worklistMap.reserve(List.size());
  LLVM_DEBUG(llvm::dbgs() << "SC: ADDING: " << List.size()
                          << " instrs to worklist\n");
  while (!List.empty()) {
    SILInstruction *I = List.back();
    List = List.slice(0, List.size()-1);    
    worklistMap.insert(std::make_pair(I, worklist.size()));
    worklist.push_back(I);
    }
}

