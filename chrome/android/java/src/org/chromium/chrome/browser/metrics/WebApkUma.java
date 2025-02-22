// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.metrics;

import android.Manifest;
import android.content.ContentResolver;
import android.os.Build;
import android.os.Environment;
import android.os.StatFs;
import android.provider.Settings;
import android.text.TextUtils;
import android.text.format.DateUtils;

import androidx.annotation.IntDef;

import org.chromium.base.ContextUtils;
import org.chromium.base.metrics.RecordHistogram;
import org.chromium.base.task.AsyncTask;
import org.chromium.chrome.browser.preferences.ChromePreferenceManager;
import org.chromium.chrome.browser.util.ConversionUtils;
import org.chromium.chrome.browser.webapps.WebApkDistributor;
import org.chromium.chrome.browser.webapps.WebApkUkmRecorder;
import org.chromium.chrome.browser.webapps.WebappDataStorage;
import org.chromium.chrome.browser.webapps.WebappRegistry;

import java.io.File;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.TimeUnit;

/**
 * Centralizes UMA data collection for WebAPKs. NOTE: Histogram names and values are defined in
 * tools/metrics/histograms/histograms.xml. Please update that file if any change is made.
 */
public class WebApkUma {
    // This enum is used to back UMA histograms, and should therefore be treated as append-only.
    @IntDef({UpdateRequestSent.WHILE_WEBAPK_CLOSED})
    @Retention(RetentionPolicy.SOURCE)
    public @interface UpdateRequestSent {
        // Deprecated: FIRST_TRY = 0;
        // Deprecated: ONSTOP = 1;
        // Deprecated: WHILE_WEBAPK_IN_FOREGROUND = 2;
        int WHILE_WEBAPK_CLOSED = 3;
        int NUM_ENTRIES = 4;
    }

    // This enum is used to back UMA histograms, and should therefore be treated as append-only.
    // The queued request times shouldn't exceed three.
    @IntDef({UpdateRequestQueued.ONCE, UpdateRequestQueued.TWICE, UpdateRequestQueued.THREE_TIMES})
    @Retention(RetentionPolicy.SOURCE)
    public @interface UpdateRequestQueued {
        int ONCE = 0;
        int TWICE = 1;
        int THREE_TIMES = 2;
        int NUM_ENTRIES = 3;
    }

    // This enum is used to back UMA histograms, and should therefore be treated as append-only.
    @IntDef({GooglePlayInstallResult.SUCCESS, GooglePlayInstallResult.FAILED_NO_DELEGATE,
            GooglePlayInstallResult.FAILED_TO_CONNECT_TO_SERVICE,
            GooglePlayInstallResult.FAILED_CALLER_VERIFICATION_FAILURE,
            GooglePlayInstallResult.FAILED_POLICY_VIOLATION,
            GooglePlayInstallResult.FAILED_API_DISABLED,
            GooglePlayInstallResult.FAILED_REQUEST_FAILED,
            GooglePlayInstallResult.FAILED_DOWNLOAD_CANCELLED,
            GooglePlayInstallResult.FAILED_DOWNLOAD_ERROR,
            GooglePlayInstallResult.FAILED_INSTALL_ERROR,
            GooglePlayInstallResult.FAILED_INSTALL_TIMEOUT,
            GooglePlayInstallResult.REQUEST_FAILED_POLICY_DISABLED,
            GooglePlayInstallResult.REQUEST_FAILED_UNKNOWN_ACCOUNT,
            GooglePlayInstallResult.REQUEST_FAILED_NETWORK_ERROR,
            GooglePlayInstallResult.REQUSET_FAILED_RESOLVE_ERROR,
            GooglePlayInstallResult.REQUEST_FAILED_NOT_GOOGLE_SIGNED})
    @Retention(RetentionPolicy.SOURCE)
    public @interface GooglePlayInstallResult {
        int SUCCESS = 0;
        int FAILED_NO_DELEGATE = 1;
        int FAILED_TO_CONNECT_TO_SERVICE = 2;
        int FAILED_CALLER_VERIFICATION_FAILURE = 3;
        int FAILED_POLICY_VIOLATION = 4;
        int FAILED_API_DISABLED = 5;
        int FAILED_REQUEST_FAILED = 6;
        int FAILED_DOWNLOAD_CANCELLED = 7;
        int FAILED_DOWNLOAD_ERROR = 8;
        int FAILED_INSTALL_ERROR = 9;
        int FAILED_INSTALL_TIMEOUT = 10;
        // REQUEST_FAILED_* errors are the error codes shown in the "reason" of
        // the returned Bundle when calling installPackage() API returns false.
        int REQUEST_FAILED_POLICY_DISABLED = 11;
        int REQUEST_FAILED_UNKNOWN_ACCOUNT = 12;
        int REQUEST_FAILED_NETWORK_ERROR = 13;
        int REQUSET_FAILED_RESOLVE_ERROR = 14;
        int REQUEST_FAILED_NOT_GOOGLE_SIGNED = 15;
        int NUM_ENTRIES = 16;
    }

