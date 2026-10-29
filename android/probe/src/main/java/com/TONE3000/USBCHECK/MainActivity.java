package com.TONE3000.USBCHECK;

import android.app.Activity;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.ServiceConnection;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbManager;
import android.os.Build;
import android.os.Bundle;
import android.os.IBinder;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/**
 * Probe UI (plan section 4, L1). The session itself lives in ProbeService so
 * that the stream survives this activity and the screen turning off; this
 * class shows the log, forwards commands, and owns the USB permission
 * prompt (the system dismisses the dialog when a service requests it).
 *
 * Every mode can also be driven over adb:
 *   adb shell am start -n com.TONE3000.USBCHECK/.MainActivity -e mode stream
 *   adb shell am startservice -n com.TONE3000.USBCHECK/.ProbeService -e mode status
 */
public final class MainActivity extends Activity implements ProbeService.Listener {
    private static final String ACTION_PERMISSION = "com.TONE3000.USBCHECK.ACTIVITY_PERMISSION";

    private TextView logView;
    private final StringBuilder logText = new StringBuilder();
    private ProbeService service;
    private boolean bound;
    private UsbManager usbManager;

    private String pendingMode;
    private int pendingRate = 48000;
    private int pendingPackets = 2;
    private int pendingUrbs = 3;
    private int pendingRing = 512;
    private boolean receiverRegistered;

    private final ServiceConnection connection = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName name, IBinder binder) {
            service = ((ProbeService.LocalBinder) binder).getService();
            bound = true;
            service.addListener(MainActivity.this);
        }

        @Override
        public void onServiceDisconnected(ComponentName name) {
            service = null;
            bound = false;
        }
    };

    private final BroadcastReceiver permissionReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (!ACTION_PERMISSION.equals(intent.getAction())) return;
            boolean granted =
                    intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false);
            log("usb: permission " + (granted ? "granted" : "denied"));
            if (granted && pendingMode != null) {
                ProbeService.sendMode(MainActivity.this, pendingMode, pendingRate,
                        pendingPackets, pendingUrbs, pendingRing);
                pendingMode = null;
            }
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);

        LinearLayout top = new LinearLayout(this);
        top.addView(modeButton("dump", "Dump"));
        top.addView(modeButton("stream", "Stream"));
        top.addView(modeButton("rtl", "RTL"));
        top.addView(modeButton("sweep", "Sweep"));
        top.addView(modeButton("stop", "Stop"));
        root.addView(top);

        LinearLayout recovery = new LinearLayout(this);
        recovery.addView(rateButton(48000, "48k"));
        recovery.addView(rateButton(96000, "96k"));
        recovery.addView(modeButton("status", "Status"));
        recovery.addView(modeButton("discard", "Drop URB"));
        root.addView(recovery);

        logView = new TextView(this);
        logView.setGravity(Gravity.BOTTOM);
        logView.setTextSize(11);
        ScrollView scroll = new ScrollView(this);
        scroll.addView(logView);
        root.addView(scroll);
        setContentView(root);

        usbManager = getSystemService(UsbManager.class);
        IntentFilter filter = new IntentFilter(ACTION_PERMISSION);
        if (Build.VERSION.SDK_INT >= 33) {
            registerReceiver(permissionReceiver, filter, Context.RECEIVER_EXPORTED);
        } else {
            registerReceiver(permissionReceiver, filter);
        }
        receiverRegistered = true;

        startService(new Intent(this, ProbeService.class));
        bindService(new Intent(this, ProbeService.class), connection, Context.BIND_AUTO_CREATE);
        handleIntent(getIntent());
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        handleIntent(intent);
    }

    @Override
    protected void onDestroy() {
        if (receiverRegistered) {
            unregisterReceiver(permissionReceiver);
            receiverRegistered = false;
        }
        if (bound) {
            if (service != null) service.removeListener(this);
            unbindService(connection);
            bound = false;
        }
        // Deliberately do not stop the service or the stream: the probe is
        // meant to keep running with the screen off.
        super.onDestroy();
    }

    private void handleIntent(Intent intent) {
        String mode = intent != null ? intent.getStringExtra("mode") : null;
        if (mode == null) {
            log("ready: tap a mode (or am start ... -e mode dump)");
            return;
        }
        int rate = intent.getIntExtra("rate", 48000);
        int packets = intent.getIntExtra("packets", 2);
        int urbs = intent.getIntExtra("urbs", 3);
        int ring = intent.getIntExtra("ring", 512);
        log("mode: " + mode + " (from intent)");
        dispatch(mode, rate, packets, urbs, ring);
    }

    /** Owns the permission prompt; the service is only told about the mode
        once the grant exists. */
    private void dispatch(String mode, int rate, int packets, int urbs, int ring) {
        UsbDevice device = findAudioDevice();
        if (device == null) {
            log("usb: no USB Audio device attached");
            return;
        }
        if (usbManager.hasPermission(device)) {
            ProbeService.sendMode(this, mode, rate, packets, urbs, ring);
            return;
        }
        pendingMode = mode;
        pendingRate = rate;
        pendingPackets = packets;
        pendingUrbs = urbs;
        pendingRing = ring;
        log("usb: requesting permission for " + device.getProductName());
        Intent reply = new Intent(ACTION_PERMISSION);
        reply.setPackage(getPackageName());
        PendingIntent pi = PendingIntent.getBroadcast(
                this, 0, reply, PendingIntent.FLAG_MUTABLE);
        usbManager.requestPermission(device, pi);
    }

    private UsbDevice findAudioDevice() {
        for (UsbDevice device : usbManager.getDeviceList().values()) {
            for (int i = 0; i < device.getInterfaceCount(); ++i) {
                if (device.getInterface(i).getInterfaceClass()
                        == android.hardware.usb.UsbConstants.USB_CLASS_AUDIO) {
                    return device;
                }
            }
        }
        return null;
    }

    private Button modeButton(final String mode, String label) {
        Button b = new Button(this);
        b.setText(label);
        b.setOnClickListener(v -> dispatch(mode, 48000, 8, 3, 192));
        return b;
    }

    private Button rateButton(final int rate, String label) {
        Button b = new Button(this);
        b.setText(label);
        b.setOnClickListener(v -> dispatch("rate", rate, 8, 3, 192));
        return b;
    }

    @Override
    public void onLog(final String line) {
        runOnUiThread(() -> {
            logText.append(line).append('\n');
            if (logText.length() > 200_000) {
                logText.delete(0, logText.length() - 150_000);
            }
            logView.setText(logText);
        });
    }

    private void log(String line) {
        onLog(line);
    }
}
