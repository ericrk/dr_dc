// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.compositor.bottombar.ephemeraltab;

import android.content.Context;
import android.graphics.drawable.Drawable;
import android.os.Handler;
import android.support.annotation.DrawableRes;
import android.text.TextUtils;

import org.chromium.base.Callback;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.ChromeActivity;
import org.chromium.chrome.browser.compositor.bottombar.OverlayContentDelegate;
import org.chromium.chrome.browser.compositor.bottombar.OverlayContentProgressObserver;
import org.chromium.chrome.browser.compositor.bottombar.OverlayPanelContent;
import org.chromium.chrome.browser.favicon.FaviconHelper;
import org.chromium.chrome.browser.favicon.FaviconUtils;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.ssl.SecurityStateModel;
import org.chromium.chrome.browser.tabmodel.TabLaunchType;
import org.chromium.chrome.browser.ui.widget.RoundedIconGenerator;
import org.chromium.chrome.browser.widget.bottomsheet.BottomSheet;
import org.chromium.chrome.browser.widget.bottomsheet.BottomSheetController;
import org.chromium.chrome.browser.widget.bottomsheet.EmptyBottomSheetObserver;
import org.chromium.components.embedder_support.view.ContentView;
import org.chromium.components.security_state.ConnectionSecurityLevel;
import org.chromium.content_public.browser.LoadUrlParams;
import org.chromium.content_public.browser.NavigationHandle;
import org.chromium.content_public.browser.WebContentsObserver;
import org.chromium.ui.base.PageTransition;

/**
 * Central class for ephemeral tab, responsible for spinning off other classes necessary to display
 * the preview tab UI.
 */
public class EphemeralTabCoordinator {
    /** The delay (four video frames) after which the hide progress will be hidden. */
    private static final long HIDE_PROGRESS_BAR_DELAY_MS = (1000 / 60) * 4;

    // TODO(crbug/1001256): Use Context after removing dependency on OverlayPanelContent.
    private final ChromeActivity mActivity;
    private final BottomSheetController mBottomSheetController;
    private final FaviconLoader mFaviconLoader;
    private OverlayPanelContent mPanelContent;
    private WebContentsObserver mWebContentsObserver;
    private EphemeralTabSheetContent mSheetContent;
    private boolean mIsIncognito;
    private String mUrl;

    /**
     * Constructor.
     * @param activity The associated {@link ChromeActivity}.
     * @param bottomSheetController The associated {@link BottomSheetController}.
     */
    public EphemeralTabCoordinator(
            ChromeActivity activity, BottomSheetController bottomSheetController) {
        mActivity = activity;
        mBottomSheetController = bottomSheetController;
        mFaviconLoader = new FaviconLoader(mActivity);
        mBottomSheetController.getBottomSheet().addObserver(new EmptyBottomSheetObserver() {
            @Override
            public void onSheetStateChanged(int newState) {
                if (newState == BottomSheet.SheetState.HIDDEN) destroyContent();
            }
        });
    }

    /**
     * Entry point for ephemeral tab flow. This will create an ephemeral tab and show it in the
     * bottom sheet.
     * @param url The URL to be shown.
     * @param title The title to be shown.
     * @param isIncognito Whether we are currently in incognito mode.
     */
    public void requestOpenSheet(String url, String title, boolean isIncognito) {
        mUrl = url;
        mIsIncognito = isIncognito;
        if (mSheetContent == null) {
            mSheetContent = new EphemeralTabSheetContent(
                    mActivity, () -> openInNewTab(), () -> onToolbarClick());
        }

        getContent().loadUrl(url, true);
        getContent().updateBrowserControlsState(true);
        if (mWebContentsObserver == null) mWebContentsObserver = createWebContentsObserver();
        mSheetContent.attachWebContents(
                getContent().getWebContents(), (ContentView) getContent().getContainerView());
        mSheetContent.updateTitle(title);
        mBottomSheetController.requestShowContent(mSheetContent, true);

        // TODO(donnd): Collect UMA with OverlayPanel.StateChangeReason.CLICK.
    }

    private OverlayPanelContent getContent() {
        if (mPanelContent == null) {
            mPanelContent = new OverlayPanelContent(new EphemeralTabPanelContentDelegate(),
                    new PageLoadProgressObserver(), mActivity, mIsIncognito,
                    mActivity.getResources().getDimensionPixelSize(
                            R.dimen.toolbar_height_no_shadow));
        }
        return mPanelContent;
    }

    private void destroyContent() {
        if (mSheetContent != null) {
            mSheetContent.destroy();
            mSheetContent = null;
        }

        if (mPanelContent != null) {
            mPanelContent.destroy();
            mPanelContent = null;
        }

        if (mWebContentsObserver != null) {
            mWebContentsObserver.destroy();
            mWebContentsObserver = null;
        }
    }

    private void openInNewTab() {
        if (canPromoteToNewTab() && mUrl != null) {
            mBottomSheetController.hideContent(mSheetContent, /* animate= */ true);
            mActivity.getCurrentTabCreator().createNewTab(
                    new LoadUrlParams(mUrl, PageTransition.LINK), TabLaunchType.FROM_LINK,
                    mActivity.getActivityTabProvider().get());
        }
    }

