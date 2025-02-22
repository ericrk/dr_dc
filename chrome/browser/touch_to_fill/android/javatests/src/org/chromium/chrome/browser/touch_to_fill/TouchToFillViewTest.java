// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.touch_to_fill;

import static org.hamcrest.Matchers.instanceOf;
import static org.hamcrest.Matchers.is;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThat;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.timeout;
import static org.mockito.Mockito.verify;

import static org.chromium.chrome.browser.touch_to_fill.TouchToFillProperties.CredentialProperties.CREDENTIAL;
import static org.chromium.chrome.browser.touch_to_fill.TouchToFillProperties.CredentialProperties.FORMATTED_ORIGIN;
import static org.chromium.chrome.browser.touch_to_fill.TouchToFillProperties.CredentialProperties.ON_CLICK_LISTENER;
import static org.chromium.chrome.browser.touch_to_fill.TouchToFillProperties.FORMATTED_URL;
import static org.chromium.chrome.browser.touch_to_fill.TouchToFillProperties.ORIGIN_SECURE;
import static org.chromium.chrome.browser.touch_to_fill.TouchToFillProperties.SHEET_ITEMS;
import static org.chromium.chrome.browser.touch_to_fill.TouchToFillProperties.VISIBLE;
import static org.chromium.content_public.browser.test.util.CriteriaHelper.pollUiThread;

import static java.util.Arrays.asList;

import android.support.test.filters.MediumTest;
import android.support.v7.widget.RecyclerView;
import android.text.method.PasswordTransformationMethod;
import android.view.View;
import android.widget.TextView;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;

import org.chromium.base.Callback;
import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.base.test.util.ScalableTimeout;
import org.chromium.chrome.browser.ChromeActivity;
import org.chromium.chrome.browser.ChromeSwitches;
import org.chromium.chrome.browser.touch_to_fill.data.Credential;
import org.chromium.chrome.browser.widget.bottomsheet.BottomSheet.SheetState;
import org.chromium.chrome.test.ChromeJUnit4ClassRunner;
import org.chromium.chrome.test.ChromeTabbedActivityTestRule;
import org.chromium.content_public.browser.test.util.CriteriaHelper;
import org.chromium.content_public.browser.test.util.TestThreadUtils;
import org.chromium.content_public.browser.test.util.TouchCommon;
import org.chromium.ui.modelutil.MVCListAdapter;
import org.chromium.ui.modelutil.PropertyModel;

import java.util.Collections;

/**
 * View tests for the Touch To Fill component ensure that model changes are reflected in the sheet.
 */
@RunWith(ChromeJUnit4ClassRunner.class)
@CommandLineFlags.Add({ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE})
public class TouchToFillViewTest {
    private static final Credential ANA = new Credential("Ana", "S3cr3t", "Ana", "", false);
    private static final Credential NO_ONE =
            new Credential("", "***", "No Username", "m.example.xyz", true);
    private static final Credential BOB =
            new Credential("Bob", "***", "Bob", "mobile.example.xyz", true);

    @Mock
    private TouchToFillProperties.ViewEventListener mMockListener;
    @Mock
    private Callback<Credential> mCredentialCallback;

    private PropertyModel mModel;
    private TouchToFillView mTouchToFillView;

    @Rule
    public ChromeTabbedActivityTestRule mActivityTestRule = new ChromeTabbedActivityTestRule();

    @Before
    public void setUp() throws InterruptedException {
        MockitoAnnotations.initMocks(this);
        mActivityTestRule.startMainActivityOnBlankPage();
        mModel = TouchToFillProperties.createDefaultModel(mMockListener);
        TestThreadUtils.runOnUiThreadBlocking(() -> {
            mTouchToFillView =
                    new TouchToFillView(getActivity(), getActivity().getBottomSheetController());
            TouchToFillCoordinator.setUpModelChangeProcessors(mModel, mTouchToFillView);
        });
    }

    @Test
    @MediumTest
    public void testVisibilityChangedByModel() {
        // After setting the visibility to true, the view should exist and be visible.
        TestThreadUtils.runOnUiThreadBlocking(() -> mModel.set(VISIBLE, true));
        pollUiThread(() -> getBottomSheetState() == SheetState.FULL);
        assertThat(mTouchToFillView.getContentView().isShown(), is(true));

        // After hiding the view, the view should still exist but be invisible.
        TestThreadUtils.runOnUiThreadBlocking(() -> mModel.set(VISIBLE, false));
        pollUiThread(() -> getBottomSheetState() == SheetState.HIDDEN);
        assertThat(mTouchToFillView.getContentView().isShown(), is(false));
    }

    @Test
    @MediumTest
    public void testSubtitleUrlChangedByModel() {
        TestThreadUtils.runOnUiThreadBlocking(() -> {
            mModel.set(FORMATTED_URL, "www.example.org");
            mModel.set(ORIGIN_SECURE, true);
            mModel.set(VISIBLE, true);
        });
        pollUiThread(() -> getBottomSheetState() == SheetState.FULL);
        TextView subtitle =
                mTouchToFillView.getContentView().findViewById(R.id.touch_to_fill_sheet_subtitle);

        assertThat(subtitle.getText(), is("www.example.org"));

        TestThreadUtils.runOnUiThreadBlocking(() -> {
            mModel.set(FORMATTED_URL, "m.example.org");
            mModel.set(ORIGIN_SECURE, false);
        });

        assertThat(subtitle.getText(), is(getFormattedNotSecureSubtitle("m.example.org")));
    }

