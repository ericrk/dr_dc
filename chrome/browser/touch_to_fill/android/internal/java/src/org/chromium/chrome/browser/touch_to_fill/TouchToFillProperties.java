// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.touch_to_fill;

import android.graphics.Bitmap;

import androidx.annotation.IntDef;

import org.chromium.base.Callback;
import org.chromium.chrome.browser.touch_to_fill.data.Credential;
import org.chromium.ui.modelutil.ListModel;
import org.chromium.ui.modelutil.MVCListAdapter;
import org.chromium.ui.modelutil.PropertyKey;
import org.chromium.ui.modelutil.PropertyModel;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

/**
 * Properties defined here reflect the visible state of the TouchToFill-components.
 */
class TouchToFillProperties {
    static final PropertyModel.WritableBooleanPropertyKey VISIBLE =
            new PropertyModel.WritableBooleanPropertyKey("visible");
    static final PropertyModel.WritableObjectPropertyKey<String> FORMATTED_URL =
            new PropertyModel.WritableObjectPropertyKey<>("formatted_url");
    static final PropertyModel.WritableBooleanPropertyKey ORIGIN_SECURE =
            new PropertyModel.WritableBooleanPropertyKey("origin_secure");
    static final PropertyModel
            .ReadableObjectPropertyKey<ListModel<MVCListAdapter.ListItem>> SHEET_ITEMS =
            new PropertyModel.ReadableObjectPropertyKey<>("sheet_items");
    static final PropertyModel.ReadableObjectPropertyKey<ViewEventListener> VIEW_EVENT_LISTENER =
            new PropertyModel.ReadableObjectPropertyKey<>("view_event_listener");

    static PropertyModel createDefaultModel(ViewEventListener listener) {
        return new PropertyModel
                .Builder(VISIBLE, FORMATTED_URL, ORIGIN_SECURE, SHEET_ITEMS, VIEW_EVENT_LISTENER)
                .with(VISIBLE, false)
                .with(ORIGIN_SECURE, false)
                .with(SHEET_ITEMS, new ListModel<>())
                .with(VIEW_EVENT_LISTENER, listener)
                .build();
    }

    /**
     * Properties for a credential entry in TouchToFill sheet.
     */
    static class CredentialProperties {
        static final PropertyModel.WritableObjectPropertyKey<Bitmap> FAVICON =
                new PropertyModel.WritableObjectPropertyKey<>("favicon");
        static final PropertyModel.ReadableObjectPropertyKey<Credential> CREDENTIAL =
                new PropertyModel.ReadableObjectPropertyKey<>("credential");
        static final PropertyModel.ReadableObjectPropertyKey<String> FORMATTED_ORIGIN =
                new PropertyModel.ReadableObjectPropertyKey<>("formatted_url");
        static final PropertyModel
                .ReadableObjectPropertyKey<Callback<Credential>> ON_CLICK_LISTENER =
                new PropertyModel.ReadableObjectPropertyKey<>("on_click_listener");

        static final PropertyKey[] ALL_KEYS = {
                FAVICON, CREDENTIAL, FORMATTED_ORIGIN, ON_CLICK_LISTENER};

        private CredentialProperties() {}
    }

    /**
     * This interface is used by the view to communicate events back to the mediator. It abstracts
     * from the view by stripping information like parents, id or context.
     */
    interface ViewEventListener {

        /** Called if the user dismissed the view. */
        void onDismissed();
    }

    @IntDef({ItemType.HEADER, ItemType.CREDENTIAL})
    @Retention(RetentionPolicy.SOURCE)
    @interface ItemType {
        /**
         * The header at the top of the touch to fill sheet.
         */
        int HEADER = 1;

        /**
         * A section containing a user's name and password.
         */
        int CREDENTIAL = 2;
    }

    /**
     * Returns the sheet item type for a given item.
     * @param item An {@link MVCListAdapter.ListItem}.
     * @return The {@link ItemType} of the given list item.
     */
    static @ItemType int getItemType(MVCListAdapter.ListItem item) {
        return item.type;
    }

    private TouchToFillProperties() {}
}
