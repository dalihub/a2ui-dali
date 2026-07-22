#include "renderer/render-internal.h"
#include <dali/devel-api/adaptor-framework/image-loading.h>

namespace A2ui
{

View A2uiRenderer::RenderImage(const ComponentModel& comp, DataContext& ctx)
{
  if(!comp.rawNode) return View::New();

  const TreeNode* urlNode = comp.rawNode->Find("url");
  std::string url = urlNode ? ResolveString(urlNode, ctx) : "";

  const char* variant = GetNodeString(*comp.rawNode, "variant", "");

  // Web composer rule (viewerTheme Image.all = width:100%, object-fit:cover): an image with
  // no explicit size variant fills the container width and takes its height from its own
  // aspect ratio — a full-width banner, NOT a centred square. Fixed-size variants
  // (icon/avatar/feature) keep the catalog's boxes; a Row thumbnail stays small.
  bool thumbnail = mImageThumbnailHint;
  mImageThumbnailHint = false;

  float w = Metrics::SquareImage();
  float h = w;
  bool  isHeader   = false;
  bool  fullWidth  = false;   // fill container width, height from aspect ratio

  if(strcmp(variant, "icon") == 0)               { w = h = Metrics::IconSize(); }
  else if(strcmp(variant, "avatar") == 0)        { w = h = (mAvatarSmallHint ? Metrics::AvatarSmall() : Metrics::AvatarSize()); }
  else if(strcmp(variant, "smallFeature") == 0)  { w = h = Metrics::SmallFeature(); }
  else if(strcmp(variant, "mediumFeature") == 0)
  {
    // 2:1 banner (the catalog box is 400x200) that FILLS its container. A fixed 400-wide box kept
    // its 200 height even when the pane was only ~190 wide, coming out near-SQUARE. Fill the pane
    // width and derive height from the box aspect (0.5) against the actual available width.
    float availW = (mTextWidthBudget > 0.0f) ? std::min(mTextWidthBudget, Metrics::MediumFeature())
                                             : Metrics::MediumFeature();
    w = MATCH_PARENT;
    h = availW * 0.5f;
  }
  else if(strcmp(variant, "largeFeature") == 0)  { w = Metrics::LargeFeature();  h = Metrics::FeatureHeight(); }
  else if(strcmp(variant, "square") == 0)        { w = h = Metrics::SquareImage(); }
  else if(strcmp(variant, "header") == 0)        { w = MATCH_PARENT; h = Metrics::FeatureHeight(); isHeader = true; }
  else if(thumbnail)                             { w = h = Metrics::RowThumbnail(); }  // Row media tile (~160dp square)
  else                                           { fullWidth = true; }          // default → full-width media

  // Explicit width/height on the A2UI node override the variant default (the catalog's
  // posters declare e.g. width:150, height:225). These are logical units, so dp-scale them;
  // an explicit size also opts the image out of full-width sizing.
  if(!isHeader)
  {
    float jw = GetNodeFloat(*comp.rawNode, "width", -1.0f);
    float jh = GetNodeFloat(*comp.rawNode, "height", -1.0f);
    if(jw > 0.0f) { w = Metrics::Dp(jw); fullWidth = false; }
    if(jh > 0.0f) { h = Metrics::Dp(jh); fullWidth = false; }
  }

  // Resolve a url to a loadable path: remote urls pass through (DALi loads what it can);
  // a relative local path is prefixed with the image dir and must exist on disk.
  auto resolvePath = [this](const std::string& u) -> std::string {
    if(u.empty()) return std::string();
    if(u.find("://") != std::string::npos) return u;
    std::string full = (u[0] == '/') ? u : (mImageDir + u);
    std::ifstream f(full);
    return f.is_open() ? full : std::string();
  };

  // Full-width media height. The web draws a full-width image at its intrinsic aspect ratio
  // but CAPS the height at ~0.82·width (object-fit:cover crops a tall/square source into that
  // box) — so a wide photo stays a short banner and a square cover becomes a ~5:4 banner, not
  // a full square. Takes an ALREADY-RESOLVED path (callers resolve once; re-resolving here
  // would double the image-dir prefix and silently fall back). Global rule, no per-card sizing.
  auto fullWidthHeight = [](const std::string& resolvedPath) -> float {
    float aspect = 0.66f;  // h/w fallback for an unknown/remote file (3:2 landscape)
    if(!resolvedPath.empty())
    {
      Dali::ImageDimensions dim = Dali::GetClosestImageSize(resolvedPath);
      if(dim.GetWidth() > 0 && dim.GetHeight() > 0)
        aspect = static_cast<float>(dim.GetHeight()) / static_cast<float>(dim.GetWidth());
    }
    if(aspect < 0.30f) aspect = 0.30f;                         // clamp extreme panoramas
    if(aspect > Metrics::FullWidthImageMaxAspect())            // web cover-crop cap (~0.82)
      aspect = Metrics::FullWidthImageMaxAspect();
    return aspect * Metrics::CardContentWidth();
  };

  float cornerR = Metrics::RadiusImage();
  bool isAvatar = (strcmp(variant, "avatar") == 0);
  bool isIcon   = (strcmp(variant, "icon") == 0);
  if(isAvatar && w > 0 && h > 0) cornerR = std::min(w, h) * 0.5f;  // avatar = full circle

  // A2UI fit → DALi fitting mode. "cover" (default) fills the box keeping aspect (crops
  // overflow); "contain" fits inside; "fill" stretches.
  const char* fitSpec = GetNodeString(*comp.rawNode, "fit",
                          GetNodeString(*comp.rawNode, "fittingMode", "cover"));
  auto fitMode = Ui::Image::FittingMode::OVER_FIT_KEEP_ASPECT_RATIO; // cover
  if(strcmp(fitSpec, "contain") == 0)   fitMode = Ui::Image::FittingMode::FIT_KEEP_ASPECT_RATIO;
  else if(strcmp(fitSpec, "fill") == 0) fitMode = Ui::Image::FittingMode::FILL;

  // Build the image view for a resolved path — or a grey placeholder when it's empty.
  auto buildView = [this, isAvatar, isIcon, isHeader, fullWidth, w, h, cornerR, fitMode, fullWidthHeight]
                   (const std::string& resolved) -> View {
    float vh = fullWidth ? fullWidthHeight(resolved) : h;
    if(resolved.empty())
    {
      View ph = View::New();
      ph.SetRequestedWidth((isHeader || fullWidth) ? MATCH_PARENT : w);
      ph.SetRequestedHeight(vh);
      ph.SetCornerRadius(cornerR);
      ph.SetBackgroundColor(COLOR_IMG_PLACEHOLDER);
      if(!isHeader && !fullWidth) ph.SetLayoutParams(FlexLayoutParams::New().SetAlignSelf(FlexAlign::CENTER));
      return ph;
    }
    // Full-width banner: prefer the pre-centre-cropped variant (res/sample-images/cropped/X) when
    // present. The web cover-crops a banner from the CENTRE; the installed dali-ui ImageView crops
    // from the top and ignores SetPixelArea, so a source pre-cropped to the banner aspect's centre
    // band is the deterministic way to show the same pixels. Only the full-width path uses it —
    // avatars/thumbnails keep the original (uncropped) source.
    std::string bannerPath = resolved;
    if(isHeader || fullWidth)
    {
      auto slash = resolved.find_last_of('/');
      if(slash != std::string::npos)
      {
        std::string cand = resolved.substr(0, slash) + "/cropped/" + resolved.substr(slash + 1);
        std::ifstream cf(cand);
        if(cf.is_open()) { cf.close(); bannerPath = cand; }
      }
      vh = fullWidthHeight(bannerPath);   // recompute height from the (possibly cropped) aspect
    }
    ImageView img = ImageView::New(bannerPath.c_str());
    img.SetFittingMode(fitMode);
    // Round the picture with an alpha mask (circle for avatars, rounded-rect for the rest). Force
    // MASKING_ON_RENDERING (GPU): the CPU/ON_LOADING path bakes the mask into the texture at load
    // and DISCARDS SetPixelArea, which is what blocked centre-cropping before. On the GPU path the
    // mask shader passes pixelArea through, so rounded corners AND a centred crop coexist.
    float desiredW = (w > 0.0f ? w : Metrics::CardContentWidth());
    std::string maskPath = mImageDir + (isAvatar ? "masks/circle.png" : "masks/rounded.png");
    std::ifstream maskFile(maskPath);
    if(maskFile.is_open())
    {
      maskFile.close();
      img.SetAlphaMaskUrl(maskPath.c_str());
      img.SetMaskingMode(Ui::Image::MaskingType::MASKING_ON_RENDERING);
    }
    else img.SetCornerRadius(cornerR);
    img.SetDesiredWidth(static_cast<int>(desiredW));
    if(isHeader || fullWidth)
    {
      // Web cover-crops a full-width banner from the CENTRE. DALi's OVER_FIT auto-crop owns the
      // pixelArea uniform and resets it every layout pass, so a manual SetPixelArea is clobbered.
      // FILL never touches that uniform, so: FILL + an explicit centre-band pixelArea reproduces a
      // centred cover-crop deterministically. (NO DesiredHeight — it forced an unstable/blurry
      // pre-cropped decode.) Band aspect == box aspect, so FILL shows it undistorted.
      float srcAspect = 1.0f;  // h/w
      Dali::ImageDimensions dim = Dali::GetClosestImageSize(bannerPath);
      if(dim.GetWidth() > 0 && dim.GetHeight() > 0)
        srcAspect = static_cast<float>(dim.GetHeight()) / static_cast<float>(dim.GetWidth());
      float boxAspect = (desiredW > 0.0f) ? (vh / desiredW) : srcAspect;
      if(srcAspect > boxAspect * 1.01f)              // source taller than box → centre-crop height
      {
        float vis = boxAspect / srcAspect;
        img.SetPixelArea(Vector4(0.0f, (1.0f - vis) * 0.5f, 1.0f, vis));
        img.SetFittingMode(Ui::Image::FittingMode::FILL);
      }
      else if(boxAspect > srcAspect * 1.01f)         // source wider than box → centre-crop width
      {
        float vis = srcAspect / boxAspect;
        img.SetPixelArea(Vector4((1.0f - vis) * 0.5f, 0.0f, vis, 1.0f));
        img.SetFittingMode(Ui::Image::FittingMode::FILL);
      }
      img.SetRequestedWidth(MATCH_PARENT);
      img.SetRequestedHeight(vh);
    }
    else if(isAvatar || isIcon)
    {
      img.SetDesiredHeight(static_cast<int>(vh));
      img.SetRequestedWidth(w);
      img.SetRequestedHeight(h);
      img.SetLayoutParams(FlexLayoutParams::New().SetAlignSelf(FlexAlign::CENTER));
    }
    else
    {
      img.SetDesiredHeight(static_cast<int>(vh));
      img.SetRequestedWidth(MATCH_PARENT);
      // Only cap when w is a real declared width. mediumFeature passes w=MATCH_PARENT (a negative
      // sentinel) to fill its container; feeding that to SetMaximumWidth clamps the width to a
      // NEGATIVE value and collapses the image. A MATCH_PARENT image wants no max-width cap anyway.
      if(w > 0.0f) img.SetMaximumWidth(w);
      img.SetRequestedHeight(h);
      img.SetLayoutParams(FlexLayoutParams::New().SetAlignSelf(FlexAlign::CENTER));
    }
    return img;
  };

  // A data-bound url is usually empty at first paint — the surface renders on
  // updateComponents, before the updateDataModel that carries the url. Wrap the image in a
  // stable container and rebuild it when the bound value arrives (a one-shot placeholder
  // can't self-replace).
  if(urlNode && urlNode->GetType() == TreeNode::OBJECT)
  {
    // A FlexLayout (not a plain View): a plain View does NOT lay out its child by the child's
    // RequestedWidth/Height, so a data-bound AVATAR's inner ImageView fell back to a tiny natural
    // size (the chat avatars rendered ~13px inside a correct 40dp box). A FlexLayout sizes the
    // child to fill, so the avatar/banner fills its container as the literal-url path already did.
    FlexLayout container = FlexLayout::New();
    container.SetDirection(FlexDirection::COLUMN);
    container.SetAlignItems(FlexAlign::STRETCH);
    container.SetJustifyContent(FlexJustify::CENTER);
    container.SetRequestedWidth((isHeader || fullWidth) ? MATCH_PARENT : w);
    container.SetRequestedHeight(fullWidth ? fullWidthHeight(resolvePath(url)) : h);
    // Carry the fixed width as MaximumWidth too (mirrors the literal-url ImageView): a Row pins its
    // image slot from GetMaximumWidth(), and a bound image returns THIS container — without it the
    // slot stays responsive and a sibling text Column overlaps it at narrow widths. Guard w>0 like the
    // literal path (line ~189): mediumFeature passes w=MATCH_PARENT (negative), and a negative
    // MaximumWidth clamps the container to a negative width and collapses it.
    if(!isHeader && !fullWidth && w > 0.0f) container.SetMaximumWidth(w);
    if(!isHeader && !fullWidth) container.SetLayoutParams(FlexLayoutParams::New().SetAlignSelf(FlexAlign::CENTER));
    container.Add(buildView(resolvePath(url)));
    bool capFull = fullWidth;
    WatchBinding(urlNode, ctx,
      [container, buildView, resolvePath, fullWidthHeight, capFull](const std::string& val) mutable {
        while(container.GetChildViewCount() > 0) container.Remove(container.GetChildViewAt(0u));
        if(capFull) container.SetRequestedHeight(fullWidthHeight(resolvePath(val)));
        container.Add(buildView(resolvePath(val)));
      });
    return container;
  }

  return buildView(resolvePath(url));
}
} // namespace A2ui
