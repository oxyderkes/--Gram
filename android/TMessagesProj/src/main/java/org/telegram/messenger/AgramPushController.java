/*
 * This file is part of Agram and is licensed under GNU GPL v2 or later.
 */
package org.telegram.messenger;

import android.content.Context;
import android.content.Intent;
import android.text.TextUtils;
import android.util.Base64;
import android.util.SparseArray;

import androidx.core.content.ContextCompat;

import org.json.JSONObject;
import org.telegram.tgnet.ConnectionsManager;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.net.InetSocketAddress;
import java.net.Proxy;
import java.net.URL;
import java.net.URLConnection;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;
import java.util.HashSet;
import java.util.Set;

import javax.net.ssl.HttpsURLConnection;

/**
 * Owns the built-in Simple Push transport. The Android service is shared only
 * for lifecycle management; each subscription is permanently bound to one
 * immutable container id, account slot and random endpoint.
 */
public final class AgramPushController {

    private static final AgramPushController INSTANCE = new AgramPushController();
    private static final String TOPIC_PREFIX = "agram_";
    private static final long INITIAL_RECONNECT_DELAY_MS = 2_000L;
    private static final long MAX_RECONNECT_DELAY_MS = 60_000L;

    private final Object sync = new Object();
    private final SparseArray<Subscription> subscriptions = new SparseArray<>();
    private final SecureRandom secureRandom = new SecureRandom();
    private boolean serviceRunning;

    public static AgramPushController getInstance() {
        return INSTANCE;
    }

    private AgramPushController() {
    }

    public void restoreActiveRegistrations() {
        if (hasActiveAgramPushAccounts()) {
            requestServiceStart();
        } else {
            requestServiceStop();
        }
    }

    public void onAccountAuthorized(int account) {
        AgramContainerManager.ContainerRecord record = AgramContainerManager.getInstance().ensureContainer(account);
        if (AgramContainerManager.PUSH_AGRAM.equals(record.pushMode)) {
            requestServiceStart();
            refreshSubscriptions();
        }
    }

    public void onPushSettingsChanged(int account) {
        AgramContainerManager.ContainerRecord record = AgramContainerManager.getInstance().ensureContainer(account);
        if (AgramContainerManager.PUSH_AGRAM.equals(record.pushMode)
                && UserConfig.getInstance(account).isClientActivated()) {
            requestServiceStart();
        }
        refreshSubscriptions();
    }

    public void unregisterAccount(int account, boolean notifyTelegram) {
        AgramContainerManager.ContainerRecord record = AgramContainerManager.getInstance().getContainer(account);
        if (record == null) {
            return;
        }
        synchronized (sync) {
            stopSubscriptionLocked(account);
        }
        if (notifyTelegram && !TextUtils.isEmpty(record.agramPushEndpoint)) {
            MessagesController.getInstance(account).unregisterAgramPush(record.agramPushEndpoint);
        }
        AgramContainerManager.getInstance().saveAgramPushEndpoint(account, "", "unregistered");
        AgramPushService.updateForegroundNotification(subscriptionCount());
        // Logout clears UserConfig asynchronously. Re-evaluate after it has
        // completed, without recreating an endpoint in the departing slot.
        AndroidUtilities.runOnUIThread(this::restoreActiveRegistrations, 1_500L);
    }

    void onServiceStarted() {
        synchronized (sync) {
            serviceRunning = true;
        }
        refreshSubscriptions();
    }

    void onServiceStopped() {
        synchronized (sync) {
            serviceRunning = false;
            for (int index = subscriptions.size() - 1; index >= 0; index--) {
                subscriptions.valueAt(index).stop();
            }
            subscriptions.clear();
        }
    }

    private void refreshSubscriptions() {
        synchronized (sync) {
            if (!serviceRunning) {
                return;
            }
            Set<Integer> requiredAccounts = new HashSet<>();
            for (int account = 0; account < UserConfig.MAX_ACCOUNT_COUNT; account++) {
                AgramContainerManager.ContainerRecord record = AgramContainerManager.getInstance().getContainer(account);
                if (record == null
                        || !UserConfig.getInstance(account).isClientActivated()
                        || !AgramContainerManager.PUSH_AGRAM.equals(record.pushMode)) {
                    continue;
                }
                requiredAccounts.add(account);
                String endpoint = ensureEmbeddedEndpoint(account, record);
                AgramContainerManager.ContainerRecord current = AgramContainerManager.getInstance().ensureContainer(account);
                Subscription existing = subscriptions.get(account);
                if (existing == null || !existing.matches(current.id, endpoint)) {
                    stopSubscriptionLocked(account);
                    Subscription subscription = new Subscription(account, current.id, endpoint);
                    subscriptions.put(account, subscription);
                    subscription.start();
                }
                registerEndpointWithTelegram(account, endpoint);
            }
            for (int index = subscriptions.size() - 1; index >= 0; index--) {
                int account = subscriptions.keyAt(index);
                if (!requiredAccounts.contains(account)) {
                    subscriptions.valueAt(index).stop();
                    subscriptions.removeAt(index);
                }
            }
            AgramPushService.updateForegroundNotification(subscriptions.size());
            if (subscriptions.size() == 0) {
                requestServiceStop();
            }
        }
    }

