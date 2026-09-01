/*
 * This file is part of Agram and is licensed under GNU GPL v2 or later.
 */
package org.telegram.messenger;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.os.Build;
import android.text.TextUtils;
import android.util.SparseArray;

import org.telegram.tgnet.ConnectionsManager;

/** Applies per-container proxy policy and keeps Tor profiles fail-closed. */
public final class AgramNetworkController {
    private static final String ORBOT_PACKAGE = "org.torproject.android";
    private static final String ACTION_START = "org.torproject.android.intent.action.START";
    private static final String ACTION_STATUS = "org.torproject.android.intent.action.STATUS";
    private static final String EXTRA_PACKAGE_NAME = "org.torproject.android.intent.extra.PACKAGE_NAME";
    private static final String EXTRA_STATUS = "org.torproject.android.intent.extra.STATUS";
    private static final String EXTRA_SOCKS_PORT = "org.torproject.android.intent.extra.SOCKS_PROXY_PORT";
    private static final String STATUS_ON = "ON";

    private static final AgramNetworkController INSTANCE = new AgramNetworkController();
    private final SparseArray<String> state = new SparseArray<>();
    private boolean receiverRegistered;

    public static AgramNetworkController getInstance() {
        return INSTANCE;
    }

    private AgramNetworkController() {
    }

    public synchronized void initialize(Context context) {
        if (receiverRegistered) {
            return;
        }
        IntentFilter filter = new IntentFilter(ACTION_STATUS);
        if (Build.VERSION.SDK_INT >= 33) {
            context.registerReceiver(orbotStatusReceiver, filter, Context.RECEIVER_EXPORTED);
        } else {
            context.registerReceiver(orbotStatusReceiver, filter);
        }
        receiverRegistered = true;
    }

    public void apply(int account) {
        apply(account, true);
    }

    public void prepare(int account) {
        apply(account, false);
    }

    private void apply(int account, boolean resume) {
        initialize(ApplicationLoader.applicationContext);
        AgramContainerManager.ProxyProfile proxy = AgramContainerManager.getInstance().getProxyProfile(account);
        if (AgramContainerManager.NETWORK_TOR.equals(proxy.mode)) {
            pause(account, isOrbotInstalled() ? "tor_starting" : "orbot_required");
            startOrbot();
            return;
        }
        if (AgramContainerManager.NETWORK_PROXY.equals(proxy.mode)) {
            if (TextUtils.isEmpty(proxy.address)) {
                ConnectionsManager.native_setProxySettings(account, "", 1080, "", "", "");
                if (proxy.killSwitch) {
                    pause(account, "proxy_required");
                }
                return;
            }
            ConnectionsManager.native_setProxySettings(
                    account, proxy.address, proxy.port, proxy.username, proxy.password, proxy.secret);
            state.put(account, "proxy_active");
            if (resume) {
                ConnectionsManager.native_resumeNetwork(account, false);
            }
            return;
        }
        ConnectionsManager.native_setProxySettings(account, "", 1080, "", "", "");
        state.put(account, "direct");
        if (resume) {
            ConnectionsManager.native_resumeNetwork(account, false);
        }
    }

    public void onProxyError() {
        for (int account = 0; account < UserConfig.MAX_ACCOUNT_COUNT; account++) {
            AgramContainerManager.ContainerRecord record = AgramContainerManager.getInstance().getContainer(account);
            if (record != null && record.killSwitch
                    && !AgramContainerManager.NETWORK_DIRECT.equals(record.proxyMode)) {
                pause(account, "proxy_error");
            }
        }
    }

    public boolean isOrbotInstalled() {
        try {
            ApplicationLoader.applicationContext.getPackageManager().getPackageInfo(ORBOT_PACKAGE, 0);
            return true;
        } catch (PackageManager.NameNotFoundException error) {
            return false;
        }
    }

    public void openOrbot(Context context) {
        Intent intent = context.getPackageManager().getLaunchIntentForPackage(ORBOT_PACKAGE);
        if (intent != null) {
            context.startActivity(intent);
        }
    }

    public String getState(int account) {
        String value = state.get(account);
        return value == null ? "not_applied" : value;
    }

    private void pause(int account, String reason) {
        state.put(account, reason);
        ConnectionsManager.native_pauseNetwork(account);
    }

    private void startOrbot() {
        Intent intent = new Intent(ACTION_START);
        intent.setPackage(ORBOT_PACKAGE);
        intent.putExtra(EXTRA_PACKAGE_NAME, ApplicationLoader.applicationContext.getPackageName());
        ApplicationLoader.applicationContext.sendBroadcast(intent);
    }

    private final BroadcastReceiver orbotStatusReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (!ACTION_STATUS.equals(intent.getAction())) {
                return;
            }
            String status = intent.getStringExtra(EXTRA_STATUS);
            int port = intent.getIntExtra(EXTRA_SOCKS_PORT, 9050);
            if (port <= 0 || port > 65535) {
                port = 9050;
            }
            for (int account = 0; account < UserConfig.MAX_ACCOUNT_COUNT; account++) {
                AgramContainerManager.ContainerRecord record = AgramContainerManager.getInstance().getContainer(account);
                if (record == null || !AgramContainerManager.NETWORK_TOR.equals(record.proxyMode)) {
                    continue;
                }
                if (STATUS_ON.equals(status)) {
                    ConnectionsManager.native_setProxySettings(account, "127.0.0.1", port, "", "", "");
                    state.put(account, "tor_active");
                    ConnectionsManager.native_resumeNetwork(account, false);
                } else if (record.killSwitch) {
                    pause(account, "tor_unavailable");
                }
            }
        }
    };
}
