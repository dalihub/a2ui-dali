/* Copyright (c) 2026 Samsung Electronics Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "expression-parser.h"
#include <dali/integration-api/debug.h>
#include <algorithm>
#include <regex>
#include <sstream>
#include <cmath>
#include <cstdlib>
#include <cctype>
#include <ctime>
#include <unistd.h>
#include <sys/types.h>

using Dali::Ui::Integration::TreeNode;

namespace A2ui
{

namespace
{

/**
 * Index of the next live "${" at or after @p from, or npos.
 *
 * The basic catalog reserves "\${" for a literal dollar-brace ("To include a literal `${`
 * sequence, escape it as `\${`"), so a token preceded by an odd number of backslashes is
 * text, not an expression. Used by both the interpolator and the dependency scan so a
 * literal never gets evaluated *or* watched.
 */
std::size_t FindLiveToken(const std::string& s, std::size_t from)
{
  for(std::size_t p = s.find("${", from); p != std::string::npos; p = s.find("${", p + 2))
  {
    std::size_t backslashes = 0;
    while(p > backslashes && s[p - 1 - backslashes] == '\\') ++backslashes;
    if((backslashes % 2) == 0) return p;
  }
  return std::string::npos;
}

/// Drop the escape from every literal "\${" once interpolation is done.
std::string UnescapeDollarBrace(const std::string& s)
{
  std::string out;
  out.reserve(s.size());
  for(std::size_t i = 0; i < s.size(); ++i)
  {
    if(s[i] == '\\' && i + 2 < s.size() && s[i + 1] == '$' && s[i + 2] == '{')
    {
      out += "${";
      i += 2;
      continue;
    }
    out += s[i];
  }
  return out;
}

} // namespace