    private String ensureEmbeddedEndpoint(int account, AgramContainerManager.ContainerRecord record) {
        String baseUrl = pushBaseUrl();
        if (isEmbeddedEndpoint(record.agramPushEndpoint, baseUrl)) {
            return record.agramPushEndpoint;
        }
        String legacyEndpoint = record.agramPushEndpoint;
        byte[] random = new byte[32];
        secureRandom.nextBytes(random);
        String topic = TOPIC_PREFIX + Base64.encodeToString(
                random, Base64.NO_WRAP | Base64.NO_PADDING | Base64.URL_SAFE);
        String endpoint = baseUrl + "/" + topic;
        AgramContainerManager.getInstance().saveAgramPushEndpoint(account, endpoint, "starting");
        if (UserConfig.getInstance(account).isClientActivated() && !TextUtils.isEmpty(legacyEndpoint)) {
            MessagesController.getInstance(account).unregisterAgramPush(legacyEndpoint);
        }
        return endpoint;
    }

    private void registerEndpointWithTelegram(int account, String endpoint) {
        if (UserConfig.getInstance(account).isClientActivated() && !TextUtils.isEmpty(endpoint)) {
            MessagesController.getInstance(account).registerAgramPush(endpoint);
        }
    }

    private void onMessage(int account, String containerId, String endpoint) {
        AgramContainerManager.ContainerRecord current = AgramContainerManager.getInstance().getContainer(account);
        if (current == null
                || !containerId.equals(current.id)
                || !endpoint.equals(current.agramPushEndpoint)
                || !AgramContainerManager.PUSH_AGRAM.equals(current.pushMode)
                || !UserConfig.getInstance(account).isClientActivated()) {
            return;
        }
        ApplicationLoader.postInitApplication();
        ConnectionsManager.onInternalPushReceived(account);
        ConnectionsManager.getInstance(account).resumeNetworkMaybe();
    }

    private void stopSubscriptionLocked(int account) {
        Subscription subscription = subscriptions.get(account);
        if (subscription != null) {
            subscription.stop();
            subscriptions.remove(account);
        }
    }

    private int subscriptionCount() {
        synchronized (sync) {
            return subscriptions.size();
        }
    }

    private boolean hasActiveAgramPushAccounts() {
        for (int account = 0; account < UserConfig.MAX_ACCOUNT_COUNT; account++) {
            AgramContainerManager.ContainerRecord record = AgramContainerManager.getInstance().getContainer(account);
            if (record != null
                    && UserConfig.getInstance(account).isClientActivated()
                    && AgramContainerManager.PUSH_AGRAM.equals(record.pushMode)) {
                return true;
            }
        }
        return false;
    }

    private void requestServiceStart() {
        Context context = ApplicationLoader.applicationContext;
        try {
            ContextCompat.startForegroundService(context, new Intent(context, AgramPushService.class));
        } catch (Throwable error) {
            FileLog.e("Unable to start Agram Push service", error);
        }
    }

    private void requestServiceStop() {
        Context context = ApplicationLoader.applicationContext;
        try {
            context.stopService(new Intent(context, AgramPushService.class));
        } catch (Throwable error) {
            FileLog.e("Unable to stop Agram Push service", error);
        }
    }

    private static String pushBaseUrl() {
        String configured = BuildConfig.AGRAM_PUSH_BASE_URL == null
                ? "" : BuildConfig.AGRAM_PUSH_BASE_URL.trim();
        if (!configured.startsWith("https://")) {
            configured = "https://ntfy.sh";
        }
        while (configured.endsWith("/")) {
            configured = configured.substring(0, configured.length() - 1);
        }
        return configured;
    }

    private static boolean isEmbeddedEndpoint(String endpoint, String baseUrl) {
        return !TextUtils.isEmpty(endpoint) && endpoint.startsWith(baseUrl + "/" + TOPIC_PREFIX);
    }

    private final class Subscription implements Runnable {
        private final int account;
        private final String containerId;
        private final String endpoint;
        private final String topic;
        private volatile boolean stopped;
        private volatile HttpsURLConnection connection;
        private Thread thread;
        private String lastMessageId;

        Subscription(int account, String containerId, String endpoint) {
            this.account = account;
            this.containerId = containerId;
            this.endpoint = endpoint;
            this.topic = endpoint.substring(endpoint.lastIndexOf('/') + 1);
        }

        boolean matches(String expectedContainerId, String expectedEndpoint) {
            return containerId.equals(expectedContainerId) && endpoint.equals(expectedEndpoint);
        }

        void start() {
            thread = new Thread(this, "AgramPush-" + account);
            thread.setDaemon(true);
            thread.start();
        }

