// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_TOUCH_TO_FILL_TOUCH_TO_FILL_VIEW_H_
#define CHROME_BROWSER_TOUCH_TO_FILL_TOUCH_TO_FILL_VIEW_H_

#include "base/callback_forward.h"
#include "base/containers/span.h"
#include "base/strings/string_piece_forward.h"
#include "base/util/type_safety/strong_alias.h"

namespace password_manager {
struct CredentialPair;
}

// This class represents the interface used for communicating between the Touch
// To Fill controller with the Android frontend.
class TouchToFillView {
 public:
  using IsOriginSecure = util::StrongAlias<class IsOriginSecureTag, bool>;

  TouchToFillView() = default;
  TouchToFillView(const TouchToFillView&) = delete;
  TouchToFillView& operator=(const TouchToFillView&) = delete;
  virtual ~TouchToFillView() = default;

  // Instructs Touch To Fill to show the provided |credentials| to the user.
  // |formatted_url| contains a human friendly version of the current origin.
  // |is_origin_secure| indicates whether the current frame origin is secure.
  // After user interaction either OnCredentialSelected() or OnDismiss() gets
  // invoked.
  virtual void Show(
      base::StringPiece16 formatted_url,
      IsOriginSecure is_origin_secure,
      base::span<const password_manager::CredentialPair> credentials) = 0;

  // Invoked in case the user chooses an entry from the credential list
  // presented to them.
  virtual void OnCredentialSelected(
      const password_manager::CredentialPair& credential) = 0;

  // Invoked if the user dismissed the Touch To Fill sheet without choosing a
  // credential.
  virtual void OnDismiss() = 0;
};

#endif  // CHROME_BROWSER_TOUCH_TO_FILL_TOUCH_TO_FILL_VIEW_H_
