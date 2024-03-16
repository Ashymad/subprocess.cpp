#pragma once

#include <string>
#include <vector>
#include <vector>
#include <memory>
#include <thread>
#include <functional>
#include <map>
#include <stdexcept>

namespace subprocess
{
using std::string;
using std::vector;

typedef std::unique_ptr<class Subprocess> subprocess;
typedef std::unique_ptr<class RunSubprocess> runsubprocess;
typedef std::shared_ptr<class Gettable> gettable;
typedef vector<subprocess> script;
typedef struct GettableVariable $;

// Standard input, output and error file descriptors
struct Streams
{
    static const int None = -1; // No fd for this stream
    static const int New = -2;  // The process needs to create it's own fd for this stream,
                                // it can be retrieved using RunSubprocess::get_streams()

    int in = None;
    int out = None;
    int err = None;
};

// Process capabilites with regards to standard input, output and error
// Each one can be a combination of the flags
// These are checked against the provided Streams struct for compatiblity
struct StreamFlags
{
    static const int Ignore = 1; // Can have no fd
    static const int Create = 2; // Can create a new fd
    static const int Accept = 4; // Can accept a fd

    int in = Ignore;
    int out = Ignore;
    int err = Ignore;
};

// Represents a subprocess that was started and is possibly still running
class RunSubprocess
{
  public:
    // Returns standard streams fds of the running subprocess
    virtual Streams get_streams() const = 0;
    // Waits until the subprocess is finished, returns return code
    virtual int wait() = 0;
};

// Represents the subprocess environment
class Environment
{
    friend class Read;
    friend class Exec;
    friend GettableVariable;

    std::map<string, std::pair<string, bool>> env;

    Environment(char **list);

    vector<const char *> envp() const;

  public:
    Environment(const Environment &env = global);

    // The environment of this process (**environ)
    static const Environment global;

    // Run a subprocess in this environment
    int run(const subprocess &);
    int run(const subprocess &) const;

    // Run a script in this environment
    int run(const script &);
    int run(const script &) const;
};

// Represents an argument that gets evaluated at a subprocess runtime
class Gettable
{
    template <typename T>
    friend subprocess open(const T &path, int mode);
    template <typename S, typename... T>
    friend subprocess make_subprocess(const T &...args);

    // The argument can be either a plain string or an environment variable
    static gettable make_gettable(const string &value);
    static gettable make_gettable(const GettableVariable &var);

  public:
    // This funcion evaluates the argument and returns a string
    virtual string get(const Environment &env) const = 0;
};

class Subprocess
{
  public:
    virtual runsubprocess start(Streams, const Environment & = Environment::global) const = 0;
    virtual runsubprocess start(Streams, Environment &) const;

    virtual subprocess copy() const = 0;

    virtual StreamFlags get_flags() const;
    runsubprocess start() const;

    // Check if the Streams are compatible with the process StreamFlags
    void check_streams(Streams) const;
    static bool check_stream(int, int);
};

template <typename... Ptrs>
script make_script(Ptrs &&...ptrs)
{
    script vec;
    (vec.emplace_back(std::forward<Ptrs>(ptrs)), ...);
    return vec;
}

class Read : public Subprocess
{
    friend subprocess read(const string &name);

    const string name;

    Read(const string &name);

  public:
    StreamFlags get_flags() const override;
    subprocess copy() const override;
    runsubprocess start(Streams, const Environment &) const override;
    runsubprocess start(Streams, Environment &) const override;
};

struct GettableVariable : public Gettable
{
    GettableVariable(const string &name);

    const string name;

    string get(const Environment &env) const;
};

class GettableString : public Gettable
{
    friend class Gettable;

    const string value;

    GettableString(const string &value);

  public:
    string get(const Environment &env) const;
};

class RunPipe : public RunSubprocess
{
    friend class Pipe;

    runsubprocess lhs;
    runsubprocess rhs;

    RunPipe(runsubprocess &&lhs, runsubprocess &&rhs);

  public:
    Streams get_streams() const override;
    int wait() override;
};

class Pipe : public Subprocess
{
    friend subprocess operator|(const subprocess &, const subprocess &);
    template <typename T>
    friend subprocess operator>>(const subprocess &lhs, const T &rhs);
    template <typename T>
    friend subprocess operator>(const subprocess &lhs, const T &rhs);
    template <typename T>
    friend subprocess operator<(const subprocess &lhs, const T &rhs);
    template <typename T>
    friend subprocess operator<<(const subprocess &lhs, const T &rhs);

    Pipe(const subprocess &lhs, const subprocess &rhs);

    template <typename E>
    runsubprocess start(Streams str, E env) const
    {
        check_streams(str);

        StreamFlags lhs_f = lhs->get_flags();
        StreamFlags rhs_f = rhs->get_flags();

        runsubprocess lhs_p;
        runsubprocess rhs_p;

        if ((lhs_f.out & StreamFlags::Create) && (rhs_f.in & StreamFlags::Accept))
        {
            lhs_p = lhs->start({.in = str.in, .out = Streams::New}, env);
            rhs_p = rhs->start({.in = lhs_p->get_streams().out, .out = str.out}, env);
        }
        else if ((lhs_f.out & StreamFlags::Accept) && (rhs_f.in & StreamFlags::Create))
        {
            rhs_p = rhs->start({.in = Streams::New, .out = str.out}, env);
            lhs_p = lhs->start({.in = str.in, .out = rhs_p->get_streams().in}, env);
        }
        else
        {
            throw std::invalid_argument("Invalid pipe connection attempt");
        }

        return runsubprocess(new RunPipe(std::move(lhs_p), std::move(rhs_p)));
    }

