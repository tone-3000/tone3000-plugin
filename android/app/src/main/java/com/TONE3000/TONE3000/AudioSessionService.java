package com.TONE3000.TONE3000;

import android.Manifest;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.IBinder;
import android.util.Log;

// Keeps the audio engine fully alive while the app is not visible (screen
// locked, another app in front). Without a foreground service of type
// "microphone", Android 11+ feeds a backgrounded app silence from the
// microphone, and the process drops to background scheduling priority. The
// service does no work itself: the Oboe streams JUCE opened keep running in
// the same process, this only changes how Android treats that process.
public class AudioSessionService extends Service
{
    private static final String TAG = "TONE3000";
    private static final String CHANNEL_ID = "audio_session";
    private static final int NOTIFICATION_ID = 1;

    static void startIfPermitted (Context context)
    {
        // A microphone-type foreground service may only start once
        // RECORD_AUDIO is granted (Android 14+ throws otherwise), and only
        // from the foreground, which the caller's onResume() guarantees.
        if (context.checkSelfPermission (Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED)
            return;

        try
        {
            context.startForegroundService (new Intent (context, AudioSessionService.class));
        }
        catch (RuntimeException e)
        {
            Log.w (TAG, "Could not start the audio session service", e);
        }
    }

    static void stop (Context context)
    {
        context.stopService (new Intent (context, AudioSessionService.class));
    }

    @Override
    public int onStartCommand (Intent intent, int flags, int startId)
    {
        try
        {
            if (Build.VERSION.SDK_INT >= 30)
                startForeground (NOTIFICATION_ID, buildNotification(), ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE);
            else
                startForeground (NOTIFICATION_ID, buildNotification());
        }
        catch (RuntimeException e)
        {
            // SecurityException if the permission was revoked in between, or
            // ForegroundServiceStartNotAllowedException (Android 12+) if the
            // app was already backgrounded. Audio still runs, just without
            // the background guarantees.
            Log.w (TAG, "Could not enter the foreground", e);
            stopSelf();
        }

        return START_NOT_STICKY;
    }

    // Swiping the app away from Recents ends the session.
    @Override
    public void onTaskRemoved (Intent rootIntent)
    {
        stopSelf();
    }

    @Override
    public IBinder onBind (Intent intent)
    {
        return null;
    }

    private Notification buildNotification()
    {
        NotificationManager manager = getSystemService (NotificationManager.class);
        NotificationChannel channel = new NotificationChannel (CHANNEL_ID, "Audio processing",
                                                               NotificationManager.IMPORTANCE_LOW);
        channel.setDescription ("Shown while TONE3000 keeps processing audio in the background.");
        channel.setShowBadge (false);
        manager.createNotificationChannel (channel);

        // The launcher's own intent: brings the existing task forward rather
        // than stacking a second MainActivity (and a second JUCE window).
        Intent openApp = getPackageManager().getLaunchIntentForPackage (getPackageName());
        PendingIntent contentIntent = PendingIntent.getActivity (this, 0, openApp, PendingIntent.FLAG_IMMUTABLE);

        Notification.Builder builder = new Notification.Builder (this, CHANNEL_ID)
                   .setSmallIcon (R.drawable.ic_notification)
                   .setContentTitle ("TONE3000 is running")
                   .setContentText ("Audio keeps playing while the screen is off.")
                   .setContentIntent (contentIntent)
                   .setOngoing (true)
                   .setCategory (Notification.CATEGORY_SERVICE);

        // Android 12+ otherwise holds a foreground service's notification
        // back for up to 10 s.
        if (Build.VERSION.SDK_INT >= 31)
            builder.setForegroundServiceBehavior (Notification.FOREGROUND_SERVICE_IMMEDIATE);

        return builder.build();
    }
}
