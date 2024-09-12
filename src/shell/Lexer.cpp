// SPDX-License-Identifier: Apache-2.0
module;

#include <crispy/logstore.h>

#include <fmt/format.h>

#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

auto inline lexerLog = logstore::category("lexer ", "Lexer logger", logstore::category::state::Enabled);

using namespace std::string_view_literals;

export module Lexer;

namespace endo
{

export enum class Token
{
    Invalid,

    AmpNumber,      // '&' DIGIT+
    Backslash,      // '\'
    DollarDollar,   // $$
    DollarName,     // $NAME
    DollarNot,      // $!
    DollarQuestion, // $?
    DollarNumber,   // '$' DIGIT+
    EndOfInput,     // EOF
    LineFeed,       // LF
    Equal,          // =
    Greater,        // >
    GreaterEqual,   // >=
    GreaterGreater, // >>
    Less,           // <
    LessEqual,      // <=
    LessLess,       // <<
    LessRndOpen,    // <(
    Not,            // !
    Number,         // DIGIT+
    Pipe,           // |
    RndClose,       // )
    RndOpen,        // (
    Semicolon,      // ;
    String,         // "..." and '...'
    Identifier,     // space delimited text not containing any of the unescaped symbols above
};

export enum class BuiltinFunction
{
    Exit,
    Cd,
    Pwd,
    Env,
    Fg,
    Bg,
    Read,  // read VAR
    Time,  // time COMMAND
    If,    // if (EXPR) COMMAND [else COMMAND]
    While, // while (EXPR) COMMAND
};

export struct LineColumn
{
    int line;   // 0-based index
    int column; // 0-based index
};

export struct SourceLocation
{
    int line;              // 0-based index
    int column;            // 0-based index
    std::string_view name; // e.g. stdin, or a filename
};

export struct SourceLocationRange
{
    LineColumn begin;
    LineColumn end;
    std::string_view name; // e.g. stdin, or a filename
};

export struct TokenInfo
{
    Token token;
    std::string literal;
    SourceLocationRange location;
};

export class Source
{
  public:
    virtual ~Source() = default;

    virtual void rewind() = 0;
    [[nodiscard]] virtual char32_t readChar() = 0;
    [[nodiscard]] virtual std::string_view readGraphemeCluster() = 0;
    [[nodiscard]] virtual SourceLocation currentSourceLocation() const noexcept = 0;
};

export class StringSource final: public Source
{
  public:
    explicit StringSource(std::string source): _source { std::move(source) } {}
    void rewind() override
    {
        _location.line = 0;
        _location.column = 0;
    }
    [[nodiscard]] char32_t readChar() override
    {
        if (_offset >= _source.size())
            return -1;

        auto const ch = _source[_offset];

        ++_offset;
        ++_location.column;
        if (ch == '\n')
        {
            ++_location.line;
            _location.column = 0;
        }

        return ch;
    }

    [[nodiscard]] std::string_view readGraphemeCluster() override
    {
        if (_offset >= _source.size())
            return {};

        return std::string_view(_source.data() + _offset, 1);
    }
    [[nodiscard]] SourceLocation currentSourceLocation() const noexcept override { return _location; }

  private:
    SourceLocation _location;
    std::string _source;
    size_t _offset = 0;
};

export class Lexer
{

    template <typename... T>
    void trace(std::string info, T... args)
    {
        fmt::print(fmt::runtime("Lexer: " + info), args...);
    }

