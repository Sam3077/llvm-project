//===- unittest/Tooling/TransformerTest.cpp -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/Transformer/Transformer.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Tooling/Transformer/RangeSelector.h"
#include "clang/Tooling/Transformer/Stencil.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace clang;
using namespace tooling;
using namespace ast_matchers;
namespace {
using ::testing::IsEmpty;
using transformer::cat;
using transformer::changeTo;
using transformer::RewriteRule;

constexpr char KHeaderContents[] = R"cc(
  struct string {
    string(const char*);
    char* c_str();
    int size();
  };
  int strlen(const char*);

  namespace proto {
  struct PCFProto {
    int foo();
  };
  struct ProtoCommandLineFlag : PCFProto {
    PCFProto& GetProto();
  };
  }  // namespace proto
  class Logger {};
  void operator<<(Logger& l, string msg);
  Logger& log(int level);
)cc";

static ast_matchers::internal::Matcher<clang::QualType>
isOrPointsTo(const clang::ast_matchers::DeclarationMatcher &TypeMatcher) {
  return anyOf(hasDeclaration(TypeMatcher), pointsTo(TypeMatcher));
}

static std::string format(StringRef Code) {
  const std::vector<Range> Ranges(1, Range(0, Code.size()));
  auto Style = format::getLLVMStyle();
  const auto Replacements = format::reformat(Style, Code, Ranges);
  auto Formatted = applyAllReplacements(Code, Replacements);
  if (!Formatted) {
    ADD_FAILURE() << "Could not format code: "
                  << llvm::toString(Formatted.takeError());
    return std::string();
  }
  return *Formatted;
}

static void compareSnippets(StringRef Expected,
                     const llvm::Optional<std::string> &MaybeActual) {
  ASSERT_TRUE(MaybeActual) << "Rewrite failed. Expecting: " << Expected;
  auto Actual = *MaybeActual;
  std::string HL = "#include \"header.h\"\n";
  auto I = Actual.find(HL);
  if (I != std::string::npos)
    Actual.erase(I, HL.size());
  EXPECT_EQ(format(Expected), format(Actual));
}

// FIXME: consider separating this class into its own file(s).
class ClangRefactoringTestBase : public testing::Test {
protected:
  void appendToHeader(StringRef S) { FileContents[0].second += S; }

  void addFile(StringRef Filename, StringRef Content) {
    FileContents.emplace_back(Filename, Content);
  }

  llvm::Optional<std::string> rewrite(StringRef Input) {
    std::string Code = ("#include \"header.h\"\n" + Input).str();
    auto Factory = newFrontendActionFactory(&MatchFinder);
    if (!runToolOnCodeWithArgs(
            Factory->create(), Code, std::vector<std::string>(), "input.cc",
            "clang-tool", std::make_shared<PCHContainerOperations>(),
            FileContents)) {
      llvm::errs() << "Running tool failed.\n";
      return None;
    }
    if (ErrorCount != 0) {
      llvm::errs() << "Generating changes failed.\n";
      return None;
    }
    auto ChangedCode =
        applyAtomicChanges("input.cc", Code, Changes, ApplyChangesSpec());
    if (!ChangedCode) {
      llvm::errs() << "Applying changes failed: "
                   << llvm::toString(ChangedCode.takeError()) << "\n";
      return None;
    }
    return *ChangedCode;
  }

  Transformer::ChangeConsumer consumer() {
    return [this](Expected<AtomicChange> C) {
      if (C) {
        Changes.push_back(std::move(*C));
      } else {
        consumeError(C.takeError());
        ++ErrorCount;
      }
    };
  }

  template <typename R>
  void testRule(R Rule, StringRef Input, StringRef Expected) {
    Transformer T(std::move(Rule), consumer());
    T.registerMatchers(&MatchFinder);
    compareSnippets(Expected, rewrite(Input));
  }

