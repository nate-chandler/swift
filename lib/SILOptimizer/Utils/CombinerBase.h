//===--- SILCombiner.h ------------------------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "swift/SIL/SILInstruction.h"
#include "swift/SIL/SILVisitor.h"
#include "swift/SILOptimizer/Utils/Local.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"

STATISTIC(numCombined, "Number of instructions combined");

namespace swift {

/// Manages a list of instructions awaiting visitation by the combiner.
class CombinerWorklist final {
  llvm::SmallVector<SILInstruction *, 256> worklist;
  llvm::DenseMap<SILInstruction *, unsigned> worklistMap;

  void operator=(const CombinerWorklist &RHS) = delete;
  CombinerWorklist(const CombinerWorklist &worklist) = delete;

public:
  CombinerWorklist() {}

  /// Returns true if the worklist is empty.
  bool isEmpty() const { return worklist.empty(); }

  /// Add the specified instruction to the worklist if it isn't already in it.
  void add(SILInstruction *I);

  /// If the given ValueBase is a SILInstruction add it to the worklist.
  void addValue(ValueBase *V) {
    if (auto *I = V->getDefiningInstruction())
      add(I);
  }

  /// Add the given list of instructions in reverse order to the worklist. This
  /// routine assumes that the worklist is empty and the given list has no
  /// duplicates.
  void addInitialGroup(ArrayRef<SILInstruction *> List);

  // If I is in the worklist, remove it.
  void remove(SILInstruction *I) {
    auto It = worklistMap.find(I);
    if (It == worklistMap.end())
      return; // Not in worklist.

    // Don't bother moving everything down, just null out the slot. We will
    // check before we process any instruction if it is null.
    worklist[It->second] = nullptr;
    worklistMap.erase(It);
  }

  /// Remove the top element from the worklist.
  SILInstruction *removeOne() {
    SILInstruction *I = worklist.pop_back_val();
    worklistMap.erase(I);
    return I;
  }

  /// When an instruction has been simplified, add all of its users to the
  /// worklist, since additional simplifications of its users may have been
  /// exposed.
  void addUsersToWorklist(ValueBase *I) {
    for (auto UI : I->getUses())
      add(UI->getUser());
  }

  void addUsersToWorklist(SILValue value) {
    for (auto *use : value->getUses())
      add(use->getUser());
  }

  /// When an instruction has been simplified, add all of its users to the
  /// worklist, since additional simplifications of its users may have been
  /// exposed.
  void addUsersOfAllResultsToWorklist(SILInstruction *I) {
    for (auto result : I->getResults()) {
      addUsersToWorklist(result);
    }
  }

  /// Check that the worklist is empty and nuke the backing store for the map if
  /// it is large.
  void zap() {
    assert(worklistMap.empty() && "Worklist empty, but the map is not?");

    // Do an explicit clear, this shrinks the map if needed.
    worklistMap.clear();
  }
};

template <typename Derived, typename RetTy>
class CombinerBase : public SILInstructionVisitor<Derived, RetTy> {
  Derived &asDerived() { return static_cast<Derived &>(*this); }

protected:
  using Worklist = CombinerWorklist;

  /// Worklist containing all of the instructions primed for simplification.
  Worklist worklist;

  /// Variable to track if the SILCombiner made any changes.
  bool madeChange;

  /// The current iteration of the SILCombine.
  unsigned iteration;

public:
  CombinerBase() : worklist(), madeChange(false), iteration(0){};

  bool runOnFunction(SILFunction &F) {
    clear();

    bool Changed = false;
    // Perform iterations until we do not make any changes.
    while (asDerived().doOneIteration(F, iteration)) {
      Changed = true;
      iteration++;
    }

    // Cleanup the builder and return whether or not we made any changes.
    return Changed;
  }

  void clear() {
    iteration = 0;
    worklist.zap();
    madeChange = false;
  }

  /// Replace all of the results of the old instruction with the
  /// corresponding results of the new instruction.
  void replaceInstUsesPairwiseWith(SILInstruction *oldI, SILInstruction *newI) {
    LLVM_DEBUG(llvm::dbgs() << "SC: Replacing " << *oldI << "\n"
                            << "    with " << *newI << '\n');

    auto oldResults = oldI->getResults();
    auto newResults = newI->getResults();
    assert(oldResults.size() == newResults.size());
    for (auto i : indices(oldResults)) {
      // Add all modified instrs to worklist.
      worklist.addUsersToWorklist(oldResults[i]);

      oldResults[i]->replaceAllUsesWith(newResults[i]);
    }
  }

  // This method is to be used when a value is found to be dead,
  // replaceable with another preexisting expression. Here we add all
  // uses of oldValue to the worklist, replace all uses of oldValue
  // with newValue.
  void replaceValueUsesWith(SILValue oldValue, SILValue newValue) {
    worklist.addUsersToWorklist(
        oldValue); // Add all modified instrs to worklist.

    LLVM_DEBUG(llvm::dbgs() << "SC: Replacing " << oldValue << "\n"
                            << "    with " << newValue << '\n');

    oldValue->replaceAllUsesWith(newValue);
  }

