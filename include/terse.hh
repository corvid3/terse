#pragma once

#include <algorithm>
#include <array>
#include <charconv>
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
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace terse {

template<std::size_t N>
struct comptime_str
{
  constexpr comptime_str(char const (&str)[N])
  {
    std::copy(str, str + N - 1, data);
  };

  constexpr operator std::string_view() const { return { data, data + N - 1 }; }
  char data[N - 1]{};
};

// name of the argument
template<comptime_str LONGHAND,
         auto SHORTHAND,
         comptime_str USAGE,
         auto CLASS_PTR>
struct Option
{
  static constexpr comptime_str longhand = LONGHAND;
  static constexpr std::optional<char> shorthand = SHORTHAND;
  static constexpr comptime_str usage = USAGE;
  static constexpr auto class_ptr = CLASS_PTR;
};

template<typename T>
concept is_nonterminal_subcommand = requires(T t) { t.terse_subcmds; };
template<typename T>
concept is_terminal_subcommand = !is_nonterminal_subcommand<T>;

template<typename T>
concept has_bares = requires(T t) {
  requires std::same_as<decltype(t.terse_bares), std::vector<std::string>>;
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
    else if (what.starts_with('-'))
      queue.push({ true, true, what.substr(1) });
    else
      queue.push({ false, false, what });
  }

  // void try_parse_argument(std::stringview)

  // T should be a tuple of Options
  template<typename T>
  auto static convert_to_longhand(char const c) -> std::string_view
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
  void static apply_option(std::queue<Token>& /*unused*/,
                           bool* ptr,
                           std::string_view /*unused*/)
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
        std::errc::invalid_argument) {
      throw std::runtime_error(
        std::format("option {} requested an integer argument, but did not get "
                    "a valid integer literal",
                    longhand));
    }
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
        if (not([&]<typename OPT>(OPT const&) -> bool {
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
  struct vtt;

  template<typename... Ts>
  struct vtt<std::variant<Ts...>>
  {
    using T = std::tuple<Ts...>;
  };

  template<typename T>
  using variant_to_tuple = vtt<T>::T;

  template<std::size_t I = 0, typename V>
  auto static subcmdparse(auto& toks,
                          auto& out_bares,
                          std::string_view scmd_text) -> V
  {
    if constexpr (I >= std::variant_size_v<V>)
      throw std::runtime_error(std::format("unknown subcommand {}", scmd_text));
    else {
      using SCMD = std::variant_alternative_t<I, V>;
      if constexpr (std::same_as<SCMD, std::monostate>)
        return subcmdparse<I + 1, V>(toks, out_bares, scmd_text);
      else {
        if (SCMD::name == scmd_text)
          return V(parse<SCMD>(toks, out_bares));
        return subcmdparse<I + 1, V>(toks, out_bares, scmd_text);
      }
    }
  }

  template<is_nonterminal_subcommand COMMAND>
  auto static parse(std::queue<Token>& toks,
                    std::vector<std::string>& out_bares) -> COMMAND
  {
    COMMAND cmd;

    for (;;) {
      if (toks.empty())
        break;

      auto const tok = toks.front();

      if (not tok.is_opt)
        break;

      toks.pop();

      {
        std::string_view longhand;

        if (tok.is_shorthand) {
          longhand =
            convert_to_longhand<typename COMMAND::options>(tok.what.front());
        } else {
          longhand = tok.what;
        }

        apply_longhand(toks, &cmd, longhand);
      }
    }

    // just return the subcommand and the
    // monostate to declare no commands after
    if (toks.empty())
      return cmd;

    auto scmd_text = toks.front().what;
    toks.pop();

    cmd.terse_subcmds = subcmdparse<0, decltype(COMMAND::terse_subcmds)>(
      toks, out_bares, scmd_text);

    return cmd;
  }

  template<is_terminal_subcommand COMMAND>
  auto static parse(std::queue<Token>& toks,
                    std::vector<std::string>& out_bares) -> COMMAND
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

        if (tok.is_shorthand) {
          longhand =
            convert_to_longhand<typename COMMAND::options>(tok.what.front());
        } else {
          longhand = tok.what;
        }

        apply_longhand(toks, &cmd, longhand);
      } else {
        out_bares.emplace_back(tok.what);
      }
    }

    return cmd;
  }

  template<typename TOPLEVEL_SUBCOMMAND>
  friend auto execute(int argc, char** argv);

  template<typename scmd, typename tuple>
  friend auto get(tuple& t) -> decltype(auto);
  template<typename scmd, typename tuple>
  friend auto holds(tuple& t) -> decltype(auto);

  template<typename CMD>
  friend auto print_usage() -> std::string;

  static void set_bares(std::vector<std::string> const& bares,
                        has_bares auto& i)
  {
    i.terse_bares = bares;
  }

  static void set_bares(const std::vector<std::string>& /*unused*/,
                        auto const& /*unused*/) {};
};

