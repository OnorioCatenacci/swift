//===--- SILLocation.h - Location information for SIL nodes -----*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2015 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_SIL_LOCATION_H
#define SWIFT_SIL_LOCATION_H

#include "llvm/ADT/PointerUnion.h"
#include "swift/AST/Decl.h"
#include "swift/AST/Expr.h"
#include "swift/AST/Stmt.h"

#include <cstddef>
#include <type_traits>

namespace swift {

class SourceLoc;

/// SILLocation - This is a pointer to the AST node that a SIL instruction was
/// derived from. This may be null if AST information is unavailable or
/// stripped.
///
/// FIXME: This should eventually include inlining history, generics
/// instantiation info, etc (when we get to it).
///
class SILLocation {
private:
  template <class T, class Enable = void>
  struct base_type;

  template <class T>
  struct base_type<T,
      typename std::enable_if<std::is_base_of<Decl, T>::value>::type> {
    using type = Decl;
  };

  template <class T>
  struct base_type<T,
      typename std::enable_if<std::is_base_of<Expr, T>::value>::type> {
    using type = Expr;
  };

  template <class T>
  struct base_type<T,
      typename std::enable_if<std::is_base_of<Stmt, T>::value>::type> {
    using type = Stmt;
  };

  template <class T>
  struct base_type<T,
      typename std::enable_if<std::is_base_of<Pattern, T>::value>::type> {
    using type = Pattern;
  };

  typedef llvm::PointerUnion4<Stmt*, Expr*, Decl*, Pattern*> ASTNodeTy;

public:
  enum LocationKind {
    // FIXME: NoneKind is to be removed.
    NoneKind = 0,
    RegularKind = 1,
    ReturnKind = 2,
    ImplicitReturnKind = 3,
    InlinedKind = 4,
    MandatoryInlinedKind = 5,
    CleanupKind = 6,
    ArtificialUnreachableKind = 7,
    SILFileKind = 8
  };

protected:
  /// \brief Primary AST location.
  ASTNodeTy ASTNode;

  // If coming from a .sil file, this is the location in the .sil file.
  // FIXME: We should be able to reuse the ASTNodes memory to store this. We
  // could just store Value.getPointer() in the pointer union.
  SourceLoc SILFileSourceLoc;

  /// \brief The kind of this SIL location.
  unsigned KindData;

  enum {
    BaseBits = 4, BaseMask = 0xF,

    /// \brief Used to mark this instruction as part of auto-generated
    /// code block.
    AutoGeneratedBit = 5,

    /// \brief Used to redefine the default source location used to
    /// represent this SILLocation. For example, when the host instruction
    /// is known to correspond to the beginning or the end of the source
    /// range of the ASTNode.
    PointsToStartBit = 6, PointsToEndBit = 7,

    /// \brief Used to notify that this instruction belongs to the top-
    /// level (module) scope.
    ///
    /// FIXME: If Module becomes a Decl, this could be removed.
    IsInTopLevel = 8,

    /// \brief Marks this instruction as belonging to the
    /// function prologue.
    IsInPrologue = 9
  };

  template <typename T>
  T *getNodeAs(ASTNodeTy Node) const {
    using base = typename base_type<T>::type*;
    return dyn_cast_or_null<T>(Node.dyn_cast<base>());
  }

  template <typename T>
  bool isNode(ASTNodeTy Node) const {
    if (ASTNode.is<typename base_type<T>::type*>())
      return isa<T>(Node.get<typename base_type<T>::type*>());
    return false;
  }

  template <typename T>
  T *castNodeTo(ASTNodeTy Node) const {
    return cast<T>(Node.get<typename base_type<T>::type*>());
  }

  // SILLocation constructors.
  SILLocation(LocationKind K) : KindData(K) {}
  SILLocation(Stmt *S, LocationKind K) : ASTNode(S), KindData(K) {}
  SILLocation(Expr *E, LocationKind K) : ASTNode(E), KindData(K) {}
  SILLocation(Decl *D, LocationKind K) : ASTNode(D), KindData(K) {}
  SILLocation(Pattern *P, LocationKind K) : ASTNode(P), KindData(K) {}

  // This constructor is used to support getAs operation.
  SILLocation() {}

