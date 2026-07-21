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

#ifndef DALI_GENERATIVE_UI_EXPRESSION_PARSER_H
#define DALI_GENERATIVE_UI_EXPRESSION_PARSER_H

#include "data-context.h"
#include <string>
#include <functional>
#include <unordered_map>
#include <vector>
#include <dali-ui-foundation/integration-api/builder/tree-node.h>
#include <dali-ui-foundation/integration-api/builder/json-parser.h>

namespace A2ui
{

/**
 * Evaluates A2UI client-side function calls ({call: "name", args: {...}}).
 *
 * Built-in validation functions: required, regex, email, length
 * Built-in format functions: formatString, formatNumber
 */
class ExpressionParser
{
public:
  using FunctionImpl = std::function<std::string(const Dali::Ui::Integration::TreeNode& args,
                                                 const DataContext& ctx)>;

  ExpressionParser();

  // Non-copyable (lambdas capture `this`)
  ExpressionParser(const ExpressionParser&) = delete;
  ExpressionParser& operator=(const ExpressionParser&) = delete;
  ExpressionParser(ExpressionParser&&) = default;
  ExpressionParser& operator=(ExpressionParser&&) = default;

  /**
   * Evaluate a function call node.
   * @param callNode  TreeNode with "call" and "args" fields
   * @param ctx       Data context for resolving bound values
   * @return result string ("true"/"false" for validators, formatted string for formatters)
   */
  std::string Evaluate(const Dali::Ui::Integration::TreeNode& callNode, const DataContext& ctx) const;

  /**
   * Register a custom function.
   */
  void RegisterFunction(const std::string& name, FunctionImpl impl);

  /**
   * Every data-model path a binding node reads, resolved against @p ctx.
   *
   * A binding is only reactive if we know what it depends on. For a plain
   * {"path": …} that is the path itself, but a FunctionCall hides its inputs:
   * nested {"path": …} nodes inside `args`, and — because `formatString` embeds
   * expressions in a string literal — "${…}" tokens that no JSON walk would see.
   * The spec requires such a call to re-run whenever any of those change, so the
   * renderer watches every path this returns.
   *
   * @param[in]  node  The binding node (a property value; may be any node type)
   * @param[in]  ctx   Scope used to resolve relative paths (list item scope)
   * @param[out] out   Resolved absolute paths, appended, without duplicates
   */
  void CollectDependencyPaths(const Dali::Ui::Integration::TreeNode& node,
                              const DataContext& ctx,
                              std::vector<std::string>& out) const;

private:
  /**
   * Resolve an argument value: if it's a {path:...} binding, resolve via DataContext;
   * if it's a literal, return its string value.
   */
  static std::string ResolveArg(const Dali::Ui::Integration::TreeNode& args,
                                const char* key,
                                const DataContext& ctx);

  static std::string GetArgString(const Dali::Ui::Integration::TreeNode& args, const char* key);
  static float       GetArgFloat(const Dali::Ui::Integration::TreeNode& args, const char* key, float fallback = 0.0f);
  static int         GetArgInt(const Dali::Ui::Integration::TreeNode& args, const char* key, int fallback = 0);

  /**
   * Interpolate a format string, resolving nested ${...} patterns inside-out.
   * Supports data paths (${/path}) and inline function calls (${func(key: val)}).
   */
  std::string InterpolateString(const std::string& input, const DataContext& ctx) const;

  /**
   * Resolve a single inline expression (content between ${ and }).
   */
  std::string ResolveInlineExpression(const std::string& expr, const DataContext& ctx) const;

  std::unordered_map<std::string, FunctionImpl> mFunctions;
};

} // namespace A2ui

#endif // DALI_GENERATIVE_UI_EXPRESSION_PARSER_H
