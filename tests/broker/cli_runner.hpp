#pragma once
#include <string>
#include <vector>

namespace il2bridge::testing {

struct CliResult {
    int exit_code = -1;
    std::string out;
    std::string err;
};

// Runs the il2bridge CLI as a child process and captures its streams. The CLI
// is a separate process by design: its contract is argv in, stdout and an exit
// code out, and nothing in main.cpp is reachable from a linked test.
CliResult run_cli(const std::vector<std::string>& arguments);

} // namespace il2bridge::testing
