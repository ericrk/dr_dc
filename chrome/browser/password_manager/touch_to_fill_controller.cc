// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/password_manager/touch_to_fill_controller.h"

#include <utility>

#include "base/logging.h"
#include "chrome/browser/touch_to_fill/touch_to_fill_view.h"
#include "components/favicon/core/favicon_service.h"
#include "components/password_manager/core/browser/android_affiliation/affiliation_utils.h"
#include "components/password_manager/core/browser/origin_credential_store.h"
#include "components/password_manager/core/browser/password_manager_driver.h"
#include "components/password_manager/core/browser/password_manager_metrics_util.h"
#include "components/url_formatter/elide_url.h"
#include "content/public/browser/web_contents.h"
#include "services/network/public/cpp/is_potentially_trustworthy.h"

using password_manager::CredentialPair;
using password_manager::PasswordManagerDriver;

namespace {

void OnImageFetched(base::OnceCallback<void(const gfx::Image&)> callback,
                    const favicon_base::FaviconRawBitmapResult& bitmap_result) {
  gfx::Image image;
  if (bitmap_result.is_valid())
    image = gfx::Image::CreateFrom1xPNGBytes(bitmap_result.bitmap_data);
  std::move(callback).Run(image);
}

}  // namespace

TouchToFillController::TouchToFillController(
    content::WebContents* web_contents,
    favicon::FaviconService* favicon_service)
    : web_contents_(web_contents), favicon_service_(favicon_service) {}

TouchToFillController::~TouchToFillController() = default;

void TouchToFillController::Show(base::span<const CredentialPair> credentials,
                                 base::WeakPtr<PasswordManagerDriver> driver) {
  DCHECK(!driver_ || driver_.get() == driver.get());
  driver_ = std::move(driver);

  if (!view_)
    view_ = TouchToFillViewFactory::Create(this);

  const GURL& url = driver_->GetLastCommittedURL();
  view_->Show(url_formatter::FormatUrlForSecurityDisplay(
                  url, url_formatter::SchemeDisplay::OMIT_HTTP_AND_HTTPS),
              TouchToFillView::IsOriginSecure(
                  network::IsUrlPotentiallyTrustworthy(url)),
              credentials);
}

void TouchToFillController::OnCredentialSelected(
    const CredentialPair& credential) {
  if (!driver_)
    return;

  password_manager::metrics_util::LogFilledCredentialIsFromAndroidApp(
      password_manager::IsValidAndroidFacetURI(credential.origin_url.spec()));
  driver_->TouchToFillDismissed();
  std::exchange(driver_, nullptr)
      ->FillSuggestion(credential.username, credential.password);
}

void TouchToFillController::OnDismiss() {
  if (!driver_)
    return;

  std::exchange(driver_, nullptr)->TouchToFillDismissed();
}

gfx::NativeView TouchToFillController::GetNativeView() {
  return web_contents_->GetNativeView();
}

void TouchToFillController::FetchFavicon(
    const std::string& credential_origin,
    int desired_size_in_pixel,
    base::OnceCallback<void(const gfx::Image&)> callback) {
  favicon_service_->GetRawFaviconForPageURL(
      GURL(credential_origin), {favicon_base::IconType::kFavicon},
      desired_size_in_pixel,
      /* fallback_to_host = */ true,
      base::BindOnce(&OnImageFetched, std::move(callback)), &favicon_tracker_);
}