ExpressionParser::ExpressionParser()
{
  // === Validation functions ===

  RegisterFunction("required", [](const TreeNode& args, const DataContext& ctx) -> std::string {
    std::string val = ResolveArg(args, "value", ctx);
    return val.empty() ? "false" : "true";
  });

  RegisterFunction("email", [](const TreeNode& args, const DataContext& ctx) -> std::string {
    std::string val = ResolveArg(args, "value", ctx);
    if(val.empty()) return "false";
    static const std::regex emailRegex(R"([a-zA-Z0-9._%+\-]+@[a-zA-Z0-9.\-]+\.[a-zA-Z]{2,})");
    return std::regex_match(val, emailRegex) ? "true" : "false";
  });

  RegisterFunction("regex", [](const TreeNode& args, const DataContext& ctx) -> std::string {
    std::string val = ResolveArg(args, "value", ctx);
    std::string pattern = GetArgString(args, "pattern");
    if(pattern.empty()) return "false";
    try
    {
      return std::regex_match(val, std::regex(pattern)) ? "true" : "false";
    }
    catch(const std::regex_error&)
    {
      DALI_LOG_ERROR("[A2UI] ExpressionParser: invalid regex pattern: %s\n", pattern.c_str());
      return "false";
    }
  });

  RegisterFunction("length", [](const TreeNode& args, const DataContext& ctx) -> std::string {
    std::string val = ResolveArg(args, "value", ctx);
    int len = static_cast<int>(val.size());
    int minLen = GetArgInt(args, "min", 0);
    int maxLen = GetArgInt(args, "max", 0x7FFFFFFF);
    return (len >= minLen && len <= maxLen) ? "true" : "false";
  });

  // === Format functions ===

  RegisterFunction("formatString", [this](const TreeNode& args, const DataContext& ctx) -> std::string {
    std::string value = ResolveArg(args, "value", ctx);
    if(value.empty()) return "";
    return InterpolateString(value, ctx);
  });

  // === Phase 4 validation functions ===

  RegisterFunction("numeric", [](const TreeNode& args, const DataContext& ctx) -> std::string {
    std::string val = ResolveArg(args, "value", ctx);
    if(val.empty()) return "false";
    char* end = nullptr;
    std::strtod(val.c_str(), &end);
    return (end != val.c_str() && *end == '\0') ? "true" : "false";
  });

  // === Phase 4 logical functions ===

  RegisterFunction("and", [this](const TreeNode& args, const DataContext& ctx) -> std::string {
    const TreeNode* valuesNode = args.Find("values");
    if(!valuesNode || valuesNode->GetType() != TreeNode::ARRAY) return "false";
    for(auto it = valuesNode->CBegin(); it != valuesNode->CEnd(); ++it)
    {
      const TreeNode& child = (*it).second;
      std::string result = this->Evaluate(child, ctx);
      if(result != "true") return "false";
    }
    return "true";
  });

  RegisterFunction("or", [this](const TreeNode& args, const DataContext& ctx) -> std::string {
    const TreeNode* valuesNode = args.Find("values");
    if(!valuesNode || valuesNode->GetType() != TreeNode::ARRAY) return "false";
    for(auto it = valuesNode->CBegin(); it != valuesNode->CEnd(); ++it)
    {
      const TreeNode& child = (*it).second;
      std::string result = this->Evaluate(child, ctx);
      if(result == "true") return "true";
    }
    return "false";
  });

  RegisterFunction("not", [this](const TreeNode& args, const DataContext& ctx) -> std::string {
    const TreeNode* valueNode = args.Find("value");
    if(!valueNode) return "true";
    // If value is a function call (has "call" field), evaluate it
    if(valueNode->GetType() == TreeNode::OBJECT && valueNode->Find("call"))
    {
      std::string result = this->Evaluate(*valueNode, ctx);
      return (result == "true") ? "false" : "true";
    }
    std::string val = ResolveArg(args, "value", ctx);
    return (val == "true") ? "false" : "true";
  });

  // === Phase 4 format functions ===

  RegisterFunction("formatCurrency", [](const TreeNode& args, const DataContext& ctx) -> std::string {
    std::string valStr = ResolveArg(args, "value", ctx);
    if(valStr.empty()) return "";

    double num = std::strtod(valStr.c_str(), nullptr);
    std::string currency = GetArgString(args, "currency");
    if(currency.empty()) currency = "USD";

    // Determine symbol
    std::string symbol = "$";
    if(currency == "EUR") symbol = "\xe2\x82\xac";      // Euro sign
    else if(currency == "GBP") symbol = "\xc2\xa3";      // Pound sign
    else if(currency == "JPY") symbol = "\xc2\xa5";      // Yen sign
    else if(currency == "KRW") symbol = "\xe2\x82\xa9";  // Won sign
    else if(currency == "CNY") symbol = "\xc2\xa5";      // Yuan sign

    // Format with 2 decimal places and grouping
    bool isNegative = (num < 0);
    if(isNegative) num = -num;

    std::ostringstream oss;
    oss.precision(currency == "JPY" || currency == "KRW" ? 0 : 2);
    oss << std::fixed << num;

    std::string result = oss.str();

    // Insert thousands separators
    std::string intPart, fracPart;
    size_t dotPos = result.find('.');
    if(dotPos != std::string::npos)
    {
      intPart = result.substr(0, dotPos);
      fracPart = result.substr(dotPos);
    }
    else
    {
      intPart = result;
    }

    std::string grouped;
    int count = 0;
    for(int i = static_cast<int>(intPart.size()) - 1; i >= 0; --i)
    {
      if(count > 0 && count % 3 == 0)
        grouped = "," + grouped;
      grouped = intPart[i] + grouped;
      count++;
    }

    result = symbol + grouped + fracPart;
    if(isNegative) result = "-" + result;
    return result;
  });

  RegisterFunction("formatDate", [](const TreeNode& args, const DataContext& ctx) -> std::string {
    std::string valStr = ResolveArg(args, "value", ctx);
    if(valStr.empty()) return "";

    std::string format = GetArgString(args, "format");
    if(format.empty()) format = "yyyy-MM-dd";

    // Parse ISO 8601 (YYYY-MM-DD[THH:MM[:SS]][Z|±HH:MM]) into integer fields.
    auto toi = [&valStr](size_t pos, size_t n) -> int {
      if(pos + n > valStr.size()) return 0;
      try { return std::stoi(valStr.substr(pos, n)); } catch(...) { return 0; }
    };
    int year = 0, month = 1, day = 1, hour = 0, minute = 0, second = 0;
    if(valStr.size() >= 10) { year = toi(0, 4); month = toi(5, 2); day = toi(8, 2); }
    if(valStr.size() >= 16) { hour = toi(11, 2); minute = toi(14, 2); }
    if(valStr.size() >= 19) { second = toi(17, 2); }

    // Timezone: the web's date-fns formats the instant in the *local* zone. A trailing 'Z'
    // (or ±HH:MM offset) marks the value as UTC, so convert it to local time the same way
    // (10:15Z → 19:15 in a UTC+9 host) instead of printing the raw UTC clock. A value with
    // no zone marker is already local (naive) and is used as-is.
    bool   hasZ = (valStr.find('Z') != std::string::npos || valStr.find('z') != std::string::npos);
    int    offMin = 0;  // explicit ±HH:MM offset in minutes, if any
    {
      size_t tpos = valStr.find('T');
      size_t sgn  = (tpos != std::string::npos) ? valStr.find_first_of("+-", tpos + 1) : std::string::npos;
      if(sgn != std::string::npos && sgn + 6 <= valStr.size() && valStr[sgn + 3] == ':')
      {
        int oh = 0, om = 0;
        try { oh = std::stoi(valStr.substr(sgn + 1, 2)); om = std::stoi(valStr.substr(sgn + 4, 2)); } catch(...) {}
        offMin = (oh * 60 + om) * (valStr[sgn] == '-' ? -1 : 1);
        hasZ = true;
      }
    }
    if(hasZ && valStr.size() >= 16)
    {
      std::tm utc{};
      utc.tm_year = year - 1900; utc.tm_mon = month - 1; utc.tm_mday = day;
      utc.tm_hour = hour; utc.tm_min = minute - offMin; utc.tm_sec = second;
      std::time_t epoch = timegm(&utc);           // interpret fields as UTC → epoch
      std::tm local{};
      localtime_r(&epoch, &local);                // → host-local broken-down time
      year = local.tm_year + 1900; month = local.tm_mon + 1; day = local.tm_mday;
      hour = local.tm_hour; minute = local.tm_min; second = local.tm_sec;
    }
    if(month < 1) month = 1;
    if(month > 12) month = 12;

    // English names + locale-independent weekday (Sakamoto), to match the web (date-fns).
    static const char* kWeekFull[7]  = {"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
    static const char* kWeekShort[7] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    static const char* kMonFull[12]  = {"January","February","March","April","May","June",
                                        "July","August","September","October","November","December"};
    static const char* kMonShort[12] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    static const int   kSk[12]       = {0,3,2,5,0,3,5,1,4,6,2,4};
    int wy = year, wm = month;
    if(wm < 3) wy -= 1;
    int wday = ((wy + wy/4 - wy/100 + wy/400 + kSk[wm-1] + day) % 7 + 7) % 7;

    auto pad2 = [](int v) { std::string s = std::to_string(v); return s.size() < 2 ? "0" + s : s; };
    int h12 = hour % 12; if(h12 == 0) h12 = 12;

    // Walk the format left→right, consuming whole token runs (EEEE, MMM, dd, ...) and
    // 'quoted literals'. Longest-run-wins is implicit: a run of N identical letters is one token.
    std::string out;
    for(size_t i = 0; i < format.size();)
    {
      char c = format[i];
      if(c == '\'')  // quoted literal: copy verbatim until the next quote
      {
        ++i;
        while(i < format.size() && format[i] != '\'') out += format[i++];
        if(i < format.size()) ++i;
        continue;
      }
      if(!std::isalpha(static_cast<unsigned char>(c))) { out += c; ++i; continue; }
      size_t j = i; while(j < format.size() && format[j] == c) ++j;
      int len = static_cast<int>(j - i);
      switch(c)
      {
        case 'E': out += (len >= 4) ? kWeekFull[wday] : kWeekShort[wday]; break;
        case 'M':
          if(len >= 4)      out += kMonFull[month - 1];
          else if(len == 3) out += kMonShort[month - 1];
          else if(len == 2) out += pad2(month);
          else              out += std::to_string(month);
          break;
        case 'd': out += (len >= 2) ? pad2(day) : std::to_string(day); break;
        case 'y':
        case 'Y': out += (len <= 2) ? pad2(year % 100) : std::to_string(year); break;
        case 'h': out += (len >= 2) ? pad2(h12) : std::to_string(h12); break;
        case 'H': out += (len >= 2) ? pad2(hour) : std::to_string(hour); break;
        case 'm': out += (len >= 2) ? pad2(minute) : std::to_string(minute); break;
        case 's': out += (len >= 2) ? pad2(second) : std::to_string(second); break;
        case 'a': out += (hour < 12) ? "AM" : "PM"; break;
        default: break;  // unknown token → omit (date-fns-like)
      }
      i = j;
    }
    return out;
  });

  RegisterFunction("pluralize", [](const TreeNode& args, const DataContext& ctx) -> std::string {
    std::string valStr = ResolveArg(args, "value", ctx);
    double count = valStr.empty() ? 0 : std::strtod(valStr.c_str(), nullptr);

    // Support both spec keys (one/other) and legacy keys (singular/plural)
    std::string one   = GetArgString(args, "one");
    std::string other = GetArgString(args, "other");
    if(one.empty())   one   = GetArgString(args, "singular");
    if(other.empty()) other = GetArgString(args, "plural");
    if(other.empty()) other = one + "s";

    return (std::abs(count) == 1.0) ? one : other;
  });

  RegisterFunction("openUrl", [](const TreeNode& args, const DataContext& ctx) -> std::string {
    std::string url = ResolveArg(args, "url", ctx);
    if(url.empty()) return "false";
    // Validate URL scheme (security: only http/https allowed)
    if(url.substr(0, 7) != "http://" && url.substr(0, 8) != "https://")
    {
      DALI_LOG_ERROR("[A2UI] openUrl: blocked non-http URL: %s\n", url.c_str());
      return "false";
    }
    // On Tizen, use app_control to open browser.
    // For desktop, use fork+exec to avoid command injection.
#ifdef __linux__
    pid_t pid = fork();
    if(pid == 0)
    {
      // Child process — exec xdg-open with URL as a separate argument
      execlp("xdg-open", "xdg-open", url.c_str(), nullptr);
      _exit(127); // exec failed
    }
    // Parent does not wait (fire-and-forget)
#endif
    return "true";
  });

  RegisterFunction("formatNumber", [](const TreeNode& args, const DataContext& ctx) -> std::string {
    std::string valStr = ResolveArg(args, "value", ctx);
    if(valStr.empty()) return "";

    double num = std::strtod(valStr.c_str(), nullptr);

    // Grouping defaults to true (A2UI spec: locale-appropriate formatting)
    const TreeNode* groupingNode = args.Find("grouping");
    bool grouping = true;
    if(groupingNode && groupingNode->GetType() == TreeNode::BOOLEAN)
      grouping = groupingNode->GetBoolean();

    // Check for decimals
    int decimals = GetArgInt(args, "decimals", -1);

    std::ostringstream oss;
    if(decimals >= 0)
    {
      oss.precision(decimals);
      oss << std::fixed << num;
    }
    else
    {
      oss << num;
    }

    std::string result = oss.str();

    if(grouping)
    {
      // Insert thousands separators
      std::string intPart, fracPart;
      size_t dotPos = result.find('.');
      if(dotPos != std::string::npos)
      {
        intPart = result.substr(0, dotPos);
        fracPart = result.substr(dotPos);
      }
      else
      {
        intPart = result;
      }

      // Insert commas from right
      std::string grouped;
      int count = 0;
      bool negative = (!intPart.empty() && intPart[0] == '-');
      size_t startIdx = negative ? 1 : 0;

      for(int i = static_cast<int>(intPart.size()) - 1; i >= static_cast<int>(startIdx); --i)
      {
        if(count > 0 && count % 3 == 0)
        {
          grouped = "," + grouped;
        }
        grouped = intPart[i] + grouped;
        count++;
      }

      if(negative) grouped = "-" + grouped;
      result = grouped + fracPart;
    }

    return result;
  });
}

std::string ExpressionParser::Evaluate(const TreeNode& callNode, const DataContext& ctx) const
{
  const TreeNode* nameNode = callNode.Find("call");
  if(!nameNode || nameNode->GetType() != TreeNode::STRING)
  {
    return "";
  }

  std::string funcName = nameNode->GetString();

  // `@` is reserved for core system context evaluations, which read the render context
  // rather than the catalog — so they are resolved here rather than registered as catalog
  // functions. `args` is optional on these, unlike catalog functions.
  if(funcName == "@index")
  {
    // A2UI restricts @index to Collection Scope: outside a list template there is no
    // iteration to report, and the call is an evaluation error rather than a 0.
    if(ctx.GetCollectionIndex() < 0)
    {
      DALI_LOG_ERROR("[A2UI] ExpressionParser: @index outside a list template\n");
      return "";
    }

    int             offset   = 0;
    const TreeNode* argsNode = callNode.Find("args");
    if(argsNode)
    {
      const std::string raw = ResolveArg(*argsNode, "offset", ctx);
      if(!raw.empty())
      {
        char* end = nullptr;
        const long parsed = std::strtol(raw.c_str(), &end, 10);
        if(end && *end == '\0') offset = static_cast<int>(parsed);
      }
    }
    return std::to_string(ctx.GetCollectionIndex() + offset);
  }

  auto it = mFunctions.find(funcName);
  if(it == mFunctions.end())
  {
    DALI_LOG_ERROR("[A2UI] ExpressionParser: unknown function '%s'\n", funcName.c_str());
    return "";
  }

  const TreeNode* argsNode = callNode.Find("args");
  if(!argsNode)
  {
    // No args provided — pass callNode but functions should handle missing keys gracefully
    DALI_LOG_ERROR("[A2UI] ExpressionParser: function '%s' called without args\n", funcName.c_str());
    return "";
  }

  return it->second(*argsNode, ctx);
}

void ExpressionParser::RegisterFunction(const std::string& name, FunctionImpl impl)
{
  mFunctions[name] = std::move(impl);
}

std::string ExpressionParser::ResolveArg(const TreeNode& args, const char* key,
                                         const DataContext& ctx)
{
  const TreeNode* node = args.Find(key);
  if(!node) return "";

  // Literal string
  if(node->GetType() == TreeNode::STRING)
  {
    return node->GetString();
  }

  // Data binding: {path: "/..."}
  if(node->GetType() == TreeNode::OBJECT)
  {
    const TreeNode* pathNode = node->Find("path");
    if(pathNode && pathNode->GetType() == TreeNode::STRING)
    {
      return ctx.GetString(pathNode->GetString());
    }
  }

  // Numeric values
  if(node->GetType() == TreeNode::INTEGER)
  {
    return std::to_string(node->GetInteger());
  }
  if(node->GetType() == TreeNode::FLOAT)
  {
    return DataModel::FormatFloatToken(node->GetFloat());  // web-style shortest decimal
  }
  if(node->GetType() == TreeNode::BOOLEAN)
  {
    return node->GetBoolean() ? "true" : "false";
  }

  return "";
}

std::string ExpressionParser::GetArgString(const TreeNode& args, const char* key)
{
  const TreeNode* node = args.Find(key);
  if(!node) return "";
  if(node->GetType() == TreeNode::STRING) return node->GetString();
  return "";
}

float ExpressionParser::GetArgFloat(const TreeNode& args, const char* key, float fallback)
{
  const TreeNode* node = args.Find(key);
  if(!node) return fallback;
  if(node->GetType() == TreeNode::FLOAT) return node->GetFloat();
  if(node->GetType() == TreeNode::INTEGER) return static_cast<float>(node->GetInteger());
  return fallback;
}

int ExpressionParser::GetArgInt(const TreeNode& args, const char* key, int fallback)
{
  const TreeNode* node = args.Find(key);
  if(!node) return fallback;
  if(node->GetType() == TreeNode::INTEGER) return node->GetInteger();
  if(node->GetType() == TreeNode::FLOAT) return static_cast<int>(node->GetFloat());
  return fallback;
}

void ExpressionParser::CollectDependencyPaths(const TreeNode& node, const DataContext& ctx,
                                              std::vector<std::string>& out) const
{
  auto add = [&out](std::string path) {
    if(path.empty()) return;
    if(std::find(out.begin(), out.end(), path) == out.end()) out.push_back(std::move(path));
  };

  if(node.GetType() == TreeNode::STRING)
  {
    // A formatString template. Take every "${" and read to the next '}': for a nested
    // "${fn(value:${/b})}" that yields "fn(value:${/b" (a call — its own inner token is
    // visited separately) and "/b" (the real dependency), mirroring how InterpolateString
    // resolves innermost-first.
    const char* raw = node.GetString();
    std::string str = raw ? raw : "";
    for(std::size_t pos = FindLiveToken(str, 0); pos != std::string::npos;
        pos = FindLiveToken(str, pos + 2))
    {
      std::size_t end = str.find('}', pos + 2);
      if(end == std::string::npos) break;
      std::string token = str.substr(pos + 2, end - pos - 2);

      std::size_t b = token.find_first_not_of(" \t");
      std::size_t e = token.find_last_not_of(" \t");
      if(b == std::string::npos) continue;
      token = token.substr(b, e - b + 1);

      if(token[0] == '/')                                 // absolute path
      {
        add(ctx.Resolve(token));
      }
      else if(token.find('(') == std::string::npos)       // bare name → relative path
      {
        // ResolveInlineExpression tries the scoped path first and falls back to the
        // root-level one, so both are dependencies.
        add(ctx.Resolve(token));
        add("/" + token);
      }
      // else: an inline function call — its arguments are separate "${…}" tokens.
    }
    return;
  }

  if(node.GetType() == TreeNode::OBJECT || node.GetType() == TreeNode::ARRAY)
  {
    const TreeNode* pathNode = node.GetChild("path");
    if(pathNode && pathNode->GetType() == TreeNode::STRING)
    {
      add(ctx.Resolve(pathNode->GetString()));
    }
    for(auto it = node.CBegin(); it != node.CEnd(); ++it)
    {
      CollectDependencyPaths((*it).second, ctx, out);
    }
  }
}

std::string ExpressionParser::InterpolateString(const std::string& input, const DataContext& ctx) const
{
  std::string result = input;

  // Iteratively resolve innermost ${...} patterns (inside-out)
  for(int iter = 0; iter < 20; ++iter)
  {
    size_t pos = 0;
    bool found = false;

    while(pos < result.size())
    {
      size_t start = FindLiveToken(result, pos);
      if(start == std::string::npos) break;

      // Find matching '}' — skip nested ${...} by looking for innermost
      size_t end = result.find('}', start + 2);
      if(end == std::string::npos) break;

      size_t nested = FindLiveToken(result, start + 2);
      if(nested != std::string::npos && nested < end)
      {
        // There's a deeper nested ${ — skip to it
        pos = start + 2;
        continue;
      }

      // This is an innermost ${...}
      std::string expr = result.substr(start + 2, end - start - 2);
      std::string resolved = ResolveInlineExpression(expr, ctx);

      result = result.substr(0, start) + resolved + result.substr(end + 1);
      found = true;
      break; // Restart from beginning after replacement
    }

    if(!found) break;
  }

  return UnescapeDollarBrace(result);
}

std::string ExpressionParser::ResolveInlineExpression(const std::string& expr, const DataContext& ctx) const
{
  // 1. Data path: starts with /
  if(!expr.empty() && expr[0] == '/')
  {
    return ctx.GetString(expr);
  }

  // 2. Function call: funcName(key: val, key: 'val', ...)
  size_t parenOpen = expr.find('(');
  if(parenOpen != std::string::npos && !expr.empty() && expr.back() == ')')
  {
    std::string funcName = expr.substr(0, parenOpen);
    std::string argsStr  = expr.substr(parenOpen + 1, expr.size() - parenOpen - 2);

    auto it = mFunctions.find(funcName);
    if(it == mFunctions.end())
    {
      DALI_LOG_ERROR("[A2UI] InterpolateString: unknown function '%s'\n", funcName.c_str());
      return "";
    }

    // Parse inline args "key: val, key: 'str'" → JSON {"key":"val","key":"str"}
    std::ostringstream json;
    json << "{";

    bool first = true;
    size_t pos = 0;
    while(pos < argsStr.size())
    {
      // Skip whitespace
      while(pos < argsStr.size() && argsStr[pos] == ' ') pos++;
      if(pos >= argsStr.size()) break;

      // Read key (until ':')
      size_t colonPos = argsStr.find(':', pos);
      if(colonPos == std::string::npos) break;

      std::string key = argsStr.substr(pos, colonPos - pos);
      while(!key.empty() && key.back() == ' ') key.pop_back();
      pos = colonPos + 1;

      // Skip whitespace after colon
      while(pos < argsStr.size() && argsStr[pos] == ' ') pos++;

      // Read value
      std::string value;
      if(pos < argsStr.size() && (argsStr[pos] == '\'' || argsStr[pos] == '"'))
      {
        // Quoted string
        char quote = argsStr[pos];
        pos++;
        size_t endQ = argsStr.find(quote, pos);
        if(endQ == std::string::npos) endQ = argsStr.size();
        value = argsStr.substr(pos, endQ - pos);
        pos = endQ + 1;
      }
      else
      {
        // Unquoted value (until comma or end)
        size_t endV = argsStr.find(',', pos);
        if(endV == std::string::npos) endV = argsStr.size();
        value = argsStr.substr(pos, endV - pos);
        while(!value.empty() && value.back() == ' ') value.pop_back();
        pos = endV;
      }

      // Skip comma
      if(pos < argsStr.size() && argsStr[pos] == ',') pos++;

      if(!first) json << ",";
      first = false;

      json << "\"" << key << "\":";

      // Check if value is numeric
      char* numEnd = nullptr;
      std::strtod(value.c_str(), &numEnd);
      if(numEnd && numEnd != value.c_str() && *numEnd == '\0' && !value.empty())
      {
        json << value;
      }
      else
      {
        json << "\"" << value << "\"";
      }
    }

    json << "}";

    Dali::Ui::Integration::JsonParser parser = Dali::Ui::Integration::JsonParser::New();
    if(!parser.Parse(json.str()))
    {
      DALI_LOG_ERROR("[A2UI] InterpolateString: failed to parse inline args: %s\n", json.str().c_str());
      return "";
    }

    const TreeNode* root = parser.GetRoot();
    if(!root) return "";

    return it->second(*root, ctx);
  }

  // 3. Simple variable name
  std::string val = ctx.GetString(expr);
  if(val.empty()) val = ctx.GetString("/" + expr);
  return val;
}

} // namespace A2ui
