// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/Visitor.h>


#include <fmt/format.h>

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

import CoreVM;

namespace endo::ast
{

struct Visitor;

struct Node
{
    virtual ~Node() = default;

    virtual void accept(Visitor&) const = 0;
};

struct FileDescriptor final: public Node
{
    int value;

    explicit FileDescriptor(int value): value(value) {}

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

struct Expr: public Node
{
};

// <FILE
struct InputRedirect final: public Node
{
    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

// "string that may contain spaces"
// 'string that may contain spaces'
// string
//
// This is a literal parameter.
// It is a string.
// It may be quoted.
struct LiteralExpr final: Expr
{
    std::string value;

    explicit LiteralExpr(std::string value): value(std::move(value)) {}

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

// >FILE
// 1>FILE
// 1>&2
//
// This is an output redirect.
// It is a file descriptor, followed by a target.
// The target is either a file descriptor or a path.
// If the target is a file descriptor, then the output of the file descriptor is redirected to the target.
// If the target is a path, then the output of the file descriptor is redirected to the file at the path.
// If the source is omitted, then the output of file descriptor 1 is redirected.
struct OutputRedirect final: public Expr
{
    std::unique_ptr<FileDescriptor> source;
    std::variant<std::unique_ptr<FileDescriptor>, std::unique_ptr<LiteralExpr>> target;

    OutputRedirect(std::unique_ptr<FileDescriptor> source, std::unique_ptr<FileDescriptor> target):
        source(std::move(source)), target(std::move(target))
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

// <(command)
//
// This is a bashism, but it's useful for endo.
// It's a way to pass the output of a command as a file (e.g. to a program that expects a file).
// It is the path to the file descriptor of the command's output, which is a pipe.
struct CommandFileSubst final: public Expr
{
    std::unique_ptr<Node> command;

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

struct Statement: public Node
{
};

struct BuiltinExitStmt final: public Statement
{
    std::reference_wrapper<CoreVM::NativeCallback const> callback;
    std::unique_ptr<Expr> code;

    BuiltinExitStmt(std::reference_wrapper<CoreVM::NativeCallback const> callback,
                    std::unique_ptr<Expr> code):
        callback { callback }, code { std::move(code) }
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

struct BuiltinExportStmt final: public Statement
{
    std::reference_wrapper<CoreVM::NativeCallback const> callback;
    std::string name;

    BuiltinExportStmt(std::reference_wrapper<CoreVM::NativeCallback const> callback, std::string name):
        callback { callback }, name { std::move(name) }
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

struct BuiltinTrueStmt final: public Statement
{
    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

struct BuiltinFalseStmt final: public Statement
{
    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

struct BuiltinReadStmt final: public Statement
{
    std::reference_wrapper<CoreVM::NativeCallback const> callback;
    std::vector<std::unique_ptr<Expr>> parameters;

    BuiltinReadStmt(std::reference_wrapper<CoreVM::NativeCallback const> callback,
                    std::vector<std::unique_ptr<Expr>> parameters = {}):
        callback { callback }, parameters { std::move(parameters) }
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

struct BuiltinSetStmt final: public Statement
{
    std::reference_wrapper<CoreVM::NativeCallback const> callback;
    std::unique_ptr<Expr> name;
    std::unique_ptr<Expr> value;

    BuiltinSetStmt(std::reference_wrapper<CoreVM::NativeCallback const> callback,
                   std::unique_ptr<Expr> name,
                   std::unique_ptr<Expr> value):
        callback { callback }, name {std::move( name )}, value { std::move(value) }
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};


struct VariableSubstExpr final: public Statement
{
    std::reference_wrapper<CoreVM::NativeCallback const> callback;
    std::unique_ptr<Expr> name;

    VariableSubstExpr(std::reference_wrapper<CoreVM::NativeCallback const> callback,
                   std::unique_ptr<Expr> name):
        callback { callback }, name {std::move( name )}
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};


struct BuiltinChDirStmt final: public Statement
{
    std::reference_wrapper<CoreVM::NativeCallback const> callback;
    std::unique_ptr<Expr> path;

    BuiltinChDirStmt(std::reference_wrapper<CoreVM::NativeCallback const> callback,
                     std::unique_ptr<Expr> path):
        callback { callback }, path { std::move(path) }
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

// /bin/ls -hal
//
// This is a program call.
// It is a path to an executable, followed by arguments.
// It may also have input and output redirects.
struct ProgramCall final: public Statement
{
    std::string program;
    std::vector<std::unique_ptr<Expr>> parameters;
    std::vector<std::unique_ptr<OutputRedirect>> outputRedirects;
    std::reference_wrapper<CoreVM::NativeCallback const> callback;

    ProgramCall(CoreVM::NativeCallback const& callback,
                std::string program,
                std::vector<std::unique_ptr<Expr>> parameters,
                std::vector<std::unique_ptr<OutputRedirect>> outputRedirects):
        program(std::move(program)),
        parameters(std::move(parameters)),
        outputRedirects(std::move(outputRedirects)),
        callback(callback)
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

// $(command)
// `command`
//
// This is a substitution parameter.
// It is a parameter, because it can be used as an argument to a program call.
struct SubstitutionExpr final: public Expr
{
    std::unique_ptr<Statement> pipeline;

    SubstitutionExpr(std::unique_ptr<Statement> pipeline): pipeline(std::move(pipeline)) {}
    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

// a | b | (c | d) | e
//
// This is a call pipeline.
// It is a sequence of program calls, separated by pipes.
struct CallPipeline final: public Statement
{
    std::vector<std::unique_ptr<ProgramCall>> calls;

    CallPipeline(std::vector<std::unique_ptr<ProgramCall>> calls): calls(std::move(calls)) {}

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

// { a; b; }
//
// This is a compound statement.
// It is a sequence of statements, separated by semicolons.
struct CompoundStmt final: public Statement
{
    std::vector<std::unique_ptr<Node>> statements;

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

// if command; then a; else b; fi
//
// This is an if statement.
// It is a condition, followed by a then block, followed by an optional else block.
struct IfStmt final: public Statement
{
    std::unique_ptr<Statement> condition;
    std::unique_ptr<Statement> thenBlock;
    std::unique_ptr<Statement> elseBlock;

    IfStmt(std::unique_ptr<Statement> condition,
           std::unique_ptr<Statement> thenBlock,
           std::unique_ptr<Statement> elseBlock):
        condition(std::move(condition)), thenBlock(std::move(thenBlock)), elseBlock(std::move(elseBlock))
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

// while command; do a; done
//
// This is a while statement.
// It is a condition, followed by a body.
struct WhileStmt final: public Statement
{
    std::unique_ptr<Statement> condition;
    std::unique_ptr<Statement> body;

    WhileStmt(std::unique_ptr<Statement> condition, std::unique_ptr<Statement> body):
        condition(std::move(condition)), body(std::move(body))
    {
    }

    void accept(Visitor& visitor) const override { visitor.visit(*this); }
};

} // namespace endo::ast
