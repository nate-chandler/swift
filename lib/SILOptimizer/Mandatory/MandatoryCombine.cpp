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
#include "../SILCombiner/SILCombiner.h"
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

class MandatoryCombiner final : public CombinerBase<MandatoryCombiner, SILInstruction *> {

  bool madeChange;
  unsigned iteration;
  InstModCallbacks instModCallbacks;
  CombinerWorklist worklist;
  llvm::SmallVectorImpl<SILInstruction *> &createdInstructions;
  llvm::SmallVector<SILInstruction *, 16> instructionsPendingDeletion;

public:
  MandatoryCombiner(
      llvm::SmallVectorImpl<SILInstruction *> &createdInstructions)
      : madeChange(false), iteration(0),
        instModCallbacks(
            [&](SILInstruction *instruction) {
              worklist.remove(instruction);
              instructionsPendingDeletion.push_back(instruction);
            },
            [&](SILInstruction *instruction) { worklist.add(instruction); }),
        worklist(), createdInstructions(createdInstructions){};

  /// \returns whether all the values are of trivial type in the provided
  ///          function.
  template <typename Values>
  static bool areAllValuesTrivial(Values values, SILFunction &function) {
    return llvm::all_of(values, [&](SILValue value) -> bool {
      return value->getType().isTrivial(function);
    });
  }

  /// Base visitor that does not do anything.
  SILInstruction *visitSILInstruction(SILInstruction *) {
    return nullptr;
  }

  SILInstruction *visitApplyInst(ApplyInst *instruction) {
    // Apply this pass only to partial applies all of whose arguments are
    // trivial.
    auto calledValue = instruction->getCalleeOrigin();
    if (calledValue == nullptr) {
      return nullptr;
    }
    auto fullApplyCallee = calledValue->getDefiningInstruction();
    if (fullApplyCallee == nullptr) {
      return nullptr;
    }
    auto partialApply = dyn_cast<PartialApplyInst>(fullApplyCallee);
    if (partialApply == nullptr) {
      return nullptr;
    }
    auto *function = partialApply->getCalleeFunction();
    if (function == nullptr) {
      return nullptr;
    }
    ApplySite fullApplySite(instruction);
    auto fullApplyArguments = fullApplySite.getArguments();
    if (!areAllValuesTrivial(fullApplyArguments, *function)) {
      return nullptr;
    }
    auto partialApplyArguments = ApplySite(partialApply).getArguments();
    if (!areAllValuesTrivial(partialApplyArguments, *function)) {
      return nullptr;
    }

    auto callee = partialApply->getCallee();

    ApplySite partialApplySite(partialApply);

    SmallVector<SILValue, 8> argsVec;
    llvm::copy(partialApplyArguments, std::back_inserter(argsVec));
    llvm::copy(fullApplyArguments, std::back_inserter(argsVec));

    SILBuilderWithScope builder(instruction, &createdInstructions);
    ApplyInst *replacement = builder.createApply(
        /*Loc=*/instruction->getDebugLocation().getLocation(), /*Fn=*/callee,
        /*Subs=*/partialApply->getSubstitutionMap(),
        /*Args*/ argsVec,
        /*isNonThrowing=*/instruction->isNonThrowing(),
        /*SpecializationInfo=*/partialApply->getSpecializationInfo());

    replaceInstructionWithVisitResult(instruction, *replacement, /*instructionDescription*/nullptr);
    tryDeleteDeadClosure(partialApply, instModCallbacks);
    return nullptr;
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

      visitInstruction(instruction);

      for (SILInstruction *instruction : instructionsPendingDeletion) {
        eraseInstFromFunction(*instruction);
      }
      instructionsPendingDeletion.clear();

      // Our tracking list has been accumulating instructions created by the
      // SILBuilder during this iteration. Go through the tracking list and add
      // its contents to the worklist and then clear said list in preparation
      // for the next iteration.
      for (SILInstruction *instruction : createdInstructions) {
        LLVM_DEBUG(llvm::dbgs() << "MC: add " << *instruction
                                << " from tracking list to worklist\n");
        worklist.add(instruction);
      }
      createdInstructions.clear();
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

class MandatoryCombine final : public SILFunctionTransform {

  llvm::SmallVector<SILInstruction *, 64> createdInstructions;

  void run() override {
    auto *function = getFunction();

    MandatoryCombiner combiner(createdInstructions);
    bool madeChange = combiner.runOnFunction(*function);

    if (madeChange) {
      invalidateAnalysis(SILAnalysis::InvalidationKind::Instructions);
    }
  }

  void handleDeleteNotification(SILNode *node) override {
    auto instruction = dyn_cast<SILInstruction>(node);
    if (instruction == nullptr) {
      return;
    }

    // Linear searching the tracking list doesn't hurt because usually it only
    // contains a few elements.
    auto iterator = std::find(createdInstructions.begin(),
                              createdInstructions.end(), instruction);
    if (iterator != createdInstructions.end()) {
      createdInstructions.erase(iterator);
    }
  }

  bool needsNotifications() override { return true; }
};

} // end anonymous namespace

SILTransform *swift::createMandatoryCombine() { return new MandatoryCombine(); }