  public:
    explicit Lexer(std::unique_ptr<Source> source): _source { std::move(source) }
    {
        nextChar();
        nextToken();
    }
    Token nextToken()
    {
        // auto const postLogger = crispy::finally { [this]() mutable {
        //     lexerLog()("Lexer.nextToken ~> {}\n", _currentToken);
        // } };

        consumeWhitespace();
        switch (_currentChar)
        {
            case (char32_t) -1: return confirmToken(Token::EndOfInput);
            case '\r':
                nextChar();
                if (_currentChar == '\n')
                    return consumeCharAndConfirmToken(Token::LineFeed);
                return confirmToken(Token::Invalid);
            case '\n': return consumeCharAndConfirmToken(Token::LineFeed);
            case ';': return consumeCharAndConfirmToken(Token::Semicolon);
            case '=': return consumeCharAndConfirmToken(Token::Equal);
            case '|': return consumeCharAndConfirmToken(Token::Pipe);
            case '>':
                nextChar();
                if (_currentChar == '>')
                    return consumeCharAndConfirmToken(Token::GreaterGreater);
                else if (_currentChar == '=')
                    return consumeCharAndConfirmToken(Token::GreaterEqual);
                else
                    return consumeCharAndConfirmToken(Token::Greater);
            case '<':
                nextChar();
                if (_currentChar == '<')
                    return consumeCharAndConfirmToken(Token::LessLess);
                else if (_currentChar == '=')
                    return consumeCharAndConfirmToken(Token::LessEqual);
                else if (_currentChar == '(')
                    return consumeCharAndConfirmToken(Token::LessRndOpen);
                else
                    return confirmToken(Token::Less);
            case '(': return consumeCharAndConfirmToken(Token::RndOpen);
            case ')': return consumeCharAndConfirmToken(Token::RndClose);
            case '\\': return consumeCharAndConfirmToken(Token::Backslash);
            case '!': return consumeCharAndConfirmToken(Token::Not);
            case '$':
                nextChar();
                if (_currentChar == '$')
                    return consumeCharAndConfirmToken(Token::DollarDollar);
                else if (_currentChar == '!')
                    return consumeCharAndConfirmToken(Token::DollarNot);
                else if (_currentChar == '?')
                    return consumeCharAndConfirmToken(Token::DollarQuestion);
                else if (_currentChar == '{')
                    return consumeIdentifierInsideBracket(Token::DollarName);
                else if (_currentChar < 0x80 && std::isalpha(static_cast<char>(_currentChar)))
                    return consumeIdentifier(Token::DollarName);
                else if (_currentChar < 0x80 && std::isdigit(static_cast<char>(_currentChar)))
                    return consumeCharAndConfirmToken(Token::DollarNumber);
                else
                    return confirmToken(Token::Invalid);
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9': return consumeNumber();
            case '"':
            case '\'': return consumeString();
            default: return consumeIdentifier();
        }
        return confirmToken(Token::Invalid);
        // crispy::unreachable();
    }

    [[nodiscard]] Token currentToken() const noexcept { return _currentToken.token; }
    [[nodiscard]] std::string const& currentLiteral() const noexcept { return _currentToken.literal; }
    [[nodiscard]] SourceLocationRange currentRange() const noexcept { return _currentRange; }

    [[nodiscard]] bool isDirective(std::string_view name) const noexcept
    {
        return currentToken() == Token::Identifier && currentLiteral() == name;
    }

    static std::vector<TokenInfo> tokenize(std::unique_ptr<Source> source)
    {
        auto tokens = std::vector<TokenInfo> {};
        auto lexer = Lexer { std::move(source) };

        while (lexer.currentToken() != Token::EndOfInput)
        {
            tokens.emplace_back(
                TokenInfo { lexer.currentToken(), lexer.currentLiteral(), lexer.currentRange() });
            lexer.nextToken();
        }

        return tokens;
    }

  private:
    [[nodiscard]] bool eof() const noexcept { return _currentChar == char32_t(-1); }
    [[nodiscard]] char32_t currentChar() const noexcept { return _currentChar; }

    void consumeWhitespace()
    {
        _nextToken.literal = {};

        while (strchr(" \t", (int) _currentChar))
            nextChar();

        auto const [line, column, name] = _source->currentSourceLocation();
        _nextToken.location.name = name;
        _nextToken.location.begin = { .line = line, .column = column };
        _nextToken.location.end = _nextToken.location.begin;
    }

    Token consumeNumber()
    {
        while (strchr("0123456789", (int) _currentChar))
        {
            _nextToken.literal += static_cast<char>(_currentChar); // TODO: UTF-8
            nextChar();
        }

        return confirmToken(Token::Number);
    }

    Token consumeIdentifier() { return consumeIdentifier(Token::Identifier); }

    Token consumeIdentifierInsideBracket(Token token)
    {
        auto constexpr ReservedSymbols = U"|<>()[]!$'\"\t\r\n ;"sv;
        int countBracket = 0;
        while (!eof() && ReservedSymbols.find(_currentChar) == std::string_view::npos)
        {
            trace("consumeIdentifier: '{}'\n", (char) _currentChar);
            if (_currentChar != '{' && _currentChar != '}')
                _nextToken.literal += static_cast<char>(_currentChar); // TODO: UTF-8
            nextChar();
        }
        if (countBracket != 0)
            trace("not balanced brackets \n"); // TODO
        trace("consumeIdentifier: result: '{}'\n", _nextToken.literal);
        return confirmToken(token);
    }

