// SPDX-License-Identifier: Apache-2.0
module;

#include <shell/AST.h>
#include <shell/ScopedLogger.h>

#include <crispy/utils.h>
#include <crispy/logstore.h>

#include <memory>
#include <optional>

auto inline parserLog = logstore::category("parser", "Parser logger", logstore::category::state::Enabled);
#define TRACE_SCOPE(message) ScopedLogger _logger { message, parserLog };
#define TRACE_FMT(message, ...) do { parserLog()(ScopedLogger::write(::fmt::format(message, __VA_ARGS__))); } while (0)
#define TRACE(message) do { parserLog()(ScopedLogger::write(::fmt::format(message))); } while (0)


import ASTPrinter;
import Lexer;
import CoreVM;

export module Parser;

namespace endo
{

export class Parser
{
  public:
    explicit Parser(CoreVM::Runtime& runtime,
                    CoreVM::diagnostics::Report& report,
                    std::unique_ptr<Source> source):
        _runtime { runtime }, _report { report }, _lexer { std::move(source) }
    {
    }

    std::unique_ptr<ast::Statement> parse() { return parseBlock("global"); }

  private:
    [[nodiscard]] bool isEndOfBlock() const noexcept
    {
        // clang-format off
    return _lexer.currentToken() == Token::EndOfInput
        || _lexer.isDirective("else")
        || _lexer.isDirective("elif")
        || _lexer.isDirective("fi")
        || _lexer.isDirective("done");
        // clang-format on
    }

    [[nodiscard]] bool isEndOfStmt() const noexcept
    {
        // clang-format off
    return _lexer.currentToken() == Token::EndOfInput
        || _lexer.currentToken() == Token::LineFeed
        || _lexer.currentToken() == Token::Pipe
        || _lexer.currentToken() == Token::Semicolon;
        // clang-format on
    }

    std::unique_ptr<ast::Statement> parseBlock(std::string_view traceMessage = {})
    {
        TRACE_SCOPE(
            fmt::format("parseBlock{}", traceMessage.empty() ? "" : fmt::format(" ({})", traceMessage)));
        auto scope = std::make_unique<ast::CompoundStmt>();
        while (!isEndOfBlock())
        {
            if (consumeUntilNotOneOf(Token::Semicolon, Token::LineFeed))
                continue;
            auto stmt = parseStmt();
            if (!stmt)
            {
                TRACE_FMT("Parsed scope.1: {}", ast::ASTPrinter::print(*scope));
                return scope;
            }

            TRACE_FMT("Parsed statement: {}", ast::ASTPrinter::print(*stmt));
            scope->statements.emplace_back(std::move(stmt));
        }
        TRACE_FMT("Parsed scope.3 (current token: {}): {}",
                  _lexer.currentLiteral(),
                  ast::ASTPrinter::print(*scope));
        return scope;
    }

    std::unique_ptr<ast::Statement> parseStmt()
    {
        TRACE_SCOPE("parseStmt");
        switch (_lexer.currentToken())
        {
            case Token::DollarName: {
                auto name = std::make_unique<ast::LiteralExpr>(_lexer.currentLiteral());
                _lexer.nextToken();
                return std::make_unique<ast::VariableSubstExpr>(*_runtime.find("getenv(S)S"),
                                                                std::move(name));
            }
            case Token::String:
            case Token::Identifier:
                if (_lexer.isDirective("if"))
                    return parseIf();
                else if (_lexer.isDirective("while"))
                    return parseWhile();
                else if (_lexer.isDirective("exit"))
                {
                    _lexer.nextToken();
                    std::unique_ptr<ast::Expr> code;
                    if (!isEndOfStmt())
                        code = parseParameter();
                    assert(_runtime.find("exit(I)V") != nullptr);
                    return std::make_unique<ast::BuiltinExitStmt>(*_runtime.find("exit(I)V"),
                                                                  std::move(code));
                }
                else if (_lexer.isDirective("true"))
                {
                    _lexer.nextToken();
                    return std::make_unique<ast::BuiltinTrueStmt>();
                }
                else if (_lexer.isDirective("false"))
                {
                    _lexer.nextToken();
                    return std::make_unique<ast::BuiltinFalseStmt>();
                }
                else if (_lexer.isDirective("read"))
                {
                    _lexer.nextToken();
                    std::vector<std::unique_ptr<ast::Expr>> parameters = parseParameterList();
                    CoreVM::NativeCallback const& callback =
                        *_runtime.find(parameters.empty() ? "read()S" : "read(s)S");
                    return std::make_unique<ast::BuiltinReadStmt>(callback, std::move(parameters));
                }
                else if (_lexer.isDirective("export"))
                {
                    _lexer.nextToken();
                    auto name = consumeLiteral();
                    return std::make_unique<ast::BuiltinExportStmt>(*_runtime.find("export(S)V"), name);
                }
                else if (_lexer.isDirective("set"))
                {
                    _lexer.nextToken();
                    auto name = parseParameter();
                    auto value = parseParameter();
                    return std::make_unique<ast::BuiltinSetStmt>(
                        *_runtime.find("set(SS)B"), std::move(name), std::move(value));
                }
                else if (_lexer.isDirective("cd"))
                {
                    _lexer.nextToken();
                    if (isEndOfStmt())
                        return std::make_unique<ast::BuiltinChDirStmt>(*_runtime.find("cd()B"), nullptr);
                    else
                    {
                        auto param = parseParameter();
                        return std::make_unique<ast::BuiltinChDirStmt>(*_runtime.find("cd(S)B"),
                                                                       std::move(param));
                    }
                }
                else
                {
                    return parseCallPipeline();
                }
            case Token::EndOfInput:
                _report.syntaxError(CoreVM::SourceLocation(), "Unexpected end of input");
                return nullptr;
            default: _report.syntaxError(CoreVM::SourceLocation(), "Unexpected token"); return nullptr;
        }

        return nullptr;
    }

