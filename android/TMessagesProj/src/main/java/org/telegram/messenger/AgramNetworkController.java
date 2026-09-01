/*
 * This file is part of Agram and is licensed under GNU GPL v2 or later.
 */
package org.telegram.messenger;

import android.text.TextUtils;
import android.util.SparseArray;
import android.util.SparseBooleanArray;

import org.telegram.tgnet.ConnectionsManager;

/** Applies per-container proxy policy and keeps routed profiles fail-closed. */
public final class AgramNetworkController {
    private static final AgramNetworkController INSTANCE = new AgramNetworkController();
    private final SparseArray<String> state = new SparseArray<>();
    private final SparseBooleanArray managedAccounts = new SparseBooleanArray();

    public static AgramNetworkController getInstance() {
        return INSTANCE;
    }

    private AgramNetworkController() {
    }

    public void apply(int account) {
        apply(account, true);
    }

    public void prepare(int account) {
        apply(account, false);
    }

    private void apply(int account, boolean resume) {
        managedAccounts.put(account, true);
        AgramContainerManager.ContainerRecord record = AgramContainerManager.getInstance().ensureContainer(account);
        AgramContainerManager.ProxyProfile proxy = AgramContainerManager.getInstance().getProxyProfile(account);
        if (AgramContainerManager.NETWORK_TOR.equals(proxy.mode)) {
            int port = AgramTorManager.getInstance().getSocksPort();
            if (port > 0) {
                applyTor(account, record, port, resume);
            } else {
                // Tor can take time to bootstrap. Pause first so neither MTProto
                // nor push can escape over the direct connection in that window.
                pause(account, "tor_starting");
                ConnectionsManager.native_setProxySettings(account, "", 1080, "", "", "");
                AgramTorManager.getInstance().ensureStarted();
            }
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
            AgramTorManager.getInstance().stopIfUnused();
            return;
        }
        ConnectionsManager.native_setProxySettings(account, "", 1080, "", "", "");
        state.put(account, "direct");
        if (resume) {
            ConnectionsManager.native_resumeNetwork(account, false);
        }
        AgramTorManager.getInstance().stopIfUnused();
    }

    public void onTorStateChanged(String torState, int port) {
        for (int account = 0; account < UserConfig.MAX_ACCOUNT_COUNT; account++) {
            if (!managedAccounts.get(account)) {
                continue;
            }
            AgramContainerManager.ContainerRecord record = AgramContainerManager.getInstance().getContainer(account);
            if (record == null || !AgramContainerManager.NETWORK_TOR.equals(record.proxyMode)) {
                continue;
            }
            if (AgramTorManager.STATE_READY.equals(torState) && port > 0) {
                applyTor(account, record, port, true);
            } else {
                pause(account, AgramTorManager.STATE_ERROR.equals(torState) ? "tor_error" : "tor_unavailable");
                ConnectionsManager.native_setProxySettings(account, "", 1080, "", "", "");
            }
            AgramPushController.getInstance().onNetworkRouteChanged(account);
        }
    }

    public void onProxyError() {
        for (int account = 0; account < UserConfig.MAX_ACCOUNT_COUNT; account++) {
            if (!managedAccounts.get(account)) {
                continue;
            }
            AgramContainerManager.ContainerRecord record = AgramContainerManager.getInstance().getContainer(account);
            if (record != null && record.killSwitch
                    && !AgramContainerManager.NETWORK_DIRECT.equals(record.proxyMode)) {
                pause(account, AgramContainerManager.NETWORK_TOR.equals(record.proxyMode)
                        ? "tor_error" : "proxy_error");
            }
        }
    }

    public String getState(int account) {
        String value = state.get(account);
        return value == null ? "not_applied" : value;
    }

    private void applyTor(int account, AgramContainerManager.ContainerRecord record, int port, boolean resume) {
        String isolationId = TextUtils.isEmpty(record.torIsolationId)
                ? "agram-account-" + account : record.torIsolationId;
        ConnectionsManager.native_setProxySettings(
                account, "127.0.0.1", port, "<torS0X>0", isolationId, "");
        state.put(account, "tor_active");
        if (resume) {
            ConnectionsManager.native_resumeNetwork(account, false);
        }
    }

    private void pause(int account, String reason) {
        state.put(account, reason);
        ConnectionsManager.native_pauseNetwork(account);
    }
}
