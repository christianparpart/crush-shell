// SPDX-License-Identifier: Apache-2.0
module;
#include <shell/AST.h>
#include <shell/ScopedLogger.h>

#include <typeinfo>

// {{{ trace macros
// clang-format off
#if 0 // defined(TRACE_PARSER)
    #define TRACE_SCOPE(message) ScopedLogger _logger { message }
    #define TRACE(message, ...) do { ScopedLogger::write(::fmt::format(message, __VA_ARGS__)); } while (0)
#else
    #define TRACE_SCOPE(message) do {} while (0)
    #define TRACE(message, ...) do {} while (0)
#endif
// clang-format on
// }}}

import CoreVM;
import ASTPrinter;

export module IRGenerator;

#define GLOBAL_SCOPE_INIT_NAME "@main"

namespace endo
{

export class IRGenerator final: public CoreVM::IRBuilder, public ast::Visitor
{
  public:
    static CoreVM::IRProgram* generate(ast::Statement const& rootNode)
    {
        IRGenerator generator;

        generator.setProgram(std::make_unique<CoreVM::IRProgram>());
        generator.setHandler(generator.getHandler(GLOBAL_SCOPE_INIT_NAME));
        generator.setInsertPoint(generator.createBlock("EntryPoint"));
        generator.codegen(&rootNode);
        generator.createRet(generator.get(CoreVM::CoreNumber(0)));

        return generator.program();
    }

  private:
    IRGenerator()
    {
        _processCallSignature.setReturnType(CoreVM::LiteralType::Number);
        _processCallSignature.setName("ProcessCall");
    }

    CoreVM::Value* codegen(ast::Node const* node)
    {
        TRACE_SCOPE(fmt::format("codegen({})", node ? typeid(*node).name() : "nullptr"));
        _result = nullptr;
        if (node)
            node->accept(*this);
        return _result;
    }

    void visit(ast::BuiltinExitStmt const& node) override
    {
        CoreVM::Value* exitCode = nullptr;
        if (!node.code)
            exitCode = get(CoreVM::CoreNumber(0));
        else
        {
            exitCode = codegen(node.code.get());
            if (exitCode->type() == CoreVM::LiteralType::String)
                exitCode = createS2N(exitCode);
            else if (exitCode->type() != CoreVM::LiteralType::Number)
                throw std::runtime_error("exit code must be a number");
        }
        _result = createCallFunction(getBuiltinFunction(node.callback.get()), { exitCode }, "exit");
    }

    void visit(ast::BuiltinExportStmt const& node) override
    {
        auto callArguments = std::vector<CoreVM::Value*> {};
        callArguments.push_back(get(node.name));
        _result = createCallFunction(getBuiltinFunction(node.callback.get()), callArguments, "export");
    }

    void visit(ast::BuiltinChDirStmt const& node) override
    {
        auto callArguments = std::vector<CoreVM::Value*> {};
        if (node.path)
            callArguments.push_back(codegen(node.path.get()));

        _result = createCallFunction(getBuiltinFunction(node.callback.get()), callArguments, "chdir");
    }

    void visit(ast::BuiltinSetStmt const& node) override
    {
        auto callArguments = std::vector<CoreVM::Value*> {};
        if (node.name && node.value)
        {
            callArguments.push_back(codegen(node.name.get()));
            callArguments.push_back(codegen(node.value.get()));
        }

        _result = createCallFunction(getBuiltinFunction(node.callback.get()), callArguments, "set");
    }

    void visit(ast::VariableSubstExpr const& node) override
    {
        auto callArguments = std::vector<CoreVM::Value*> {};
        if (node.name )
        {
            callArguments.push_back(codegen(node.name.get()));
        }

        _result = createCallFunction(getBuiltinFunction(node.callback.get()), callArguments, "getenv");
    }

    void visit(ast::BuiltinFalseStmt const&) override { _result = get(CoreVM::CoreNumber(1)); }

    void visit(ast::BuiltinReadStmt const& node) override
    {
        auto callArguments = std::vector<CoreVM::Value*> {};
        if (!node.parameters.empty())
            callArguments.emplace_back(get(createCallArgs(node.parameters)));

        _result = createCallFunction(getBuiltinFunction(node.callback.get()), callArguments, "read");
    }

    void visit(ast::BuiltinTrueStmt const&) override { _result = get(CoreVM::CoreNumber(0)); }

    void visit(ast::CallPipeline const& node) override
    {
        // A | B | C | D
        //
        // process      | stdin             |   stdout
        // -------------------------------------------------------
        // A            | STDIN             |   pipe 1 (write end)
        // B            | pipe 1 (read end) |   pipe 2 (write end)
        // C            | pipe 2 (read end) |   pipe 3 (write end)
        // D            | pipe 3 (read end) |   STDOUT

        for (size_t i = 0; i < node.calls.size(); ++i)
        {
            std::unique_ptr<ast::ProgramCall> const& call = node.calls[i];
            bool const lastInChain = i == node.calls.size() - 1;
            std::vector<CoreVM::Value*> callArguments {};
            callArguments.push_back(get(lastInChain));
            callArguments.push_back(get(createCallArgs(call->program, call->parameters)));
            _result =
                createCallFunction(getBuiltinFunction(call->callback.get()), callArguments, "callProcess");
        }
    }

