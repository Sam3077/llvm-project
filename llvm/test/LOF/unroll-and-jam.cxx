#include "compiledtestboilerplate.h"
#include <llvm/LOF/LoopOpt.h>
#include <llvm/LOF/Green.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/AsmParser/Parser.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/IR/DiagnosticPrinter.h>
#include <llvm/LOF/LoopOptPass.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/Dominators.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include "gtest/gtest.h"
using namespace llvm;


static const char* IR = R"IR(
define void @test1(i32 %I, i32 %J, i32* noalias nocapture %A, i32* noalias nocapture readonly %B) #0 {
entry:
  %cmp = icmp ne i32 %J, 0
  %cmpJ = icmp ne i32 %I, 0
  %or.cond = and i1 %cmp, %cmpJ
  br i1 %or.cond, label %for.outer.preheader, label %for.end

for.outer.preheader:
  br label %for.outer

for.outer:
  %i = phi i32 [ %add8, %for.latch ], [ 0, %for.outer.preheader ]
  br label %for.inner

for.inner:
  %j = phi i32 [ 0, %for.outer ], [ %inc, %for.inner ]
  %sum = phi i32 [ 0, %for.outer ], [ %add, %for.inner ]
  %arrayidx = getelementptr inbounds i32, i32* %B, i32 %j
  %0 = load i32, i32* %arrayidx, align 4, !tbaa !5
  %add = add i32 %0, %sum
  %inc = add nuw i32 %j, 1
  %exitcond = icmp eq i32 %inc, %J
  br i1 %exitcond, label %for.latch, label %for.inner

for.latch:
  %add.lcssa = phi i32 [ %add, %for.inner ]
  %arrayidx6 = getelementptr inbounds i32, i32* %A, i32 %i
  store i32 %add.lcssa, i32* %arrayidx6, align 4, !tbaa !5
  %add8 = add nuw i32 %i, 1
  %exitcond25 = icmp eq i32 %add8, %I
  br i1 %exitcond25, label %for.end.loopexit, label %for.outer

for.end.loopexit:
  br label %for.end

for.end:
  ret void
}


!5 = !{!6, !6, i64 0}
!6 = !{!"int", !7, i64 0}
!7 = !{!"omnipotent char", !8, i64 0}
!8 = !{!"Simple C/C++ TBAA"}
!9 = !{!10, !10, i64 0}
!10 = !{!"short", !7, i64 0}
)IR";





static std::unique_ptr<Module> parseAssembly(const char *Assembly, LLVMContext &Context) {
 llvm::SMDiagnostic Error;
  std::unique_ptr<Module> M = parseAssemblyString(Assembly, Error, Context);

  if (!M) {
    std::string ErrMsg;
    raw_string_ostream OS(ErrMsg);
    Error.print("", OS);
    report_fatal_error(OS.str());
  }

  assert(M && !verifyModule(*M, &errs()));
  return M;
}



TEST(UnrollAndJamTest, LOFConversion) {
  LLVMContext Context;

  auto M = parseAssembly(IR, Context);

#if 0
  //LoopInfoWrapperPass LIWrap{};
  //LIWrap.runOnFunction();

  for (auto &F : M->functions()) {
    DominatorTree DT{};
    DT.recalculate(F);

    LoopInfo LI{  };
    LI.analyze(DT);

    ScalarEvolution SE{};

    / LoopOptimizer *createLoopOptimizer(llvm::Function *Func,llvm:: LoopInfo *LI, llvm::ScalarEvolution *SE);
  }
#endif



  std::vector<std::unique_ptr<lof::LoopOptimizer>> LOFs;

  auto MPM = new llvm::legacy::PassManager();
  MPM->add(createLoopFrameworkAnalyzerPass(LOFs));
  MPM->run(*M.get());

  for (auto &LOF : LOFs) {
    auto Root = LOF->getOriginalRoot();
    Root->dump();

    auto Normalized = LOF->normalize(Root);
    Normalized->dump();
  }


#if 0
  LLVMContext Context;
  std::unique_ptr<Module> M = getModule(Context, __FILE__, "IR");
  //auto *F = M->getFunction("test1");

  auto  MPM = new llvm::legacy::PassManager();
  MPM->add(createLoopOptimizationFrameworkPass());

  //MPM->doInitialization();
  MPM->run(*M.get());
  //MPM->doFinalization();
#endif
}