  // This method is to be used when an instruction is found to be dead,
  // replaceable with another preexisting expression. Here we add all uses of I
  // to the worklist, replace all uses of I with the new value, then return I,
  // so that the combiner will know that I was modified.
  void replaceInstUsesWith(SingleValueInstruction &I, ValueBase *V) {
    worklist.addUsersToWorklist(&I); // Add all modified instrs to worklist.

    LLVM_DEBUG(llvm::dbgs() << "SC: Replacing " << I << "\n"
                            << "    with " << *V << '\n');

    I.replaceAllUsesWith(V);
  }

  // Some instructions can never be "trivially dead" due to side effects or
  // producing a void value. In those cases, since we cannot rely on
  // SILCombines trivially dead instruction DCE in order to delete the
  // instruction, visit methods should use this method to delete the given
  // instruction and upon completion of their peephole return the value returned
  // by this method.
  SILInstruction *eraseInstFromFunction(SILInstruction &I,
                                        SILBasicBlock::iterator &InstIter,
                                        bool AddOperandsToWorklist = true) {
    // Delete any debug users first.
    for (auto result : I.getResults()) {
      while (!result->use_empty()) {
        auto *user = result->use_begin()->getUser();
        assert(user->isDebugInstruction());
        if (InstIter == user->getIterator())
          ++InstIter;
        worklist.remove(user);
        user->eraseFromParent();
      }
    }
    if (InstIter == I.getIterator())
      ++InstIter;

    eraseSingleInstFromFunction(I, worklist, AddOperandsToWorklist);
    madeChange = true;
    // Dummy return, so the caller doesn't need to explicitly return nullptr.
    return nullptr;
  }

  SILInstruction *eraseInstFromFunction(SILInstruction &I,
                                        bool AddOperandsToWorklist = true) {
    SILBasicBlock::iterator nullIter;
    return eraseInstFromFunction(I, nullIter, AddOperandsToWorklist);
  }

  static void eraseSingleInstFromFunction(SILInstruction &I,
                                          CombinerWorklist &worklist,
                                          bool AddOperandsToWorklist) {
    LLVM_DEBUG(llvm::dbgs() << "SC: ERASE " << I << '\n');

    assert(!I.hasUsesOfAnyResult() && "Cannot erase instruction that is used!");

    // Make sure that we reprocess all operands now that we reduced their
    // use counts.
    if (I.getNumOperands() < 8 && AddOperandsToWorklist) {
      for (auto &OpI : I.getAllOperands()) {
        if (auto *Op = OpI.get()->getDefiningInstruction()) {
          LLVM_DEBUG(llvm::dbgs() << "SC: add op " << *Op
                                  << " from erased inst to worklist\n");
          worklist.add(Op);
        }
      }
    }
    worklist.remove(&I);
    I.eraseFromParent();
  }

  void addInitialGroup(ArrayRef<SILInstruction *> List) {
    worklist.addInitialGroup(List);
  }

  void visitInstruction(SILInstruction *instruction) {
#ifndef NDEBUG
    std::string instructionDescription;
#endif
    LLVM_DEBUG(llvm::raw_string_ostream SS(instructionDescription); instruction->print(SS);
               instructionDescription = SS.str(););
    LLVM_DEBUG(llvm::dbgs() << "SC: Visiting: " << instructionDescription << '\n');

    if (auto replacement = this->visit(instruction)) {
      replaceInstructionWithVisitResult(instruction, *replacement, &instructionDescription);
    }
  }

  void replaceInstructionWithVisitResult(SILInstruction *instruction, SILInstruction &replacement, std::string *instructionDescription) {
    ++numCombined;
    // Should we replace the old instruction with a new one?
    if (&replacement != instruction) {
      assert(&*std::prev(SILBasicBlock::iterator(instruction)) == &replacement &&
            "Expected new instruction inserted before existing instruction!");

      LLVM_DEBUG(llvm::dbgs() << "SC: Old = " << *instruction << '\n'
                              << "    New = " << replacement << '\n');

      // Everything uses the new instruction now.
      replaceInstUsesPairwiseWith(instruction, &replacement);

      // Push the new instruction and any users onto the worklist.
      worklist.add(&replacement);
      worklist.addUsersOfAllResultsToWorklist(&replacement);

      eraseInstFromFunction(*instruction);
    } else {
      if (instructionDescription != nullptr) {
        LLVM_DEBUG(llvm::dbgs() << "SC: Mod = " << instructionDescription << '\n'
                                << "    New = " << *instruction << '\n');
      }

      // If the instruction was modified, it's possible that it is now dead.
      // if so, remove it.
      if (isInstructionTriviallyDead(instruction)) {
        eraseInstFromFunction(*instruction);
      } else {
        worklist.add(instruction);
        worklist.addUsersOfAllResultsToWorklist(instruction);
      }
    }
    madeChange = true;
  }

};

} // end namespace swift
