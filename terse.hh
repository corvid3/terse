#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <format>
#include <optional>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace terse {

// inline int comptime_id_hack = 0;
// template<typename T>
// inline const int hacki = comptime_id_hack++;
// template<auto S>
// struct tagged
// {
//   static inline int ID = hacki<tagged>;
//   auto constexpr static inline VAL = S;
// };

template<std::size_t N>
struct comptime_str
{
  constexpr comptime_str(char const (&str)[N])
  // ->comptime_str<N, hacki<comptime_str>>
  {
    // -1 cuz null terminator
    std::copy(str, str + N - 1, data);
  };

  constexpr operator std::string_view() const { return { data, data + N - 1 }; }
  // constexpr operator char const*() const { return data; }
  char data[N - 1]{};
};

// name of the argument
template<comptime_str LONGHAND,
         auto SHORTHAND,
         comptime_str USAGE,
         auto CLASS_PTR>
struct Option
{
  static constexpr std::string_view longhand = LONGHAND;
  static constexpr std::optional<char> shorthand = SHORTHAND;
  static constexpr std::string_view usage = USAGE;
  static constexpr auto class_ptr = CLASS_PTR;
};

struct TerminalSubcommand
{
  // provide default empty subcommands
  using subcommands = std::tuple<>;
};

struct NonterminalSubcommand
{
  // provide default empty subcommands
  using subcommands = std::tuple<>;
};

template<typename T>
concept is_terminal_subcommand = requires(T t) {
  { TerminalSubcommand{ t } } -> std::same_as<TerminalSubcommand>;
};

template<typename T>
concept is_nonterminal_subcommand = requires(T t) {
  { NonterminalSubcommand{ t } } -> std::same_as<NonterminalSubcommand>;
};

class _impl
{
  struct Token
  {
    bool is_opt;
    bool is_shorthand;

    std::string_view what;
  };

  void static into_tokqueue(std::queue<Token>& queue, std::string_view what)
  {
    if (what.starts_with("--"))
      queue.push({ true, false, what.substr(2) });

    // shorthand parsing requires an update, for now
    // it is very simple.
    // eventually, one needs to be able to "stack" shorthand
    else if (what.starts_with('-'))
      queue.push({ true, true, what.substr(1) });

    else
      queue.push({ false, false, what });
  }

  // void try_parse_argument(std::stringview)

  // T should be a tuple of Options
  template<typename T>
  std::string_view static convert_to_longhand(char const c)
  {
    return std::apply(
      [&](auto... m) -> std::string_view {
        std::string_view out{};

        // early returns when it finds the first correct match
        if (not([&](auto arg) {
              if (not decltype(arg)::shorthand.has_value())
                return false;

              if (decltype(arg)::shorthand == c) {
                out = decltype(arg)::longhand;
                return true;
              }

              return false;
            }(m) ||
                ...)) {

          // TODO: include parsing stack information
          throw std::runtime_error(
            std::format("unknown shorthand command {}", c));
        }

        return out;
      },
      T());
  }

  template<typename Command>
  void static apply_option(std::queue<Token>& toks,
                           std::string* ptr,
                           std::string_view longhand)
  {
    if (toks.empty())
      throw std::runtime_error(
        std::format("expected string literal after option {}", longhand));

    auto const tok = toks.front();
    toks.pop();

    if (tok.is_opt)
      throw std::runtime_error(
        std::format("expected string literal after option {}", longhand));

    *ptr = std::string(tok.what);
  }

  template<typename Command>
  void static apply_option(std::queue<Token>& toks,
                           bool* ptr,
                           std::string_view longhand)
  {
    *ptr = true;
  }

  template<typename Command, std::integral T>
    requires(not std::same_as<T, bool>)
  void static apply_option(std::queue<Token>& toks,
                           T* ptr,
                           std::string_view longhand)
  {
    if (toks.empty())
      throw std::runtime_error(
        std::format("expected integer literal after option {}", longhand));

    auto const tok = toks.front();
    toks.pop();

    if (tok.is_opt)
      throw std::runtime_error(
        std::format("expected integer literal after option {}", longhand));

    if (std::from_chars(&*tok.what.begin(), &*tok.what.end(), *ptr).ec ==
        std::errc::invalid_argument)
      throw std::runtime_error(
        std::format("option {} requested an integer argument, but did not get "
                    "a valid integer literal",
                    longhand));
  }

  template<typename Command, typename T>
  void static apply_option(std::queue<Token>& toks,
                           std::optional<T>* ptr,
                           std::string_view longhand)
  {
    T m;
    apply_option<Command>(toks, &m, longhand);
    *ptr = std::move(m);
  }

  template<typename Command>
  auto static apply_longhand(std::queue<Token>& toks,
                             Command* cmd,
                             std::string_view longhand)
  {
    std::apply(
      [&](auto&&... opts) {
        if (not([&]<typename OPT>(OPT const& opt) -> bool {
              if (OPT::longhand != longhand)
                return false;

              apply_option<Command>(toks, &(cmd->*OPT::class_ptr), longhand);
              return true;
            }(opts) || ...)) {
          throw std::runtime_error(
            std::format("unable to find option by name of <{}>", longhand));
        }
      },
      typename Command::options());
  }

  template<typename T>
  struct tuple_to_variant_impl;

