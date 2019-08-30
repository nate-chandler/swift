//===------- MandatoryCombiner.cpp ----------------------------------------===//
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
///
///  \file
///
///  Defines the MandatoryCombiner function transform.  The pass contains basic
///  instruction combines to be performed at the begining of both the Onone and
///  also the performance pass pipelines, after the diagnostics passes have been
///  run.  It is intended to be run before and to be independent of other
///  transforms.
///
///  The intention of this pass is to be a place for mandatory peepholes that
///  are not needed for diagnostics. Please put any such peepholes here instead
///  of in the diagnostic passes.
///
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "sil-mandatory-combiner"
#include "swift/SIL/SILVisitor.h"
#include "swift/SILOptimizer/PassManager/Passes.h"
#include "swift/SILOptimizer/PassManager/Transforms.h"
#include "swift/SILOptimizer/Utils/Local.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <vector>

using namespace swift;

namespace {

class MandatoryCombiner final
    : public SILInstructionVisitor<MandatoryCombiner, void> {

  class Worklist {
    llvm::SmallVector<SILInstruction *, 256> worklist;
    llvm::DenseMap<SILInstruction *, unsigned> worklistMap;

    void operator=(const class worklist &RHS) = delete;
    Worklist(const class Worklist &Worklist) = delete;

  public:
    Worklist() {}

    /// Returns true if the worklist is empty.
    bool isEmpty() const { return worklist.empty(); }

    /// Add the specified instruction to the worklist if it isn't already in it.
    void add(SILInstruction *I) {
      if (!worklistMap.insert(std::make_pair(I, worklist.size())).second)
        return;

      LLVM_DEBUG(llvm::dbgs() << "SC: ADD: " << *I << '\n');
      worklist.push_back(I);
    }

    /// If the given ValueBase is a SILInstruction add it to the worklist.
    void addValue(ValueBase *V) {
      if (auto *I = V->getDefiningInstruction())
        add(I);
    }

    /// Add the given list of instructions in reverse order to the worklist.
    /// This routine assumes that the worklist is empty and the given list has
    /// no duplicates.
    void addInitialGroup(ArrayRef<SILInstruction *> List) {
      assert(worklist.empty() && "Worklist must be empty to add initial group");
      worklist.reserve(List.size() + 16);
      worklistMap.reserve(List.size());
      LLVM_DEBUG(llvm::dbgs()
                 << "SC: ADDING: " << List.size() << " instrs to worklist\n");
      while (!List.empty()) {
        SILInstruction *I = List.back();
        List = List.slice(0, List.size() - 1);
        worklistMap.insert(std::make_pair(I, worklist.size()));
        worklist.push_back(I);
      }
    }

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

    /// Check that the worklist is empty and nuke the backing store for the map
    /// if it is large.
    void zap() {
      assert(worklistMap.empty() && "Worklist empty, but the map is not?");

      // Do an explicit clear, this shrinks the map if needed.
      worklistMap.clear();
    }
  };

  bool madeChange;
  unsigned iteration;
  InstModCallbacks instModCallbacks;
  Worklist worklist;

public:
  MandatoryCombiner()
      : madeChange(false), iteration(0),
        instModCallbacks(
            [&](SILInstruction *instruction) {
              worklist.remove(instruction);
              instruction->eraseFromParent();
            },
            [&](SILInstruction *instruction) { worklist.add(instruction); }),
        worklist(){};

  /// \returns whether all the values are of trivial type in the provided
  ///          function.
  template <typename Values>
  static bool areAllValuesTrivial(Values values, SILFunction &function) {
    return llvm::all_of(values, [&](SILValue value) -> bool {
      return value->getType().isTrivial(function);
    });
  }

  void beforeVisit(SILInstruction *inst) {
    //    llvm::dbgs() << "visiting instruction" << *inst << "\n";
    llvm::dbgs() << "visiting instruction of kind " << (int)inst->getKind()
                 << "\n";
  }

  /// Base visitor that does not do anything.
  void visitSILInstruction(SILInstruction *) {}

  void visitApplyInst(ApplyInst *instruction) {
    // Apply this pass only to partial applies all of whose arguments are
    // trivial.
    auto calledValue = instruction->getCalleeOrigin();
    if (calledValue == nullptr) {
      return;
    }
    auto fullApplyCallee = calledValue->getDefiningInstruction();
    if (fullApplyCallee == nullptr) {
      return;
    }
    auto partialApply = dyn_cast<PartialApplyInst>(fullApplyCallee);
    if (partialApply == nullptr) {
      return;
    }
    auto *function = partialApply->getCalleeFunction();
    if (function == nullptr) {
      return;
    }
    ApplySite fullApplySite(instruction);
    auto fullApplyArguments = fullApplySite.getArguments();
    if (!areAllValuesTrivial(fullApplyArguments, *function)) {
      return;
    }
    auto partialApplyArguments = ApplySite(partialApply).getArguments();
    if (!areAllValuesTrivial(partialApplyArguments, *function)) {
      return;
    }

    auto callee = partialApply->getCallee();

    ApplySite partialApplySite(partialApply);

    SmallVector<SILValue, 8> argsVec;
    llvm::copy(partialApplyArguments, std::back_inserter(argsVec));
    llvm::copy(fullApplyArguments, std::back_inserter(argsVec));

    SILBuilderWithScope builder(instruction);
    ApplyInst *replacement = builder.createApply(
        /*Loc=*/instruction->getDebugLocation().getLocation(), /*Fn=*/callee,
        /*Subs=*/partialApply->getSubstitutionMap(),
        /*Args*/ argsVec,
        /*isNonThrowing=*/instruction->isNonThrowing(),
        /*SpecializationInfo=*/partialApply->getSpecializationInfo());
    instruction->replaceAllUsesWith(replacement);
    instruction->eraseFromParent();
    worklist.remove(instruction);
    worklist.add(replacement);

    bool deletedDeadClosure =
        tryDeleteDeadClosure(partialApply, instModCallbacks);
    if (deletedDeadClosure) {
      madeChange = true;
    }
  }

  void addReachableCodeToWorklist(SILFunction &function) {
    llvm::SmallVector<SILInstruction *, 128> instructions;
    for (auto &block : function) {
      for (auto iterator = block.begin(), end = block.end(); iterator != end;) {
        auto *instruction = &*iterator;
        instructions.push_back(instruction);
        ++iterator;
      }
    }
    worklist.addInitialGroup(instructions);
  }

  /// \return whether a change was made.
  bool doOneIteration(SILFunction &function, unsigned iteration) {
    madeChange = false;

    addReachableCodeToWorklist(function);

    while (!worklist.isEmpty()) {
      auto *instruction = worklist.removeOne();
      if (instruction == nullptr) {
        continue;
      }

      visit(instruction);
    }

    worklist.zap();
    return madeChange;
  }

  /// Applies the MandatoryCombiner to the provided function.
  ///
  /// \param function the function to which to apply the MandatoryCombiner.
  ///
  /// \return whether a change was made.
  bool runOnFunction(SILFunction &function) {
    bool changed = false;

    while (doOneIteration(function, iteration)) {
      changed = true;
      ++iteration;
    }

    return changed;
  }
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
//                            Top Level Entrypoint
//===----------------------------------------------------------------------===//

namespace {

struct MandatoryCombine : SILFunctionTransform {
  void run() override {
    auto *function = getFunction();

    MandatoryCombiner combiner;
    bool madeChange = combiner.runOnFunction(*function);

    if (madeChange) {
      invalidateAnalysis(SILAnalysis::InvalidationKind::Instructions);
    }
  }
};

} // end anonymous namespace

SILTransform *swift::createMandatoryCombine() { return new MandatoryCombine(); }
