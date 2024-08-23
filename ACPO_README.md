# The AI-Enabled Continuous Program Optimization Infrastructure for LLVM

Welcome to the ACPO Extension to LLVM!

LLVM is the most popular open-source compiler framework, with massive community
involvement. With the addition of ACPO framework, we are providing tools
to include ML models for decision making, example models for some passes and
a wide range of feature collectors to make performance-oriented compiler
development easier to perform.

The changes provided comprise LLVM-side changes to enable the use of ML models
in the LLVM compiler, which are meant to be used in tandem with CPLLab-Huawei/ACPO
repository that comprises models, ML-framework interfaces and data analysis scripts
to make compiler developers more productive.

## Getting the Source Code and Building LLVM

Consult the
[Getting Started with LLVM](https://llvm.org/docs/GettingStarted.html#getting-the-source-code-and-building-llvm)
page for information on building and running LLVM.

For information on how to contribute to the LLVM project, please take a look at
the [Contributing to LLVM](https://llvm.org/docs/Contributing.html) guide.

## Adding ACPO into your flow

With the changes provided the LLVM-side of the compiler is already included. To make it work,
you will need to include the CPLLab-Huawei/ACPO added to your build to ensure interface with
ML frameworks and models can be developed, stored and deployed.

Here are the steps to build and test LLVM with ACPO:
1. Build LLVM with ENABLE_ACPO macro turned on.
2. Include a repo https://github.com/Huawei-CPLLab/ACPO to obtain models and model training infrastructure.
3. Test the flow on existing ACPO-enabled passes, such as inliner to ensure your set up is correct.
4. Begin adding ACPO to passes of your choice.