  clang::ast_matchers::MatchFinder MatchFinder;
  // Records whether any errors occurred in individual changes.
  int ErrorCount = 0;
  AtomicChanges Changes;

private:
  FileContentMappings FileContents = {{"header.h", ""}};
};

class TransformerTest : public ClangRefactoringTestBase {
protected:
  TransformerTest() { appendToHeader(KHeaderContents); }
};

// Given string s, change strlen($s.c_str()) to REPLACED.
static RewriteRule ruleStrlenSize() {
  StringRef StringExpr = "strexpr";
  auto StringType = namedDecl(hasAnyName("::basic_string", "::string"));
  auto R = makeRule(
      callExpr(callee(functionDecl(hasName("strlen"))),
               hasArgument(0, cxxMemberCallExpr(
                                  on(expr(hasType(isOrPointsTo(StringType)))
                                         .bind(StringExpr)),
                                  callee(cxxMethodDecl(hasName("c_str")))))),
      changeTo(cat("REPLACED")), cat("Use size() method directly on string."));
  return R;
}

TEST_F(TransformerTest, StrlenSize) {
  std::string Input = "int f(string s) { return strlen(s.c_str()); }";
  std::string Expected = "int f(string s) { return REPLACED; }";
  testRule(ruleStrlenSize(), Input, Expected);
}

// Tests that no change is applied when a match is not expected.
TEST_F(TransformerTest, NoMatch) {
  std::string Input = "int f(string s) { return s.size(); }";
  testRule(ruleStrlenSize(), Input, Input);
}

// Tests replacing an expression.
TEST_F(TransformerTest, Flag) {
  StringRef Flag = "flag";
  RewriteRule Rule = makeRule(
      cxxMemberCallExpr(on(expr(hasType(cxxRecordDecl(
                                    hasName("proto::ProtoCommandLineFlag"))))
                               .bind(Flag)),
                        unless(callee(cxxMethodDecl(hasName("GetProto"))))),
      changeTo(node(Flag), cat("EXPR")));

  std::string Input = R"cc(
    proto::ProtoCommandLineFlag flag;
    int x = flag.foo();
    int y = flag.GetProto().foo();
  )cc";
  std::string Expected = R"cc(
    proto::ProtoCommandLineFlag flag;
    int x = EXPR.foo();
    int y = flag.GetProto().foo();
  )cc";

  testRule(std::move(Rule), Input, Expected);
}

TEST_F(TransformerTest, AddIncludeQuoted) {
  RewriteRule Rule = makeRule(callExpr(callee(functionDecl(hasName("f")))),
                              changeTo(cat("other()")));
  addInclude(Rule, "clang/OtherLib.h");

  std::string Input = R"cc(
    int f(int x);
    int h(int x) { return f(x); }
  )cc";
  std::string Expected = R"cc(#include "clang/OtherLib.h"

    int f(int x);
    int h(int x) { return other(); }
  )cc";

  testRule(Rule, Input, Expected);
}

TEST_F(TransformerTest, AddIncludeAngled) {
  RewriteRule Rule = makeRule(callExpr(callee(functionDecl(hasName("f")))),
                              changeTo(cat("other()")));
  addInclude(Rule, "clang/OtherLib.h", transformer::IncludeFormat::Angled);

  std::string Input = R"cc(
    int f(int x);
    int h(int x) { return f(x); }
  )cc";
  std::string Expected = R"cc(#include <clang/OtherLib.h>

    int f(int x);
    int h(int x) { return other(); }
  )cc";

  testRule(Rule, Input, Expected);
}

TEST_F(TransformerTest, NodePartNameNamedDecl) {
  StringRef Fun = "fun";
  RewriteRule Rule = makeRule(functionDecl(hasName("bad")).bind(Fun),
                              changeTo(name(Fun), cat("good")));

  std::string Input = R"cc(
    int bad(int x);
    int bad(int x) { return x * x; }
  )cc";
  std::string Expected = R"cc(
    int good(int x);
    int good(int x) { return x * x; }
  )cc";

  testRule(Rule, Input, Expected);
}