  // Constructors for specifying the kind and the special flags for a
  // specific SILLocation. Meant to be used in conjunction with
  // getSpecialFlags.
  SILLocation(LocationKind K, unsigned Flags) : KindData(unsigned(K) | Flags) {}
  SILLocation(Stmt *S, LocationKind K,
              unsigned Flags) : ASTNode(S), KindData(unsigned(K) | Flags) {}
  SILLocation(Expr *E, LocationKind K,
              unsigned Flags) : ASTNode(E), KindData(unsigned(K) | Flags) {}
  SILLocation(Decl *D, LocationKind K,
              unsigned Flags) : ASTNode(D), KindData(unsigned(K) | Flags) {}
  SILLocation(Pattern *P, LocationKind K,
              unsigned Flags) : ASTNode(P), KindData(unsigned(K) | Flags) {}

private:
  friend class ImplicitReturnLocation;
  friend class MandatoryInlinedLocation;
  friend class InlinedLocation;
  friend class CleanupLocation;

  void setKind(LocationKind K) { KindData |= (K & BaseMask); }
  unsigned getSpecialFlags() const { return unsigned(KindData) & ~BaseMask; }
public:

  /// When an ASTNode gets implicitely converted into a SILLocation we
  /// construct a RegularLocation. Since RegularLocations represent the majority
  /// of locations, this greatly simplifies the user code.
  SILLocation(Stmt *S) : ASTNode(S), KindData(RegularKind) {}
  SILLocation(Expr *E) : ASTNode(E), KindData(RegularKind) {}
  SILLocation(Decl *D) : ASTNode(D), KindData(RegularKind) {}
  SILLocation(Pattern *P) : ASTNode(P), KindData(RegularKind) {}

  /// \brief Check if the location wraps an AST node or a valid SIL file
  /// location.
  ///
  /// Artificial locations and the top-level module locations will be null.
  bool isNull() const {
    return ASTNode.isNull() && SILFileSourceLoc.isInvalid();
  }
  LLVM_EXPLICIT operator bool() const { return !isNull(); }

  /// \brief Marks the location as coming from auto-generated body.
  void markAutoGenerated() { KindData |= (1 << AutoGeneratedBit); }

  /// \brief Returns true if the location represents an artifically generated
  /// body, such as thunks or default destructors.
  ///
  /// These locations should not be included in the debug line table.
  /// These might also need special handling by the debugger since they might
  /// contain calls, which the debugger could be able to step into.
  bool isAutoGenerated() const { return KindData & (1 << AutoGeneratedBit); }

  /// \brief Changes the default source location position to point to start of
  /// the AST node.
  void pointToStart() { KindData |= (1 << PointsToStartBit); }

  /// \brief Changes the default source location position to point to the end of
  /// the AST node.
  void pointToEnd() { KindData |= (1 << PointsToEndBit); }

  /// \brief Mark this location as the location corresponding to the top-level
  /// (module-level) code.
  void markAsInTopLevel() { KindData |= (1 << IsInTopLevel); }

  /// \brief Check is this location is associated with the top level/module.
  bool isInTopLevel() const { return KindData & (1 << IsInTopLevel); }

  /// \brief Mark this location as being part of the function
  /// prologue, which means that it deals with setting up the stack
  /// frame. The first breakpoint location in a function is at the end
  /// of the prologue.
  void markAsPrologue() { KindData |= (1 << IsInPrologue); }

  /// \brief Check is this location is part of a function's implicit prologue.
  bool isInPrologue() const { return KindData & (1 << IsInPrologue); }

  bool hasASTLocation() const { return !ASTNode.isNull(); }

  /// \brief Check if the corresponding source code location definitely points
  ///  to the start of the AST node.
  bool alwaysPointsToStart() const { return KindData & (1 << PointsToStartBit);}

  /// \brief Check if the corresponding source code location definitely points
  ///  to the end of the AST node.
  bool alwaysPointsToEnd() const { return KindData & (1 << PointsToEndBit); }

  LocationKind getKind() const { return (LocationKind)(KindData & BaseMask); }

  template <typename T>
  bool is() const {
    return T::isKind(*this);
  }

  template <typename T>
  T castTo() const {
    assert(T::isKind(*this));
    T t;
    SILLocation& tr = t;
    tr = *this;
    return t;
  }