    private void onToolbarClick() {
        if (mBottomSheetController.getBottomSheet().getSheetState()
                == BottomSheet.SheetState.PEEK) {
            mBottomSheetController.expandSheet();
        }
    }

    private boolean canPromoteToNewTab() {
        return !mActivity.isCustomTab();
    }

    private void onFaviconAvailable(Drawable drawable) {
        if (mSheetContent == null) return;
        mSheetContent.startFaviconAnimation(drawable);
    }

    private WebContentsObserver createWebContentsObserver() {
        return new WebContentsObserver(mPanelContent.getWebContents()) {
            @Override
            public void titleWasSet(String title) {
                mSheetContent.updateTitle(title);
            }

            @Override
            public void didFinishNavigation(NavigationHandle navigation) {
                if (navigation.hasCommitted() && navigation.isInMainFrame()) {
                    mSheetContent.updateURL(mPanelContent.getWebContents().getVisibleUrl());
                }
            }
        };
    }

    @DrawableRes
    static int getSecurityIconResource(@ConnectionSecurityLevel int securityLevel) {
        switch (securityLevel) {
            case ConnectionSecurityLevel.NONE:
            case ConnectionSecurityLevel.WARNING:
                return R.drawable.omnibox_info;
            case ConnectionSecurityLevel.DANGEROUS:
                return R.drawable.omnibox_https_invalid;
            case ConnectionSecurityLevel.SECURE_WITH_POLICY_INSTALLED_CERT:
            case ConnectionSecurityLevel.SECURE:
            case ConnectionSecurityLevel.EV_SECURE:
                return R.drawable.omnibox_https_valid;
            default:
                assert false;
        }
        return 0;
    }

    /**
     * Observes the ephemeral tab web contents and loads the associated favicon.
     */
    private class EphemeralTabPanelContentDelegate extends OverlayContentDelegate {
        private String mCurrentUrl;

        @Override
        public void onMainFrameLoadStarted(String url, boolean isExternalUrl) {
            if (TextUtils.equals(mCurrentUrl, url)) return;

            mCurrentUrl = url;
            mFaviconLoader.loadFavicon(url, (drawable) -> onFaviconAvailable(drawable));
        }

        @Override
        public void onSSLStateUpdated() {
            int securityLevel = SecurityStateModel.getSecurityLevelForWebContents(
                    mPanelContent.getWebContents());
            mSheetContent.setSecurityIcon(getSecurityIconResource(securityLevel));
            mSheetContent.updateURL(mPanelContent.getWebContents().getVisibleUrl());
        }
    }

    /** Observes the ephemeral tab page load progress and updates the progress bar. */
    private class PageLoadProgressObserver extends OverlayContentProgressObserver {
        @Override
        public void onProgressBarStarted() {
            if (mSheetContent == null) return;
            mSheetContent.setProgressVisible(true);
            mSheetContent.setProgress(0);
        }

        @Override
        public void onProgressBarUpdated(int progress) {
            if (mSheetContent == null) return;
            mSheetContent.setProgress(progress);
        }

        @Override
        public void onProgressBarFinished() {
            // Hides the Progress Bar after a delay to make sure it is rendered for at least
            // a few frames, otherwise its completion won't be visually noticeable.
            new Handler().postDelayed(() -> {
                if (mSheetContent == null) return;
                mSheetContent.setProgressVisible(false);
            }, HIDE_PROGRESS_BAR_DELAY_MS);
        }
    }

    /**
     * Helper class to generate a favicon for a given URL and resize it to the desired dimensions
     * for displaying it on the image view.
     */
    private static class FaviconLoader {
        private final Context mContext;
        private final FaviconHelper mFaviconHelper;
        private final FaviconHelper.DefaultFaviconHelper mDefaultFaviconHelper;
        private final RoundedIconGenerator mIconGenerator;
        private final int mFaviconSize;

        /** Constructor. */
        public FaviconLoader(Context context) {
            mContext = context;
            mFaviconHelper = new FaviconHelper();
            mDefaultFaviconHelper = new FaviconHelper.DefaultFaviconHelper();
            mIconGenerator = FaviconUtils.createCircularIconGenerator(mContext.getResources());
            mFaviconSize =
                    mContext.getResources().getDimensionPixelSize(R.dimen.preview_tab_favicon_size);
        }

        /**
         * Generates a favicon for a given URL. If no favicon was could be found or generated from
         * the URL, a default favicon will be shown.
         * @param url The URL for which favicon is to be generated.
         * @param callback The callback to be invoked to display the final image.
         */
        public void loadFavicon(final String url, Callback<Drawable> callback) {
            FaviconHelper.FaviconImageCallback imageCallback = (bitmap, iconUrl) -> {
                Drawable drawable =
                        FaviconUtils.getIconDrawableWithFilter(bitmap, url, mIconGenerator,
                                mDefaultFaviconHelper, mContext.getResources(), mFaviconSize);
                callback.onResult(drawable);
            };

            mFaviconHelper.getLocalFaviconImageForURL(
                    Profile.getLastUsedProfile(), url, mFaviconSize, imageCallback);
        }

    }
}