  public:
    const subprocess lhs;
    const subprocess rhs;

    StreamFlags get_flags() const override;
    subprocess copy() const override;
    runsubprocess start(Streams, const Environment &) const override;
    runsubprocess start(Streams, Environment &) const override;
};

class Or : public Subprocess
{
    friend subprocess operator||(const subprocess &lhs, const subprocess &rhs);
    subprocess lhs;
    subprocess rhs;

    Or(const subprocess &lhs, const subprocess &rhs);

  public:
    subprocess copy() const override;
    runsubprocess start(Streams, const Environment &) const override;
    runsubprocess start(Streams, Environment &) const override;
};

class And : public Subprocess
{
    friend subprocess operator&&(const subprocess &lhs, const subprocess &rhs);
    subprocess lhs;
    subprocess rhs;

    And(const subprocess &lhs, const subprocess &rhs);

  public:
    subprocess copy() const override;
    runsubprocess start(Streams, const Environment &) const override;
    runsubprocess start(Streams, Environment &) const override;
};

template <typename S, typename... T>
subprocess make_subprocess(const T &...args)
{
    return subprocess(new S({Gettable::make_gettable(args)...}));
}

class Exec : public Subprocess
{
    template <typename S, typename... T>
    friend subprocess make_subprocess(const T &...args);

    vector<gettable> argv;
    Exec(const vector<gettable> &a);

  public:
    StreamFlags get_flags() const override;
    subprocess copy() const override;
    runsubprocess start(Streams, const Environment &) const override;
};

class RunExec : public RunSubprocess
{
    friend Exec;
    pid_t pid;
    Streams streams;
    RunExec(pid_t pid, Streams streams);

  public:
    Streams get_streams() const override;
    int wait() override;
};

class Echo : public Subprocess
{
    template <typename S, typename... T>
    friend subprocess make_subprocess(const T &...args);

    vector<gettable> var;
    Echo(const vector<gettable> &a);

  public:
    StreamFlags get_flags() const override;
    subprocess copy() const override;
    runsubprocess start(Streams, const Environment &) const override;
};

class RunThread : public RunSubprocess
{
    friend Read;
    friend Echo;
    friend And;
    friend Or;

    int ret;
    std::thread thread;
    Streams streams;

    RunThread(std::function<void(int &)> &&fun, Streams str);

  public:
    Streams get_streams() const override;
    int wait() override;
};

class File : public Subprocess
{
    template <typename T>
    friend subprocess open(const T &path, int mode);

    gettable path;
    int mode;

    File(const gettable &path, int mode);

  public:
    static const int Read = 1;
    static const int Write = 2;
    static const int Append = 4;

    StreamFlags get_flags() const override;
    subprocess copy() const override;
    runsubprocess start(Streams, const Environment &) const override;
};

class True : public Subprocess
{
  public:
    subprocess copy() const override;
    runsubprocess start(Streams, const Environment &) const override;
};

class False : public Subprocess
{
  public:
    subprocess copy() const override;
    runsubprocess start(Streams, const Environment &) const override;
};

class RunEmpty : public RunSubprocess
{
    friend class File;
    friend class True;
    friend class False;

    Streams streams;
    int ret;
    RunEmpty(Streams str, int ret);

  public:
    Streams get_streams() const override;
    int wait() override;
};

// Read stdout of pipe into a environment variable ename, requires a non-const Environment
subprocess read(const string &name);

// Open a file, use macros from File:: to specifie open mode
template <typename T>
subprocess open(const T &f, int mode)
{
    return subprocess(new File(Gettable::make_gettable(f), mode));
}

// Echo a string or variable
#define echo make_subprocess<Echo>

// Execute a shell command
#define exec make_subprocess<Exec>

// False: always returns -1
const static subprocess false_ = subprocess(new False());
// True: always returns 0
const static subprocess true_ = subprocess(new True());

// Construct a pipe
subprocess operator|(const subprocess &lhs, const subprocess &rhs);
// Construct an ... or ...
subprocess operator||(const subprocess &lhs, const subprocess &rhs);
// Construct an ... and ...
subprocess operator&&(const subprocess &lhs, const subprocess &rhs);

// Redirect output to a file, append
template <typename T>
subprocess operator>>(const subprocess &lhs, const T &rhs)
{
    return subprocess(new Pipe(lhs, open(rhs, File::Write | File::Append)));
}

// Redirect output to a file, truncate the file
template <typename T>
subprocess operator>(const subprocess &lhs, const T &rhs)
{
    return subprocess(new Pipe(lhs, open(rhs, File::Write)));
}

// Read stdin from a file
template <typename T>
subprocess operator<(const subprocess &lhs, const T &rhs)
{
    return subprocess(new Pipe(open(rhs, File::Read), lhs));
}

// Read stdin from a string
template <typename T>
subprocess operator<<(const subprocess &lhs, const T &rhs)
{
    return subprocess(new Pipe(echo(rhs), lhs));
}

int run(const subprocess &subprocess, Environment &env);
int run(const vector<subprocess> &script, Environment &env);

int run(const subprocess &subprocess, const Environment &env = Environment::global);
int run(const vector<subprocess> &script, const Environment &env = Environment::global);

namespace dev
{
extern const string null;
extern const string zero;
} // namespace dev

} // namespace subprocess