    // This enum is used to back UMA histograms, and should therefore be treated as append-only.
    @IntDef({Permission.OTHER, Permission.LOCATION, Permission.MICROPHONE, Permission.CAMERA,
            Permission.STORAGE})
    @Retention(RetentionPolicy.SOURCE)
    public @interface Permission {
        int OTHER = 0;
        int LOCATION = 1;
        int MICROPHONE = 2;
        int CAMERA = 3;
        int STORAGE = 4;
        int NUM_ENTRIES = 5;
    }

    public static final String HISTOGRAM_UPDATE_REQUEST_SENT =
            "WebApk.Update.RequestSent";

    public static final String HISTOGRAM_UPDATE_REQUEST_QUEUED = "WebApk.Update.RequestQueued";

    private static final String HISTOGRAM_LAUNCH_TO_SPLASHSCREEN_VISIBLE =
            "WebApk.Startup.Cold.ShellLaunchToSplashscreenVisible";
    private static final String HISTOGRAM_LAUNCH_TO_SPLASHSCREEN_HIDDEN =
            "WebApk.Startup.Cold.ShellLaunchToSplashscreenHidden";

    private static final long WEBAPK_EXTRA_INSTALLATION_SPACE_BYTES =
            100 * (long) ConversionUtils.BYTES_PER_MEGABYTE; // 100 MB

    /** Makes recordings that were deferred in order to not load native. */
    public static void recordDeferredUma() {
        ChromePreferenceManager preferenceManager = ChromePreferenceManager.getInstance();
        Set<String> uninstalledPackages = preferenceManager.readStringSet(
                ChromePreferenceManager.WEBAPK_UNINSTALLED_PACKAGES);
        if (uninstalledPackages.isEmpty()) return;

        long fallbackUninstallTimestamp = System.currentTimeMillis();
        WebappRegistry.warmUpSharedPrefs();
        for (String uninstalledPackage : uninstalledPackages) {
            RecordHistogram.recordBooleanHistogram("WebApk.Uninstall.Browser", true);

            String webApkId = WebappRegistry.webApkIdForPackage(uninstalledPackage);
            WebappDataStorage webappDataStorage =
                    WebappRegistry.getInstance().getWebappDataStorage(webApkId);
            if (webappDataStorage != null) {
                long uninstallTimestamp = webappDataStorage.getWebApkUninstallTimestamp();
                if (uninstallTimestamp == 0) {
                    uninstallTimestamp = fallbackUninstallTimestamp;
                }
                WebApkUkmRecorder.recordWebApkUninstall(webappDataStorage.getWebApkManifestUrl(),
                        WebApkDistributor.BROWSER, webappDataStorage.getWebApkVersionCode(),
                        webappDataStorage.getLaunchCount(),
                        uninstallTimestamp - webappDataStorage.getWebApkInstallTimestamp());
            }
        }
        preferenceManager.writeStringSet(
                ChromePreferenceManager.WEBAPK_UNINSTALLED_PACKAGES, new HashSet<String>());

        // TODO(http://crbug.com/1000312): Clear WebappDataStorage for uninstalled WebAPK.
    }