  template <typename T>
  Optional<T> getAs() const {
    if (!T::isKind(*this))
      return Optional<T>();
    T t;
    SILLocation& tr = t;
    tr = *this;
    return t;
  }

  /// \brief If the current value is of the specified AST unit type T,
  /// return it, otherwise return null.
  template <typename T>
  T *getAsASTNode() const { return getNodeAs<T>(ASTNode); }

  /// \brief Returns true if the Location currently points to the AST node
  /// matching type T.
  template <typename T>
  bool isASTNode() const { return isNode<T>(ASTNode); }

  /// \brief Returns the primary value as the specified AST node type. If the
  /// specified type is incorrect, asserts.
  template <typename T>
  T *castToASTNode() const { return castNodeTo<T>(ASTNode); }

  SourceLoc getSourceLoc() const;
  SourceLoc getStartSourceLoc() const;
  SourceLoc getEndSourceLoc() const;
  
  SourceRange getSourceRange() const {
    return { getStartSourceLoc(), getEndSourceLoc() };
  }

  /// Pretty-print the value.
  void dump(const SourceManager &SM) const;
  void print(raw_ostream &OS, const SourceManager &SM) const;

  inline bool operator==(const SILLocation& R) const {
    return KindData == R.KindData &&
      ASTNode.getOpaqueValue() == R.ASTNode.getOpaqueValue() &&
      SILFileSourceLoc == R.SILFileSourceLoc;
  }
};

/// Allowed on any instruction.
class RegularLocation : public SILLocation {
public:
  RegularLocation(Stmt *S) : SILLocation(S, RegularKind) {}
  RegularLocation(Expr *E) : SILLocation(E, RegularKind) {}
  RegularLocation(Decl *D) : SILLocation(D, RegularKind) {}
  RegularLocation(Pattern *P) : SILLocation(P, RegularKind) {}

  /// \brief Returns a location representing the module.
  static RegularLocation getModuleLocation() {
    RegularLocation Loc;
    Loc.markAsInTopLevel();
    return Loc;
  }

  /// \brief If the current value is of the specified AST unit type T,
  /// return it, otherwise return null.
  template <typename T>
  T *getAs() const { return getNodeAs<T>(ASTNode); }

  /// \brief Returns true if the Location currently points to the AST node
  /// matching type T.
  template <typename T>
  bool is() const { return isNode<T>(ASTNode); }

  /// \brief Returns the primary value as the specified AST node type. If the
  /// specified type is incorrect, asserts.
  template <typename T>
  T *castTo() const { return castNodeTo<T>(ASTNode); }

  static RegularLocation getAutoGeneratedLocation() {
    RegularLocation AL;
    AL.markAutoGenerated();
    return AL;
  }

private:
  RegularLocation() : SILLocation(RegularKind) {}

  friend class SILLocation;
  static bool isKind(const SILLocation& L) {
    return L.getKind() == RegularKind;
  }
};

/// \brief Used to represent a return instruction in user code.
///
/// Allowed on an BranchInst, ReturnInst, AutoreleaseReturnInst.
class ReturnLocation : public SILLocation {
public:
  ReturnLocation(ReturnStmt *RS) : SILLocation(RS, ReturnKind) {}

  /// Construct the return location for a constructor or a destructor.
  ReturnLocation(BraceStmt *BS)
    : SILLocation(BS, ReturnKind) {}

  ReturnStmt *get() {
    return castToASTNode<ReturnStmt>();
  }

private:
  friend class SILLocation;
  static bool isKind(const SILLocation& L) {
    return L.getKind() == ReturnKind;
  }
  ReturnLocation() : SILLocation(ReturnKind) {}
};

/// \brief Used on the instruction that was generated to represent an implicit
/// return from a function.
///
/// Allowed on an BranchInst, ReturnInst, AutoreleaseReturnInst.
class ImplicitReturnLocation : public SILLocation {
public:

  ImplicitReturnLocation(AbstractClosureExpr *E)
    : SILLocation(E, ImplicitReturnKind) { }

  ImplicitReturnLocation(ReturnStmt *S)
  : SILLocation(S, ImplicitReturnKind) { }

