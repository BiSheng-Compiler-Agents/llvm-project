# Protean Compiler Framework

[![LLVM](https://img.shields.io/badge/LLVM-v19.1.7-blue)](https://github.com/llvm/llvm-project/releases/tag/llvmorg-19.1.7)

Welcome to the Protean Compiler project!

This repository contains the source code for Protean, a C/C++ compiler based
on [LLVM 19.x](https://github.com/llvm/llvm-project/tree/release/19.x) with
ML-guided phase ordering capabilities.

Protean's ML model, a.k.a. `IR2Score`, can leverage either our own handcrafted
static features, i.e. `Protean Feature Set (PFS)`, defined in
 `llvm/include/llvm/Analysis/ProteanCollectFeatures.h`, or
[IR2Vec](https://github.com/IITH-Compilers/IR2Vec.git) embeddings. Users can
decide which version of the model to use by passing a command-line option when
compiling their projects with Protean:

```
-Wprotean,-use-protean-collect=false // Use IR2Score trained w/ IR2VEC
-Wprotean,-use-protean-collect=true  // Use IR2Score trained w/ PFS
```

## Build Instuctions

IR2Vec is a submodule in this repository, so after cloning the project, you
will need to initialize the submodule:

```sh
git submodule update --init --recursive
```

Once the submodule is also checked out, [build LLVM](README-llvm.md) as you
normally would with CMake. For example:

```sh
mkdir build

cmake -G Ninja -B $PWD/build \
    -DCMAKE_BUILD_TYPE="Release" \
    -DLLVM_ENABLE_PROJECTS="clang;lld" \
    $PWD/llvm

cmake --build $PWD/build -j8
```

The build will produce the `protean` executable as `$PWD/build/bin/protean`.

## Citation

If you use any of the materials in this project, i.e. code, provided models,
methodology, etc., please cite this work:

```
@article{ashouri2026protean,
  title={Protean Compiler: An Agile Framework to Drive Fine-grain Phase Ordering},
  author={Ashouri, Amir H and Bagi, Shayan Shirahmad Gale and Satheeskumar, Kavin and Srikanth, Tejas and Zhao, Jonathan and Saidoun, Ibrahim and Wang, Ziwen and Chan, Bryan and Czajkowski, Tomasz S},
  journal={arXiv preprint arXiv:2602.06142},
  year={2026}
}
```

This work has been accepted by [ACM TACO](https://dl.acm.org/journal/taco) in
June 2026, and is scheduled to be presented at the HiPEAC 2027 conference as
an invited paper. Details will be added here soon.