TEST_F(TransformerTest, NodePartNameDeclRef) {
  std::string Input = R"cc(
    template <typename T>
    T bad(T x) {
      return x;
    }
    int neutral(int x) { return bad<int>(x) * x; }
  )cc";
  std::string Expected = R"cc(
    template <typename T>
    T bad(T x) {
      return x;
    }
    int neutral(int x) { return good<int>(x) * x; }
  )cc";

  StringRef Ref = "ref";
  testRule(makeRule(declRefExpr(to(functionDecl(hasName("bad")))).bind(Ref),
                    changeTo(name(Ref), cat("good"))),
           Input, Expected);
}

TEST_F(TransformerTest, NodePartNameDeclRefFailure) {
  std::string Input = R"cc(
    struct Y {
      int operator*();
    };
    int neutral(int x) {
      Y y;
      int (Y::*ptr)() = &Y::operator*;
      return *y + x;
    }
  )cc";

  StringRef Ref = "ref";
  Transformer T(makeRule(declRefExpr(to(functionDecl())).bind(Ref),
                         changeTo(name(Ref), cat("good"))),
                consumer());
  T.registerMatchers(&MatchFinder);
  EXPECT_FALSE(rewrite(Input));
}

TEST_F(TransformerTest, NodePartMember) {
  StringRef E = "expr";
  RewriteRule Rule = makeRule(memberExpr(member(hasName("bad"))).bind(E),
                              changeTo(member(E), cat("good")));

  std::string Input = R"cc(
    struct S {
      int bad;
    };
    int g() {
      S s;
      return s.bad;
    }
  )cc";
  std::string Expected = R"cc(
    struct S {
      int bad;
    };
    int g() {
      S s;
      return s.good;
    }
  )cc";

  testRule(Rule, Input, Expected);
}

TEST_F(TransformerTest, NodePartMemberQualified) {
  std::string Input = R"cc(
    struct S {
      int bad;
      int good;
    };
    struct T : public S {
      int bad;
    };
    int g() {
      T t;
      return t.S::bad;
    }
  )cc";
  std::string Expected = R"cc(
    struct S {
      int bad;
      int good;
    };
    struct T : public S {
      int bad;
    };
    int g() {
      T t;
      return t.S::good;
    }
  )cc";

  StringRef E = "expr";
  testRule(makeRule(memberExpr().bind(E), changeTo(member(E), cat("good"))),
           Input, Expected);
}

TEST_F(TransformerTest, NodePartMemberMultiToken) {
  std::string Input = R"cc(
    struct Y {
      int operator*();
      int good();
      template <typename T> void foo(T t);
    };
    int neutral(int x) {
      Y y;
      y.template foo<int>(3);
      return y.operator *();
    }
  )cc";
  std::string Expected = R"cc(
    struct Y {
      int operator*();
      int good();
      template <typename T> void foo(T t);
    };
    int neutral(int x) {
      Y y;
      y.template good<int>(3);
      return y.good();
    }
  )cc";

  StringRef MemExpr = "member";
  testRule(makeRule(memberExpr().bind(MemExpr),
                    changeTo(member(MemExpr), cat("good"))),
           Input, Expected);
}

TEST_F(TransformerTest, InsertBeforeEdit) {
  std::string Input = R"cc(
    int f() {
      return 7;
    }
  )cc";
  std::string Expected = R"cc(
    int f() {
      int y = 3;
      return 7;
    }
  )cc";

  StringRef Ret = "return";
  testRule(makeRule(returnStmt().bind(Ret),
                    insertBefore(statement(Ret), cat("int y = 3;"))),
           Input, Expected);
}

