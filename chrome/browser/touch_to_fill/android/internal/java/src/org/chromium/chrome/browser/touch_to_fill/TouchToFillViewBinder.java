// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.touch_to_fill;

import static org.chromium.chrome.browser.touch_to_fill.TouchToFillProperties.CredentialProperties.CREDENTIAL;
import static org.chromium.chrome.browser.touch_to_fill.TouchToFillProperties.CredentialProperties.FAVICON;
import static org.chromium.chrome.browser.touch_to_fill.TouchToFillProperties.CredentialProperties.FORMATTED_ORIGIN;
import static org.chromium.chrome.browser.touch_to_fill.TouchToFillProperties.CredentialProperties.ON_CLICK_LISTENER;
import static org.chromium.chrome.browser.touch_to_fill.TouchToFillProperties.FORMATTED_URL;
import static org.chromium.chrome.browser.touch_to_fill.TouchToFillProperties.ORIGIN_SECURE;
import static org.chromium.chrome.browser.touch_to_fill.TouchToFillProperties.SHEET_ITEMS;
import static org.chromium.chrome.browser.touch_to_fill.TouchToFillProperties.VIEW_EVENT_LISTENER;
import static org.chromium.chrome.browser.touch_to_fill.TouchToFillProperties.VISIBLE;
import static org.chromium.chrome.browser.util.UrlUtilities.stripScheme;

import android.text.method.PasswordTransformationMethod;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageView;
import android.widget.TextView;

import org.chromium.chrome.browser.touch_to_fill.TouchToFillProperties.ItemType;
import org.chromium.chrome.browser.touch_to_fill.data.Credential;
import org.chromium.ui.modelutil.MVCListAdapter;
import org.chromium.ui.modelutil.PropertyKey;
import org.chromium.ui.modelutil.PropertyModel;
import org.chromium.ui.modelutil.RecyclerViewAdapter;
import org.chromium.ui.modelutil.SimpleRecyclerViewMcp;

/**
 * Provides functions that map {@link TouchToFillProperties} changes in a {@link PropertyModel} to
 * the suitable method in {@link TouchToFillView}.
 */
class TouchToFillViewBinder {
    /**
     * Called whenever a property in the given model changes. It updates the given view accordingly.
     * @param model The observed {@link PropertyModel}. Its data need to be reflected in the view.
     * @param view The {@link TouchToFillView} to update.
     * @param propertyKey The {@link PropertyKey} which changed.
     */
    static void bindTouchToFillView(
            PropertyModel model, TouchToFillView view, PropertyKey propertyKey) {
        if (propertyKey == VIEW_EVENT_LISTENER) {
            view.setEventListener(model.get(VIEW_EVENT_LISTENER));
        } else if (propertyKey == VISIBLE) {
            view.setVisible(model.get(VISIBLE));
        } else if (propertyKey == FORMATTED_URL || propertyKey == ORIGIN_SECURE) {
            if (model.get(ORIGIN_SECURE)) {
                view.setSecureSubtitle(model.get(FORMATTED_URL));
            } else {
                view.setNonSecureSubtitle(model.get(FORMATTED_URL));
            }
        } else if (propertyKey == SHEET_ITEMS) {
            view.setSheetItemListAdapter(
                    new RecyclerViewAdapter<>(new SimpleRecyclerViewMcp<>(model.get(SHEET_ITEMS),
                                                      TouchToFillProperties::getItemType,
                                                      TouchToFillViewBinder::connectPropertyModel),
                            TouchToFillViewBinder::createViewHolder));
        } else {
            assert false : "Unhandled update to property:" + propertyKey;
        }
    }

    /**
     * Factory used to create a new View inside the ListView inside the TouchToFillView.
     * @param parent The parent {@link ViewGroup} of the new item.
     * @param itemType The type of View to create.
     */
    private static TouchToFillViewHolder createViewHolder(
            ViewGroup parent, @ItemType int itemType) {
        switch (itemType) {
            case ItemType.HEADER:
                return null;
            case ItemType.CREDENTIAL:
                return new TouchToFillViewHolder(parent, R.layout.touch_to_fill_credential_item,
                        TouchToFillViewBinder::bindCredentialView);
        }
        assert false : "Cannot create view for ItemType: " + itemType;
        return null;
    }

    /**
     * This method creates a model change processor for each recycler view item when it is created.
     * @param holder A {@link TouchToFillViewHolder} holding the view and view binder for the MCP.
     * @param item A {@link MVCListAdapter.ListItem} holding the {@link PropertyModel} for the MCP.
     */
    private static void connectPropertyModel(
            TouchToFillViewHolder holder, MVCListAdapter.ListItem item) {
        holder.setupModelChangeProcessor(item.model);
    }

    /**
     * Called whenever a credential is bound to this view holder. Please note that this method
     * might be called on the same list entry repeatedly, so make sure to always set a default
     * for unused fields.
     * @param model The model containing the data for the view
     * @param view The view to be bound
     * @param propertyKey The key of the property to be bound
     */
    private static void bindCredentialView(
            PropertyModel model, ViewGroup view, PropertyKey propertyKey) {
        Credential credential = model.get(CREDENTIAL);
        if (propertyKey == FAVICON) {
            ImageView imageView = view.findViewById(R.id.favicon);
            imageView.setImageBitmap(model.get(FAVICON));
        } else if (propertyKey == ON_CLICK_LISTENER) {
            view.setOnClickListener(
                    clickedView -> { model.get(ON_CLICK_LISTENER).onResult(credential); });
        } else if (propertyKey == FORMATTED_ORIGIN) {
            TextView pslOriginText = view.findViewById(R.id.credential_origin);
            pslOriginText.setText(model.get(FORMATTED_ORIGIN));
            pslOriginText.setVisibility(
                    credential.isPublicSuffixMatch() ? View.VISIBLE : View.GONE);
        } else if (propertyKey == CREDENTIAL) {
            TextView pslOriginText = view.findViewById(R.id.credential_origin);
            String formattedOrigin = stripScheme(credential.getOriginUrl());
            formattedOrigin =
                    formattedOrigin.replaceFirst("/$", ""); // Strip possibly trailing slash.
            pslOriginText.setText(formattedOrigin);
            pslOriginText.setVisibility(
                    credential.isPublicSuffixMatch() ? View.VISIBLE : View.GONE);

            TextView usernameText = view.findViewById(R.id.username);
            usernameText.setText(credential.getFormattedUsername());

            TextView passwordText = view.findViewById(R.id.password);
            passwordText.setText(credential.getPassword());
            passwordText.setTransformationMethod(new PasswordTransformationMethod());
        } else {
            assert false : "Unhandled update to property:" + propertyKey;
        }
    }

    private TouchToFillViewBinder() {}
}
