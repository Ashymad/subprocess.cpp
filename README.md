# subprocess.cpp

This is a library that tries to bend c++ into behaving like shell for subprocess management.
It is probably not wise to use it as it is, but in case you'd like to try:


If you're `using subprocess`, then:


To run a command and get it's return code:
```cpp
int code = run(exec("cmd", "-arg"));
```
To run a pipe:
```cpp
int code = run(echo("text") | exec("rev") | exec("tee"));
```
ALl the commands run in an `Environment`, but the default one is immutable.
So to be able to save your own environmental variables you'll need to create
your own `Environment`. The defeault `Environment` constructor will already
fill in all the `environ` variables for you.:
```cpp
Environment env{};
int code = env.run(exec("cat", "/proc/cpuinfo") | exec("grep", "-m", "1", "bugs") | read("bugs"));
std::cout << "Bugs: " << ${"bugs"}.get(env) << std::endl;
```
But this is obviously a useless use of cat, so you could replace the second line with:
```cpp
int code = env.run(exec("grep", "-m", "1", "bugs") < "/proc/cpuinfo" | read("bugs"));
```
Your `Environment` will safely store your variable until you want to use it again, wherever a string might go, env var might go as well.
```cpp
int code = env.run(exec("mktemp") | read("tmpfile") && echo(${"bugs"}) > ${"tmpfile"});
```
If your wondering what except `>`, `>>` and `<` file redirections is supported, than you can also use `<<` which would correspond to `<<<` in bash.
```cpp
int code = run(exec("tee") << "Some text to be tee'd");
```
And of course `${"var"}` works there as well. You might have also noticed that
`&&` is supported, and obviously `||` is as well and they do short-circuit like
they should. Besides the already introduced `echo` and `read` built-ins there
is also `false_` and `true_` for your logical pleasure.


With all of these you should be able to create your own script:
```cpp
Environment env{}:
int code = env.run(make_script(
    exec("mktemp") | read("tmpfile"),
    echo("Hello world!") > ${"tmpfile"},
    exec("tee") < ${"tmpfile"}
));
```

