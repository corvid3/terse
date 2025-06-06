#include "terse.hh"
#include <format>
#include <iostream>
#include <optional>
#include <tuple>
#include <variant>

struct Foo : terse::TerminalSubcommand
{
  constexpr static auto name = "foo";

  bool inner_verbose = false;

  using options = std::tuple<
    terse::
      Option<"verbose", 'v', "prints verbosely, extra", &Foo::inner_verbose>>;
};

struct ToplevelOptions : terse::NonterminalSubcommand
{
  bool verbose = false;
  int m = 0;
  std::string pathing;

  using options = std::tuple<
    terse::
      Option<"verbose", 'v', "prints verbosely", &ToplevelOptions::verbose>,
    terse::Option<"mem", 'm', "aa", &ToplevelOptions::m>,
    terse::Option<"path", 'p', "sets path", &ToplevelOptions::pathing>>;

  using subcommands = std::tuple<Foo>;

  static constexpr auto const name = "test";
  static constexpr auto const usage = "usage test";
};

int
main(int argc, char** argv)
{
  auto [opts, scmds, bares] = terse::execute<ToplevelOptions>(argc, argv);

  std::cout << std::format("tl verbose: {}\n", opts.verbose);

  if (std::holds_alternative<Foo>(scmds)) {
    auto const& foo = std::get<Foo>(scmds);
    std::cout << std::format("inner verbose: {}\n", foo.inner_verbose);
  }

  std::cout << terse::print_usage<ToplevelOptions>();
}
