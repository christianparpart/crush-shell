module;

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <ranges>
#include <string>
#include <utility>
#include <vector>
#include <fmt/core.h>
#include <sys/wait.h>

#include <unistd.h>

#include <llvm/ADT/APFloat.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ExecutionEngine/JITSymbol.h>
#include <llvm/ExecutionEngine/Orc/CompileUtils.h>
#include <llvm/ExecutionEngine/Orc/Core.h>
#include <llvm/ExecutionEngine/Orc/ExecutionUtils.h>
#include <llvm/ExecutionEngine/Orc/ExecutorProcessControl.h>
#include <llvm/ExecutionEngine/Orc/IRCompileLayer.h>
#include <llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h>
#include <llvm/ExecutionEngine/Orc/Shared/ExecutorSymbolDef.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/StandardInstrumentations.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Scalar/GVN.h>
#include <llvm/Transforms/Scalar/Reassociate.h>
#include <llvm/Transforms/Scalar/SimplifyCFG.h>
#include <llvm/Transforms/Utils.h>

using namespace llvm;
using namespace llvm::orc;

export module LLVMBackend;

class TestJIT
{
  private:
    std::unique_ptr<ExecutionSession> ES;

    DataLayout DL;
    MangleAndInterner Mangle;

    RTDyldObjectLinkingLayer ObjectLayer;
    IRCompileLayer CompileLayer;

    JITDylib& MainJD;

  public:
    TestJIT(std::unique_ptr<ExecutionSession> ES, JITTargetMachineBuilder JTMB, DataLayout DL):
        ES(std::move(ES)),
        DL(std::move(DL)),
        Mangle(*this->ES, this->DL),
        ObjectLayer(*this->ES, []() { return std::make_unique<SectionMemoryManager>(); }),
        CompileLayer(*this->ES, ObjectLayer, std::make_unique<ConcurrentIRCompiler>(std::move(JTMB))),
        MainJD(this->ES->createBareJITDylib("<main>"))
    {
        MainJD.addGenerator(
            cantFail(DynamicLibrarySearchGenerator::GetForCurrentProcess(DL.getGlobalPrefix())));
        if (JTMB.getTargetTriple().isOSBinFormatCOFF())
        {
            ObjectLayer.setOverrideObjectFlagsWithResponsibilityFlags(true);
            ObjectLayer.setAutoClaimResponsibilityForObjectSymbols(true);
        }
    }

    ~TestJIT()
    {
        if (auto Err = ES->endSession())
            ES->reportError(std::move(Err));
    }

    static Expected<std::unique_ptr<TestJIT>> Create()
    {
        auto EPC = SelfExecutorProcessControl::Create();
        if (!EPC)
            return EPC.takeError();

        auto ES = std::make_unique<ExecutionSession>(std::move(*EPC));

        JITTargetMachineBuilder JTMB(ES->getExecutorProcessControl().getTargetTriple());

        auto DL = JTMB.getDefaultDataLayoutForTarget();
        if (!DL)
            return DL.takeError();

        return std::make_unique<TestJIT>(std::move(ES), std::move(JTMB), std::move(*DL));
    }

    const DataLayout& getDataLayout() const { return DL; }

    JITDylib& getMainJITDylib() { return MainJD; }

    Error addModule(ThreadSafeModule TSM, ResourceTrackerSP RT = nullptr)
    {
        if (!RT)
            RT = MainJD.getDefaultResourceTracker();
        return CompileLayer.add(RT, std::move(TSM));
    }

    Expected<ExecutorSymbolDef> lookup(StringRef Name) { return ES->lookup({ &MainJD }, Mangle(Name.str())); }
};

std::vector<std::string> tokenizer(const std::string& p_pcstStr, char delim)
{
    std::vector<std::string> tokens;
    std::stringstream mySstream(p_pcstStr);
    std::string temp;

    while (getline(mySstream, temp, delim))
    {
        tokens.push_back(temp);
    }

    return tokens;
}

extern "C" void execute_llvmbackend(const char* prog, const char* params, int stdinFd, int stdoutFd)
{

    std::vector<const char*> argv;
    argv.push_back(prog);
    argv.push_back(params);

    pid_t const pid = fork();

    fmt::print("program to execute {} with args: {} \b", prog, params);
    
    switch (pid)
    {
        case -1: fmt::print("Failed to fork(): {} \n", strerror(errno)); return;
        case 0: {
            // child process
            if (stdinFd != STDIN_FILENO)
                dup2(stdinFd, STDIN_FILENO);
            if (stdoutFd != STDOUT_FILENO)
                dup2(stdoutFd, STDOUT_FILENO);
            execvp(prog, const_cast<char* const*>(argv.data()));
        }
        default: {
            // parent process
            int wstatus = 0;
                waitpid(pid, &wstatus, 0);
                if (WIFSIGNALED(wstatus))
                    fmt::print("child process exited with signal {} \n", WTERMSIG(wstatus));
                else if (WIFEXITED(wstatus))
                    fmt::print("child process exited with code {} \n", WEXITSTATUS(wstatus));
                else if (WIFSTOPPED(wstatus))
                    fmt::print("child process stopped with signal {} \n", WSTOPSIG(wstatus));
                else
                    fmt::print("child process exited with unknown status {} \n", wstatus);
                break;
        }
    }
}

// int jitPart(std::string input)
// {
//     using namespace llvm;
//     using namespace llvm::orc;

//     std::unique_ptr<IRBuilder<>> builder;

//     // Create an LLJIT instance.
//     ExitOnError ExitOnErr;
//     auto J = ExitOnErr(TestJIT::Create());

//     auto Context = std::make_unique<LLVMContext>();

//     builder = std::make_unique<IRBuilder<>>(*Context);

