// SPDX-License-Identifier: Apache-2.0

#include <crispy/escape.h>

#include <catch2/catch.hpp>

using namespace std::string_literals;
using namespace std::string_view_literals;

using crispy::escape;
import Shell;
import TTY;

namespace
{
struct TestShell
{
    endo::TestPTY pty;
    endo::TestEnvironment env;
    int exitCode = -1;

    endo::Shell shell { pty, env };

    std::string_view output() const noexcept { return pty.output(); }

    TestShell& operator()(std::string_view cmd)
    {
        exitCode = shell.execute(std::string(cmd));
        // UNSCOPED_INFO("output: " + std::string(pty.output()));
        // TODO: print report if it is not empty
        return *this;
    }
};
} // namespace

// TEST_CASE("shell.syntax.exit")
// {
//     TestShell shell;
//     CHECK(shell("exit").exitCode == 0);
//     CHECK(shell("exit 1").exitCode == 1);
//     CHECK(shell("exit 123").exitCode == 123);
// }

// TEST_CASE("shell.syntax.if")
// {
//     TestShell shell;
//     CHECK(shell("if true; then exit 2; else exit 3; fi").exitCode == 2);
//     CHECK(shell("if false; then exit 2; else exit 3; fi").exitCode == 3);
// }

// TEST_CASE("shell.syntax.pipes")
// {
//     CHECK(escape(TestShell()("echo hello | grep ll").output()) == escape("hello\n"));
//     CHECK(escape(TestShell()("echo hello | grep ll | grep hell").output()) == escape("hello\n"));
// }

// TEST_CASE("shell.builtin.read.DefaultVar")
// {
//     auto const input = "hello world"s;
//     TestShell shell;
//     shell.pty.writeToStdin(input + "\n"s);
//     shell("read");
//     CHECK(shell.env.get("REPLY").value_or("NONE") == input);
// }

// TEST_CASE("shell.builtin.read.CustomVar")
// {
//     auto const input = "hello world"s;
//     TestShell shell;
//     shell.pty.writeToStdin(input + "\n"s);
//     shell("read BRU");
//     CHECK(shell.env.get("BRU").value_or("NONE") == input);
// }

// TEST_CASE("shell.builtin.set_variable")
// {
//     TestShell shell;
//     shell("set BRU hello");
//     CHECK(shell.env.get("BRU").value_or("NONE") == "hello");
// }

// TEST_CASE("shell.builtin.get_variable")
// {
//     TestShell shell;
//     shell("set BRU hello");
//     CHECK(shell.env.get("BRU").value_or("NONE") == "hello");
//     shell("$BRU");
// }

// TEST_CASE("shell.builtin.get_variable_inside_curl_brackets")
// {
//     TestShell shell;
//     shell("set BRU hello");
//     CHECK(shell.env.get("BRU").value_or("NONE") == "hello");
//     shell("${BRU}");
// }


TEST_CASE("shell.builtin.set_and_export_variable")
{
    TestShell shell;
    shell("set BRU hello");
    CHECK(shell.env.get("BRU").value_or("NONE") == "hello");

    shell("export $BRU");
    CHECK(shell("echo $BRU").output() == "hello\n");
}

// TEST_CASE("shell.builtin.read.prompt") TODO
// {
//     TestShell shell;
//     shell.pty.writeToStdin("hello\n");
//     shell("read -p 'Enter your name: ' NAME");
//     CHECK(shell.output() == "Enter your name: ");
//     CHECK(shell.env.get("NAME").value_or("NONE") == "hello");
// }

// TEST_CASE("shell.syntax.&& ||") TODO
// {
//     TestShell shell;
//     CHECK(shell("true && exit 2 || exit 3") == 2);
//     CHECK(shell("false && exit 2 || exit 3") == 3);
//     CHECK(shell("false || true") == 0);
// }
