package com.TONE3000.USBCHECK;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.usb.UsbConstants;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbInterface;
import android.hardware.usb.UsbManager;
import android.os.Build;
import android.util.Log;

import java.util.HashMap;

/**
 * The probe's USB shell (plan M1.2): enumerate USB Audio devices, request
 * permission, open the connection and claim the AudioControl and
 * AudioStreaming interfaces with force=true (which detaches the kernel's
 * snd-usb-audio; expected and required), and hand the raw fd to native.
 * Handles ACTION_USB_DEVICE_DETACHED by tearing down cleanly.
 *
 * MIDI/vendor/HID interfaces are left alone (the quirk registry decides
 * about odd extra audio-class interfaces).
 */
public final class UsbShell {
    public static final String TAG = "T3KUSB";
    private static final String ACTION_PERMISSION = "com.TONE3000.USBCHECK.USB_PERMISSION";

    public interface Listener {
        void onLog(String line);
        /** Permission granted, connection open, interfaces claimed, fd live. */
        void onDeviceReady(UsbDevice device, UsbDeviceConnection connection, int fd);
        void onDeviceDetached();
    }

    private final Context context;
    private final UsbManager usbManager;
    private final Listener listener;
    private UsbDevice activeDevice;
    private UsbDeviceConnection connection;
    private boolean receiversRegistered = false;

    public UsbShell(Context context, Listener listener) {
        this.context = context;
        this.usbManager = (UsbManager) context.getSystemService(Context.USB_SERVICE);
        this.listener = listener;
    }

    public void start() {
        if (receiversRegistered) return;
        receiversRegistered = true;

        IntentFilter permissionFilter = new IntentFilter(ACTION_PERMISSION);
        context.registerReceiver(permissionReceiver, permissionFilter, exportedFlag());

        IntentFilter detachFilter = new IntentFilter(UsbManager.ACTION_USB_DEVICE_DETACHED);
        context.registerReceiver(detachReceiver, detachFilter, exportedFlag());
    }

    public void stop() {
        if (!receiversRegistered) return;
        receiversRegistered = false;
        try {
            context.unregisterReceiver(permissionReceiver);
            context.unregisterReceiver(detachReceiver);
        } catch (IllegalArgumentException ignored) {
        }
        close();
    }

    private int exportedFlag() {
        // System broadcasts (USB attach/detach, the permission reply) must
        // reach dynamically registered receivers; on API 33+ the flag is
        // mandatory at registration time.
        return Build.VERSION.SDK_INT >= 33 ? Context.RECEIVER_EXPORTED : 0;
    }

    /** Find the first device exposing a USB Audio class interface. The
        activity owns the permission prompt (the system dismisses it when a
        service requests it); pass true there, false from the service. */
    public UsbDevice findAudioDevice() {
        return findAudioDevice(true);
    }

    public UsbDevice findAudioDevice(boolean requestPermission) {
        HashMap<String, UsbDevice> devices = usbManager.getDeviceList();
        listener.onLog("usb: " + devices.size() + " device(s) on the bus");
        for (UsbDevice device : devices.values()) {
            boolean audio = false;
            StringBuilder ifaces = new StringBuilder();
            for (int i = 0; i < device.getInterfaceCount(); ++i) {
                UsbInterface intf = device.getInterface(i);
                ifaces.append(" [").append(intf.getId()).append(" cls ")
                        .append(intf.getInterfaceClass()).append('/')
                        .append(intf.getInterfaceSubclass()).append('/')
                        .append(intf.getInterfaceProtocol()).append(']');
                if (intf.getInterfaceClass() == UsbConstants.USB_CLASS_AUDIO) audio = true;
            }
            listener.onLog("usb: " + deviceName(device) + ifaces);
            if (!audio) continue;

            if (usbManager.hasPermission(device)) {
                openAndClaim(device);
            } else if (!requestPermission) {
                listener.onLog("usb: permission not granted; drive the activity once to grant it");
            } else {
                listener.onLog("usb: requesting permission for " + deviceName(device));
                // Targeting U+: a FLAG_MUTABLE PendingIntent must carry an
                // explicit intent (setPackage), or getBroadcast throws. The
                // flag itself is required: the system fills in the grant
                // result as extras.
                Intent intent = new Intent(ACTION_PERMISSION);
                intent.setPackage(context.getPackageName());
                PendingIntent pi = PendingIntent.getBroadcast(
                        context, 0, intent, PendingIntent.FLAG_MUTABLE);
                usbManager.requestPermission(device, pi);
            }
            return device;
        }
        listener.onLog("usb: no USB Audio device found");
        return null;
    }

    private void openAndClaim(UsbDevice device) {
        close();
        UsbDeviceConnection conn = usbManager.openDevice(device);
        if (conn == null) {
            listener.onLog("usb: openDevice failed for " + deviceName(device));
            return;
        }
        connection = conn;
        activeDevice = device;
        final int fd = conn.getFileDescriptor();

        for (int i = 0; i < device.getInterfaceCount(); ++i) {
            UsbInterface intf = device.getInterface(i);
            // Which kernel driver held this interface before we take it
            // (plan M1.3 wants this in the report).
            String driver = NativeBridge.nativeGetDriver(fd, intf.getId());
            listener.onLog("usb: iface " + intf.getId() + " kernel driver='"
                    + (driver.isEmpty() ? "none" : driver) + "'");

            if (intf.getInterfaceClass() != UsbConstants.USB_CLASS_AUDIO) {
                listener.onLog("usb: iface " + intf.getId() + " not audio class, leaving alone");
                continue;
            }
            if (NativeBridge.nativeQuirkIgnoresInterface(
                    device.getVendorId(), device.getProductId(), intf.getId())) {
                listener.onLog("usb: iface " + intf.getId() + " left unclaimed (quirk)");
                continue;
            }
            // force=true detaches snd-usb-audio; required for our own URBs.
            boolean claimed = conn.claimInterface(intf, true);
            listener.onLog("usb: iface " + intf.getId() + " claim=" + claimed);
        }
        listener.onDeviceReady(device, conn, fd);
    }

    public void close() {
        if (connection != null) {
            connection.close();
            connection = null;
            activeDevice = null;
        }
    }

    public UsbDevice getActiveDevice() {
        return activeDevice;
    }

    /** Raw descriptor bytes of the active configuration (the fixture the L0
        parser tests consume, plan M1.3). */
    public byte[] getRawDescriptors() {
        return connection != null ? connection.getRawDescriptors() : null;
    }

    private static String deviceName(UsbDevice device) {
        return String.format("vid=0x%04x pid=0x%04x %s %s",
                device.getVendorId(), device.getProductId(),
                device.getManufacturerName(), device.getProductName());
    }

    private final BroadcastReceiver permissionReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context ctx, Intent intent) {
            if (!ACTION_PERMISSION.equals(intent.getAction())) return;
            UsbDevice device = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
            boolean granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false);
            if (device == null) return;
            listener.onLog("usb: permission " + (granted ? "granted" : "denied") + " for "
                    + deviceName(device));
            if (granted) openAndClaim(device);
        }
    };

    private final BroadcastReceiver detachReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context ctx, Intent intent) {
            if (!UsbManager.ACTION_USB_DEVICE_DETACHED.equals(intent.getAction())) return;
            UsbDevice device = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
            if (device == null || activeDevice == null
                    || device.getDeviceId() != activeDevice.getDeviceId()) {
                return;
            }
            listener.onLog("usb: device detached");
            close();
            listener.onDeviceDetached();
        }
    };
}