  template<typename... Ts>
  struct tuple_to_variant_impl<std::tuple<Ts...>>
  {
    using T = std::variant<std::monostate, Ts...>;
  };

  template<typename SUBCOMMAND>
  using tuple_to_variant =
    tuple_to_variant_impl<typename SUBCOMMAND::subcommands>::T;

  template<is_nonterminal_subcommand COMMAND>
  std::pair<COMMAND, tuple_to_variant<COMMAND>> static parse(
    std::queue<Token>& toks,
    std::vector<std::string>& out_bares)
  {
    COMMAND cmd;

    tuple_to_variant<COMMAND> subcommands = std::monostate{};

    for (;;) {
      if (toks.empty())
        break;

      auto const tok = toks.front();

      if (not tok.is_opt)
        break;

      toks.pop();

      {
        std::string_view longhand;

        if (tok.is_shorthand)
          longhand =
            convert_to_longhand<typename COMMAND::options>(tok.what.front());
        else
          longhand = tok.what;

        apply_longhand(toks, &cmd, longhand);
      }
    }

    // just return the subcommand and the
    // monostate to declare no commands after
    if (toks.empty())
      return { cmd, subcommands };

    auto scmd_text = toks.front().what;
    toks.pop();

    std::apply(
      [&](auto&&... scmds) {
        if (not(... || [&]<typename SCMD>(SCMD const&) {
              if (SCMD::name == scmd_text) {
                subcommands = parse<SCMD>(toks, out_bares);
                return true;
              }
              return false;
            }(scmds))) {
          throw std::runtime_error(
            std::format("unknown subcommand {}", scmd_text));
        };
      },
      typename COMMAND::subcommands());

    return { cmd, subcommands };
  }

  template<is_terminal_subcommand COMMAND>
  COMMAND static parse(std::queue<Token>& toks,
                       std::vector<std::string>& out_bares)
  {
    COMMAND cmd;

    // attempt to parse all options

    for (;;) {
      // end of parsing,
      // if this is a nonterminal command
      // then throw an error
      // otherwise return the current parse state
      if (toks.empty())
        break;

      auto const tok = toks.front();
      toks.pop();

      if (tok.is_opt) {
        std::string_view longhand;

        if (tok.is_shorthand)
          longhand =
            convert_to_longhand<typename COMMAND::options>(tok.what.front());
        else
          longhand = tok.what;

        apply_longhand(toks, &cmd, longhand);
      } else
        out_bares.push_back(std::string(tok.what));
    }

    return cmd;
  }

  template<typename TOPLEVEL_SUBCOMMAND>
  friend auto execute(int argc, char** argv);
};

template<typename TOPLEVEL_SUBCOMMAND>
auto
execute(int argc, char** argv)
{
  using out_type_inner = decltype(_impl::parse<TOPLEVEL_SUBCOMMAND>(
    std::declval<std::queue<_impl::Token>&>(),
    std::declval<std::vector<std::string>&>()));

  using out_type = decltype(std::tuple_cat(
    std::declval<out_type_inner>(), std::tuple(std::vector<std::string>())));

  if (argc == 0)
    throw std::runtime_error("malformed argc in terse parse");

  if (argc == 1)
    return out_type{};

  std::queue<_impl::Token> tok_queue;

  for (auto i = 1; i < argc; i++)
    _impl::into_tokqueue(tok_queue, std::string_view(argv[i]));

  // TODO: if -h or --help is detected in any of
  // the arguments, print usage

  // do some jank where we thread the bares vector through
  // the parsing stack, because bares may only
  // appear after a terminal subcommand, but arguments
  // can appear anywhere
  std::vector<std::string> bares;
  auto&& [lhs, rhs] = _impl::parse<TOPLEVEL_SUBCOMMAND>(tok_queue, bares);

  return out_type{ lhs, rhs, bares };
}

template<typename CMD>
std::string
print_usage()
{
  auto constexpr cmd_name = CMD::name;
  auto constexpr cmd_usage = CMD::usage;

  std::stringstream what;

  if (std::apply([]<typename... Ts>(Ts&&...) { return sizeof...(Ts); },
                 typename CMD::subcommands()) > 0) {
    what << std::format("USAGE: {} [subcommand] [arguments]\n", cmd_name);
  } else {
    what << std::format("USAGE: {} [arguments]\n", cmd_name);
  }

  what << cmd_usage << "\n\n";

  if constexpr (std::tuple_size_v<typename CMD::subcommands> > 0) {
    what << "subcommands:\n";

    std::apply(
      [&]<typename... SCMDS>(SCMDS&&...) {
        (what << ...
              << std::format("    {}\x1b[20G{}\n", SCMDS::name, SCMDS::usage));
      },
      typename CMD::subcommands());
  }

  if constexpr (std::tuple_size_v<typename CMD::options> > 0) {
    what << "options:\n";

    auto const get_format = []<typename OPT>() static {
      if constexpr (OPT::shorthand)
        return std::format("    --{}\x1b[20G-{}\x1b[25G{}\n",
                           OPT::longhand,
                           *OPT::shorthand,
                           OPT::usage);
      else
        return std::format("    --{}\x1b[25G{}\n", OPT::longhand, OPT::usage);
    };

    std::apply(
      [&]<typename... OPTS>(OPTS&&...) {
        (what << ... << get_format.template operator()<OPTS>());
      },
      typename CMD::options());
  };

  return what.str();
}

};