TEST_F(TransformerTest, InsertAfterEdit) {
  std::string Input = R"cc(
    int f() {
      int x = 5;
      return 7;
    }
  )cc";
  std::string Expected = R"cc(
    int f() {
      int x = 5;
      int y = 3;
      return 7;
    }
  )cc";

  StringRef Decl = "decl";
  testRule(makeRule(declStmt().bind(Decl),
                    insertAfter(statement(Decl), cat("int y = 3;"))),
           Input, Expected);
}

TEST_F(TransformerTest, RemoveEdit) {
  std::string Input = R"cc(
    int f() {
      int x = 5;
      return 7;
    }
  )cc";
  std::string Expected = R"cc(
    int f() {
      return 7;
    }
  )cc";

  StringRef Decl = "decl";
  testRule(makeRule(declStmt().bind(Decl), remove(statement(Decl))), Input,
           Expected);
}

TEST_F(TransformerTest, MultiChange) {
  std::string Input = R"cc(
    void foo() {
      if (10 > 1.0)
        log(1) << "oh no!";
      else
        log(0) << "ok";
    }
  )cc";
  std::string Expected = R"(
    void foo() {
      if (true) { /* then */ }
      else { /* else */ }
    }
  )";

  StringRef C = "C", T = "T", E = "E";
  testRule(makeRule(ifStmt(hasCondition(expr().bind(C)),
                           hasThen(stmt().bind(T)), hasElse(stmt().bind(E))),
                    {changeTo(node(C), cat("true")),
                     changeTo(statement(T), cat("{ /* then */ }")),
                     changeTo(statement(E), cat("{ /* else */ }"))}),
           Input, Expected);
}

TEST_F(TransformerTest, OrderedRuleUnrelated) {
  StringRef Flag = "flag";
  RewriteRule FlagRule = makeRule(
      cxxMemberCallExpr(on(expr(hasType(cxxRecordDecl(
                                    hasName("proto::ProtoCommandLineFlag"))))
                               .bind(Flag)),
                        unless(callee(cxxMethodDecl(hasName("GetProto"))))),
      changeTo(node(Flag), cat("PROTO")));

  std::string Input = R"cc(
    proto::ProtoCommandLineFlag flag;
    int x = flag.foo();
    int y = flag.GetProto().foo();
    int f(string s) { return strlen(s.c_str()); }
  )cc";
  std::string Expected = R"cc(
    proto::ProtoCommandLineFlag flag;
    int x = PROTO.foo();
    int y = flag.GetProto().foo();
    int f(string s) { return REPLACED; }
  )cc";

  testRule(applyFirst({ruleStrlenSize(), FlagRule}), Input, Expected);
}

TEST_F(TransformerTest, OrderedRuleRelated) {
  std::string Input = R"cc(
    void f1();
    void f2();
    void call_f1() { f1(); }
    void call_f2() { f2(); }
  )cc";
  std::string Expected = R"cc(
    void f1();
    void f2();
    void call_f1() { REPLACE_F1; }
    void call_f2() { REPLACE_F1_OR_F2; }
  )cc";

  RewriteRule ReplaceF1 =
      makeRule(callExpr(callee(functionDecl(hasName("f1")))),
               changeTo(cat("REPLACE_F1")));
  RewriteRule ReplaceF1OrF2 =
      makeRule(callExpr(callee(functionDecl(hasAnyName("f1", "f2")))),
               changeTo(cat("REPLACE_F1_OR_F2")));
  testRule(applyFirst({ReplaceF1, ReplaceF1OrF2}), Input, Expected);
}

// Change the order of the rules to get a different result. When `ReplaceF1OrF2`
// comes first, it applies for both uses, so `ReplaceF1` never applies.
TEST_F(TransformerTest, OrderedRuleRelatedSwapped) {
  std::string Input = R"cc(
    void f1();
    void f2();
    void call_f1() { f1(); }
    void call_f2() { f2(); }
  )cc";
  std::string Expected = R"cc(
    void f1();
    void f2();
    void call_f1() { REPLACE_F1_OR_F2; }
    void call_f2() { REPLACE_F1_OR_F2; }
  )cc";

  RewriteRule ReplaceF1 =
      makeRule(callExpr(callee(functionDecl(hasName("f1")))),
               changeTo(cat("REPLACE_F1")));
  RewriteRule ReplaceF1OrF2 =
      makeRule(callExpr(callee(functionDecl(hasAnyName("f1", "f2")))),
               changeTo(cat("REPLACE_F1_OR_F2")));
  testRule(applyFirst({ReplaceF1OrF2, ReplaceF1}), Input, Expected);
}

