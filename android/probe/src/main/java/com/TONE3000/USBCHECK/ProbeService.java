package com.TONE3000.USBCHECK;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.os.Binder;
import android.os.Build;
import android.os.IBinder;
import android.os.PowerManager;
import android.util.Log;

import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Foreground owner of the probe session (plan M2.3). The stream and the USB
 * connection live here, not in the activity: the activity may be destroyed,
 * the screen may turn off, and Android will still keep the process at
 * foreground priority. Every command arrives as a service intent with a
 * "mode" extra, so adb can drive the probe without touching the UI:
 *
 *   adb shell am startservice -n com.TONE3000.USBCHECK/.ProbeService \
 *       -e mode stream -e rate 48000 -e packets 2 -e urbs 3
 *   adb shell am startservice ... -e mode rate -e rate 96000
 *   adb shell am startservice ... -e mode status
 *
 * The partial wake lock is deliberate: raw isochronous URBs do not go through
 * AudioFlinger, so nothing else holds the CPU awake while the screen is off.
 * The real app gets the same guarantee from AudioSessionService.
 */
public final class ProbeService extends Service implements UsbShell.Listener {
    private static final String TAG = UsbShell.TAG;
    private static final String ACTION_MODE = "com.TONE3000.USBCHECK.MODE";
    private static final String CHANNEL_ID = "probe";
    private static final int NOTIFICATION_ID = 1;
    private static final int LOG_RING = 2000;

    public interface Listener {
        void onLog(String line);
    }

    public final class LocalBinder extends Binder {
        public ProbeService getService() {
            return ProbeService.this;
        }
    }

    private final IBinder binder = new LocalBinder();
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private final CopyOnWriteArrayList<Listener> listeners = new CopyOnWriteArrayList<>();
    private final ArrayDeque<String> logRing = new ArrayDeque<>();
    private final SimpleDateFormat clock = new SimpleDateFormat("HH:mm:ss.SSS", Locale.US);

    private UsbShell shell;
    private PowerManager.WakeLock wakeLock;
    private int activeFd = -1;
    private boolean streaming = false;
    private boolean foreground = false;
    private String pendingMode;
    private int pendingRate = 48000;
    private int pendingPackets = 2;
    private int pendingUrbs = 3;
    private int pendingRing = 512;

    /** Queue a command for the service; safe from the activity or adb. */
    public static void sendMode(Context context, String mode, int rate, int packets, int urbs) {
        sendMode(context, mode, rate, packets, urbs, 512);
    }

    public static void sendMode(Context context, String mode, int rate, int packets, int urbs,
                                int ring) {
        Intent intent = new Intent(context, ProbeService.class)
                .setAction(ACTION_MODE)
                .putExtra("mode", mode)
                .putExtra("rate", rate)
                .putExtra("packets", packets)
                .putExtra("urbs", urbs)
                .putExtra("ring", ring);
        context.startService(intent);
    }