template<typename TOPLEVEL_SUBCOMMAND>
auto
execute(int argc, char** argv)
{
  using out_type = decltype(_impl::parse<TOPLEVEL_SUBCOMMAND>(
    std::declval<std::queue<_impl::Token>&>(),
    std::declval<std::vector<std::string>&>()));

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
  auto out = _impl::parse<TOPLEVEL_SUBCOMMAND>(tok_queue, bares);
  _impl::set_bares(bares, out);
  return out;
}

template<typename T>
struct member_pointer_destructure;

template<typename T, typename C>
struct member_pointer_destructure<T C::* const>
{
  using type = T;
};

template<typename T>
using member_pointer_destructure_t =
  typename member_pointer_destructure<T>::type;

template<typename CMD>
auto
print_usage() -> std::string
{
  // simple name of the subcommand
  auto constexpr cmd_name = (std::string_view)CMD::name;

  // what bares/opts are taken
  auto constexpr cmd_usage = (std::string_view)CMD::usage;

  // short and sweet description
  // auto constexpr cmd_shortdesc = (std::string_view)CMD::short_description;

  // lengthy explanation
  auto constexpr cmd_description = (std::string_view)CMD::description;

  std::stringstream what;

  what << cmd_name << ' ' << cmd_usage << "\n\n" << cmd_description << "\n\n";

  /* FIXME: print usage for subcommands */
  // if constexpr (std::tuple_size_v<
  //                 _impl::variant_to_tuple<typename CMD::terse_subcmds>> > 0)
  //                 {
  //   what << "subcommands:\n";
  //   std::apply(
  //     [&]<typename... SCMDS>(SCMDS&&...) {
  //       (what << ...
  //             << std::format("    {}\x1b[20G{}\n",
  //                            (std::string_view)SCMDS::name,
  //                            (std::string_view)SCMDS::short_description));
  //     },
  //     _impl::variant_to_tuple<typename CMD::terse_subcmds>());
  // }

  if constexpr (std::tuple_size_v<typename CMD::options> > 0) {
    what << "\n";

    auto const get_format = []<typename OPT>() static {
      if constexpr (OPT::shorthand) {
        return std::format(
          "    --{}\x1b[20G-{}\x1b[23G{}\x1b[30G{}\n",
          (std::string_view)OPT::longhand,
          *OPT::shorthand,
          std::same_as<member_pointer_destructure_t<decltype(OPT::class_ptr)>,
                       bool>
            ? ""
            : "<val>",
          (std::string_view)OPT::usage);
      } else {
        return std::format(
          "    --{}\x1b[23G{}\x1b[30G{}\n",
          (std::string_view)OPT::longhand,
          std::same_as<member_pointer_destructure_t<decltype(OPT::class_ptr)>,
                       bool>
            ? ""
            : "<val>",
          (std::string_view)OPT::usage);
      }
    };

    std::apply(
      [&]<typename... OPTS>(OPTS&&...) {
        (what << ... << get_format.template operator()<OPTS>());
      },
      typename CMD::options());
  };

  return what.str();
}

// template<typename scmd, typename tuple>
// auto
// get(tuple& t) -> decltype(auto)
// {
//   return std::get<_impl::Selector<scmd>>(t);
// }

// template<typename scmd, typename tuple>
// auto
// holds(tuple& t) -> decltype(auto)
// {
//   return std::holds_alternative<_impl::Selector<scmd>>(t);
// }

};
