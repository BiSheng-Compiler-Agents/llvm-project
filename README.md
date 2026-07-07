# Protean Compiler Framework
[![LLVM](https://img.shields.io/badge/LLVM-v19.1.7-blue)](https://github.com/llvm/llvm-project/releases/tag/llvmorg-19.1.7)


Welcome to the Protean Compiler Repo!

This repository contains the source code for Protean compiler, integrated
into [LLVM 19.x](https://github.com/llvm/llvm-project/tree/release/19.x).

## Build Instuctions

Protean compiler's ML models, aka `IR2Score`, can either leverge our own handcrafted static features, i.e., `Protean Feature Set (PFS)`, defined under `llvm/include/llvm/Analysis/ProteanCollectFeatures.h` or [IR2VEC](https://github.com/IITH-Compilers/IR2Vec.git) embeddings. In this repository, we have provided a working version of both models and users can decide which model to use by toggling a command-line argument when compiling their projects with Protean enabled:

```
-Wprotean,-use-protean-collect=false // Use IR2Score trained w/ IR2VEC
-Wprotean,-use-protean-collect=True  // Use IR2Score trained w/ PFS
```

### Clone IR2VEC

We have provided a patch to take care of the needed changes, so just go ahead and clone us and IR2VEC and apply the patch:

```
git clone https://github.com/Huawei-CPLLab/Protean.git
cd Protean
PROTEAN=$PWD
cd llvm/lib/IR2Vec
git init
git remote add origin https://github.com/IITH-Compilers/IR2Vec.git
git fetch origin llvm19
git checkout -b llvm19 origin/llvm19
git apply ir2vec-llvm19.patch
cd $PROTEAN
```

### Build Protean Compiler (LLVM 19.x)

Once you cloned us and IR2VEC with the above instructions, [build LLVM](README-llvm.md) as you normally would with either cmake or ninja. Here is an example:

```
mkdir build && cd build
cmake -G Ninja -S ../llvm -B . -DCMAKE_BUILD_TYPE="Release"  -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DLLVM_CCACHE_BUILD=OFF  -DLLVM_ENABLE_PROJECTS="clang;lld"
cmake --build . -j8
```

Once the build is done, you can find protean binary under: `$PROTEAN/build/bin/protean`

## Protean Paper

If you use any of the materials in this project, i.e., code, provided models, methodology, etc., you should cite this work:
```
@article{ashouri2026protean,
  title={Protean Compiler: An Agile Framework to Drive Fine-grain Phase Ordering},
  author={Ashouri, Amir H and Bagi, Shayan Shirahmad Gale and Satheeskumar, Kavin and Srikanth, Tejas and Zhao, Jonathan and Saidoun, Ibrahim and Wang, Ziwen and Chan, Bryan and Czajkowski, Tomasz S},
  journal={arXiv preprint arXiv:2602.06142},
  year={2026}
}
```
This work was accepted at [ACM TACO](https://dl.acm.org/journal/taco) in June 2026 and is scheduled to be presented at the HiPEAC 2027 conference as an invited paper. Details will be added here accordingly.

