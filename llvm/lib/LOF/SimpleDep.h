#ifndef LLVM_LOF_SIMPLEDEP_H
#define LLVM_LOF_SIMPLEDEP_H

#include "llvm/LOF/Green.h"
#include "Dep.h"

  namespace lof {
    class SimpleDep : public Dep {
    private:
    public:
    }; // class SimpleDep


    class SimpleDepAnalysis {
    private:
      Green* Stmt;
    public:
      SimpleDepAnalysis(Green *Stmt): Stmt(Stmt) {}

      void analyze() {}

    }; // class SimpleDepAnalysis


  } // namespace lof
#endif /* LLVM_LOF_SIMPLEDEP_H */