    /** Sets WebAPK uninstall to be recorded next time that native is loaded. */
    public static void deferRecordWebApkUninstalled(String packageName) {
        ChromePreferenceManager.getInstance().addToStringSet(
                ChromePreferenceManager.WEBAPK_UNINSTALLED_PACKAGES, packageName);
        String webApkId = WebappRegistry.webApkIdForPackage(packageName);
        WebappRegistry.warmUpSharedPrefsForId(webApkId);
        WebappDataStorage webappDataStorage =
                WebappRegistry.getInstance().getWebappDataStorage(webApkId);
        if (webappDataStorage != null) {
            webappDataStorage.setWebApkUninstallTimestamp();
        }
    }

    /**
     * Records the time point when a request to update a WebAPK is sent to the WebAPK Server.
     * @param type representing when the update request is sent to the WebAPK server.
     */
    public static void recordUpdateRequestSent(@UpdateRequestSent int type) {
        RecordHistogram.recordEnumeratedHistogram(
                HISTOGRAM_UPDATE_REQUEST_SENT, type, UpdateRequestSent.NUM_ENTRIES);
    }

    /**
     * Records the times that an update request has been queued once, twice and three times before
     * sending to WebAPK server.
     * @param times representing the times that an update has been queued.
     */
    public static void recordUpdateRequestQueued(@UpdateRequestQueued int times) {
        RecordHistogram.recordEnumeratedHistogram(
                HISTOGRAM_UPDATE_REQUEST_QUEUED, times, UpdateRequestQueued.NUM_ENTRIES);
    }

    /**
     * Records duration between starting of the WebAPK shell until the splashscreen is shown.
     * @param durationMs duration in milliseconds
     */
    public static void recordShellApkLaunchToSplashscreenVisible(long durationMs) {
        RecordHistogram.recordMediumTimesHistogram(
                HISTOGRAM_LAUNCH_TO_SPLASHSCREEN_VISIBLE, durationMs);
    }

    /**
     * Records duration between starting of the WebAPK shell until the splashscreen is hidden.
     * @param durationMs duration in milliseconds
     */
    public static void recordShellApkLaunchToSplashscreenHidden(long durationMs) {
        RecordHistogram.recordMediumTimesHistogram(
                HISTOGRAM_LAUNCH_TO_SPLASHSCREEN_HIDDEN, durationMs);
    }

    /** Records whether a WebAPK has permission to display notifications. */
    public static void recordNotificationPermissionStatus(boolean permissionEnabled) {
        RecordHistogram.recordBooleanHistogram(
                "WebApk.Notification.Permission.Status", permissionEnabled);
    }

    /**
     * Records whether installing a WebAPK from Google Play succeeded. If not, records the reason
     * that the install failed.
     */
    public static void recordGooglePlayInstallResult(@GooglePlayInstallResult int result) {
        RecordHistogram.recordEnumeratedHistogram("WebApk.Install.GooglePlayInstallResult", result,
                GooglePlayInstallResult.NUM_ENTRIES);
    }

    /** Records the error code if installing a WebAPK via Google Play fails. */
    public static void recordGooglePlayInstallErrorCode(int errorCode) {
        // Don't use an enumerated histogram as there are > 30 potential error codes. In practice,
        // a given client will always get the same error code.
        RecordHistogram.recordSparseHistogram(
                "WebApk.Install.GooglePlayErrorCode", Math.min(errorCode, 1000));
    }

    /**
     * Records whether updating a WebAPK from Google Play succeeded. If not, records the reason
     * that the update failed.
     */
    public static void recordGooglePlayUpdateResult(@GooglePlayInstallResult int result) {
        RecordHistogram.recordEnumeratedHistogram("WebApk.Update.GooglePlayUpdateResult", result,
                GooglePlayInstallResult.NUM_ENTRIES);
    }

    /** Records the duration of a WebAPK session (from launch/foreground to background). */
    public static void recordWebApkSessionDuration(
            @WebApkDistributor int distributor, long duration) {
        RecordHistogram.recordLongTimesHistogram(
                "WebApk.Session.TotalDuration2." + getWebApkDistributorUmaSuffix(distributor),
                duration);
    }

    /** Records the current Shell APK version. */
    public static void recordShellApkVersion(
            int shellApkVersion, @WebApkDistributor int distributor) {
        RecordHistogram.recordSparseHistogram(
                "WebApk.ShellApkVersion2." + getWebApkDistributorUmaSuffix(distributor),
                shellApkVersion);
    }

