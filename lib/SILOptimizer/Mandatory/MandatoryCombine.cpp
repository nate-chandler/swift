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

#define LLVM_DEBUG(STUFF) STUFF

using namespace swift;

STATISTIC(numCombinedMandatory, "Number of instructions mandatorily combined");

namespace {

using Callback = std::function<void(void)>;
Callback makeDefaultCallback() {
  return [] {};
};

class MandatoryCombiner final
    : public SILInstructionVisitor<MandatoryCombiner,
                                   std::pair<SILInstruction *, Callback>> {

  bool madeChange;
  unsigned iteration;
  InstModCallbacks instModCallbacks;
  SILCombineWorklist worklist;
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

  /// Replace all of the results of the old instruction with the
  /// corresponding results of the new instruction.
  void replaceInstUsesPairwiseWith(SILInstruction *oldInstruction,
                                   SILInstruction *newInstruction) {
    LLVM_DEBUG(llvm::dbgs() << "MC: Replacing " << *oldInstruction << "\n"
                            << "    with " << *newInstruction << '\n');

    auto oldResults = oldInstruction->getResults();
    auto newResults = newInstruction->getResults();
    assert(oldResults.size() == newResults.size());
    for (auto i : indices(oldResults)) {
      // Add all modified instrs to worklist.
      worklist.addUsersToWorklist(oldResults[i]);

      oldResults[i]->replaceAllUsesWith(newResults[i]);
    }
  }

  void eraseInstFromFunction(SILInstruction &instruction,
                             bool addOperandsToWorklist = true) {
    SILBasicBlock::iterator nullIter;
    return eraseInstFromFunction(instruction, nullIter, addOperandsToWorklist);
  }

  // Some instructions can never be "trivially dead" due to side effects or
  // producing a void value. In those cases, since we cannot rely on
  // SILCombines trivially dead instruction DCE in order to delete the
  // instruction, visit methods should use this method to delete the given
  // instruction and upon completion of their peephole return the value returned
  // by this method.
  void eraseInstFromFunction(SILInstruction &instruction,
                             SILBasicBlock::iterator &instIter,
                             bool addOperandsToWorklist = true) {
    // Delete any debug users first.
    for (auto result : instruction.getResults()) {
      while (!result->use_empty()) {
        auto *user = result->use_begin()->getUser();
        assert(user->isDebugInstruction());
        if (instIter == user->getIterator())
          ++instIter;
        worklist.remove(user);
        user->eraseFromParent();
      }
    }
    if (instIter == instruction.getIterator())
      ++instIter;

    eraseSingleInstFromFunction(instruction, addOperandsToWorklist);
    madeChange = true;
  }

  void eraseSingleInstFromFunction(SILInstruction &instruction,
                                   bool addOperandsToWorklist = true) {
    LLVM_DEBUG(llvm::dbgs() << "MC: ERASE " << instruction << '\n');

    assert(!instruction.hasUsesOfAnyResult() &&
           "Cannot erase instruction that is used!");

    // Make sure that we reprocess all operands now that we reduced their
    // use counts.
    if (instruction.getNumOperands() < 8 && addOperandsToWorklist) {
      for (auto &operand : instruction.getAllOperands()) {
        if (auto *op = operand.get()->getDefiningInstruction()) {
          LLVM_DEBUG(llvm::dbgs() << "MC: add op " << *op
                                  << " from erased inst to worklist\n");
          worklist.add(op);
        }
      }
    }
    worklist.remove(&instruction);
    instruction.eraseFromParent();
  }

  /// Base visitor that does not do anything.
  std::pair<SILInstruction *, Callback> visitSILInstruction(SILInstruction *) {
    return {nullptr, makeDefaultCallback()};
  }

  std::pair<SILInstruction *, Callback> visitApplyInst(ApplyInst *instruction) {
    std::pair<SILInstruction *, Callback> defaultReturn = {
        nullptr, makeDefaultCallback()};
    // Apply this pass only to partial applies all of whose arguments are
    // trivial.
    auto calledValue = instruction->getCalleeOrigin();
    if (calledValue == nullptr) {
      return defaultReturn;
    }
    auto fullApplyCallee = calledValue->getDefiningInstruction();
    if (fullApplyCallee == nullptr) {
      return defaultReturn;
    }
    auto partialApply = dyn_cast<PartialApplyInst>(fullApplyCallee);
    if (partialApply == nullptr) {
      return defaultReturn;
    }
    auto *function = partialApply->getCalleeFunction();
    if (function == nullptr) {
      return defaultReturn;
    }
    ApplySite fullApplySite(instruction);
    auto fullApplyArguments = fullApplySite.getArguments();
    if (!areAllValuesTrivial(fullApplyArguments, *function)) {
      return defaultReturn;
    }
    auto partialApplyArguments = ApplySite(partialApply).getArguments();
    if (!areAllValuesTrivial(partialApplyArguments, *function)) {
      return defaultReturn;
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

    LLVM_DEBUG(llvm::dbgs() << "MC: WILL SOONN ATTEMPT DELETE DEAD CLOSURE\n"
                            << *partialApply;)
    return {replacement, [this, partialApply] {
              LLVM_DEBUG(llvm::dbgs() << "MC: ATTEMPTING DELETE DEAD CLOSURE\n"
                                      << *partialApply;)
              auto success =
                  tryDeleteDeadClosure(partialApply, this->instModCallbacks);
            }};
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

#ifndef NDEBUG
      std::string instructionDescription;
#endif
      LLVM_DEBUG(llvm::raw_string_ostream stream(instructionDescription);
                 instruction->print(stream);
                 instructionDescription = stream.str(););
      LLVM_DEBUG(llvm::dbgs() << "MC: Visiting: " << instruction << '\n');

      auto replacementAndCallback = visit(instruction);
      auto replacement = replacementAndCallback.first;
      auto callback = replacementAndCallback.second;
      if (replacement) {
        ++numCombinedMandatory;
        if (replacement != instruction) {
          assert(
              &*std::prev(SILBasicBlock::iterator(instruction)) ==
                  replacement &&
              "Expected new instruction inserted before existing instruction!");

          LLVM_DEBUG(llvm::dbgs() << "MC: Old = " << *instruction << '\n'
                                  << "    New = " << *replacement << '\n');

          replaceInstUsesPairwiseWith(instruction, replacement);
          worklist.add(replacement);
          worklist.addUsersOfAllResultsToWorklist(replacement);

          eraseInstFromFunction(*instruction);
          callback();
        } else {
          LLVM_DEBUG(llvm::dbgs()
                     << "MC: Mod = " << instructionDescription << '\n'
                     << "    New = " << *replacement << '\n');

          // If the instruction was modified, it's possible that it is now dead.
          // if so, remove it.
          if (isInstructionTriviallyDead(instruction)) {
            eraseInstFromFunction(*instruction);
            callback();
          } else {
            worklist.add(replacement);
            worklist.addUsersOfAllResultsToWorklist(replacement);
          }
        }
        for (SILInstruction *instruction : instructionsPendingDeletion) {
          eraseSingleInstFromFunction(*instruction);
        }
        instructionsPendingDeletion.clear();
      }

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