// Verify that a set of rules whose matchers have different base kinds works
// properly, including that `applyFirst` produces multiple matchers.  We test
// two different kinds of rules: Expr and Decl. We place the Decl rule in the
// middle to test that `buildMatchers` works even when the kinds aren't grouped
// together.
TEST_F(TransformerTest, OrderedRuleMultipleKinds) {
  std::string Input = R"cc(
    void f1();
    void f2();
    void call_f1() { f1(); }
    void call_f2() { f2(); }
  )cc";
  std::string Expected = R"cc(
    void f1();
    void DECL_RULE();
    void call_f1() { REPLACE_F1; }
    void call_f2() { REPLACE_F1_OR_F2; }
  )cc";

  RewriteRule ReplaceF1 =
      makeRule(callExpr(callee(functionDecl(hasName("f1")))),
               changeTo(cat("REPLACE_F1")));
  RewriteRule ReplaceF1OrF2 =
      makeRule(callExpr(callee(functionDecl(hasAnyName("f1", "f2")))),
               changeTo(cat("REPLACE_F1_OR_F2")));
  RewriteRule DeclRule = makeRule(functionDecl(hasName("f2")).bind("fun"),
                                  changeTo(name("fun"), cat("DECL_RULE")));

  RewriteRule Rule = applyFirst({ReplaceF1, DeclRule, ReplaceF1OrF2});
  EXPECT_EQ(transformer::detail::buildMatchers(Rule).size(), 2UL);
  testRule(Rule, Input, Expected);
}

//
// Negative tests (where we expect no transformation to occur).
//

// Tests for a conflict in edits from a single match for a rule.
TEST_F(TransformerTest, TextGeneratorFailure) {
  std::string Input = "int conflictOneRule() { return 3 + 7; }";
  // Try to change the whole binary-operator expression AND one its operands:
  StringRef O = "O";
  auto AlwaysFail = [](const ast_matchers::MatchFinder::MatchResult &)
      -> llvm::Expected<std::string> {
    return llvm::createStringError(llvm::errc::invalid_argument, "ERROR");
  };
  Transformer T(
      makeRule(binaryOperator().bind(O), changeTo(node(O), AlwaysFail)),
      consumer());
  T.registerMatchers(&MatchFinder);
  EXPECT_FALSE(rewrite(Input));
  EXPECT_THAT(Changes, IsEmpty());
  EXPECT_EQ(ErrorCount, 1);
}

// Tests for a conflict in edits from a single match for a rule.
TEST_F(TransformerTest, OverlappingEditsInRule) {
  std::string Input = "int conflictOneRule() { return 3 + 7; }";
  // Try to change the whole binary-operator expression AND one its operands:
  StringRef O = "O", L = "L";
  Transformer T(makeRule(binaryOperator(hasLHS(expr().bind(L))).bind(O),
                         {changeTo(node(O), cat("DELETE_OP")),
                          changeTo(node(L), cat("DELETE_LHS"))}),
                consumer());
  T.registerMatchers(&MatchFinder);
  EXPECT_FALSE(rewrite(Input));
  EXPECT_THAT(Changes, IsEmpty());
  EXPECT_EQ(ErrorCount, 1);
}

