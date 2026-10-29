package com.TONE3000.TONE3000;

import android.app.Activity;
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

import java.lang.ref.WeakReference;

/**
 * Java half of the "USB Direct" audio device (plan M3.1). The activity owns
 * the USB permission prompt; the native UsbDirectAudioIODevice calls the
 * static open/close/descriptor methods on the message thread when the
 * settings select this device type.
 *
 * Claiming the AudioControl and AudioStreaming interfaces with force=true
 * detaches snd-usb-audio for this app, exactly as the probe does. Interfaces
 * the quirk registry recognises (the AMS-22's extra AC block and MIDI
 * function) stay with the kernel.
 */
public final class UsbDirectBridge {
    private static final String TAG = "TONE3000";
    private static final String ACTION_PERMISSION = "com.TONE3000.TONE3000.USB_PERMISSION";

    private static UsbManager usbManager;
    private static UsbDeviceConnection connection;
    private static UsbDevice device;
    private static WeakReference<Activity> activityRef;
    private static boolean receiversRegistered;

    private UsbDirectBridge() {}

    public static void register(Activity activity) {
        activityRef = new WeakReference<>(activity);
        usbManager = (UsbManager) activity.getSystemService(Context.USB_SERVICE);
        Log.i(TAG, "USB Direct: register usbManager=" + (usbManager != null)
                + " devices=" + (usbManager != null ? usbManager.getDeviceList().size() : -1));
        if (receiversRegistered) return;
        receiversRegistered = true;
        IntentFilter permission = new IntentFilter(ACTION_PERMISSION);
        IntentFilter detach = new IntentFilter(UsbManager.ACTION_USB_DEVICE_DETACHED);
        if (Build.VERSION.SDK_INT >= 33) {
            activity.registerReceiver(permissionReceiver, permission, Context.RECEIVER_EXPORTED);
            activity.registerReceiver(detachReceiver, detach, Context.RECEIVER_EXPORTED);
        } else {
            activity.registerReceiver(permissionReceiver, permission);
            activity.registerReceiver(detachReceiver, detach);
        }
    }

    /** Ask for USB access from the resumed activity (the system dismisses
        prompts that a service requests). */
    public static void ensurePermission(Activity activity) {
        if (usbManager == null) register(activity);
        UsbDevice d = findAudioDevice();
        if (d == null || usbManager.hasPermission(d)) return;
        Intent reply = new Intent(ACTION_PERMISSION);
        reply.setPackage(activity.getPackageName());
        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= 31) flags |= PendingIntent.FLAG_MUTABLE;
        PendingIntent pi = PendingIntent.getBroadcast(activity, 0, reply, flags);
        Log.i(TAG, "USB Direct: requesting permission for " + d.getProductName());
        usbManager.requestPermission(d, pi);
    }

    public static boolean isAvailable() {
        UsbDevice d = findAudioDevice();
        Log.i(TAG, "USB Direct: isAvailable=" + (d != null)
                + " usbManager=" + (usbManager != null));
        return d != null;
    }

    public static String deviceName() {
        UsbDevice d = findAudioDevice();
        Log.i(TAG, "USB Direct: deviceName=" + (d != null ? d.getProductName() : "none"));
        if (d == null) return "USB audio device";
        String name = d.getProductName();
        return name != null && !name.isEmpty() ? name : "USB audio device";
    }

    /** Open (or reuse) the device and claim its audio interfaces. Returns the
        usbfs fd, or -1. */
    public static int openDevice() {
        UsbDevice d = findAudioDevice();
        if (d == null || usbManager == null) return -1;
        if (connection != null && device != null && device.getDeviceId() == d.getDeviceId())
            return connection.getFileDescriptor();
        if (!usbManager.hasPermission(d)) return -1;

        UsbDeviceConnection conn = usbManager.openDevice(d);
        if (conn == null) {
            Log.w(TAG, "USB Direct: openDevice failed");
            return -1;
        }
        connection = conn;
        device = d;
        for (int i = 0; i < d.getInterfaceCount(); ++i) {
            UsbInterface intf = d.getInterface(i);
            if (intf.getInterfaceClass() != UsbConstants.USB_CLASS_AUDIO) continue;
            if (nativeQuirkIgnoresInterface(d.getVendorId(), d.getProductId(),
                                            intf.getId())) {
                Log.i(TAG, "USB Direct: iface " + intf.getId() + " left to the kernel (quirk)");
                continue;
            }
            boolean claimed = conn.claimInterface(intf, true);
            Log.i(TAG, "USB Direct: iface " + intf.getId() + " claim=" + claimed);
        }
        Log.i(TAG, "USB Direct: fd=" + conn.getFileDescriptor());
        return conn.getFileDescriptor();
    }

    public static void closeDevice() {
        if (connection != null) {
            connection.close();
            connection = null;
            device = null;
        }
    }

    public static byte[] getRawDescriptors() {
        byte[] raw = connection != null ? connection.getRawDescriptors() : null;
        Log.i(TAG, "USB Direct: descriptors=" + (raw != null ? raw.length : -1));
        return raw;
    }

    private static UsbDevice findAudioDevice() {
        if (usbManager == null) return null;
        for (UsbDevice d : usbManager.getDeviceList().values()) {
            for (int i = 0; i < d.getInterfaceCount(); ++i) {
                if (d.getInterface(i).getInterfaceClass() == UsbConstants.USB_CLASS_AUDIO)
                    return d;
            }
        }
        return null;
    }

    /**
     * True when the kernel's snd-usb-audio still holds the AudioControl
     * interface, i.e. a physical replug has rebound it. False after USB
     * Direct claimed the interfaces: Oboe/AAudio opening the device in that
     * state blocks in the audio HAL long enough to ANR (observed 2026-10-19).
     *
     * Opening the device without claiming an interface is side-effect free;
     * the ioctl only asks which driver is bound.
     */
    public static boolean kernelDriverBound() {
        UsbDevice d = findAudioDevice();
        if (d == null || usbManager == null || !usbManager.hasPermission(d)) return false;
        UsbDeviceConnection conn = usbManager.openDevice(d);
        if (conn == null) return false;
        boolean bound = nativeKernelDriverBound(conn.getFileDescriptor());
        conn.close();
        return bound;
    }

    private static final BroadcastReceiver permissionReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (!ACTION_PERMISSION.equals(intent.getAction())) return;
            boolean granted =
                    intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false);
            Log.i(TAG, "USB Direct: permission " + (granted ? "granted" : "denied"));
            if (granted) {
                Activity activity = activityRef != null ? activityRef.get() : null;
                if (activity != null) ensurePermission(activity);
            }
        }
    };

    private static final BroadcastReceiver detachReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (!UsbManager.ACTION_USB_DEVICE_DETACHED.equals(intent.getAction())) return;
            UsbDevice gone = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
            if (device != null && gone != null && gone.getDeviceId() == device.getDeviceId()) {
                Log.i(TAG, "USB Direct: device detached");
                closeDevice();
            }
        }
    };

    /** Quirk registry lookup (plugin/usb/UsbQuirks, via JNI). */
    private static native boolean nativeQuirkIgnoresInterface(int vid, int pid, int iface);

    /** USBDEVFS_GETDRIVER on interface 0 (plugin/usb/android/UsbfsTransport). */
    private static native boolean nativeKernelDriverBound(int fd);
}
