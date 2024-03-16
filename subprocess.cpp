#include "subprocess.h"
#include <sys/wait.h>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <system_error>

namespace subprocess
{
using std::string;
using std::vector;

subprocess operator|(const subprocess &lhs, const subprocess &rhs)
{
    return subprocess(new Pipe(lhs, rhs));
}

subprocess operator||(const subprocess &lhs, const subprocess &rhs)
{
    return subprocess(new Or(lhs, rhs));
}

subprocess operator&&(const subprocess &lhs, const subprocess &rhs)
{
    return subprocess(new And(lhs, rhs));
}

int run(const subprocess &subprocess, Environment &env)
{
    return subprocess->start({}, env)->wait();
}

int run(const script &scr, Environment &env)
{
    for (const subprocess &sub : scr)
    {
        int r = run(sub, env);
        if (r != 0)
            return r;
    }
    return 0;
}

int run(const subprocess &subprocess, const Environment &env)
{
    return subprocess->start({}, env)->wait();
}

int run(const script &scr, const Environment &env)
{
    for (const subprocess &sub : scr)
    {
        int r = run(sub, env);
        if (r != 0)
            return r;
    }
    return 0;
}

Environment::Environment(char **list) :
    env{}
{
    for (size_t i = 0; list[i]; i++)
    {
        const char *del = strchr(list[i], '=');
        env.emplace(string(list[i], del - list[i]), std::pair<string, bool>(string(del + 1), true));
    }
};

Environment::Environment(const Environment &env) :
    env(env.env)
{
}

vector<const char *> Environment::envp() const
{
    vector<const char *> out{};
    for (auto const &el : env)
    {
        if (el.second.second)
        {
            out.push_back(el.second.first.c_str());
        }
    }
    out.push_back(nullptr);
    return out;
}

GettableVariable::GettableVariable(const string &name) :
    name(name)
{
}

string GettableVariable::get(const Environment &env) const
{
    return env.env.at(name).first;
}

int Environment::run(const subprocess &sub)
{
    return ::subprocess::run(sub, *this);
}

int Environment::run(const subprocess &sub) const
{
    return ::subprocess::run(sub, *this);
}

int Environment::run(const script &sub)
{
    return ::subprocess::run(sub, *this);
}

int Environment::run(const script &sub) const
{
    return ::subprocess::run(sub, *this);
}

const Environment Environment::global{environ};

gettable Gettable::make_gettable(const string &value)
{
    return gettable(new GettableString(value));
}

gettable Gettable::make_gettable(const GettableVariable &var)
{
    return gettable(new GettableVariable(var));
}

subprocess read(const string &name)
{
    return subprocess(new Read(name));
}

GettableString::GettableString(const string &value) :
    value(value)
{
}
string GettableString::get(const Environment &env) const
{
    return value;
}

runsubprocess Subprocess::start() const
{
    return this->start({});
}

runsubprocess Subprocess::start(Streams str, Environment &env) const
{
    return start(str, const_cast<const Environment &>(env));
}

StreamFlags Subprocess::get_flags() const
{
    return {};
}

Pipe::Pipe(const subprocess &lhs, const subprocess &rhs) :
    lhs(lhs->copy()),
    rhs(rhs->copy())
{
}

subprocess Pipe::copy() const
{
    return subprocess(new Pipe(lhs, rhs));
}

StreamFlags Pipe::get_flags() const
{
    return {.in = lhs->get_flags().in, .out = rhs->get_flags().out, .err = rhs->get_flags().out};
}

runsubprocess Pipe::start(Streams str, const Environment &env) const
{
    return start<const Environment &>(str, env);
}

runsubprocess Pipe::start(Streams str, Environment &env) const
{
    return start<Environment &>(str, env);
}

RunPipe::RunPipe(runsubprocess &&lhs, runsubprocess &&rhs) :
    lhs(std::move(lhs)),
    rhs(std::move(rhs))
{
}

Streams RunPipe::get_streams() const
{
    return {.in = lhs->get_streams().in, .out = rhs->get_streams().out, .err = rhs->get_streams().err};
}

int RunPipe::wait()
{
    return lhs->wait() | rhs->wait();
}

Exec::Exec(const vector<gettable> &a) :
    argv(a)
{
}

StreamFlags Exec::get_flags() const
{
    return {
        .in = StreamFlags::Create | StreamFlags::Accept | StreamFlags::Ignore,
        .out = StreamFlags::Create | StreamFlags::Accept | StreamFlags::Ignore,
        .err = StreamFlags::Create | StreamFlags::Accept | StreamFlags::Ignore
    };
}

bool Subprocess::check_stream(int stream, int flag)
{
    return (stream == Streams::None && (flag & StreamFlags::Ignore)) ||
           (stream == Streams::New && (flag & StreamFlags::Create)) || (stream >= 0 && (flag & StreamFlags::Accept));
}

void Subprocess::check_streams(Streams streams) const
{
    StreamFlags flags = get_flags();
    if (!check_stream(streams.in, flags.in))
        throw std::invalid_argument("Wrong file descriptor option for stdin");
    if (!check_stream(streams.out, flags.out))
        throw std::invalid_argument("Wrong file descriptor option for stdout");
    if (!check_stream(streams.err, flags.err))
        throw std::invalid_argument("Wrong file descriptor option for stderr");
}

static void open_pipe(int &str, int fds[2], bool input)
{
    if (str != Streams::None)
    {
        if (str == Streams::New)
        {
            if (::pipe2(fds, O_CLOEXEC))
            {
                throw std::system_error(std::error_code(errno, std::system_category()), strerror(errno));
            };
            str = fds[input ? 1 : 0];
        }
        else
        {
            fds[input ? 0 : 1] = str;
        }
    }
}

static void close_pipe(int fd)
{
    if (fd != Streams::None)
        close(fd);
}

runsubprocess Exec::start(Streams str, const Environment &env) const
{
    check_streams(str);
    int fds[3][2];
    memset(fds, Streams::None, sizeof(fds));

    open_pipe(str.in, fds[STDIN_FILENO], true);
    open_pipe(str.out, fds[STDOUT_FILENO], false);
    open_pipe(str.err, fds[STDERR_FILENO], false);

    pid_t pid = fork();
    if (pid == 0)
    { // child
        close_pipe(fds[STDIN_FILENO][1]);
        close_pipe(fds[STDOUT_FILENO][0]);
        close_pipe(fds[STDERR_FILENO][0]);

        if (fds[STDIN_FILENO][0] != Streams::None)
            dup2(fds[STDIN_FILENO][0], STDIN_FILENO);
        if (fds[STDOUT_FILENO][1] != Streams::None)
            dup2(fds[STDOUT_FILENO][1], STDOUT_FILENO);
        if (fds[STDERR_FILENO][1] != Streams::None)
            dup2(fds[STDERR_FILENO][1], STDERR_FILENO);

        vector<char *> c_argv{};
        c_argv.reserve(argv.size() + 1);

        for (auto &s : argv)
        {
            // The allocated strings will be freed once the child dies
            c_argv.push_back(strdup(s->get(env).c_str()));
        }
        c_argv.push_back(NULL);

        // Const casting here is safe, execve guarantees the arguments not to be changed in any way
        execvpe(c_argv[0], c_argv.data(), const_cast<char *const *>(env.envp().data()));
        exit(errno);
    }
    else if (pid < 0)
    { // error
        throw std::system_error(std::error_code(errno, std::system_category()), strerror(errno));
    }
    else
    { // parent
        close_pipe(fds[STDIN_FILENO][0]);
        close_pipe(fds[STDOUT_FILENO][1]);
        close_pipe(fds[STDERR_FILENO][1]);

        return runsubprocess(new RunExec(pid, str));
    }
}

subprocess Exec::copy() const
{
    return subprocess(new Exec(argv));
}

RunExec::RunExec(pid_t pid, Streams streams) :
    pid(pid),
    streams(streams)
{
}

int RunExec::wait()
{
    siginfo_t siginfo;
    if (waitid(P_PID, pid, &siginfo, WEXITED | WSTOPPED))
        return -1;
    return siginfo.si_status;
}

Streams RunExec::get_streams() const
{
    return streams;
}

Or::Or(const subprocess &lhs, const subprocess &rhs) :
    lhs(lhs->copy()),
    rhs(rhs->copy())
{
}

subprocess Or::copy() const
{
    return subprocess(new Or(lhs, rhs));
}

runsubprocess Or::start(Streams str, const Environment &env) const
{
    check_streams(str);
    return runsubprocess(
        new RunThread([&lhs = lhs, &rhs = rhs, &env](int &ret) { ret = !(!run(lhs, env) || !run(rhs, env)); }, str)
    );
}

runsubprocess Or::start(Streams str, Environment &env) const
{
    check_streams(str);
    return runsubprocess(
        new RunThread([&lhs = lhs, &rhs = rhs, &env](int &ret) { ret = !(!run(lhs, env) || !run(rhs, env)); }, str)
    );
}

And::And(const subprocess &lhs, const subprocess &rhs) :
    lhs(lhs->copy()),
    rhs(rhs->copy())
{
}

subprocess And::copy() const
{
    return subprocess(new And(lhs, rhs));
}

runsubprocess And::start(Streams str, const Environment &env) const
{
    check_streams(str);
    return runsubprocess(
        new RunThread([&lhs = lhs, &rhs = rhs, &env](int &ret) { ret = !(!run(lhs, env) && !run(rhs, env)); }, str)
    );
}

runsubprocess And::start(Streams str, Environment &env) const
{
    check_streams(str);
    return runsubprocess(
        new RunThread([&lhs = lhs, &rhs = rhs, &env](int &ret) { ret = !(!run(lhs, env) && !run(rhs, env)); }, str)
    );
}

RunThread::RunThread(std::function<void(int &)> &&fun, Streams str) :
    ret(0),
    thread(std::thread(std::move(fun), std::ref(ret))),
    streams(str)
{
}
Streams RunThread::get_streams() const
{
    return streams;
}
int RunThread::wait()
{
    thread.join();
    return ret;
}

Read::Read(const string &name) :
    name(name)
{
}

StreamFlags Read::get_flags() const
{
    return {.in = StreamFlags::Create | StreamFlags::Accept, .out = StreamFlags::Ignore, .err = StreamFlags::Ignore};
}

subprocess Read::copy() const
{
    return subprocess(new Read(name));
}

runsubprocess Read::start(Streams str, const Environment &env) const
{
    throw std::invalid_argument("Can't create variable in const Environment");
}

runsubprocess Read::start(Streams str, Environment &env) const
{
    check_streams(str);
    int fds[2];
    open_pipe(str.in, fds, true);

    return runsubprocess(new RunThread(
        [fds, &env, &name = name](int &ret) {
            const size_t bufsz = 1000;
            char buffer[bufsz];
            string out{};
            while (true)
            {
                ssize_t count = ::read(fds[0], buffer, bufsz);
                if (count < 0)
                {
                    ret = errno;
                    return;
                }
                else if (count == 0)
                    break;

                out.append(buffer, count);
            }
            close(fds[0]);
            // Remove trailing newline
            if (!out.empty() && out.at(out.length() - 1) == '\n')
                out.erase(out.length() - 1);

            env.env[name] = {out, false};
        },
        str
    ));
}

Echo::Echo(const vector<gettable> &a) :
    var(a)
{
}

StreamFlags Echo::get_flags() const
{
    return {.in = StreamFlags::Ignore, .out = StreamFlags::Create | StreamFlags::Accept, .err = StreamFlags::Ignore};
}

subprocess Echo::copy() const
{
    return subprocess(new Echo(var));
}

runsubprocess Echo::start(Streams str, const Environment &env) const
{
    check_streams(str);
    int fds[2];
    open_pipe(str.out, fds, false);

    return runsubprocess(new RunThread(
        [fds, &var = var, &env](int &ret) {
            size_t i = 0;
            for (const auto &gt : var)
            {
                string str = gt->get(env);
                ssize_t count = 0;
                while (true)
                {
                    ssize_t ret = ::write(fds[1], &str[count], str.size() - count);
                    if (ret < 0)
                    {
                        ret = errno;
                        return;
                    }
                    else if (static_cast<size_t>(count += ret) == str.size())
                    {
                        if (++i != var.size() && ::write(fds[1], " ", 1) < 0)
                        {
                            ret = errno;
                            return;
                        }
                        break;
                    }
                }
            }
            if (::write(fds[1], "\n", 1) < 0)
                ret = errno;
            close(fds[1]);
        },
        str
    ));
}

RunEmpty::RunEmpty(Streams streams, int ret) :
    streams(streams),
    ret(ret)
{
}

int RunEmpty::wait()
{
    return ret;
}

Streams RunEmpty::get_streams() const
{
    return streams;
}

subprocess True()
{
    return subprocess(new class True());
}

subprocess True::copy() const
{
    return subprocess(new True());
}

runsubprocess True::start(Streams str, const Environment &env) const
{
    check_streams(str);
    return runsubprocess(new RunEmpty({}, 0));
}

subprocess False()
{
    return subprocess(new class False());
}

subprocess False::copy() const
{
    return subprocess(new False());
}

runsubprocess False::start(Streams str, const Environment &env) const
{
    check_streams(str);
    return runsubprocess(new RunEmpty({}, -1));
}

File::File(const gettable &path, int mode) :
    path(path),
    mode(mode)
{
}

StreamFlags File::get_flags() const
{
    return {
        .in = mode & File::Write ? StreamFlags::Create : StreamFlags::Ignore,
        .out = mode & File::Read ? StreamFlags::Create : StreamFlags::Ignore,
        .err = StreamFlags::Ignore
    };
}

subprocess File::copy() const
{
    return subprocess(new File(path, mode));
};

runsubprocess File::start(Streams str, const Environment &env) const
{
    check_streams(str);
    int flags = O_CLOEXEC | O_CREAT;

    if ((mode & File::Read) && (mode & File::Write))
        flags |= O_RDWR | ((mode & File::Append) ? O_APPEND : 0);
    else if (mode & File::Write)
        flags |= O_WRONLY | ((mode & File::Append) ? O_APPEND : O_TRUNC);
    else if (mode & File::Read)
        flags |= O_RDONLY;

    int fd = ::open(path->get(env).c_str(), flags, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

    if (fd < 0)
        throw std::system_error(std::error_code(errno, std::system_category()), strerror(errno));

    Streams streams{
        .in = mode & File::Write ? fd : Streams::None,
        .out = mode & File::Read ? fd : Streams::None,
        .err = Streams::None
    };

    return runsubprocess(new RunEmpty(streams, 0));
}

namespace dev
{
const string null = "/dev/null";
const string zero = "/dev/zero";
} // namespace dev

} // namespace subprocess
