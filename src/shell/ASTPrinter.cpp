// SPDX-License-Identifier: Apache-2.0
module;
#include <shell/AST.h>
#include <shell/Visitor.h>

#include <crispy/assert.h>

#include <fmt/format.h>

import Lexer;

export module ASTPrinter;

namespace endo::ast
{

export class ASTPrinter: public Visitor
{
  private:
    std::string _result;

  public:
    static std::string print(Node const& node)
    {
        ASTPrinter printer;
        node.accept(printer);
        return printer._result;
    }

    void visit(FileDescriptor const& node) override { _result += fmt::format("{}", node.value); }
    void visit(InputRedirect const& node) override { crispy::ignore_unused(node); }
    void visit(OutputRedirect const& node) override
    {
        if (std::holds_alternative<std::unique_ptr<LiteralExpr>>(node.target))
        {
            _result += fmt::format(
                " {}>{}", node.source->value, std::get<std::unique_ptr<LiteralExpr>>(node.target)->value);
        }
        else
            _result += fmt::format(
                " {}>&{}", node.source->value, std::get<std::unique_ptr<FileDescriptor>>(node.target)->value);
    }
    void visit(ProgramCall const& node) override
    {
        _result += fmt::format("{}", node.program);

        for (auto const& param: node.parameters)
        {
            _result += ' ';
            param->accept(*this);
        }

        for (auto const& redirect: node.outputRedirects)
        {
            redirect->accept(*this);
        }
    }
    void visit(CallPipeline const& node) override
    {
        for (size_t i = 0; i < node.calls.size(); ++i)
        {
            if (i > 0)
                _result += " | ";
            node.calls[i]->accept(*this);
        }
    }

    void visit(BuiltinChDirStmt const& node) override
    {
        _result += "cd";

        if (node.path)
        {
            _result += ' ';
            node.path->accept(*this);
        }
    }
    void visit(BuiltinSetStmt const& node) override
    {
        _result += "set";

        if (node.name)
        {
            _result += ' ';
            node.name->accept(*this);
        }
        if (node.value)
        {
            _result += ' ';
            node.value->accept(*this);
        }
    }

    void visit(VariableSubstExpr const& node) override
    {
        _result += "getenv";
        if (node.name)
        {
            _result += ' ';
            node.name->accept(*this);
        }
    }

    void visit(BuiltinExitStmt const& node) override
    {
        _result += "exit";
        if (node.code)
        {
            _result += ' ';
            node.code->accept(*this);
        }
    }
    void visit(BuiltinExportStmt const& node) override { _result += "export " + node.name; }
    void visit(BuiltinFalseStmt const&) override { _result += "false"; }
    void visit(BuiltinReadStmt const& node) override
    {
        _result += "read";
        for (auto const& param: node.parameters)
        {
            _result += ' ';
            param->accept(*this);
        }
    }
    void visit(BuiltinTrueStmt const&) override { _result += "true"; }

    void visit(CompoundStmt const& node) override
    {
        for (size_t i = 0; i < node.statements.size(); ++i)
        {
            if (i > 0)
                _result += "; ";
            node.statements[i]->accept(*this);
        }
    }

    void visit(IfStmt const& node) override
    {
        _result += "if ";
        _result += print(*node.condition);
        _result += "; ";
        _result += print(*node.thenBlock);
        _result += "; ";
        if (node.elseBlock)
        {
            _result += "else ";
            _result += print(*node.elseBlock);
        }
        _result += "fi";
    }
    void visit(WhileStmt const& node) override
    {
        _result += "while ";
        _result += print(*node.condition);
        _result += "; ";
        _result += print(*node.body);
        _result += "done";
    }

    void visit(LiteralExpr const& node) override { _result += fmt::format("{}", node.value); }
    void visit(SubstitutionExpr const& node) override { node.pipeline->accept(*this); }
    void visit(CommandFileSubst const& node) override { crispy::ignore_unused(node); }
};

} // namespace endo::ast