    Token consumeIdentifier(Token token)
    {
        auto constexpr ReservedSymbols = U"|<>()[]{}!$'\"\t\r\n ;"sv;

        while (!eof() && ReservedSymbols.find(_currentChar) == std::string_view::npos)
        {
            _nextToken.literal += static_cast<char>(_currentChar); // TODO: UTF-8
            nextChar();
            // lexerLog()("consumeIdentifier: '{}'\n", (char) _currentChar);
        }
        lexerLog()("consumeIdentifier: result: '{}'\n", _nextToken.literal);
        return confirmToken(token);
    }

    Token consumeString()
    {
        auto const quote = _currentChar;
        nextChar();
        while (_currentChar != quote)
        {
            if (_currentChar == '\\')
                nextChar();
            _nextToken.literal += static_cast<char>(_currentChar); // TODO: UTF-8
            nextChar();
        }
        nextChar();
        return confirmToken(Token::String);
    }

    char32_t nextChar()
    {
        _currentChar = _source->readChar();
        // lexerLog()("Lexer.nextChar: '{}'\n", (char) _currentChar);
        return _currentChar;
    }

    Token consumeCharAndConfirmToken(Token token)
    {
        nextChar();
        return confirmToken(token);
    }

    Token confirmToken(Token token)
    {
        _nextToken.token = token;
        _nextToken.literal = _nextToken.literal;
        auto const [a, b, _] = _source->currentSourceLocation();
        _nextToken.location.end = { .line = a, .column = b };
        _currentToken = _nextToken;

        trace("Token is ready : {} \n", _currentToken);

        _nextToken.literal = {};
        _nextToken.location.name = _source->currentSourceLocation().name;
        _nextToken.location.begin = _nextToken.location.end;

        return token;
    }

    std::unique_ptr<Source> _source;
    char32_t _currentChar = 0;
    TokenInfo _currentToken = TokenInfo {};
    TokenInfo _nextToken = TokenInfo {};
    SourceLocationRange _currentRange {};
};
} // namespace endo

template <>
struct fmt::formatter<endo::LineColumn>: fmt::formatter<std::string>
{
    auto format(const endo::LineColumn lineColumn, format_context& ctx) -> format_context::iterator
    {
        return formatter<std::string>::format(fmt::format("{}:{}", lineColumn.line, lineColumn.column), ctx);
    }
};

template <>
struct fmt::formatter<endo::SourceLocation>: fmt::formatter<std::string>
{
    auto format(const endo::SourceLocation location, format_context& ctx) -> format_context::iterator
    {
        return formatter<std::string>::format(
            fmt::format("{}:{}:{}", location.name, location.line, location.column), ctx);
    }
};

template <>
struct fmt::formatter<endo::SourceLocationRange>: fmt::formatter<std::string>
{
    auto format(const endo::SourceLocationRange range, format_context& ctx) -> format_context::iterator
    {
        return formatter<std::string>::format(fmt::format("{}({} - {})", range.name, range.begin, range.end),
                                              ctx);
    }
};

template <>
struct fmt::formatter<endo::Token>: fmt::formatter<std::string_view>
{
    auto format(const endo::Token token, format_context& ctx) -> format_context::iterator
    {
        string_view name;
        using enum endo::Token;
        switch (token)
        {
            case AmpNumber: name = "AmpNumber"; break;
            case Backslash: name = "\\"; break;
            case DollarDollar: name = "$$"; break;
            case DollarName: name = "DollarName"; break;
            case DollarNot: name = "$!"; break;
            case DollarQuestion: name = "$?"; break;
            case DollarNumber: name = "DollarNumber"; break;
            case EndOfInput: name = "EndOfInput"; break;
            case Equal: name = "="; break;
            case Greater: name = ">"; break;
            case GreaterEqual: name = ">="; break;
            case GreaterGreater: name = ">>"; break;
            case Identifier: name = "Identifier"; break;
            case Invalid: name = "Invalid"; break;
            case Less: name = "<"; break;
            case LessEqual: name = "<="; break;
            case LessLess: name = "<<"; break;
            case LessRndOpen: name = "<("; break;
            case LineFeed: name = "LineFeed"; break;
            case Not: name = "!"; break;
            case Number: name = "Number"; break;
            case Pipe: name = "|"; break;
            case RndClose: name = ")"; break;
            case RndOpen: name = "("; break;
            case Semicolon: name = ";"; break;
            case String: name = "String"; break;
        }
        return formatter<string_view>::format(name, ctx);
    }
};

template <>
struct fmt::formatter<endo::TokenInfo>
{
    static auto parse(format_parse_context& ctx) -> format_parse_context::iterator { return ctx.begin(); }
    static auto format(endo::TokenInfo const& info, format_context& ctx) -> format_context::iterator
    {
        return fmt::format_to(ctx.out(), "({}, {}, {})", info.token, info.literal, info.location);
    }
};