    void visit(ast::CommandFileSubst const&) override
    {
        // TODO
    }
    void visit(ast::CompoundStmt const& node) override
    {
        for (auto const& stmt: node.statements)
            codegen(stmt.get());

        _result = nullptr;
    }

    void visit(ast::FileDescriptor const& node) override { _result = get(CoreVM::CoreNumber { node.value }); }

    void visit(ast::IfStmt const& node) override
    {
        CoreVM::BasicBlock* cond = createBlock("if.cond");
        CoreVM::BasicBlock* trueBlock = createBlock("if.trueBlock");
        CoreVM::BasicBlock* falseBlock = createBlock("if.falseBlock");
        CoreVM::BasicBlock* end = createBlock("if.end");

        createBr(cond);
        setInsertPoint(cond);
        createCondBr(toBool(codegen(node.condition.get())), trueBlock, falseBlock);

        setInsertPoint(trueBlock);
        codegen(node.thenBlock.get());
        createBr(end);

        setInsertPoint(falseBlock);
        codegen(node.elseBlock.get());
        createBr(end);

        setInsertPoint(end);
    }

    void visit(ast::InputRedirect const&) override
    {
        // TODO
    }
    void visit(ast::LiteralExpr const& node) override { _result = get(node.value); }

    void visit(ast::OutputRedirect const&) override
    {
        // TODO
    }
    void visit(ast::ProgramCall const& node) override
    {
        TRACE_SCOPE("ProgramCall");

        // auto redirectsArg = std::vector<CoreVM::Constant*> {};
        // redirectsArg.push_back(get(STDOUT_FILENO));
        // redirectsArg.push_back(get(STDERR_FILENO));
        // for (auto const& redirect: node.outputRedirects)
        // {
        //     auto* value = codegen(redirect.target.get());
        //     if (auto* constant = dynamic_cast<CoreVM::Constant*>(value); constant != nullptr)
        //         redirectsArg.push_back(constant);
        //     else
        //         fmt::print("Warning: non-constant argument passed to program call\n");
        // }

        auto callArguments = std::vector<CoreVM::Value*> {};
        callArguments.push_back(get(createCallArgs(node.program, node.parameters)));
        // callArguments.push_back(get(redirectsArg));

        _result = createCallFunction(getBuiltinFunction(node.callback.get()), callArguments, "callProcess");
    }

    void visit(ast::SubstitutionExpr const& node) override
    {
        node.pipeline->accept(*this);
    }
    void visit(ast::WhileStmt const& node) override
    {
        CoreVM::BasicBlock* cond = createBlock("while.cond");
        CoreVM::BasicBlock* body = createBlock("while.body");
        CoreVM::BasicBlock* end = createBlock("while.end");

        createBr(cond);

        setInsertPoint(cond);
        createCondBr(toBool(codegen(node.condition.get())), body, end);

        setInsertPoint(body);
        codegen(node.body.get());
        createBr(cond);

        setInsertPoint(end);
    }

    CoreVM::Value* toBool(CoreVM::Value* value) { return createNCmpEQ(value, get(CoreVM::CoreNumber(0))); }

    static std::vector<CoreVM::Constant*> createArray(std::vector<std::unique_ptr<ast::Expr>> const& expressions)
    {
        auto irArray = std::vector<CoreVM::Constant*> {};
        for (auto const& expr: expressions)
        {
            TRACE_SCOPE(fmt::format("Parameter: ", ast::ASTPrinter::print(*expr)));
            if (auto* constant = dynamic_cast<CoreVM::Constant*>(expr.get()); constant != nullptr)
                irArray.push_back(constant);
            else
                assert(!"TODO");
        }
        return irArray;
    }

    std::vector<CoreVM::Constant*> createCallArgs(std::vector<std::unique_ptr<ast::Expr>> const& args)
    {
        TRACE_SCOPE("createCallArgs");
        return IRGenerator::createArray(args);
    }

    std::vector<CoreVM::Constant*> createCallArgs(std::string const& programName,
                                                  std::vector<std::unique_ptr<ast::Expr>> const& args)
    {
        TRACE_SCOPE("createCallArgs");
        auto callArguments = IRGenerator::createArray(args);
        callArguments.insert(callArguments.begin(), get(programName));
        return callArguments;
    }

    CoreVM::Value* _result = nullptr;
    // CoreVM::NativeCallback _processCallCallback;
    // CoreVM::IRBuiltinFunction* _processCallFunction = nullptr;
    CoreVM::Signature _processCallSignature;
};
} // namespace endo
