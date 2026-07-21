#include "renderer/render-internal.h"

namespace A2ui
{

View A2uiRenderer::RenderTextField(const ComponentModel& comp, DataContext& ctx)
{
  if(!comp.rawNode) return View::New();

  FlexLayout container = FlexLayout::New();
  container.SetDirection(FlexDirection::COLUMN);
  container.SetRequestedWidth(MATCH_PARENT);
  container.SetRequestedHeight(WRAP_CONTENT);
  container.SetMargin(Extents(0, 0, static_cast<uint16_t>(Metrics::Dp(4)), static_cast<uint16_t>(Metrics::Dp(4))));

  // Label
  const char* labelText = GetNodeString(*comp.rawNode, "label", "");
  if(labelText[0] != '\0')
  {
    Label label = Label::New(labelText);
    label.SetFontSize(Metrics::FontLabel());
    label.SetTextColor(A2uiTheme::Color("OnSurfaceContainerLow"));  // OneUIColor.OnSurfaceContainerLow
    label.SetRequestedWidth(MATCH_PARENT);
    label.SetRequestedHeight(Metrics::LineHeight(Metrics::FontLabel()));
    label.SetMargin(Extents(0, 0, 0, static_cast<uint16_t>(Metrics::Dp(4))));
    container.Add(label);
  }

  // Bound path for two-way binding
  const TreeNode* valueNode = comp.rawNode->Find("value");
  std::string boundPath = valueNode ? GetBoundPath(valueNode, ctx) : "";

  const char* placeholder = GetNodeString(*comp.rawNode, "placeholder", "");
  std::string currentValue;
  if(!boundPath.empty())
  {
    currentValue = ctx.GetDataModel().GetString(boundPath);
  }

  // Real InputField for text input
  InputField inputField = InputField::New();
  inputField.SetPlaceholder(placeholder);
  inputField.SetPlaceholderColor(A2uiTheme::Color("OnSurfaceContainerLow"));  // neutral placeholder grey
  inputField.SetFontSize(Metrics::FontInput());
  inputField.SetTextColor(COLOR_TEXT_DEFAULT);
  inputField.SetCursorColor(COLOR_TEXT_DEFAULT);
  inputField.SetCursorWidth(2);
  inputField.SetSelectionColor(A2uiTheme::Color("Primary"));
  inputField.SetRequestedWidth(MATCH_PARENT);
  // Web inputs are a fixed ~44px tall box; a WRAP_CONTENT input collapsed to ~24px (much
  // shorter than the web), so pin the height and keep generous horizontal padding.
  inputField.SetRequestedHeight(Metrics::InputHeight());
  inputField.SetMinimumHeight(Metrics::InputHeight());
  inputField.SetPadding(Extents(static_cast<uint16_t>(Metrics::Dp(12)), static_cast<uint16_t>(Metrics::Dp(12)),
                                static_cast<uint16_t>(Metrics::Dp(10)), static_cast<uint16_t>(Metrics::Dp(10))));
  inputField.SetBackgroundColor(COLOR_INPUT_BG);
  inputField.SetCornerRadius(Metrics::RadiusInput());
  inputField.SetBorderlineWidth(Metrics::BorderInput());                 // OneUI outlined input box
  inputField.SetBorderlineColor(COLOR_INPUT_BORDER);   // (was missing → no visible box)

  if(!currentValue.empty())
  {
    inputField.SetText(Dali::String(currentValue.c_str()));
  }

  // Two-way binding: InputField → DataModel
  if(!boundPath.empty())
  {
    inputField.TextChangedSignal().Connect(this,
      [boundPath, ctx](View changedView) mutable {
        InputField field = InputField::DownCast(changedView);
        if(field)
        {
          std::string val = field.GetText().CStr();
          ctx.GetDataModel().SetValue(boundPath, val);
        }
      });

  }

  // DataModel → InputField (server-side updates). Outside the two-way block on purpose:
  // write-back needs a plain {path}, but DISPLAY must also follow a FunctionCall value.
  WatchBinding(valueNode, ctx, [inputField](const std::string& val) mutable {
    std::string current = inputField.GetText().CStr();
    if(current != val)
    {
      inputField.SetText(Dali::String(val.c_str()));
    }
  });

  container.Add(inputField);

  // Error label (initially hidden)
  Label errorLabel = Label::New("");
  errorLabel.SetFontSize(Metrics::FontError());
  errorLabel.SetTextColor(COLOR_ERROR);
  errorLabel.SetRequestedWidth(MATCH_PARENT);
  errorLabel.SetRequestedHeight(WRAP_CONTENT);
  errorLabel.SetProperty(Actor::Property::VISIBLE, false);
  errorLabel.SetMargin(Extents(0, 0, static_cast<uint16_t>(Metrics::Dp(2)), 0));
  container.Add(errorLabel);

  // Setup checks validation (pass the InputField so an invalid field also gets a red outline)
  if(!boundPath.empty())
  {
    SetupChecks(comp, ctx, errorLabel, boundPath, inputField);
  }

  return container;
}
} // namespace A2ui