    private static String getWebApkDistributorUmaSuffix(@WebApkDistributor int distributor) {
        switch (distributor) {
            case WebApkDistributor.BROWSER:
                return "Browser";
            case WebApkDistributor.DEVICE_POLICY:
                return "DevicePolicy";
            default:
                return "Other";
        }
    }

    /**
     * Records the requests of Android runtime permissions which haven't been granted to Chrome when
     * Chrome is running in WebAPK runtime.
     */
    public static void recordAndroidRuntimePermissionPromptInWebApk(final String[] permissions) {
        recordPermissionUma("WebApk.Permission.ChromeWithoutPermission", permissions);
    }

    /**
     * Records the amount of requests that WekAPK runtime permissions access is deined because
     * Chrome does not have that permission.
     */
    public static void recordAndroidRuntimePermissionDeniedInWebApk(final String[] permissions) {
        recordPermissionUma("WebApk.Permission.ChromePermissionDenied2", permissions);
    }

    private static void recordPermissionUma(String permissionUmaName, final String[] permissions) {
        Set<Integer> permissionGroups = new HashSet<Integer>();
        for (String permission : permissions) {
            permissionGroups.add(getPermissionGroup(permission));
        }
        for (@Permission Integer permission : permissionGroups) {
            RecordHistogram.recordEnumeratedHistogram(
                    permissionUmaName, permission, Permission.NUM_ENTRIES);
        }
    }

    private static @Permission int getPermissionGroup(String permission) {
        if (TextUtils.equals(permission, Manifest.permission.ACCESS_COARSE_LOCATION)
                || TextUtils.equals(permission, Manifest.permission.ACCESS_FINE_LOCATION)) {
            return Permission.LOCATION;
        }
        if (TextUtils.equals(permission, Manifest.permission.RECORD_AUDIO)) {
            return Permission.MICROPHONE;
        }
        if (TextUtils.equals(permission, Manifest.permission.CAMERA)) {
            return Permission.CAMERA;
        }
        if (TextUtils.equals(permission, Manifest.permission.READ_EXTERNAL_STORAGE)
                || TextUtils.equals(permission, Manifest.permission.WRITE_EXTERNAL_STORAGE)) {
            return Permission.STORAGE;
        }
        return Permission.OTHER;
    }

    /**
     * Recorded when a WebAPK is launched from the homescreen. Records the time elapsed since the
     * previous WebAPK launch. Not recorded the first time that a WebAPK is launched.
     */
    public static void recordLaunchInterval(long intervalMs) {
        RecordHistogram.recordCustomCountHistogram("WebApk.LaunchInterval2",
                (int) (DateUtils.MINUTE_IN_MILLIS * intervalMs), 30,
                (int) TimeUnit.DAYS.toMinutes(90), 50);
    }

    /** Records to UMA the count of old "WebAPK update request" files. */
    public static void recordNumberOfStaleWebApkUpdateRequestFiles(int count) {
        RecordHistogram.recordCountHistogram("WebApk.Update.NumStaleUpdateRequestFiles", count);
    }

    /** Records whether Chrome could bind to the WebAPK service. */
    public static void recordBindToWebApkServiceSucceeded(boolean bindSucceeded) {
        RecordHistogram.recordBooleanHistogram("WebApk.WebApkService.BindSuccess", bindSucceeded);
    }

    /** Records the network error code caught when a WebAPK is launched. */
    public static void recordNetworkErrorWhenLaunch(int errorCode) {
        RecordHistogram.recordSparseHistogram("WebApk.Launch.NetworkError", -errorCode);
    }

    /**
     * Records whether a WebAPK navigation is within the WebAPK's scope.
     */
    public static void recordNavigation(boolean isNavigationInScope) {
        RecordHistogram.recordBooleanHistogram("WebApk.Navigation.InScope", isNavigationInScope);
    }

