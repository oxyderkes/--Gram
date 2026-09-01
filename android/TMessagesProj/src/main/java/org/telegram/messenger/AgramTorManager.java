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
import android.content.SharedPreferences;
import android.os.Build;
import android.os.IBinder;
import android.text.TextUtils;
import android.util.Base64;

import org.json.JSONObject;
import org.torproject.jni.TorService;

import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.CopyOnWriteArrayList;

import IPtProxy.Controller;
import IPtProxy.IPtProxy;
import IPtProxy.OnTransportEvents;

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

    private static final String SETTINGS = "agram_tor_settings";
    private static final String BRIDGE_SCOPE = "agram_tor_bridges_v1";
    private static final String BRIDGE_DATA = "bridge_data";

    public interface Listener {
        void onTorStateChanged(String state);
    }

    public static final class BridgeConfig {
        public final boolean enabled;
        public final String lines;

        private BridgeConfig(boolean enabled, String lines) {
            this.enabled = enabled;
            this.lines = lines == null ? "" : lines;
        }
    }

    private static final AgramTorManager INSTANCE = new AgramTorManager();
    private final CopyOnWriteArrayList<Listener> listeners = new CopyOnWriteArrayList<>();
    private final ArrayList<String> activeTransports = new ArrayList<>();
    private Context applicationContext;
    private TorService torService;
    private Controller transportController;
    private boolean receiverRegistered;
    private boolean binding;
    private boolean bound;
    private boolean startScheduled;
    private volatile String state = STATE_STOPPED;
    private volatile String lastError = "";
    private volatile int socksPort;

    public static AgramTorManager getInstance() {
        return INSTANCE;
    }

    private AgramTorManager() {
    }

    public String getState() {
        return state;
    }

    public String getLastError() {
        return lastError;
    }

    public int getSocksPort() {
        return STATE_READY.equals(state) ? socksPort : 0;
    }

    public String getBootstrapStatus() {
        TorService service = torService;
        if (service == null) {
            return "";
        }
        try {
            String value = service.getInfo("status/bootstrap-phase");
            return value == null ? "" : value;
        } catch (Throwable error) {
            return "";
        }
    }

    public void addListener(Listener listener) {
        if (listener != null) {
            listeners.addIfAbsent(listener);
        }
    }

    public void removeListener(Listener listener) {
        listeners.remove(listener);
    }

    /** Starts Tor and configured pluggable transports off the UI thread. */
    public synchronized void ensureStarted() {
        if (STATE_READY.equals(state) || binding || bound || startScheduled) {
            return;
        }
        applicationContext = ApplicationLoader.applicationContext.getApplicationContext();
        registerReceiver();
        lastError = "";
        startScheduled = true;
        updateState(STATE_STARTING, 0);
        Utilities.globalQueue.postRunnable(() -> {
            try {
                configureTor();
                AndroidUtilities.runOnUIThread(this::bindTorService);
            } catch (Throwable error) {
                FileLog.e("Unable to configure embedded Tor", error);
                synchronized (AgramTorManager.this) {
                    startScheduled = false;
                    lastError = safeError(error);
                    stopTransports();
                    updateState(STATE_ERROR, 0);
                }
            }
        });
    }

    /** Restarts the one shared daemon; all Tor containers pause fail-closed. */
    public synchronized void restart() {
        disconnect();
        updateState(STATE_STOPPED, 0);
        AndroidUtilities.runOnUIThread(this::ensureStarted, 700);
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

    public BridgeConfig getBridgeConfig() {
        try {
            SharedPreferences preferences = ApplicationLoader.applicationContext
                    .getSharedPreferences(SETTINGS, Context.MODE_PRIVATE);
            String encoded = preferences.getString(BRIDGE_DATA, "");
            if (TextUtils.isEmpty(encoded)) {
                return new BridgeConfig(false, "");
            }
            byte[] encrypted = Base64.decode(encoded, Base64.NO_WRAP);
            byte[] clear = AgramSecureStore.decrypt(BRIDGE_SCOPE, encrypted,
                    AgramSecureStore.aad(BRIDGE_SCOPE, "bridges"));
            JSONObject json = new JSONObject(new String(clear, StandardCharsets.UTF_8));
            return new BridgeConfig(json.optBoolean("enabled", false), json.optString("lines", ""));
        } catch (Throwable error) {
            FileLog.e("Unable to read encrypted Tor bridges", error);
            return new BridgeConfig(false, "");
        }
    }

    public void saveBridgeConfig(boolean enabled, String bridgeLines) {
        try {
            String normalized = normalizeBridgeLines(bridgeLines);
            if (enabled && TextUtils.isEmpty(normalized)) {
                throw new IllegalArgumentException("Добавьте хотя бы одну строку Bridge");
            }
            JSONObject json = new JSONObject();
            json.put("enabled", enabled);
            json.put("lines", normalized);
            byte[] encrypted = AgramSecureStore.encrypt(BRIDGE_SCOPE,
                    json.toString().getBytes(StandardCharsets.UTF_8),
                    AgramSecureStore.aad(BRIDGE_SCOPE, "bridges"));
            ApplicationLoader.applicationContext.getSharedPreferences(SETTINGS, Context.MODE_PRIVATE)
                    .edit().putString(BRIDGE_DATA, Base64.encodeToString(encrypted, Base64.NO_WRAP)).commit();
        } catch (IllegalArgumentException error) {
            throw error;
        } catch (Throwable error) {
            throw new IllegalStateException("Не удалось сохранить мосты", error);
        }
    }

    public static String normalizeBridgeLines(String value) {
        if (TextUtils.isEmpty(value)) {
            return "";
        }
        StringBuilder result = new StringBuilder();
        for (String raw : value.split("\\r?\\n")) {
            String line = raw.trim();
            if (line.isEmpty() || line.startsWith("#")) {
                continue;
            }
            if (line.regionMatches(true, 0, "Bridge ", 0, 7)) {
                line = line.substring(7).trim();
            }
            if (line.indexOf('\u0000') >= 0 || line.length() > 1024) {
                throw new IllegalArgumentException("Некорректная строка моста");
            }
            String lower = line.toLowerCase(Locale.US);
            boolean pluggable = lower.startsWith("obfs4 ") || lower.startsWith("webtunnel ");
            boolean vanilla = line.matches("(\\[[0-9a-fA-F:]+]|[^\\s:]+):[0-9]{1,5}(\\s+[A-Fa-f0-9]{40})?.*");
            if (!pluggable && !vanilla) {
                throw new IllegalArgumentException("Поддерживаются обычные, obfs4 и webtunnel мосты");
            }
            if (result.length() > 0) {
                result.append('\n');
            }
            result.append(line);
        }
        return result.toString();
    }

    private synchronized void bindTorService() {
        startScheduled = false;
        if (!STATE_STARTING.equals(state) || applicationContext == null || binding || bound) {
            return;
        }
        try {
            binding = true;
            Intent intent = new Intent(applicationContext, TorService.class);
            intent.setAction(TorService.ACTION_START);
            if (!applicationContext.bindService(intent, serviceConnection, Context.BIND_AUTO_CREATE)) {
                binding = false;
                lastError = "service_bind_failed";
                updateState(STATE_ERROR, 0);
            }
        } catch (Throwable error) {
            binding = false;
            lastError = safeError(error);
            FileLog.e("Unable to bind embedded Tor", error);
            updateState(STATE_ERROR, 0);
        }
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
        BridgeConfig bridges = getBridgeConfig();
        List<String> bridgeLines = bridges.enabled
                ? splitLines(normalizeBridgeLines(bridges.lines)) : Collections.emptyList();
        StringBuilder torrc = new StringBuilder()
                .append("ClientOnly 1\n")
                .append("AvoidDiskWrites 1\n")
                .append("SafeLogging 1\n")
                .append("SocksPort 0\n")
                .append("SocksPort 127.0.0.1:auto IsolateSOCKSAuth\n");

        stopTransports();
        if (!bridgeLines.isEmpty()) {
            boolean needsObfs4 = containsTransport(bridgeLines, "obfs4");
            boolean needsWebTunnel = containsTransport(bridgeLines, "webtunnel");
            if (needsObfs4 || needsWebTunnel) {
                ensureTransportController();
            }
            if (needsObfs4) {
                startTransport(IPtProxy.Obfs4, "obfs4", torrc);
            }
            if (needsWebTunnel) {
                startTransport(IPtProxy.Webtunnel, "webtunnel", torrc);
            }
            torrc.append("UseBridges 1\n");
            for (String line : bridgeLines) {
                torrc.append("Bridge ").append(line).append('\n');
            }
        }
        try (FileOutputStream output = new FileOutputStream(TorService.getTorrc(applicationContext), false)) {
            output.write(torrc.toString().getBytes(StandardCharsets.UTF_8));
            output.flush();
        }
    }

    private void ensureTransportController() {
        if (transportController != null) {
            return;
        }
        File directory = new File(applicationContext.getNoBackupFilesDir(), "agram_tor/pt_state");
        if (!directory.exists() && !directory.mkdirs()) {
            throw new IllegalStateException("Unable to create private transport state directory");
        }
        transportController = new Controller(directory.getAbsolutePath(), false, false, "NOTICE",
                new OnTransportEvents() {
                    @Override
                    public void connected(String transport) {
                        FileLog.d("Agram Tor transport connected: " + transport);
                    }

                    @Override
                    public void error(String transport, Exception error) {
                        FileLog.e("Agram Tor transport error: " + transport, error);
                        onTransportFailure(transport, error);
                    }

                    @Override
                    public void stopped(String transport, Exception error) {
                        if (error != null) {
                            FileLog.e("Agram Tor transport stopped: " + transport, error);
                            onTransportFailure(transport, error);
                        }
                    }
                });
    }

    private void onTransportFailure(String transport, Throwable error) {
        synchronized (this) {
            lastError = transport + ": " + safeError(error);
            updateState(STATE_ERROR, 0);
        }
    }

    private void startTransport(String transport, String torName, StringBuilder torrc) throws Exception {
        transportController.start(transport, "");
        long port = transportController.port(transport);
        String address = transportController.localAddress(transport);
        if (port <= 0 || port > 65535) {
            throw new IllegalStateException("Transport " + torName + " did not allocate a port");
        }
        if (TextUtils.isEmpty(address)) {
            address = "127.0.0.1";
        }
        activeTransports.add(transport);
        torrc.append("ClientTransportPlugin ").append(torName)
                .append(" socks5 ").append(address).append(':').append(port).append('\n');
    }

    private synchronized void activateIfReady() {
        if (torService == null) {
            return;
        }
        int port = torService.getSocksPort();
        if (port > 0 && port <= 65535) {
            lastError = "";
            updateState(STATE_READY, port);
        }
    }

    private synchronized void disconnect() {
        startScheduled = false;
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
        stopTransports();
    }

    private synchronized void stopTransports() {
        if (transportController != null) {
            for (String transport : new ArrayList<>(activeTransports)) {
                try {
                    transportController.stop(transport);
                } catch (Throwable error) {
                    FileLog.e("Unable to stop Tor transport " + transport, error);
                }
            }
        }
        activeTransports.clear();
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
                lastError = "service_disconnected";
                updateState(STATE_ERROR, 0);
            }
        }
    };

    private final BroadcastReceiver statusReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (TorService.ACTION_ERROR.equals(intent.getAction())) {
                String message = intent.getStringExtra(Intent.EXTRA_TEXT);
                lastError = TextUtils.isEmpty(message) ? "tor_error" : message;
                FileLog.e("Embedded Tor error: " + lastError);
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

    private static boolean containsTransport(List<String> lines, String transport) {
        String prefix = transport.toLowerCase(Locale.US) + " ";
        for (String line : lines) {
            if (line.toLowerCase(Locale.US).startsWith(prefix)) {
                return true;
            }
        }
        return false;
    }

    private static List<String> splitLines(String value) {
        ArrayList<String> result = new ArrayList<>();
        if (!TextUtils.isEmpty(value)) {
            Collections.addAll(result, value.split("\\n"));
        }
        return result;
    }

    private static String safeError(Throwable error) {
        if (error == null) {
            return "unknown";
        }
        String message = error.getMessage();
        return TextUtils.isEmpty(message) ? error.getClass().getSimpleName() : message;
    }
}