//     auto TheModule = std::make_unique<Module>("test", *Context);
//     TheModule->setDataLayout(J->getDataLayout());

//     auto byteptr = builder->getPtrTy();
//     // llvm::Type::getInt8Ty(*Context)->getPo;
//     auto FunctionType = llvm::FunctionType::get(llvm::Type::getVoidTy(*Context), { byteptr, byteptr },
//     false);

//     auto fun = TheModule->getOrInsertFunction("execute", FunctionType);

//     Function* F = Function::Create(FunctionType, Function::ExternalLinkage, "__anon_expr",
//     TheModule.get());

//     // Add a basic block to the function. As before, it automatically inserts
//     // because of the last argument.
//     BasicBlock* BB = BasicBlock::Create(*Context, "EntryBlock", F);

//     builder->SetInsertPoint(BB);

//     // Get pointers to the constant `1'.

//     const auto [prog, args] = SeparateProg(input);

//     auto CalRes =
//         builder->CreateCall(fun, { builder->CreateGlobalString(prog), builder->CreateGlobalString(args) });

//     // Value *One = builder.getInt32(1);
//     // Value *Add = builder.CreateAdd(One, One);

//     builder->CreateRet(builder->getInt32(1));

//     auto RT = J->getMainJITDylib().createResourceTracker();

//     auto M = ThreadSafeModule(std::move(TheModule), std::move(Context));

//     M.getModuleUnlocked()->print(llvm::outs(), nullptr);

//     ExitOnErr(J->addModule(std::move(M), RT));

//     // Look up the JIT'd function, cast it to a function pointer, then call it.
//     auto ExprSymbol = ExitOnErr(J->lookup("__anon_expr"));

//     void (*FP)() = ExprSymbol.getAddress().toPtr<void (*)()>();
//     FP();

//     return EXIT_SUCCESS;
// }

std::tuple<std::string, std::string> SeparateProg(const std::string input)
{

    std::stringstream inStream(input);
    std::string prog;
    std::string args;
    getline(inStream, prog, ' ');
    getline(inStream, args, '\n');

    return { prog, args };
}

export class LLVMBackend
{
  public:
    LLVMBackend()
    {
        InitializeNativeTarget();
        InitializeNativeTargetAsmPrinter();
        // Create an LLJIT instance.
    }

    auto exec(std::string const& input, const int stdinFd, const int stdoutFd)
    {

        using namespace llvm;
        using namespace llvm::orc;
        std::unique_ptr<IRBuilder<>> builder;

        // Create an LLJIT instance.
        ExitOnError ExitOnErr;
        auto J = ExitOnErr(TestJIT::Create());

        auto Context = std::make_unique<LLVMContext>();

        builder = std::make_unique<IRBuilder<>>(*Context);

        auto TheModule = std::make_unique<Module>("test", *Context);
        TheModule->setDataLayout(J->getDataLayout());

        // Create new pass and analysis managers.
        auto TheFPM = std::make_unique<FunctionPassManager>();
        auto TheLAM = std::make_unique<LoopAnalysisManager>();
        auto TheFAM = std::make_unique<FunctionAnalysisManager>();
        auto TheCGAM = std::make_unique<CGSCCAnalysisManager>();
        auto TheMAM = std::make_unique<ModuleAnalysisManager>();
        auto ThePIC = std::make_unique<PassInstrumentationCallbacks>();
        auto TheSI = std::make_unique<StandardInstrumentations>(*Context, /*DebugLogging*/ true);
        TheSI->registerCallbacks(*ThePIC, TheMAM.get());

        TheFPM->addPass(ReassociatePass());
        TheFPM->addPass(GVNPass());
        TheFPM->addPass(SimplifyCFGPass());

        PassBuilder PB;
        PB.registerModuleAnalyses(*TheMAM);
        PB.registerFunctionAnalyses(*TheFAM);
        PB.crossRegisterProxies(*TheLAM, *TheFAM, *TheCGAM, *TheMAM);

        auto byteptr = builder->getPtrTy();
        auto inttype = builder->getInt64Ty();
        // llvm::Type::getInt8Ty(*Context)->getPo;
        auto FunctionType = llvm::FunctionType::get(
            llvm::Type::getVoidTy(*Context), { byteptr, byteptr, inttype, inttype }, false);

        auto fun = TheModule->getOrInsertFunction("execute_llvmbackend", FunctionType);

        Function* F =
            Function::Create(FunctionType, Function::ExternalLinkage, "__anon_expr", TheModule.get());

        // Add a basic block to the function. As before, it automatically inserts
        // because of the last argument.
        BasicBlock* BB = BasicBlock::Create(*Context, "EntryBlock", F);

        builder->SetInsertPoint(BB);

        // Get pointers to the constant `1'.

        const auto [prog, args] = SeparateProg(input);

        auto CalRes = builder->CreateCall(fun,
                                          { builder->CreateGlobalString(prog),
                                            builder->CreateGlobalString(args),
                                            builder->getInt64(stdinFd),
                                            builder->getInt64(stdoutFd) });

        // Value *One = builder.getInt32(1);
        // Value *Add = builder.CreateAdd(One, One);

        builder->CreateRet(builder->getInt32(1));

        auto RT = J->getMainJITDylib().createResourceTracker();

        auto M = ThreadSafeModule(std::move(TheModule), std::move(Context));

        M.getModuleUnlocked()->print(llvm::outs(), nullptr);

        ExitOnErr(J->addModule(std::move(M), RT));

        // Look up the JIT'd function, cast it to a function pointer, then call it.
        auto ExprSymbol = ExitOnErr(J->lookup("__anon_expr"));

        (ExprSymbol.getAddress().toPtr<void (*)()>())();
    }
};