// Tests for a conflict in edits across multiple matches (of the same rule).
TEST_F(TransformerTest, OverlappingEditsMultipleMatches) {
  std::string Input = "int conflictOneRule() { return -7; }";
  // Try to change the whole binary-operator expression AND one its operands:
  StringRef E = "E";
  Transformer T(makeRule(expr().bind(E), changeTo(node(E), cat("DELETE_EXPR"))),
                consumer());
  T.registerMatchers(&MatchFinder);
  // The rewrite process fails because the changes conflict with each other...
  EXPECT_FALSE(rewrite(Input));
  // ... but two changes were produced.
  EXPECT_EQ(Changes.size(), 2u);
  EXPECT_EQ(ErrorCount, 0);
}

TEST_F(TransformerTest, ErrorOccurredMatchSkipped) {
  // Syntax error in the function body:
  std::string Input = "void errorOccurred() { 3 }";
  Transformer T(makeRule(functionDecl(hasName("errorOccurred")),
                         changeTo(cat("DELETED;"))),
                consumer());
  T.registerMatchers(&MatchFinder);
  // The rewrite process itself fails...
  EXPECT_FALSE(rewrite(Input));
  // ... and no changes or errors are produced in the process.
  EXPECT_THAT(Changes, IsEmpty());
  EXPECT_EQ(ErrorCount, 0);
}

// Transformation of macro source text when the change encompasses the entirety
// of the expanded text.
TEST_F(TransformerTest, SimpleMacro) {
  std::string Input = R"cc(
#define ZERO 0
    int f(string s) { return ZERO; }
  )cc";
  std::string Expected = R"cc(
#define ZERO 0
    int f(string s) { return 999; }
  )cc";

  StringRef zero = "zero";
  RewriteRule R = makeRule(integerLiteral(equals(0)).bind(zero),
                           changeTo(node(zero), cat("999")));
  testRule(R, Input, Expected);
}

// Transformation of macro source text when the change encompasses the entirety
// of the expanded text, for the case of function-style macros.
TEST_F(TransformerTest, FunctionMacro) {
  std::string Input = R"cc(
#define MACRO(str) strlen((str).c_str())
    int f(string s) { return MACRO(s); }
  )cc";
  std::string Expected = R"cc(
#define MACRO(str) strlen((str).c_str())
    int f(string s) { return REPLACED; }
  )cc";

  testRule(ruleStrlenSize(), Input, Expected);
}

// Tests that expressions in macro arguments can be rewritten.
TEST_F(TransformerTest, MacroArg) {
  std::string Input = R"cc(
#define PLUS(e) e + 1
    int f(string s) { return PLUS(strlen(s.c_str())); }
  )cc";
  std::string Expected = R"cc(
#define PLUS(e) e + 1
    int f(string s) { return PLUS(REPLACED); }
  )cc";

  testRule(ruleStrlenSize(), Input, Expected);
}

// Tests that expressions in macro arguments can be rewritten, even when the
// macro call occurs inside another macro's definition.
TEST_F(TransformerTest, MacroArgInMacroDef) {
  std::string Input = R"cc(
#define NESTED(e) e
#define MACRO(str) NESTED(strlen((str).c_str()))
    int f(string s) { return MACRO(s); }
  )cc";
  std::string Expected = R"cc(
#define NESTED(e) e
#define MACRO(str) NESTED(strlen((str).c_str()))
    int f(string s) { return REPLACED; }
  )cc";

  testRule(ruleStrlenSize(), Input, Expected);
}

// Tests the corner case of the identity macro, specifically that it is
// discarded in the rewrite rather than preserved (like PLUS is preserved in the
// previous test).  This behavior is of dubious value (and marked with a FIXME
// in the code), but we test it to verify (and demonstrate) how this case is
// handled.
TEST_F(TransformerTest, IdentityMacro) {
  std::string Input = R"cc(
#define ID(e) e
    int f(string s) { return ID(strlen(s.c_str())); }
  )cc";
  std::string Expected = R"cc(
#define ID(e) e
    int f(string s) { return REPLACED; }
  )cc";

  testRule(ruleStrlenSize(), Input, Expected);
}