  ImplicitReturnLocation(AbstractFunctionDecl *AFD)
    : SILLocation(AFD, ImplicitReturnKind) { }

  /// \brief Construct from a RegularLocation; preserve all special bits.
  ///
  /// Note, this can construct an implicit return for an arbitrary expression
  /// (specifically, in case of auto-generated bodies).
  static SILLocation getImplicitReturnLoc(SILLocation L) {
    assert(L.isASTNode<Expr>() ||
           L.isASTNode<ValueDecl>() ||
           L.isASTNode<PatternBindingDecl>() ||
           (L.isNull() && L.isInTopLevel()));
    L.setKind(ImplicitReturnKind);
    return L;
  }

  AbstractClosureExpr *get() {
    return castToASTNode<AbstractClosureExpr>();
  }

private:
  friend class SILLocation;
  static bool isKind(const SILLocation& L) {
    return L.getKind() == ImplicitReturnKind;
  }
  ImplicitReturnLocation() : SILLocation(ImplicitReturnKind) {}
};

/// \brief Marks instructions that correspond to inlined function body and
/// setup code. This location should not be used for inlined transparent
/// bodies, see MandatoryInlinedLocation.
///
/// This location wraps the call site ASTNode.
///
/// Allowed on any instruction except for ReturnInst, AutoreleaseReturnInst.
class InlinedLocation : public SILLocation {
public:
  InlinedLocation(Expr *CallSite) : SILLocation(CallSite, InlinedKind) {}
  InlinedLocation(Stmt *S) : SILLocation(S, InlinedKind) {}
  InlinedLocation(Pattern *P) : SILLocation(P, InlinedKind) {}
  InlinedLocation(Decl *D) : SILLocation(D, InlinedKind) {}

  /// Constructs an inlined location when the call site is represented by a
  /// SILFile location.
  InlinedLocation(SourceLoc L) : SILLocation(InlinedKind) {
    SILFileSourceLoc = L;
  }

  /// \brief If this location represents a SIL file location, returns the source
  /// location.
  SourceLoc getFileLocation() {
    assert(ASTNode.isNull());
    return SILFileSourceLoc;
  }

  static InlinedLocation getInlinedLocation(SILLocation L);

private:
  friend class SILLocation;
  static bool isKind(const SILLocation& L) {
    return L.getKind() == InlinedKind;
  }
  InlinedLocation() : SILLocation(InlinedKind) {}

  InlinedLocation(Expr *E, unsigned F) : SILLocation(E, InlinedKind, F) {}
  InlinedLocation(Stmt *S, unsigned F) : SILLocation(S, InlinedKind, F) {}
  InlinedLocation(Pattern *P, unsigned F) : SILLocation(P, InlinedKind, F) {}
  InlinedLocation(Decl *D, unsigned F) : SILLocation(D, InlinedKind, F) {}
  InlinedLocation(SourceLoc L, unsigned F) : SILLocation(InlinedKind, F) {
    SILFileSourceLoc = L;
  }

};

/// \brief Marks instructions that correspond to inlined function body and
/// setup code for transparent functions, inlined as part of mandatory inlining
/// pass.
///
/// This location wraps the call site ASTNode.
///
/// Allowed on any instruction except for ReturnInst, AutoreleaseReturnInst.
class MandatoryInlinedLocation : public SILLocation {
public:
  MandatoryInlinedLocation(Expr *CallSite) :
    SILLocation(CallSite, MandatoryInlinedKind) {}
  MandatoryInlinedLocation(Stmt *S) : SILLocation(S, MandatoryInlinedKind) {}
  MandatoryInlinedLocation(Pattern *P) : SILLocation(P, MandatoryInlinedKind) {}
  MandatoryInlinedLocation(Decl *D) : SILLocation(D, MandatoryInlinedKind) {}

  /// Constructs an inlined location when the call site is represented by a
  /// SILFile location.
  MandatoryInlinedLocation(SourceLoc L) : SILLocation(MandatoryInlinedKind) {
    SILFileSourceLoc = L;
  }

  /// \brief If this location represents a SIL file location, returns the source
  /// location.
  SourceLoc getFileLocation() {
    assert(ASTNode.isNull());
    return SILFileSourceLoc;
  }

