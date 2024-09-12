// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch.hpp>
#include <fmt/core.h>
import Lexer;

TEST_CASE("Lexer.basic")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("echo hello world $PATH ${PATH}"));
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "echo");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "hello");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "world");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DollarName);
    CHECK(lexer.currentLiteral() == "PATH");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DollarName);
    CHECK(lexer.currentLiteral() == "PATH");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::EndOfInput);
}


TEST_CASE("Lexer.brackets")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("$PATH ${PATH} ${{PATH}} "));

    CHECK(lexer.currentToken() == endo::Token::DollarName);
    CHECK(lexer.currentLiteral() == "PATH");


    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DollarName);
    CHECK(lexer.currentLiteral() == "PATH");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DollarName);
    CHECK(lexer.currentLiteral() == "PATH");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::EndOfInput);
}
