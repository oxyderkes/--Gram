/*
 * This file is part of Agram and is licensed under GNU GPL v2 or later.
 */
package org.telegram.messenger;

import org.unifiedpush.android.connector.FailedReason;
import org.unifiedpush.android.connector.PushService;
import org.unifiedpush.android.connector.data.PushEndpoint;
import org.unifiedpush.android.connector.data.PushMessage;

/** Receives connector events without sharing endpoints between accounts. */
public class AgramUnifiedPushService extends PushService {
    @Override
    public void onNewEndpoint(PushEndpoint endpoint, String instance) {
        AgramUnifiedPushController.getInstance().onNewEndpoint(instance, endpoint.getUrl());
    }

    @Override
    public void onMessage(PushMessage message, String instance) {
        AgramUnifiedPushController.getInstance().onMessage(instance);
    }

    @Override
    public void onRegistrationFailed(FailedReason reason, String instance) {
        AgramUnifiedPushController.getInstance().onRegistrationFailed(instance, String.valueOf(reason));
    }

    @Override
    public void onUnregistered(String instance) {
        AgramUnifiedPushController.getInstance().onUnregistered(instance);
    }
}