  static MandatoryInlinedLocation getMandatoryInlinedLocation(SILLocation L);

private:
  friend class SILLocation;
  static bool isKind(const SILLocation& L) {
    return L.getKind() == MandatoryInlinedKind;
  }
  MandatoryInlinedLocation() : SILLocation(MandatoryInlinedKind) {}
  MandatoryInlinedLocation(Expr *E,
                           unsigned F) : SILLocation(E, MandatoryInlinedKind,
                                                     F) {}
  MandatoryInlinedLocation(Stmt *S,
                           unsigned F) : SILLocation(S, MandatoryInlinedKind,
                                                     F) {}
  MandatoryInlinedLocation(Pattern *P,
                           unsigned F) : SILLocation(P, MandatoryInlinedKind,
                                                     F) {}
  MandatoryInlinedLocation(Decl *D,
                           unsigned F) : SILLocation(D, MandatoryInlinedKind,
                                                     F) {}
  MandatoryInlinedLocation(SourceLoc L,
                           unsigned F) : SILLocation(MandatoryInlinedKind, F) {
    SILFileSourceLoc = L;
  }
};

/// \brief Used on the instruction performing auto-generated cleanup such as
/// deallocs, destructor calls.
///
/// The cleanups are performed after completing the evaluation of the AST Node
/// wrapped inside the SILLocation. This location wraps the statement
/// representing the enclosing scope, for example, FuncDecl, ParenExpr. The
/// scope's end location points to the SourceLoc that shows when the operation
/// is performed at runtime.
///
/// Allowed on any instruction except for ReturnInst, AutoreleaseReturnInst.
/// Locations of an inlined destructor should also be represented by this.
class CleanupLocation : public SILLocation {
public:
  CleanupLocation(Expr *E) : SILLocation(E, CleanupKind) {}
  CleanupLocation(Stmt *S) : SILLocation(S, CleanupKind) {}
  CleanupLocation(Pattern *P) : SILLocation(P, CleanupKind) {}
  CleanupLocation(Decl *D) : SILLocation(D, CleanupKind) {}

  static CleanupLocation getCleanupLocation(SILLocation L);

  /// \brief Returns a location representing a cleanup on the module level.
  static CleanupLocation getModuleCleanupLocation() {
    CleanupLocation Loc;
    Loc.markAsInTopLevel();
    return Loc;
  }

private:
  friend class SILLocation;
  static bool isKind(const SILLocation& L) {
    return L.getKind() == CleanupKind;
  }
  CleanupLocation() : SILLocation(CleanupKind) {}

  CleanupLocation(Expr *E, unsigned F) : SILLocation(E, CleanupKind, F) {}
  CleanupLocation(Stmt *S, unsigned F) : SILLocation(S, CleanupKind, F) {}
  CleanupLocation(Pattern *P, unsigned F) : SILLocation(P, CleanupKind, F) {}
  CleanupLocation(Decl *D, unsigned F) : SILLocation(D, CleanupKind, F) {}
};

/// \brief Used to represent an unreachable location that was
/// auto-generated and has no correspondance to user code. It should
/// not be used in diagnostics or for debugging.
///
/// Differentiates an unreachable instruction, which is generated by
/// DCE, from an unreachable instruction in user code (output of SILGen).
/// Allowed on an unreachable instruction.
class ArtificialUnreachableLocation : public SILLocation {
public:
  ArtificialUnreachableLocation() : SILLocation(ArtificialUnreachableKind) {}

private:
  friend class SILLocation;
  static bool isKind(const SILLocation& L) {
    return (L.getKind() == ArtificialUnreachableKind);
  }
};

/// \brief Used to represent locations coming from a parsed SIL file.
///
/// Allowed on any SILInstruction.
class SILFileLocation : public SILLocation {
public:
  SILFileLocation(SourceLoc L) : SILLocation(SILFileKind) {
    SILFileSourceLoc = L;
  }

  SourceLoc getFileLocation() {
    return SILFileSourceLoc;
  }

private:
  friend class SILLocation;
  static bool isKind(const SILLocation& L) {
    return L.getKind() == SILFileKind;
  }
  SILFileLocation() : SILLocation(SILFileKind) {}
};

} // end swift namespace


#endif