    std::string consumeLiteral()
    {
        auto literal = _lexer.currentLiteral();
        _lexer.nextToken();
        return literal;
    }

    std::unique_ptr<ast::IfStmt> parseIf()
    {
        TRACE_SCOPE("parseIf");
        // 'if' statement (LF | ';') statement ('else' statement)?
        _lexer.nextToken();
        auto condition = parseStmt();
        assert(condition != nullptr);
        if (!condition)
            return nullptr;

        if (!consumeOneOf(Token::Semicolon, Token::LineFeed))
        {
            TRACE_FMT("Expected ';' or LF after if condition but got '{}'", _lexer.currentLiteral());
            return nullptr;
        }

        TRACE_FMT("Parsed if condition: {}", ast::ASTPrinter::print(*condition));

        consumeDirective("then");

        auto thenBranch = parseBlock("trueBranch");
        if (!thenBranch)
            return nullptr;

        TRACE_FMT("Parsed if then branch: {}", ast::ASTPrinter::print(*thenBranch));

        std::unique_ptr<ast::Statement> elseBranch;
        if (_lexer.isDirective("elif"))
        {
            elseBranch = parseIf();
            TRACE_FMT("Parsed elif branch: {}", ast::ASTPrinter::print(*elseBranch));
        }
        else if (_lexer.isDirective("else"))
        {
            TRACE_FMT("Parsing else branch (current token: {}, '{}')",
                      _lexer.currentToken(),
                      _lexer.currentLiteral());
            _lexer.nextToken();
            elseBranch = parseBlock("elseBranch");
            ;
            if (!elseBranch)
            {
                TRACE_FMT("Parsed elif branch: returned NULL (cur token: {})", _lexer.currentToken());
                return nullptr;
            }
            TRACE_FMT("Parsed elif branch: {}", ast::ASTPrinter::print(*elseBranch));
        }

        TRACE_FMT("Parsed if statement finished. Current token: {}", _lexer.currentLiteral());
        consumeDirective("fi");

        return std::make_unique<ast::IfStmt>(
            std::move(condition), std::move(thenBranch), std::move(elseBranch));
    }

    std::unique_ptr<ast::WhileStmt> parseWhile()
    {
        TRACE_SCOPE("parseStmt");
        // 'while' statement (LF | ';') statement 'done'
        _lexer.nextToken(); // consume 'while'
        auto condition = parseStmt();
        if (!consumeOneOf(Token::Semicolon, Token::LineFeed))
        {
            TRACE_FMT("Expected ';' or LF after if condition but got '{}'", _lexer.currentLiteral());
            return nullptr;
        }

        consumeDirective("do");
        auto body = parseBlock("whileBody");
        consumeDirective("done");
        return std::make_unique<ast::WhileStmt>(std::move(condition), std::move(body));
    }