    /**
     * Log necessary disk usage and cache size UMAs when WebAPK installation fails.
     */
    public static void logSpaceUsageUMAWhenInstallationFails() {
        new AsyncTask<Void>() {
            long mAvailableSpaceInByte;
            long mCacheSizeInByte;
            @Override
            protected Void doInBackground() {
                mAvailableSpaceInByte = getAvailableSpaceAboveLowSpaceLimit();
                mCacheSizeInByte = getCacheDirSize();
                return null;
            }

            @Override
            protected void onPostExecute(Void result) {
                logSpaceUsageUMAOnDataAvailable(mAvailableSpaceInByte, mCacheSizeInByte);
            }
        }
                .executeOnExecutor(AsyncTask.THREAD_POOL_EXECUTOR);
    }

    private static void logSpaceUsageUMAOnDataAvailable(long spaceSize, long cacheSize) {
        RecordHistogram.recordSparseHistogram(
                "WebApk.Install.AvailableSpace.Fail", roundByteToMb(spaceSize));

        RecordHistogram.recordSparseHistogram(
                "WebApk.Install.ChromeCacheSize.Fail", roundByteToMb(cacheSize));

        RecordHistogram.recordSparseHistogram("WebApk.Install.AvailableSpaceAfterFreeUpCache.Fail",
                roundByteToMb(spaceSize + cacheSize));
    }

    private static int roundByteToMb(long bytes) {
        int mbs = (int) (bytes / (long) ConversionUtils.BYTES_PER_MEGABYTE / 10L * 10L);
        return Math.min(1000, Math.max(-1000, mbs));
    }

    private static long getDirectorySizeInByte(File dir) {
        if (dir == null) return 0;
        if (!dir.isDirectory()) return dir.length();

        long sizeInByte = 0;
        try {
            File[] files = dir.listFiles();
            if (files == null) return 0;
            for (File file : files) sizeInByte += getDirectorySizeInByte(file);
        } catch (SecurityException e) {
            return 0;
        }
        return sizeInByte;
    }

    /**
     * @return The available space that can be used to install WebAPK. Negative value means there is
     * less free space available than the system's minimum by the given amount.
     */
    public static long getAvailableSpaceAboveLowSpaceLimit() {
        StatFs partitionStats = new StatFs(Environment.getDataDirectory().getAbsolutePath());
        long partitionAvailableBytes = partitionStats.getAvailableBytes();
        long partitionTotalBytes = partitionStats.getTotalBytes();
        long minimumFreeBytes = getLowSpaceLimitBytes(partitionTotalBytes);

        long webApkExtraSpaceBytes = 0;

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            // Extra installation space is only allowed >= Android L
            webApkExtraSpaceBytes = WEBAPK_EXTRA_INSTALLATION_SPACE_BYTES;
        }

        return partitionAvailableBytes - minimumFreeBytes + webApkExtraSpaceBytes;
    }

    /**
     * @return Size of the cache directory.
     */
    public static long getCacheDirSize() {
        return getDirectorySizeInByte(ContextUtils.getApplicationContext().getCacheDir());
    }

    /**
     * Mirror the system-derived calculation of reserved bytes and return that value.
     */
    private static long getLowSpaceLimitBytes(long partitionTotalBytes) {
        // Copied from android/os/storage/StorageManager.java
        final int defaultThresholdPercentage = 10;
        // Copied from android/os/storage/StorageManager.java
        final long defaultThresholdMaxBytes = 500 * ConversionUtils.BYTES_PER_MEGABYTE;
        // Copied from android/provider/Settings.java
        final String sysStorageThresholdPercentage = "sys_storage_threshold_percentage";
        // Copied from android/provider/Settings.java
        final String sysStorageThresholdMaxBytes = "sys_storage_threshold_max_bytes";

        ContentResolver resolver = ContextUtils.getApplicationContext().getContentResolver();
        int minFreePercent = 0;
        long minFreeBytes = 0;

        // Retrieve platform-appropriate values first
        minFreePercent = Settings.Global.getInt(
                resolver, sysStorageThresholdPercentage, defaultThresholdPercentage);
        minFreeBytes = Settings.Global.getLong(
                resolver, sysStorageThresholdMaxBytes, defaultThresholdMaxBytes);

        long minFreePercentInBytes = (partitionTotalBytes * minFreePercent) / 100;

        return Math.min(minFreeBytes, minFreePercentInBytes);
    }
}
