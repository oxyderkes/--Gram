/*
 * This file is part of Agram and is licensed under GNU GPL v2 or later.
 */
package org.telegram.messenger;

import android.content.Context;
import android.text.TextUtils;

import org.telegram.tgnet.ConnectionsManager;
import org.unifiedpush.android.connector.UnifiedPush;

import java.util.ArrayList;
import java.util.List;

/** Bridges one encrypted Agram container to one UnifiedPush instance. */
public final class AgramUnifiedPushController {

    private static final AgramUnifiedPushController INSTANCE = new AgramUnifiedPushController();

    public static AgramUnifiedPushController getInstance() {
        return INSTANCE;
    }

    private AgramUnifiedPushController() {
    }

    public List<String> getAvailableDistributors(Context context) {
        try {
            return new ArrayList<>(UnifiedPush.getDistributors(context));
        } catch (Throwable error) {
            FileLog.e("Unable to enumerate UnifiedPush distributors", error);
            return new ArrayList<>();
        }
    }

    public String getCurrentDistributor(Context context) {
        try {
            String value = UnifiedPush.getSavedDistributor(context);
            return value == null ? "" : value;
        } catch (Throwable error) {
            FileLog.e("Unable to read UnifiedPush distributor", error);
            return "";
        }
    }

    public boolean registerAccount(int account, String requestedDistributor) {
        AgramContainerManager.ContainerRecord record = AgramContainerManager.getInstance().ensureContainer(account);
        if (!AgramContainerManager.PUSH_UNIFIED.equals(record.pushMode)) {
            return true;
        }
        Context context = ApplicationLoader.applicationContext;
        try {
            List<String> available = UnifiedPush.getDistributors(context);
            String distributor = UnifiedPush.getSavedDistributor(context);
            if (TextUtils.isEmpty(distributor) || !available.contains(distributor)) {
                distributor = available.contains(requestedDistributor)
                        ? requestedDistributor
                        : (available.size() == 1 ? available.get(0) : "");
                if (TextUtils.isEmpty(distributor)) {
                    AgramContainerManager.getInstance().saveUnifiedPushEndpoint(
                            account, "", "distributor_required");
                    return false;
                }
                UnifiedPush.saveDistributor(context, distributor);
            }
            AgramContainerManager.getInstance().savePushSettings(
                    account, AgramContainerManager.PUSH_UNIFIED, distributor);
            AgramContainerManager.getInstance().saveUnifiedPushEndpoint(
                    account, record.unifiedPushEndpoint, "registering");
            UnifiedPush.register(
                    context,
                    record.unifiedPushInstance,
                    "Agram · аккаунт " + (account + 1),
                    null
            );
            return true;
        } catch (Throwable error) {
            FileLog.e("Unable to register UnifiedPush for account " + account, error);
            AgramContainerManager.getInstance().saveUnifiedPushEndpoint(
                    account, record.unifiedPushEndpoint, "registration_error");
            return false;
        }
    }

    public void restoreActiveRegistrations() {
        for (int account = 0; account < UserConfig.MAX_ACCOUNT_COUNT; account++) {
            AgramContainerManager.ContainerRecord record = AgramContainerManager.getInstance().getContainer(account);
            if (record != null
                    && UserConfig.getInstance(account).isClientActivated()
                    && AgramContainerManager.PUSH_UNIFIED.equals(record.pushMode)) {
                registerAccount(account, record.unifiedPushDistributor);
                registerEndpointWithTelegram(account);
            }
        }
    }

    public void onAccountAuthorized(int account) {
        AgramContainerManager.ContainerRecord record = AgramContainerManager.getInstance().ensureContainer(account);
        if (AgramContainerManager.PUSH_UNIFIED.equals(record.pushMode)) {
            registerAccount(account, record.unifiedPushDistributor);
            registerEndpointWithTelegram(account);
        }
    }

    public void onNewEndpoint(String instance, String endpoint) {
        int account = AgramContainerManager.getInstance().findAccountByUnifiedPushInstance(instance);
        if (account < 0) {
            return;
        }
        if (TextUtils.isEmpty(endpoint) || !endpoint.startsWith("https://")) {
            AgramContainerManager.getInstance().saveUnifiedPushEndpoint(
                    account, "", "invalid_endpoint");
            return;
        }
        AgramContainerManager.getInstance().saveUnifiedPushEndpoint(account, endpoint, "endpoint_ready");
        registerEndpointWithTelegram(account);
    }

    public void onMessage(String instance) {
        int account = AgramContainerManager.getInstance().findAccountByUnifiedPushInstance(instance);
        if (account < 0 || !UserConfig.getInstance(account).isClientActivated()) {
            return;
        }
        ApplicationLoader.postInitApplication();
        ConnectionsManager.onInternalPushReceived(account);
        ConnectionsManager.getInstance(account).resumeNetworkMaybe();
    }

    public void onRegistrationFailed(String instance, String reason) {
        int account = AgramContainerManager.getInstance().findAccountByUnifiedPushInstance(instance);
        if (account >= 0) {
            AgramContainerManager.getInstance().saveUnifiedPushEndpoint(
                    account, "", "failed:" + (reason == null ? "unknown" : reason));
        }
    }

    public void onUnregistered(String instance) {
        int account = AgramContainerManager.getInstance().findAccountByUnifiedPushInstance(instance);
        if (account >= 0) {
            AgramContainerManager.getInstance().saveUnifiedPushEndpoint(account, "", "unregistered");
        }
    }

    public void unregisterAccount(int account, boolean notifyTelegram) {
        AgramContainerManager.ContainerRecord record = AgramContainerManager.getInstance().getContainer(account);
        if (record == null) {
            return;
        }
        if (notifyTelegram && !TextUtils.isEmpty(record.unifiedPushEndpoint)) {
            MessagesController.getInstance(account).unregisterAgramUnifiedPush(record.unifiedPushEndpoint);
        }
        try {
            UnifiedPush.unregister(ApplicationLoader.applicationContext, record.unifiedPushInstance);
        } catch (Throwable error) {
            FileLog.e("Unable to unregister UnifiedPush for account " + account, error);
        }
        AgramContainerManager.getInstance().saveUnifiedPushEndpoint(account, "", "unregistered");
    }

    private void registerEndpointWithTelegram(int account) {
        AgramContainerManager.ContainerRecord record = AgramContainerManager.getInstance().ensureContainer(account);
        if (UserConfig.getInstance(account).isClientActivated()
                && AgramContainerManager.PUSH_UNIFIED.equals(record.pushMode)
                && !TextUtils.isEmpty(record.unifiedPushEndpoint)) {
            MessagesController.getInstance(account).registerAgramUnifiedPush(record.unifiedPushEndpoint);
        }
    }
}