        void stop() {
            stopped = true;
            HttpsURLConnection activeConnection = connection;
            if (activeConnection != null) {
                activeConnection.disconnect();
            }
            Thread activeThread = thread;
            if (activeThread != null) {
                activeThread.interrupt();
            }
        }

        @Override
        public void run() {
            long reconnectDelay = INITIAL_RECONNECT_DELAY_MS;
            while (!stopped && isCurrentBinding()) {
                try {
                    readStream();
                    reconnectDelay = INITIAL_RECONNECT_DELAY_MS;
                } catch (Throwable error) {
                    if (!stopped) {
                        FileLog.e("Agram Push connection failed for account " + account, error);
                        AgramContainerManager.getInstance().saveAgramPushEndpoint(
                                account, endpoint, "reconnecting");
                    }
                } finally {
                    HttpsURLConnection activeConnection = connection;
                    connection = null;
                    if (activeConnection != null) {
                        activeConnection.disconnect();
                    }
                }
                if (!stopped) {
                    try {
                        Thread.sleep(reconnectDelay);
                    } catch (InterruptedException ignore) {
                        Thread.currentThread().interrupt();
                    }
                    reconnectDelay = Math.min(MAX_RECONNECT_DELAY_MS, reconnectDelay * 2L);
                }
            }
        }

        private void readStream() throws Exception {
            AgramContainerManager.ContainerRecord record = AgramContainerManager.getInstance().getContainer(account);
            if (record == null) {
                return;
            }
            String cursor = TextUtils.isEmpty(lastMessageId) ? "10m" : lastMessageId;
            URL streamUrl = new URL(endpoint + "/json?since=" + cursor);
            URLConnection raw = openConnection(streamUrl, record);
            if (!(raw instanceof HttpsURLConnection)) {
                throw new IOException("Agram Push requires HTTPS");
            }
            HttpsURLConnection https = (HttpsURLConnection) raw;
            connection = https;
            https.setConnectTimeout(20_000);
            https.setReadTimeout(0);
            https.setUseCaches(false);
            https.setRequestProperty("Accept", "application/x-ndjson");
            https.setRequestProperty("User-Agent", "AgramPush/" + BuildVars.BUILD_VERSION_STRING);
            int status = https.getResponseCode();
            if (status < 200 || status >= 300) {
                throw new IOException("Push server returned HTTP " + status);
            }
            AgramContainerManager.getInstance().saveAgramPushEndpoint(account, endpoint, "connected");
            try (BufferedReader reader = new BufferedReader(new InputStreamReader(
                    https.getInputStream(), StandardCharsets.UTF_8))) {
                String line;
                while (!stopped && (line = reader.readLine()) != null) {
                    handleLine(line);
                }
            }
        }

        private URLConnection openConnection(URL url, AgramContainerManager.ContainerRecord record) throws IOException {
            if (AgramContainerManager.NETWORK_DIRECT.equals(record.proxyMode)) {
                return url.openConnection();
            }
            if (AgramContainerManager.NETWORK_TOR.equals(record.proxyMode)) {
                Proxy proxy = new Proxy(Proxy.Type.SOCKS,
                        InetSocketAddress.createUnresolved("127.0.0.1", 9050));
                return url.openConnection(proxy);
            }
            if (AgramContainerManager.NETWORK_PROXY.equals(record.proxyMode)
                    && TextUtils.isEmpty(record.proxySecret)
                    && TextUtils.isEmpty(record.proxyUsername)
                    && TextUtils.isEmpty(record.proxyPassword)
                    && !TextUtils.isEmpty(record.proxyAddress)
                    && record.proxyPort > 0) {
                Proxy proxy = new Proxy(Proxy.Type.SOCKS,
                        InetSocketAddress.createUnresolved(record.proxyAddress, record.proxyPort));
                return url.openConnection(proxy);
            }
            throw new IOException("Selected proxy cannot carry the HTTPS push stream without bypassing container routing");
        }

        private void handleLine(String line) {
            if (TextUtils.isEmpty(line)) {
                return;
            }
            try {
                JSONObject event = new JSONObject(line);
                if (!"message".equals(event.optString("event"))
                        || !topic.equals(event.optString("topic"))) {
                    return;
                }
                String messageId = event.optString("id");
                if (!TextUtils.isEmpty(messageId) && messageId.equals(lastMessageId)) {
                    return;
                }
                lastMessageId = messageId;
                onMessage(account, containerId, endpoint);
            } catch (Throwable error) {
                FileLog.e("Unable to parse Agram Push event for account " + account, error);
            }
        }

        private boolean isCurrentBinding() {
            AgramContainerManager.ContainerRecord current = AgramContainerManager.getInstance().getContainer(account);
            return current != null
                    && containerId.equals(current.id)
                    && endpoint.equals(current.agramPushEndpoint)
                    && AgramContainerManager.PUSH_AGRAM.equals(current.pushMode)
                    && UserConfig.getInstance(account).isClientActivated();
        }
    }
}
