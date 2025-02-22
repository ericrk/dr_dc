// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.weblayer_private;

import android.app.Activity;
import android.content.Context;
import android.os.Bundle;
import android.os.RemoteException;
import android.view.View;

import org.chromium.weblayer_private.aidl.APICallException;
import org.chromium.weblayer_private.aidl.IObjectWrapper;
import org.chromium.weblayer_private.aidl.IRemoteFragment;
import org.chromium.weblayer_private.aidl.IRemoteFragmentClient;
import org.chromium.weblayer_private.aidl.ObjectWrapper;

/**
 * Base for the classes controlling a Fragment that exists in another ClassLoader. Extending this
 * class is similar to extending Fragment: e.g. one can override lifecycle methods, not forgetting
 * to call super, etc.
 */
public abstract class RemoteFragmentImpl extends IRemoteFragment.Stub {
    private final IRemoteFragmentClient mClient;

    protected RemoteFragmentImpl(IRemoteFragmentClient client) {
        mClient = client;
    }

    public View onCreateView() {
        return null;
    }

    public final Activity getActivity() {
        try {
            return ObjectWrapper.unwrap(mClient.getActivity(), Activity.class);
        } catch (RemoteException e) {
            throw new APICallException(e);
        }
    }

    // TODO(pshmakov): add dependency to androidx.annotation and put @CallSuper here.
    public void onCreate(Bundle savedInstanceState) {
        try {
            mClient.superOnCreate(ObjectWrapper.wrap(savedInstanceState));
        } catch (RemoteException e) {
            throw new APICallException(e);
        }
    }

    public void onAttach(Context context) {
        try {
            mClient.superOnAttach(ObjectWrapper.wrap(context));
        } catch (RemoteException e) {
            throw new APICallException(e);
        }
    }

    public void onActivityCreated(Bundle savedInstanceState) {
        try {
            mClient.superOnActivityCreated(ObjectWrapper.wrap(savedInstanceState));
        } catch (RemoteException e) {
            throw new APICallException(e);
        }
    }

    public void onStart() {
        try {
            mClient.superOnStart();
        } catch (RemoteException e) {
            throw new APICallException(e);
        }
    }

    public void onDestroy() {
        try {
            mClient.superOnDestroy();
        } catch (RemoteException e) {
            throw new APICallException(e);
        }
    }

    public void onDetach() {
        try {
            mClient.superOnDetach();
        } catch (RemoteException e) {
            throw new APICallException(e);
        }
    }

    public void onResume() {
        try {
            mClient.superOnResume();
        } catch (RemoteException e) {
            throw new APICallException(e);
        }
    }

    public void onDestroyView() {
        try {
            mClient.superOnDestroyView();
        } catch (RemoteException e) {
            throw new APICallException(e);
        }
    }

    public void onStop() {
        try {
            mClient.superOnStop();
        } catch (RemoteException e) {
            throw new APICallException(e);
        }
    }

    public void onPause() {
        try {
            mClient.superOnPause();
        } catch (RemoteException e) {
            throw new APICallException(e);
        }
    }

    public void onSaveInstaceState(Bundle outState) {
        try {
            mClient.superOnSaveInstanceState(ObjectWrapper.wrap(outState));
        } catch (RemoteException e) {
            throw new APICallException(e);
        }
    }

    // IRemoteFragment implementation below.

    @Override
    public final IObjectWrapper handleOnCreateView() {
        return ObjectWrapper.wrap(onCreateView());
    }

    @Override
    public final void handleOnStart() {
        onStart();
    }

    @Override
    public final void handleOnCreate(IObjectWrapper savedInstanceState) {
        onCreate(ObjectWrapper.unwrap(savedInstanceState, Bundle.class));
    }

    @Override
    public final void handleOnAttach(IObjectWrapper context) {
        onAttach(ObjectWrapper.unwrap(context, Context.class));
    }

    @Override
    public final void handleOnActivityCreated(IObjectWrapper savedInstanceState) {
        onActivityCreated(ObjectWrapper.unwrap(savedInstanceState, Bundle.class));
    }

    @Override
    public final void handleOnResume()  {
        onResume();
    }

    @Override
    public final void handleOnPause()  {
        onPause();
    }

    @Override
    public final void handleOnStop()  {
        onStop();
    }

    @Override
    public final void handleOnDestroyView()  {
        onDestroyView();
    }

    @Override
    public final void handleOnDetach()  {
        onDetach();
    }

    @Override
    public final void handleOnDestroy()  {
        onDestroy();
    }

    @Override
    public final void handleOnSaveInstanceState(IObjectWrapper outState)  {
        onSaveInstaceState(ObjectWrapper.unwrap(outState, Bundle.class));
    }
}
