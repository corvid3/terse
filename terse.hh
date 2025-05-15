#pragma once

#include <format>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace terse {

struct arg_parse_output
{
  std::string program_name;

  // bare arguments, i.e. not defined as a flag value
  // from left->right
  std::vector<std::string_view> bare;

  // all args get converted to longname
  std::map<std::string_view, std::optional<std::string_view>> args;
};

struct arg_parse_option
{
  enum class TakesValue
  {
    No,
    Optional,
    Yes,
  };

  // all args must have a longhand version
  // i.e. --some-arg
  std::string_view longhand;

  // args may optionally have a shorthand version,
  // i.e. -s
  std::optional<char> shorthand;

  TakesValue takes_value;

  std::string_view usage;
};

arg_parse_output
parse_args(int argc,
           char const** argv,
           std::span<arg_parse_option const> parse_flags);

struct usage
{
  std::string_view program_name;
  std::string_view basic_usage;
  std::span<arg_parse_option const> arguments;
};

inline arg_parse_output
parse_args(int const argc,
           char const** argv,
           std::span<arg_parse_option const> parse_flags)
{
  struct arg_parse_option_comp
  {
    bool operator()(arg_parse_option const& lhs,
                    arg_parse_option const& rhs) const
    {
      using hasher = std::hash<std::string_view>;
      return hasher()(lhs.longhand) < hasher()(rhs.longhand);
    }

    bool operator()(std::string_view const rhs,
                    arg_parse_option const& lhs) const
    {
      using hasher = std::hash<std::string_view>;
      return hasher()(lhs.longhand) < hasher()(rhs);
    }

    bool operator()(arg_parse_option const& lhs,
                    std::string_view const rhs) const
    {
      using hasher = std::hash<std::string_view>;
      return hasher()(lhs.longhand) < hasher()(rhs);
    }

    using is_transparent = void;
  };

  std::set<arg_parse_option, arg_parse_option_comp> const parse_options(
    parse_flags.begin(), parse_flags.end());

  arg_parse_output out;

  std::span<char const*> strs(argv, argc);

  int arg_idx = 0;
  out.program_name = argv[arg_idx];
  arg_idx += 1;

  auto check_out_of_bounds = [&] {
    if (arg_idx >= argc)
      throw std::runtime_error(
        "argument expected value but found out of bounds!");
  };

  auto find_by_shorthand =
    [&](char const shorthand) -> arg_parse_option const& {
    auto const longname =
      std::find_if(parse_options.begin(),
                   parse_options.end(),
                   [&](arg_parse_option const& opt) {
                     return opt.shorthand and * opt.shorthand == shorthand;
                   });

    if (longname == parse_options.end())
      throw std::runtime_error(std::format("unknown argument {}", shorthand));

    return *longname;
  };

  auto parse_insert_opt = [&](std::string_view longname) {
    auto const& lookup = parse_options.find(longname);

    if (lookup == parse_options.end())
      throw std::runtime_error(
        std::format("unknown argument found in arg parser {}", longname));

    switch (lookup->takes_value) {
      case arg_parse_option::TakesValue::No:
      no:
        out.args.insert_or_assign(longname, std::nullopt);
        break;

      case arg_parse_option::TakesValue::Optional:
        if (arg_idx + 1 >= (int)strs.size())
          goto no;
        if (std::string_view(strs[arg_idx + 1]).starts_with('-'))
          goto no;
        [[fallthrough]];

      case arg_parse_option::TakesValue::Yes:
        arg_idx += 1;
        check_out_of_bounds();
        out.args.insert_or_assign(longname, argv[arg_idx]);
        break;
    }
  };

  while (arg_idx < argc) {
    std::string_view arg_str(argv[arg_idx]);

    if (arg_str.size() == 0)
      throw std::runtime_error("somehow, arg of size 0 in parse_args");

    // check longhand first
    if (arg_str.starts_with("--")) {
      std::string_view arg_longname(arg_str.begin() + 2, arg_str.end());

      // "--" terminates argument parsing
      if (arg_longname.empty()) {
        while (arg_idx < argc)
          out.bare.push_back(argv[arg_idx++]);

        break;
      }

      // check if we have a collision (i.e. multiple named)
      if (out.args.contains(arg_longname))
        throw std::runtime_error(std::format(
          "multiple arguments of the same longname : {}", arg_longname));

      parse_insert_opt(arg_longname);

    } else if (arg_str.starts_with('-')) {
      std::string_view arg_shortname(arg_str.begin() + 1, arg_str.end());

      if (arg_shortname.size() > 1) {
        for (char const c : arg_shortname) {
          auto const& longname = find_by_shorthand(c);

          if (longname.takes_value == arg_parse_option::TakesValue::Yes)
            throw std::runtime_error(std::format(
              "argument which requires a value cannot be chained : {}",
              longname.longhand));

          out.args.insert_or_assign(longname.longhand, std::nullopt);
        }
      } else {
        // yes this incurs a performance penalty by having
        // to look up multiple times, but i don't want to
        // repeat myself
        auto const& data = find_by_shorthand(arg_shortname.front());
        parse_insert_opt(data.longhand);
      }
    } else {
      out.bare.push_back(arg_str);
    }

    arg_idx += 1;
  }

  return out;
}

inline void
print_usage(usage const& u)
{
  std::cout << std::format("{}\n\t{}\n", u.program_name, u.basic_usage);
  for (auto const& arg : u.arguments) {
    auto const longhand = arg.longhand;
    auto const shorthand = arg.shorthand.value_or(' ');

    std::cout << std::format(
      "--{:10} -{:5}\t{}\n", longhand, shorthand, arg.usage);
  }
}

};