    @Override
    public void onCreate() {
        super.onCreate();
        createChannel();
        shell = new UsbShell(this, this);
        shell.start();
        PowerManager pm = getSystemService(PowerManager.class);
        wakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "T3KUSB:probe");
        wakeLock.setReferenceCounted(false);
        log("service: ready (rate=" + pendingRate + " layout=" + pendingPackets + "x"
                + pendingUrbs + ")");
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null) {
            final String mode = intent.getStringExtra("mode");
            if (mode != null) {
                final int rate = intent.getIntExtra("rate", 48000);
                final int packets = intent.getIntExtra("packets", 2);
                final int urbs = intent.getIntExtra("urbs", 3);
                final int ring = intent.getIntExtra("ring", 512);
                worker.execute(() -> runMode(mode, rate, packets, urbs, ring));
            }
        }
        return START_NOT_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return binder;
    }

    @Override
    public void onDestroy() {
        streaming = false;
        NativeBridge.nativeStop();
        if (shell != null) shell.stop();
        if (wakeLock != null && wakeLock.isHeld()) wakeLock.release();
        worker.shutdownNow();
        stopForeground(true);
        super.onDestroy();
    }

    public void addListener(Listener listener) {
        listeners.addIfAbsent(listener);
        for (String line : snapshotLog()) listener.onLog(line);
    }

    public void removeListener(Listener listener) {
        listeners.remove(listener);
    }

    // --- command dispatch (worker thread) ---------------------------------

    private void runMode(String mode, int rate, int packets, int urbs, int ring) {
        if (!"stop".equals(mode) && !"status".equals(mode) && shell.getActiveDevice() == null) {
            pendingMode = mode;
            pendingRate = rate;
            pendingPackets = packets;
            pendingUrbs = urbs;
            pendingRing = ring;
            log("probe: " + mode + " waiting for the USB device");
            // Permission is the activity's job: the system dismisses the
            // prompt when it comes from a service.
            shell.findAudioDevice(false);
            return;
        }
        switch (mode) {
            case "stream":
                startStream(rate, packets, urbs, ring);
                break;
            case "rate":
                switchRate(rate, packets, urbs, ring);
                break;
            case "status":
                log(NativeBridge.nativeStatus());
                break;
            case "discard":
                log("recovery: " + NativeBridge.nativeDiscardOne());
                break;
            case "reset": {
                final String err = NativeBridge.nativeReset(activeFd);
                log("reset: " + (err.isEmpty() ? "ok, re-enumerating" : err));
                shell.close();  // release the now-stale fd; the kernel rebinds
                break;
            }
            case "dump":
                runDump();
                break;
            case "rtl":
                runRtl(rate, packets, urbs, ring);
                break;
            case "loopback":
                runLoopback(rate, packets, urbs, ring);
                break;
            case "sweep":
                runSweep(rate, ring);
                break;
            case "stop":
                stopStream();
                break;
            default:
                log("probe: unknown mode " + mode);
                break;
        }
    }

    private void startStream(int rate, int packets, int urbs, int ring) {
        UsbDevice device = shell.getActiveDevice();
        if (device == null) return;
        // Idempotent: "stream" also switches layouts, so stop any current
        // stream before opening the new one (returns "not running" if idle).
        NativeBridge.nativeStop();
        NativeBridge.nativeConfigureRing(ring);
        String err = NativeBridge.nativeStart(
                activeFd, shell.getRawDescriptors(), device.getVendorId(), device.getProductId(),
                rate, 2, 3, packets, urbs);
        streaming = err.isEmpty();
        if (streaming) {
            ensureForeground();  // USB permission is held at this point
            if (!wakeLock.isHeld()) wakeLock.acquire();
            log("stream: running " + rate + " Hz/2ch layout=" + packets + "x" + urbs
                    + " ring=" + ring);
        } else {
            log("stream: " + err);
        }
    }

    private void switchRate(int rate, int packets, int urbs, int ring) {
        UsbDevice device = shell.getActiveDevice();
        if (device == null) return;
        NativeBridge.nativeConfigureRing(ring);
        String result = NativeBridge.nativeSetRate(
                activeFd, shell.getRawDescriptors(), device.getVendorId(), device.getProductId(),
                rate, 2, 3, packets, urbs);
        streaming = result.startsWith("rate=");
        log("rate: " + result);
    }

    private void stopStream() {
        String err = NativeBridge.nativeStop();
        streaming = false;
        if (wakeLock.isHeld()) wakeLock.release();
        log("stream: stop " + (err.isEmpty() ? "ok" : err));
    }

    private void runDump() {
        byte[] raw = shell.getRawDescriptors();
        UsbDevice device = shell.getActiveDevice();
        if (raw == null || device == null) {
            log("dump: no raw descriptors (device not open)");
            return;
        }
        String name = "ams22-descriptors.bin";
        try (FileOutputStream out = openFileOutput(name, MODE_PRIVATE)) {
            out.write(raw);
            log("dump: wrote files/" + name + " (" + raw.length + " bytes)");
        } catch (IOException e) {
            log("dump: fixture write failed: " + e.getMessage());
        }
        String tree = NativeBridge.nativeDump(raw, device.getVendorId(), device.getProductId());
        for (String line : tree.split("\n")) log(line);
    }

    private void runRtl(int rate, int packets, int urbs, int ring) {
        UsbDevice device = shell.getActiveDevice();
        if (device == null) return;
        // RTL runs its own stream; a running stream would win "already
        // running" and the burst would go through the wrong callback mode.
        NativeBridge.nativeStop();
        NativeBridge.nativeConfigureRing(ring);
        String rows = NativeBridge.nativeRtl(
                activeFd, shell.getRawDescriptors(), device.getVendorId(), device.getProductId(),
                rate, 2, 3, packets, urbs, 5);
        appendCsv(rows);
        for (String line : rows.split("\n")) log(line);
    }

    /** AMS loopback ON: record the captured input, which is the USB playback
        mixed digitally before the DAC, to files/loopback.csv. */
    private void runLoopback(int rate, int packets, int urbs, int ring) {
        UsbDevice device = shell.getActiveDevice();
        if (device == null) {
            log("loopback: no device");
            return;
        }
        NativeBridge.nativeStop();
        NativeBridge.nativeConfigureRing(ring);
        float[] cap = NativeBridge.nativeCaptureSine(
                activeFd, shell.getRawDescriptors(), device.getVendorId(),
                device.getProductId(), rate, 2, 3, packets, urbs, 2000);
        if (cap == null || cap.length == 0) {
            log("loopback: no capture (is LOOPBACK on?)");
            return;
        }
        StringBuilder sb = new StringBuilder(cap.length * 10);
        for (float v : cap) sb.append(v).append('\n');
        try (FileOutputStream out = openFileOutput("loopback.csv", MODE_PRIVATE)) {
            out.write(sb.toString().getBytes(StandardCharsets.UTF_8));
            log("loopback: wrote " + cap.length + " samples to files/loopback.csv");
        } catch (IOException e) {
            log("loopback: write failed: " + e.getMessage());
        }
    }

    private void runSweep(int rate, int ring) {
        UsbDevice device = shell.getActiveDevice();
        if (device == null) return;
        NativeBridge.nativeConfigureRing(ring);
        String rows = NativeBridge.nativeSweep(
                activeFd, shell.getRawDescriptors(), device.getVendorId(), device.getProductId(),
                rate, 2, 3);
        appendCsv(rows);
        for (String line : rows.split("\n")) log(line);
    }

    private void appendCsv(String rows) {
        if (rows == null || rows.isEmpty()) return;
        try (FileOutputStream out = openFileOutput("rtl.csv", MODE_APPEND)) {
            out.write((rows + "\n").getBytes(StandardCharsets.UTF_8));
            log("csv: appended to files/rtl.csv");
        } catch (IOException e) {
            log("csv: write failed: " + e.getMessage());
        }
    }

    // --- UsbShell.Listener -------------------------------------------------

    @Override
    public void onLog(String line) {
        log(line);
    }

    @Override
    public void onDeviceReady(UsbDevice device, UsbDeviceConnection connection, int fd) {
        activeFd = fd;
        log("device ready: fd=" + fd);
        if (pendingMode != null) {
            final String mode = pendingMode;
            final int rate = pendingRate;
            final int packets = pendingPackets;
            final int urbs = pendingUrbs;
            final int ring = pendingRing;
            pendingMode = null;
            worker.execute(() -> runMode(mode, rate, packets, urbs, ring));
        }
    }

    @Override
    public void onDeviceDetached() {
        streaming = false;
        if (wakeLock != null && wakeLock.isHeld()) wakeLock.release();
        log("stream stopped: device detached");
        // Release whatever URBs the dead fd still had: the connection is
        // already closed, so the ioctls fail harmlessly, but the stream
        // object must not outlive the session.
        worker.execute(() -> NativeBridge.nativeStop());
    }

    // --- foreground + logging ---------------------------------------------

    private void ensureForeground() {
        if (foreground) return;
        try {
            if (Build.VERSION.SDK_INT >= 29) {
                startForeground(NOTIFICATION_ID, buildNotification(),
                        ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE);
            } else {
                startForeground(NOTIFICATION_ID, buildNotification());
            }
            foreground = true;
        } catch (RuntimeException e) {
            // Missing USB permission or a background-start restriction: the
            // stream still works while the activity is visible.
            log("service: cannot enter foreground: " + e.getMessage());
            foreground = false;
        }
    }

    private void createChannel() {
        NotificationManager manager = getSystemService(NotificationManager.class);
        NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID, "USB probe", NotificationManager.IMPORTANCE_LOW);
        channel.setDescription("Shown while the USB audio probe keeps a stream open.");
        channel.setShowBadge(false);
        manager.createNotificationChannel(channel);
    }

    private Notification buildNotification() {
        Intent open = new Intent(this, MainActivity.class);
        PendingIntent content = PendingIntent.getActivity(
                this, 0, open, PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        Intent stop = new Intent(this, ProbeService.class)
                .setAction(ACTION_MODE)
                .putExtra("mode", "stop");
        PendingIntent stopIntent = PendingIntent.getService(
                this, 1, stop, PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        Notification.Builder builder = new Notification.Builder(this, CHANNEL_ID)
                .setSmallIcon(android.R.drawable.ic_media_play)
                .setContentTitle("USBCHECK probe")
                .setContentText("USB audio stream is running")
                .setContentIntent(content)
                .setOngoing(true)
                .addAction(new Notification.Action.Builder(
                        android.graphics.drawable.Icon.createWithResource(
                                this, android.R.drawable.ic_media_pause),
                        "Stop", stopIntent).build())
                .setCategory(Notification.CATEGORY_SERVICE);
        if (Build.VERSION.SDK_INT >= 31) {
            builder.setForegroundServiceBehavior(Notification.FOREGROUND_SERVICE_IMMEDIATE);
        }
        return builder.build();
    }

    private void log(String line) {
        String stamped = clock.format(new Date()) + " " + line;
        Log.i(TAG, line);
        synchronized (logRing) {
            logRing.addLast(stamped);
            while (logRing.size() > LOG_RING) logRing.removeFirst();
        }
        for (Listener listener : listeners) listener.onLog(stamped);
        try (FileOutputStream out = openFileOutput("probe.log", MODE_APPEND)) {
            out.write((stamped + "\n").getBytes(StandardCharsets.UTF_8));
        } catch (IOException ignored) {
        }
    }

    private List<String> snapshotLog() {
        synchronized (logRing) {
            return new ArrayList<>(logRing);
        }
    }
}
