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
//
// A port of LLVM's InstCombiner to SIL. Its main purpose is for performing
// small combining operations/peepholes at the SIL level. It additionally
// performs dead code elimination when it initially adds instructions to the
// work queue in order to reduce compile time by not visiting trivially dead
// instructions.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_SILOPTIMIZER_PASSMANAGER_SILCOMBINER_H
#define SWIFT_SILOPTIMIZER_PASSMANAGER_SILCOMBINER_H

#include "swift/SIL/SILBuilder.h"
#include "swift/SIL/SILInstruction.h"
#include "swift/SIL/SILValue.h"
#include "swift/SIL/SILVisitor.h"
#include "swift/SILOptimizer/Analysis/ClassHierarchyAnalysis.h"
#include "swift/SILOptimizer/Analysis/ProtocolConformanceAnalysis.h"
#include "swift/SILOptimizer/Utils/CastOptimizer.h"
#include "swift/SILOptimizer/Utils/Existential.h"
#include "swift/SILOptimizer/Utils/Local.h"
#include "../Utils/CombinerBase.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

namespace swift {

class AliasAnalysis;


/// This is a class which maintains the state of the combiner and simplifies
/// many operations such as removing/adding instructions and syncing them with
/// the worklist.
class SILCombiner final :
    public CombinerBase<SILCombiner, SILInstruction *> {

  AliasAnalysis *AA;

  DominanceAnalysis *DA;

  // Determine the set of types a protocol conforms to in whole-module
  // compilation mode.
  ProtocolConformanceAnalysis *PCA;

  // Class hierarchy analysis needed to confirm no derived classes of a sole
  // conforming class.
  ClassHierarchyAnalysis *CHA;

  /// If set to true then the optimizer is free to erase cond_fail instructions.
  bool RemoveCondFails;

  /// Builder used to insert instructions.
  SILBuilder &Builder;

  /// Cast optimizer
  CastOptimizer CastOpt;

public:
  SILCombiner(SILOptFunctionBuilder &FuncBuilder, SILBuilder &B,
              AliasAnalysis *AA, DominanceAnalysis *DA,
              ProtocolConformanceAnalysis *PCA, ClassHierarchyAnalysis *CHA,
              bool removeCondFails)
      : CombinerBase(), AA(AA), DA(DA), PCA(PCA), CHA(CHA), 
        RemoveCondFails(removeCondFails), Builder(B),
        CastOpt(FuncBuilder, nullptr /*SILBuilderContext*/,
                /* ReplaceValueUsesAction */
                [&](SILValue Original, SILValue Replacement) {
                  replaceValueUsesWith(Original, Replacement);
                },
                /* ReplaceInstUsesAction */
                [&](SingleValueInstruction *I, ValueBase *V) {
                  replaceInstUsesWith(*I, V);
                },
                /* EraseAction */
                [&](SILInstruction *I) { eraseInstFromFunction(*I); }) {}

  /// Base visitor that does not do anything.
  SILInstruction *visitSILInstruction(SILInstruction *I) { return nullptr; }

  /// Instruction visitors.
  SILInstruction *visitReleaseValueInst(ReleaseValueInst *DI);
  SILInstruction *visitRetainValueInst(RetainValueInst *CI);
  SILInstruction *visitReleaseValueAddrInst(ReleaseValueAddrInst *DI);
  SILInstruction *visitRetainValueAddrInst(RetainValueAddrInst *CI);
  SILInstruction *visitPartialApplyInst(PartialApplyInst *AI);
  SILInstruction *visitApplyInst(ApplyInst *AI);
  SILInstruction *visitBeginApplyInst(BeginApplyInst *BAI);
  SILInstruction *visitTryApplyInst(TryApplyInst *AI);
  SILInstruction *optimizeStringObject(BuiltinInst *BI);
  SILInstruction *visitBuiltinInst(BuiltinInst *BI);
  SILInstruction *visitCondFailInst(CondFailInst *CFI);
  SILInstruction *visitStrongRetainInst(StrongRetainInst *SRI);
  SILInstruction *visitRefToRawPointerInst(RefToRawPointerInst *RRPI);
  SILInstruction *visitUpcastInst(UpcastInst *UCI);
  SILInstruction *optimizeLoadFromStringLiteral(LoadInst *LI);
  SILInstruction *visitLoadInst(LoadInst *LI);
  SILInstruction *visitIndexAddrInst(IndexAddrInst *IA);
  SILInstruction *visitAllocStackInst(AllocStackInst *AS);
  SILInstruction *visitAllocRefInst(AllocRefInst *AR);
  SILInstruction *visitSwitchEnumAddrInst(SwitchEnumAddrInst *SEAI);
  SILInstruction *visitInjectEnumAddrInst(InjectEnumAddrInst *IEAI);
  SILInstruction *visitPointerToAddressInst(PointerToAddressInst *PTAI);
  SILInstruction *visitUncheckedAddrCastInst(UncheckedAddrCastInst *UADCI);
  SILInstruction *visitUncheckedRefCastInst(UncheckedRefCastInst *URCI);
  SILInstruction *visitUncheckedRefCastAddrInst(UncheckedRefCastAddrInst *URCI);
  SILInstruction *visitBridgeObjectToRefInst(BridgeObjectToRefInst *BORI);
  SILInstruction *visitUnconditionalCheckedCastInst(
                    UnconditionalCheckedCastInst *UCCI);
  SILInstruction *
  visitUnconditionalCheckedCastAddrInst(UnconditionalCheckedCastAddrInst *UCCAI);
  SILInstruction *visitRawPointerToRefInst(RawPointerToRefInst *RPTR);
  SILInstruction *
  visitUncheckedTakeEnumDataAddrInst(UncheckedTakeEnumDataAddrInst *TEDAI);
  SILInstruction *visitStrongReleaseInst(StrongReleaseInst *SRI);
  SILInstruction *visitCondBranchInst(CondBranchInst *CBI);
  SILInstruction *
  visitUncheckedTrivialBitCastInst(UncheckedTrivialBitCastInst *UTBCI);
  SILInstruction *
  visitUncheckedBitwiseCastInst(UncheckedBitwiseCastInst *UBCI);
  SILInstruction *visitSelectEnumInst(SelectEnumInst *EIT);
  SILInstruction *visitSelectEnumAddrInst(SelectEnumAddrInst *EIT);
  SILInstruction *visitAllocExistentialBoxInst(AllocExistentialBoxInst *S);
  SILInstruction *visitThickToObjCMetatypeInst(ThickToObjCMetatypeInst *TTOCMI);
  SILInstruction *visitObjCToThickMetatypeInst(ObjCToThickMetatypeInst *OCTTMI);
  SILInstruction *visitTupleExtractInst(TupleExtractInst *TEI);
  SILInstruction *visitFixLifetimeInst(FixLifetimeInst *FLI);
  SILInstruction *visitSwitchValueInst(SwitchValueInst *SVI);
  SILInstruction *visitSelectValueInst(SelectValueInst *SVI);
  SILInstruction *
  visitCheckedCastAddrBranchInst(CheckedCastAddrBranchInst *CCABI);
  SILInstruction *
  visitCheckedCastBranchInst(CheckedCastBranchInst *CBI);
  SILInstruction *visitUnreachableInst(UnreachableInst *UI);
  SILInstruction *visitAllocRefDynamicInst(AllocRefDynamicInst *ARDI);
  SILInstruction *visitEnumInst(EnumInst *EI);
      
  SILInstruction *visitMarkDependenceInst(MarkDependenceInst *MDI);
  SILInstruction *visitClassifyBridgeObjectInst(ClassifyBridgeObjectInst *CBOI);
  SILInstruction *visitConvertFunctionInst(ConvertFunctionInst *CFI);
  SILInstruction *
  visitConvertEscapeToNoEscapeInst(ConvertEscapeToNoEscapeInst *Cvt);

  /// Instruction visitor helpers.
  SILInstruction *optimizeBuiltinCanBeObjCClass(BuiltinInst *AI);

  // Optimize the "isConcrete" builtin.
  SILInstruction *optimizeBuiltinIsConcrete(BuiltinInst *I);

  // Optimize the "trunc_N1_M2" builtin. if N1 is a result of "zext_M1_*" and
  // the following holds true: N1 > M1 and M2>= M1
  SILInstruction *optimizeBuiltinTruncOrBitCast(BuiltinInst *I);

  // Optimize the "zext_M2_M3" builtin. if M2 is a result of "zext_M1_M2"
  SILInstruction *optimizeBuiltinZextOrBitCast(BuiltinInst *I);

  // Optimize the "cmp_eq_XXX" builtin. If \p NegateResult is true then negate
  // the result bit.
  SILInstruction *optimizeBuiltinCompareEq(BuiltinInst *AI, bool NegateResult);

  SILInstruction *tryOptimizeApplyOfPartialApply(PartialApplyInst *PAI);

  SILInstruction *optimizeApplyOfConvertFunctionInst(FullApplySite AI,
                                                     ConvertFunctionInst *CFI);

  bool tryOptimizeKeypath(ApplyInst *AI);
  bool tryOptimizeInoutKeypath(BeginApplyInst *AI);

  // Optimize concatenation of string literals.
  // Constant-fold concatenation of string literals known at compile-time.
  SILInstruction *optimizeConcatenationOfStringLiterals(ApplyInst *AI);

  // Optimize an application of f_inverse(f(x)) -> x.
  bool optimizeIdentityCastComposition(ApplyInst *FInverse,
                                       StringRef FInverseName, StringRef FName);

  /// Perform one SILCombine iteration.
  bool doOneIteration(SILFunction &F, unsigned Iteration);

private:
  FullApplySite rewriteApplyCallee(FullApplySite apply, SILValue callee);

  // Build concrete existential information using findInitExistential.
  Optional<ConcreteOpenedExistentialInfo>
  buildConcreteOpenedExistentialInfo(Operand &ArgOperand);

  // Build concrete existential information using SoleConformingType.
  Optional<ConcreteOpenedExistentialInfo>
  buildConcreteOpenedExistentialInfoFromSoleConformingType(Operand &ArgOperand);

  // Common utility function to build concrete existential information for all
  // arguments of an apply instruction.
  void buildConcreteOpenedExistentialInfos(
      FullApplySite Apply,
      llvm::SmallDenseMap<unsigned, ConcreteOpenedExistentialInfo> &COEIs,
      SILBuilderContext &BuilderCtx,
      SILOpenedArchetypesTracker &OpenedArchetypesTracker);

  bool canReplaceArg(FullApplySite Apply, const OpenedArchetypeInfo &OAI,
                     const ConcreteExistentialInfo &CEI, unsigned ArgIdx);

  SILInstruction *createApplyWithConcreteType(
      FullApplySite Apply,
      const llvm::SmallDenseMap<unsigned, ConcreteOpenedExistentialInfo> &COEIs,
      SILBuilderContext &BuilderCtx);

  // Common utility function to replace the WitnessMethodInst using a
  // BuilderCtx.
  void replaceWitnessMethodInst(WitnessMethodInst *WMI,
                                SILBuilderContext &BuilderCtx,
                                CanType ConcreteType,
                                const ProtocolConformanceRef ConformanceRef);

  SILInstruction *
  propagateConcreteTypeOfInitExistential(FullApplySite Apply,
                                         WitnessMethodInst *WMI);

  SILInstruction *propagateConcreteTypeOfInitExistential(FullApplySite Apply);

  /// Propagate concrete types from ProtocolConformanceAnalysis.
  SILInstruction *propagateSoleConformingType(FullApplySite Apply,
                                              WitnessMethodInst *WMI);

  /// Add reachable code to the worklist. Meant to be used when starting to
  /// process a new function.
  void addReachableCodeToWorklist(SILBasicBlock *BB);

  typedef SmallVector<SILInstruction*, 4> UserListTy;

  /// Returns a list of instructions that project or perform reference
  /// counting operations on \p Value or on its uses.
  /// \return return false if \p Value has other than ARC uses.
  static bool recursivelyCollectARCUsers(UserListTy &Uses, ValueBase *Value);

  /// Erases an apply instruction including all it's uses \p.
  /// Inserts release/destroy instructions for all owner and in-parameters.
  /// \return Returns true if successful.
  bool eraseApply(FullApplySite FAS, const UserListTy &Users);

  /// Returns true if the results of a try_apply are not used.
  static bool isTryApplyResultNotUsed(UserListTy &AcceptedUses,
                                      TryApplyInst *TAI);
};

} // end namespace swift

#endif