// Tests that two changes in a single macro expansion do not lead to conflicts
// in applying the changes.
TEST_F(TransformerTest, TwoChangesInOneMacroExpansion) {
  std::string Input = R"cc(
#define PLUS(a,b) (a) + (b)
    int f() { return PLUS(3, 4); }
  )cc";
  std::string Expected = R"cc(
#define PLUS(a,b) (a) + (b)
    int f() { return PLUS(LIT, LIT); }
  )cc";

  testRule(makeRule(integerLiteral(), changeTo(cat("LIT"))), Input, Expected);
}

// Tests case where the rule's match spans both source from the macro and its
// arg, with the begin location (the "anchor") being the arg.
TEST_F(TransformerTest, MatchSpansMacroTextButChangeDoesNot) {
  std::string Input = R"cc(
#define PLUS_ONE(a) a + 1
    int f() { return PLUS_ONE(3); }
  )cc";
  std::string Expected = R"cc(
#define PLUS_ONE(a) a + 1
    int f() { return PLUS_ONE(LIT); }
  )cc";

  StringRef E = "expr";
  testRule(makeRule(binaryOperator(hasLHS(expr().bind(E))),
                    changeTo(node(E), cat("LIT"))),
           Input, Expected);
}

// Tests case where the rule's match spans both source from the macro and its
// arg, with the begin location (the "anchor") being inside the macro.
TEST_F(TransformerTest, MatchSpansMacroTextButChangeDoesNotAnchoredInMacro) {
  std::string Input = R"cc(
#define PLUS_ONE(a) 1 + a
    int f() { return PLUS_ONE(3); }
  )cc";
  std::string Expected = R"cc(
#define PLUS_ONE(a) 1 + a
    int f() { return PLUS_ONE(LIT); }
  )cc";

  StringRef E = "expr";
  testRule(makeRule(binaryOperator(hasRHS(expr().bind(E))),
                    changeTo(node(E), cat("LIT"))),
           Input, Expected);
}

// No rewrite is applied when the changed text does not encompass the entirety
// of the expanded text. That is, the edit would have to be applied to the
// macro's definition to succeed and editing the expansion point would not
// suffice.
TEST_F(TransformerTest, NoPartialRewriteOMacroExpansion) {
  std::string Input = R"cc(
#define ZERO_PLUS 0 + 3
    int f(string s) { return ZERO_PLUS; })cc";

  StringRef zero = "zero";
  RewriteRule R = makeRule(integerLiteral(equals(0)).bind(zero),
                           changeTo(node(zero), cat("0")));
  testRule(R, Input, Input);
}

// This test handles the corner case where a macro expands within another macro
// to matching code, but that code is an argument to the nested macro call.  A
// simple check of isMacroArgExpansion() vs. isMacroBodyExpansion() will get
// this wrong, and transform the code.
TEST_F(TransformerTest, NoPartialRewriteOfMacroExpansionForMacroArgs) {
  std::string Input = R"cc(
#define NESTED(e) e
#define MACRO(str) 1 + NESTED(strlen((str).c_str()))
    int f(string s) { return MACRO(s); }
  )cc";

  testRule(ruleStrlenSize(), Input, Input);
}

#if !defined(NDEBUG) && GTEST_HAS_DEATH_TEST
// Verifies that `Type` and `QualType` are not allowed as top-level matchers in
// rules.
TEST(TransformerDeathTest, OrderedRuleTypes) {
  RewriteRule QualTypeRule = makeRule(qualType(), changeTo(cat("Q")));
  EXPECT_DEATH(transformer::detail::buildMatchers(QualTypeRule),
               "Matcher must be.*node matcher");

  RewriteRule TypeRule = makeRule(arrayType(), changeTo(cat("T")));
  EXPECT_DEATH(transformer::detail::buildMatchers(TypeRule),
               "Matcher must be.*node matcher");
}
#endif
} // namespace
