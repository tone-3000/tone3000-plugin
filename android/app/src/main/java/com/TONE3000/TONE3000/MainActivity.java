package com.TONE3000.TONE3000;

import android.Manifest;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;

import java.io.File;
import java.io.IOException;

import com.rmsl.juce.JuceActivity;

// JUCE's stock activity plus the lifetime of AudioSessionService. The native
// methods JuceActivity declares stay registered against JuceActivity itself
// (JUCE_PUSH_NOTIFICATIONS_ACTIVITY in plugin/CMakeLists.txt), which a
// subclass inherits unchanged.
public class MainActivity extends JuceActivity
{
    private static final String PREFS = "tone3000_activity";
    private static final String ASKED_FOR_NOTIFICATIONS = "asked_for_notifications";

    @Override
    protected void onCreate (Bundle savedInstanceState)
    {
        // USB Direct (plan M3): the activity owns the USB permission prompt.
        // Register *before* super.onCreate: JUCE can build the standalone
        // holder/editor from the native onCreate, and the settings type scan
        // needs a live UsbManager by then.
        UsbDirectBridge.register (this);

        // Debug hook for the automated M3 measurement: create or remove the
        // marker the native side reads when registering the device type.
        // adb shell am start -n .../.MainActivity --ez usbDirect true
        Intent intent = getIntent();

        if (isDebuggable() && intent != null && intent.hasExtra ("usbDirect"))
        {
            File marker = new File (getDataDir(), "usb_direct_test");

            try
            {
                if (intent.getBooleanExtra ("usbDirect", false))
                {
                    if (! marker.exists())
                        marker.createNewFile();
                }
                else
                {
                    marker.delete();
                }
            }
            catch (IOException e)
            {
                Log.w ("TONE3000", "USB Direct: could not write the test marker", e);
            }
        }

        super.onCreate (savedInstanceState);
    }

    private boolean isDebuggable()
    {
        return (getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) != 0;
    }

    @Override
    protected void onResume()
    {
        super.onResume();

        // Every resume, not just the first: the microphone permission is
        // usually granted from JUCE's runtime prompt after launch, and the
        // prompt's dismissal is itself a resume. Repeat starts are no-ops
        // beyond refreshing the notification.
        AudioSessionService.startIfPermitted (this);
        UsbDirectBridge.ensurePermission (this);

        requestNotificationsOnce();
    }

    @Override
    protected void onDestroy()
    {
        if (isFinishing())
            AudioSessionService.stop (this);

        super.onDestroy();
    }

    // Android 13+ hides AudioSessionService's notification until the app may
    // post notifications. Asked for once per install, and only after the
    // microphone is granted: the service (and so the notification) only
    // exists from then on, and it keeps this prompt from stacking on JUCE's
    // microphone prompt. JUCE requests through its own fragment
    // (FragmentOverlay), so an activity-level request doesn't disturb it.
    // Dismissing this prompt is again a resume, which re-posts the
    // notification once allowed.
    private void requestNotificationsOnce()
    {
        if (Build.VERSION.SDK_INT < 33
            || checkSelfPermission (Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED
            || checkSelfPermission (Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED)
            return;

        SharedPreferences prefs = getSharedPreferences (PREFS, MODE_PRIVATE);

        if (prefs.getBoolean (ASKED_FOR_NOTIFICATIONS, false))
            return;

        prefs.edit().putBoolean (ASKED_FOR_NOTIFICATIONS, true).apply();
        requestPermissions (new String[] { Manifest.permission.POST_NOTIFICATIONS }, 0);
    }
}
