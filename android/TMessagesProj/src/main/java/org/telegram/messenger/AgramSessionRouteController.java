/*
 * This file is part of Agram and is licensed under GNU GPL v2 or later.
 */
package org.telegram.messenger;

import android.text.TextUtils;
import android.util.SparseArray;

import org.telegram.tgnet.ConnectionsManager;
import org.telegram.tgnet.TLRPC;
import org.telegram.tgnet.tl.TL_account;

import java.util.concurrent.CopyOnWriteArrayList;

/**
 * Reads the address Telegram associates with the current authorization.
 *
 * The result comes from account.getAuthorizations over the selected
 * container's own MTProto route. It is deliberately kept in memory only.
 */
public final class AgramSessionRouteController {
    public interface Listener {
        void onSessionRouteChanged(int account, RouteInfo info);
    }

    public static final class RouteInfo {
        public final String ip;
        public final String country;
        public final String region;
        public final boolean loading;
        public final String error;
        public final long updatedAt;

        private RouteInfo(String ip, String country, String region, boolean loading, String error, long updatedAt) {
            this.ip = safe(ip);
            this.country = safe(country);
            this.region = safe(region);
            this.loading = loading;
            this.error = safe(error);
            this.updatedAt = updatedAt;
        }

        public String approximateLocation() {
            if (!TextUtils.isEmpty(region) && !TextUtils.isEmpty(country)) {
                return region + ", " + country;
            }
            return !TextUtils.isEmpty(region) ? region : country;
        }
    }

    private static final long CACHE_MS = 60_000L;
    private static final AgramSessionRouteController INSTANCE = new AgramSessionRouteController();
    private final SparseArray<RouteInfo> cache = new SparseArray<>();
    private final CopyOnWriteArrayList<Listener> listeners = new CopyOnWriteArrayList<>();

    public static AgramSessionRouteController getInstance() {
        return INSTANCE;
    }

    private AgramSessionRouteController() {
    }

    public void addListener(Listener listener) {
        if (listener != null) {
            listeners.addIfAbsent(listener);
        }
    }

    public void removeListener(Listener listener) {
        listeners.remove(listener);
    }

    public RouteInfo get(int account) {
        RouteInfo info = cache.get(account);
        return info == null ? new RouteInfo("", "", "", false, "", 0) : info;
    }

    public void clear(int account) {
        cache.remove(account);
        dispatch(account, get(account));
    }

    public void refresh(int account, boolean force) {
        if (account < 0 || account >= UserConfig.MAX_ACCOUNT_COUNT
                || !UserConfig.getInstance(account).isClientActivated()) {
            clear(account);
            return;
        }
        RouteInfo current = cache.get(account);
        long now = System.currentTimeMillis();
        if (current != null && current.loading) {
            return;
        }
        if (!force && current != null && now - current.updatedAt < CACHE_MS) {
            dispatch(account, current);
            return;
        }
        RouteInfo loading = new RouteInfo(
                current == null ? "" : current.ip,
                current == null ? "" : current.country,
                current == null ? "" : current.region,
                true, "", current == null ? 0 : current.updatedAt);
        cache.put(account, loading);
        dispatch(account, loading);

        TL_account.getAuthorizations request = new TL_account.getAuthorizations();
        ConnectionsManager.getInstance(account).sendRequest(request, (response, error) ->
                AndroidUtilities.runOnUIThread(() -> {
                    if (error != null || !(response instanceof TL_account.authorizations)) {
                        RouteInfo failed = new RouteInfo(
                                loading.ip, loading.country, loading.region,
                                false, error == null ? "unavailable" : safe(error.text),
                                loading.updatedAt);
                        cache.put(account, failed);
                        dispatch(account, failed);
                        return;
                    }
                    TLRPC.TL_authorization currentAuthorization = null;
                    TL_account.authorizations result = (TL_account.authorizations) response;
                    for (TLRPC.TL_authorization authorization : result.authorizations) {
                        if ((authorization.flags & 1) != 0) {
                            currentAuthorization = authorization;
                            break;
                        }
                    }
                    RouteInfo resolved = currentAuthorization == null
                            ? new RouteInfo("", "", "", false, "current_session_missing", now)
                            : new RouteInfo(currentAuthorization.ip, currentAuthorization.country,
                                    currentAuthorization.region, false, "", now);
                    cache.put(account, resolved);
                    dispatch(account, resolved);
                }));
    }

    private void dispatch(int account, RouteInfo info) {
        AndroidUtilities.runOnUIThread(() -> {
            for (Listener listener : listeners) {
                listener.onSessionRouteChanged(account, info);
            }
        });
    }

    private static String safe(String value) {
        return value == null ? "" : value.trim();
    }
}
