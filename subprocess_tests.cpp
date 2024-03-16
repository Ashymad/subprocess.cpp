#include <gtest/gtest.h>

#include "subprocess.h"

using namespace subprocess;

TEST(SubprocessTest, IsTrueTrue) { ASSERT_TRUE(!run(true_)); }

TEST(SubprocessTest, IsFalseFalse) { ASSERT_FALSE(!run(false_)); }

TEST(SubprocessTest, IsFalseAndTrueFalse) {
  ASSERT_FALSE(!run(false_ && true_));
}

TEST(SubprocessTest, IsTrueAndTrueTrue) { ASSERT_TRUE(!run(true_ && true_)); }

TEST(SubprocessTest, IsTrueOrFalseTrue) { ASSERT_TRUE(!run(true_ || false_)); }

TEST(SubprocessTest, IsFalseOrFalseFalse) {
  ASSERT_FALSE(!run(false_ || false_));
}

TEST(SubprocessTest, EchoRead) {
  Environment env{};
  string test1 = "Does";
  string test2 = "echo";
  string test3 = "work";

  ASSERT_TRUE(!env.run(echo(test1) | read("test1")));
  ASSERT_STREQ(test1.c_str(), ${"test1"}.get(env).c_str());

  ASSERT_TRUE(!env.run(read("test2") << test2));
  ASSERT_STREQ(test2.c_str(), ${"test2"}.get(env).c_str());

  ASSERT_TRUE(!env.run(echo(${"test1"}, ${"test2"}, test3, "?") | read("out")));
  ASSERT_STREQ((test1 + " " + test2 + " " + test3 + " ?").c_str(),
               ${"out"}.get(env).c_str());
}

TEST(SubprocessTest, ShortCircuitAnd) {
  Environment env{};
  ASSERT_FALSE(!env.run(false_ && echo("test") | read("test")));
  ASSERT_THROW(${"test"}.get(env), std::out_of_range);
}

TEST(SubprocessTest, ShortCircuitOr) {
  Environment env{};
  ASSERT_TRUE(!env.run(true_ || echo("test") | read("test")));
  ASSERT_THROW(${"test"}.get(env), std::out_of_range);
}

TEST(SubprocessTest, FileWriteRead) {
  Environment env{};
  string test1 = "test1";
  string test2 = "test2";
  ASSERT_TRUE(!env.run(exec("mktemp") | read("tmpfile")));
  ASSERT_TRUE(!env.run(echo(test1) > ${"tmpfile"}));
  ASSERT_TRUE(!env.run(echo(test2) >> ${"tmpfile"}));
  ASSERT_TRUE(!env.run(read("out") < ${"tmpfile"}));
  ASSERT_TRUE(!env.run(exec("rm", ${"tmpfile"})));
  ASSERT_STREQ((test1 + "\n" + test2).c_str(), ${"out"}.get(env).c_str());
}

TEST(SubprocessTest, LongPipe) {
  // The parentheses aren't needed, they're just so gcc shuts up with
  // -Wparentheses
  ASSERT_TRUE(!run(exec("ls", "/etc") | exec("rev") |
                   exec("cut", "-d.", "-f1") | exec("rev") | exec("sort") |
                   (exec("uniq", "-c") > dev::null)));
}
