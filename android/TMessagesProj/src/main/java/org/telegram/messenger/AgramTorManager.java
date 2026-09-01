/*
 * This file is part of Agram and is licensed under GNU GPL v2 or later.
 */
package org.telegram.messenger;

import android.content.BroadcastReceiver;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.ServiceConnection;
import android.os.Build;
import android.os.IBinder;

import org.torproject.jni.TorService;

import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.CopyOnWriteArrayList;

/**
 * Owns the Tor daemon embedded in the Agram process.
 *
 * One daemon is intentionally shared to avoid running dozens of native Tor
 * instances. Container traffic is separated with IsolateSOCKSAuth and a
 * different encrypted SOCKS credential for every container.
 */
public final class AgramTorManager {
    public static final String STATE_STOPPED = "stopped";
    public static final String STATE_STARTING = "starting";
    public static final String STATE_READY = "ready";
    public static final String STATE_ERROR = "error";

    public interface Listener {
        void onTorStateChanged(String state);
    }

    private static final AgramTorManager INSTANCE = new AgramTorManager();

    private final CopyOnWriteArrayList<Listener> listeners = new CopyOnWriteArrayList<>();
    private Context applicationContext;
    private TorService torService;
    private boolean receiverRegistered;
    private boolean binding;
    private boolean bound;
    private volatile String state = STATE_STOPPED;
    private volatile int socksPort;

    public static AgramTorManager getInstance() {
        return INSTANCE;
    }

    private AgramTorManager() {
    }

    public String getState() {
        return state;
    }

    public int getSocksPort() {
        return STATE_READY.equals(state) ? socksPort : 0;
    }

    public void addListener(Listener listener) {
        if (listener != null) {
            listeners.addIfAbsent(listener);
        }
    }

    public void removeListener(Listener listener) {
        listeners.remove(listener);
    }

    public synchronized void ensureStarted() {
        if (STATE_READY.equals(state) || binding || bound) {
            return;
        }
        applicationContext = ApplicationLoader.applicationContext.getApplicationContext();
        try {
            registerReceiver();
            configureTor();
            updateState(STATE_STARTING, 0);
            binding = true;
            Intent intent = new Intent(applicationContext, TorService.class);
            intent.setAction(TorService.ACTION_START);
            if (!applicationContext.bindService(intent, serviceConnection, Context.BIND_AUTO_CREATE)) {
                binding = false;
                updateState(STATE_ERROR, 0);
            }
        } catch (Throwable error) {
            binding = false;
            FileLog.e("Unable to start embedded Tor", error);
            updateState(STATE_ERROR, 0);
        }
    }

    public synchronized void restart() {
        disconnect();
        updateState(STATE_STOPPED, 0);
        AndroidUtilities.runOnUIThread(this::ensureStarted, 1_000);
    }

    public synchronized void stopIfUnused() {
        for (int account = 0; account < UserConfig.MAX_ACCOUNT_COUNT; account++) {
            AgramContainerManager.ContainerRecord record = AgramContainerManager.getInstance().getContainer(account);
            if (record != null && AgramContainerManager.NETWORK_TOR.equals(record.proxyMode)) {
                return;
            }
        }
        disconnect();
        updateState(STATE_STOPPED, 0);
    }

    private void registerReceiver() {
        if (receiverRegistered) {
            return;
        }
        TorService.setBroadcastPackageName(applicationContext.getPackageName());
        IntentFilter filter = new IntentFilter();
        filter.addAction(TorService.ACTION_STATUS);
        filter.addAction(TorService.ACTION_ERROR);
        if (Build.VERSION.SDK_INT >= 33) {
            applicationContext.registerReceiver(statusReceiver, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            applicationContext.registerReceiver(statusReceiver, filter);
        }
        receiverRegistered = true;
    }

    private void configureTor() throws Exception {
        String torrc = "ClientOnly 1\n"
                + "AvoidDiskWrites 1\n"
                + "SafeLogging 1\n"
                + "SocksPort 0\n"
                + "SocksPort 127.0.0.1:auto IsolateSOCKSAuth\n";
        try (FileOutputStream output = new FileOutputStream(TorService.getTorrc(applicationContext), false)) {
            output.write(torrc.getBytes(StandardCharsets.UTF_8));
            output.flush();
        }
    }

    private synchronized void activateIfReady() {
        if (torService == null) {
            return;
        }
        int port = torService.getSocksPort();
        if (port > 0 && port <= 65535) {
            updateState(STATE_READY, port);
        }
    }

    private synchronized void disconnect() {
        if (applicationContext != null && (bound || binding)) {
            try {
                applicationContext.unbindService(serviceConnection);
            } catch (Throwable ignore) {
            }
        }
        torService = null;
        binding = false;
        bound = false;
        socksPort = 0;
    }

    private void updateState(String newState, int newPort) {
        if (newState.equals(state) && newPort == socksPort) {
            return;
        }
        state = newState;
        socksPort = newPort;
        AgramNetworkController.getInstance().onTorStateChanged(newState, newPort);
        AndroidUtilities.runOnUIThread(() -> {
            for (Listener listener : listeners) {
                listener.onTorStateChanged(newState);
            }
        });
    }

    private final ServiceConnection serviceConnection = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName name, IBinder service) {
            synchronized (AgramTorManager.this) {
                binding = false;
                bound = true;
                torService = ((TorService.LocalBinder) service).getService();
                activateIfReady();
            }
        }

        @Override
        public void onServiceDisconnected(ComponentName name) {
            synchronized (AgramTorManager.this) {
                torService = null;
                binding = false;
                bound = false;
                updateState(STATE_ERROR, 0);
            }
        }
    };

    private final BroadcastReceiver statusReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (TorService.ACTION_ERROR.equals(intent.getAction())) {
                String message = intent.getStringExtra(Intent.EXTRA_TEXT);
                FileLog.e("Embedded Tor error" + (message == null ? "" : ": " + message));
                updateState(STATE_ERROR, 0);
                return;
            }
            if (!TorService.ACTION_STATUS.equals(intent.getAction())) {
                return;
            }
            String servicePackage = intent.getStringExtra(TorService.EXTRA_SERVICE_PACKAGE_NAME);
            if (servicePackage != null && !context.getPackageName().equals(servicePackage)) {
                return;
            }
            String status = intent.getStringExtra(TorService.EXTRA_STATUS);
            if (TorService.STATUS_ON.equals(status)) {
                activateIfReady();
            } else if (TorService.STATUS_STARTING.equals(status)) {
                updateState(STATE_STARTING, 0);
            } else if (TorService.STATUS_OFF.equals(status) || TorService.STATUS_STOPPING.equals(status)) {
                updateState(STATE_STOPPED, 0);
            }
        }
    };
}
