#include "cli_runner.hpp"
#include <sys/wait.h>
#include <poll.h>
#include <unistd.h>
#include <csignal>
#include <cstring>
#include <vector>

namespace il2bridge::testing {
namespace {

// Drains both pipes concurrently: a child that fills one pipe's buffer while
// we block on the other would deadlock.
void drain(int out_fd, int err_fd, std::string& out, std::string& err) {
    pollfd fds[2] = {{out_fd, POLLIN, 0}, {err_fd, POLLIN, 0}};
    std::string* sinks[2] = {&out, &err};
    int open_count = 2;
    while (open_count > 0) {
        if (poll(fds, 2, -1) < 0) {
            if (errno == EINTR) continue;
            return;
        }
        for (int i = 0; i < 2; ++i) {
            if (fds[i].fd < 0 || !(fds[i].revents & (POLLIN | POLLHUP))) continue;
            char buffer[4096];
            ssize_t count = read(fds[i].fd, buffer, sizeof(buffer));
            if (count > 0) {
                sinks[i]->append(buffer, static_cast<size_t>(count));
            } else if (count == 0 || errno != EINTR) {
                fds[i].fd = -1;
                --open_count;
            }
        }
    }
}

} // namespace

CliResult run_cli(const std::vector<std::string>& arguments) {
    int out_pipe[2];
    int err_pipe[2];
    if (pipe(out_pipe) != 0) return {};
    if (pipe(err_pipe) != 0) return {};

    pid_t child = fork();
    if (child < 0) return {};
    if (child == 0) {
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(IL2BRIDGE_CLI_PATH));
        for (const std::string& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
        argv.push_back(nullptr);
        execv(IL2BRIDGE_CLI_PATH, argv.data());
        _exit(127);
    }

    close(out_pipe[1]);
    close(err_pipe[1]);
    CliResult result;
    drain(out_pipe[0], err_pipe[0], result.out, result.err);
    close(out_pipe[0]);
    close(err_pipe[0]);

    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -WTERMSIG(status);
    return result;
}

} // namespace il2bridge::testing