    @Test
    @MediumTest
    public void testCredentialsChangedByModel() {
        TestThreadUtils.runOnUiThreadBlocking(() -> {
            mTouchToFillView.setVisible(true);
            mModel.get(SHEET_ITEMS)
                    .addAll(asList(buildCredentialItem(ANA), buildCredentialItem(NO_ONE),
                            buildCredentialItem(BOB)));
        });

        pollUiThread(() -> getBottomSheetState() == SheetState.FULL);
        assertThat(getCredentials().getChildCount(), is(3));
        assertThat(getCredentialOriginAt(0).getVisibility(), is(View.GONE));
        assertThat(getCredentialNameAt(0).getText(), is(ANA.getFormattedUsername()));
        assertThat(getCredentialPasswordAt(0).getText(), is(ANA.getPassword()));
        assertThat(getCredentialPasswordAt(0).getTransformationMethod(),
                instanceOf(PasswordTransformationMethod.class));
        assertThat(getCredentialOriginAt(1).getVisibility(), is(View.VISIBLE));
        assertThat(getCredentialOriginAt(1).getText(), is("m.example.xyz"));
        assertThat(getCredentialNameAt(1).getText(), is(NO_ONE.getFormattedUsername()));
        assertThat(getCredentialPasswordAt(1).getText(), is(NO_ONE.getPassword()));
        assertThat(getCredentialPasswordAt(1).getTransformationMethod(),
                instanceOf(PasswordTransformationMethod.class));
        assertThat(getCredentialOriginAt(2).getVisibility(), is(View.VISIBLE));
        assertThat(getCredentialOriginAt(2).getText(), is("mobile.example.xyz"));
        assertThat(getCredentialNameAt(2).getText(), is(BOB.getFormattedUsername()));
        assertThat(getCredentialPasswordAt(2).getText(), is(BOB.getPassword()));
        assertThat(getCredentialPasswordAt(2).getTransformationMethod(),
                instanceOf(PasswordTransformationMethod.class));
    }

    @Test
    @MediumTest
    public void testCredentialsAreClickable() {
        TestThreadUtils.runOnUiThreadBlocking(() -> {
            mModel.get(SHEET_ITEMS).addAll(Collections.singletonList(buildCredentialItem(ANA)));
            mModel.set(VISIBLE, true);
        });
        pollUiThread(() -> getBottomSheetState() == SheetState.FULL);

        assertNotNull(getCredentials().getChildAt(0));

        TouchCommon.singleClickView(getCredentials().getChildAt(0));

        waitForEvent(mCredentialCallback).onResult(eq(ANA));
    }

    @Test
    @MediumTest
    public void testDismissesWhenHidden() {
        TestThreadUtils.runOnUiThreadBlocking(() -> mModel.set(VISIBLE, true));
        pollUiThread(() -> getBottomSheetState() == SheetState.FULL);
        TestThreadUtils.runOnUiThreadBlocking(() -> mModel.set(VISIBLE, false));
        pollUiThread(() -> getBottomSheetState() == SheetState.HIDDEN);
        verify(mMockListener).onDismissed();
    }

    private ChromeActivity getActivity() {
        return mActivityTestRule.getActivity();
    }

    private String getFormattedNotSecureSubtitle(String url) {
        return getActivity().getString(R.string.touch_to_fill_sheet_subtitle_not_secure, url);
    }

    private @SheetState int getBottomSheetState() {
        pollUiThread(() -> getActivity().getBottomSheet() != null);
        return getActivity().getBottomSheet().getSheetState();
    }

    private RecyclerView getCredentials() {
        return mTouchToFillView.getContentView().findViewById(R.id.sheet_item_list);
    }

    private TextView getCredentialNameAt(int index) {
        return getCredentials().getChildAt(index).findViewById(R.id.username);
    }

    private TextView getCredentialPasswordAt(int index) {
        return getCredentials().getChildAt(index).findViewById(R.id.password);
    }

    private TextView getCredentialOriginAt(int index) {
        return getCredentials().getChildAt(index).findViewById(R.id.credential_origin);
    }

    public static <T> T waitForEvent(T mock) {
        return verify(mock,
                timeout(ScalableTimeout.scaleTimeout(CriteriaHelper.DEFAULT_MAX_TIME_TO_POLL)));
    }

    private MVCListAdapter.ListItem buildCredentialItem(Credential credential) {
        return new MVCListAdapter.ListItem(TouchToFillProperties.ItemType.CREDENTIAL,
                new PropertyModel.Builder(TouchToFillProperties.CredentialProperties.ALL_KEYS)
                        .with(CREDENTIAL, credential)
                        .with(ON_CLICK_LISTENER, mCredentialCallback)
                        .with(FORMATTED_ORIGIN, credential.getOriginUrl())
                        .build());
    }
}