    std::unique_ptr<ast::ProgramCall> parseCall(bool piped = false)
    {
        TRACE_SCOPE("parseCall");
        std::string program = consumeLiteral();
        std::vector<std::unique_ptr<ast::Expr>> arguments = parseParameterList();
        std::vector<std::unique_ptr<ast::OutputRedirect>> outputRedirects;

        // TODO: parse outputRedirects
        // outputRedirects.emplace_back(std::make_unique<ast::OutputRedirect>(
        //     std::make_unique<ast::FileDescriptor>(1), std::make_unique<ast::FileDescriptor>(2)));

        CoreVM::NativeCallback const* builtinCallProcess = _lexer.currentToken() == Token::Pipe || piped
                                                               ? _runtime.find("callproc(Bs)I")
                                                               : _runtime.find("callproc(s)I");
        assert(builtinCallProcess != nullptr);

        return std::make_unique<ast::ProgramCall>(
            *builtinCallProcess, std::move(program), std::move(arguments), std::move(outputRedirects));
    }

    std::vector<std::unique_ptr<ast::Expr>> parseParameterList()
    {
        TRACE_SCOPE("parseParameterList");
        std::vector<std::unique_ptr<ast::Expr>> parameters;
        while (!isEndOfStmt())
        {
            auto arg = parseParameter();
            if (arg)
                parameters.emplace_back(std::move(arg));
            else
                break;
        }
        TRACE_FMT("parsed {} parameters, follow-up token: {}", parameters.size(), _lexer.currentToken());
        return parameters;
    }

    std::unique_ptr<ast::Expr> parseParameter()
    {
        TRACE_FMT("parseParameter: {} \"{}\"", _lexer.currentToken(), _lexer.currentLiteral());
        switch (_lexer.currentToken())
        {
            case Token::String:
            case Token::Number:
            case Token::Identifier: return std::make_unique<ast::LiteralExpr>(consumeLiteral()); break;
            case Token::DollarName: {
                auto name = std::make_unique<ast::LiteralExpr>(consumeLiteral());
                return std::make_unique<ast::SubstitutionExpr>(
                    std::make_unique<ast::VariableSubstExpr>(*_runtime.find("getenv(S)S"), std::move(name)));
            }
            break;
            default: _report.syntaxError(CoreVM::SourceLocation(), "Expected parameter"); return nullptr;
        }
    }

    std::unique_ptr<ast::Statement> parseCallPipeline()
    {
        TRACE_SCOPE("parseCallPipeline");

        auto call = parseCall();
        if (!call)
            return nullptr;

        if (_lexer.currentToken() != Token::Pipe)
            return call;

        std::vector<std::unique_ptr<ast::ProgramCall>> calls;
        calls.emplace_back(std::move(call));
        while (_lexer.currentToken() == Token::Pipe)
        {
            _lexer.nextToken();
            TRACE_FMT("Parsing call pipeline item (NT: {})", _lexer.currentLiteral());
            if (auto nextCall = parseCall(true); nextCall)
            {
                calls.emplace_back(std::move(nextCall));
                TRACE_FMT("Parsed call pipeline item: {} (NT: {})",
                          ast::ASTPrinter::print(*calls.back()),
                          _lexer.currentLiteral());
            }
        }

        return std::make_unique<ast::CallPipeline>(std::move(calls));
    }

    bool tryConsumeToken(Token token)
    {
        if (_lexer.currentToken() != token)
            return false;
        _lexer.nextToken();
        return true;
    }

    bool consumeOneOf(Token token)
    {
        if (_lexer.currentToken() != token)
            return false;
        _lexer.nextToken();
        return true;
    }

    template <typename... T>
    bool consumeOneOf(Token a, T... tokens)
    {
        return consumeOneOf(a) || ((consumeOneOf(tokens) || ...));
    }

    template <typename... T>
    bool consumeUntilNotOneOf(T... token)
    {
        if (!consumeOneOf(token...))
            return false;

        while (consumeOneOf(token...))
            ;

        return true;
    }

    void consumeDirective(const std::string& directive)
    {
        if (_lexer.isDirective(directive))
            _lexer.nextToken();
        else
            _report.syntaxError(CoreVM::SourceLocation(),
                                "Expected directive '{}' but got '{}'",
                                directive,
                                _lexer.currentLiteral());
    }

    CoreVM::Runtime& _runtime;            // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    CoreVM::diagnostics::Report& _report; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    Lexer _lexer;
};

} // namespace endo
