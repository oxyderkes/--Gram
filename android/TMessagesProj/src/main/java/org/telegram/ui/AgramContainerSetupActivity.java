/*
 * This file is part of Agram and is licensed under GNU GPL v2 or later.
 */
package org.telegram.ui;

import android.content.Context;
import android.content.pm.PackageInfo;
import android.content.res.ColorStateList;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.text.InputType;
import android.text.TextUtils;
import android.view.Gravity;
import android.view.View;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.Space;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

import org.telegram.messenger.AgramContainerManager;
import org.telegram.messenger.AndroidUtilities;
import org.telegram.messenger.BuildVars;
import org.telegram.messenger.LocaleController;
import org.telegram.messenger.R;
import org.telegram.messenger.UserConfig;
import org.telegram.ui.ActionBar.BaseFragment;
import org.telegram.ui.ActionBar.ActionBar;
import org.telegram.ui.ActionBar.Theme;
import org.telegram.ui.Components.LayoutHelper;

import java.util.Locale;
import java.util.TimeZone;

/**
 * Offline first-run screen. No Telegram controller is touched until the user
 * accepts the exact session profile shown here.
 */
public class AgramContainerSetupActivity extends BaseFragment {

    private final int account;
    private EditText nameField;
    private EditText pinField;
    private RadioButton minimalProfile;
    private RadioButton compatibleProfile;
    private Switch biometricSwitch;
    private RadioButton hiddenNotifications;
    private RadioButton authorNotifications;
    private RadioButton fullNotifications;
    private TextView preview;
    private AgramContainerManager.ContainerRecord record;

    public AgramContainerSetupActivity() {
        this(UserConfig.selectedAccount);
    }

    public AgramContainerSetupActivity(int account) {
        super();
        this.account = account;
        currentAccount = account;
    }

    @Override
    public boolean onFragmentCreate() {
        record = AgramContainerManager.getInstance().ensureContainer(account);
        return super.onFragmentCreate();
    }

    @Override
    public View createView(Context context) {
        boolean activeContainer = UserConfig.getInstance(account).isClientActivated();
        actionBar.setBackButtonImage(R.drawable.ic_ab_back);
        actionBar.setAllowOverlayTitle(true);
        actionBar.setTitle(activeContainer ? "Защита контейнера" : (record.profileLocked ? "Контейнер" : "Новый контейнер"));
        actionBar.setActionBarMenuOnItemClick(new ActionBar.ActionBarMenuOnItemClick() {
            @Override
            public void onItemClick(int id) {
                if (id == -1) {
                    finishFragment();
                }
            }
        });

        FrameLayout root = new FrameLayout(context);
        root.setBackgroundColor(Theme.getColor(Theme.key_windowBackgroundGray));
        fragmentView = root;

        android.widget.ScrollView scrollView = new android.widget.ScrollView(context);
        scrollView.setFillViewport(true);
        root.addView(scrollView, LayoutHelper.createFrame(LayoutHelper.MATCH_PARENT, LayoutHelper.MATCH_PARENT));

        LinearLayout content = new LinearLayout(context);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(AndroidUtilities.dp(20), AndroidUtilities.dp(20), AndroidUtilities.dp(20), AndroidUtilities.dp(32));
        scrollView.addView(content, new android.widget.ScrollView.LayoutParams(
                android.widget.ScrollView.LayoutParams.MATCH_PARENT,
                android.widget.ScrollView.LayoutParams.WRAP_CONTENT
        ));

        TextView title = text(context, "Изолированный аккаунт", 26, Theme.key_windowBackgroundWhiteBlackText, true);
        content.addView(title);
        TextView subtitle = text(
                context,
                activeContainer
                        ? "Настройки относятся только к текущему аккаунту и хранятся локально. Профиль уже активной Telegram-сессии изменить нельзя."
                        : "Сначала создаётся локальный контейнер. Только после проверки профиля будет открыта авторизация Telegram.",
                15,
                Theme.key_windowBackgroundWhiteGrayText,
                false
        );
        subtitle.setLineSpacing(AndroidUtilities.dp(2), 1f);
        content.addView(subtitle, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT, 0, 8, 0, 18));

        LinearLayout identityCard = card(context);
        content.addView(identityCard, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT, 0, 0, 0, 12));
        identityCard.addView(sectionLabel(context, "КОНТЕЙНЕР"));
        nameField = input(context, "Название контейнера");
        nameField.setText(record.name);
        nameField.setSelection(nameField.length());
        identityCard.addView(nameField, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, 52));

        LinearLayout profileCard = card(context);
        content.addView(profileCard, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT, 0, 0, 0, 12));
        profileCard.addView(sectionLabel(context, "ПРОФИЛЬ СЕССИИ"));

        minimalProfile = radio(context, "Минимальный", "Telegram увидит нейтральное Agram Android и основную версию Android.");
        compatibleProfile = radio(context, "Совместимый", "Передаются стандартные значения текущего устройства, как в upstream Telegram.");
        profileCard.addView(minimalProfile);
        profileCard.addView(compatibleProfile);
        minimalProfile.setChecked(record.profileMode == AgramContainerManager.PROFILE_MINIMAL);
        compatibleProfile.setChecked(record.profileMode == AgramContainerManager.PROFILE_COMPATIBLE);
        minimalProfile.setOnClickListener(v -> {
            minimalProfile.setChecked(true);
            compatibleProfile.setChecked(false);
            updatePreview();
        });
        compatibleProfile.setOnClickListener(v -> {
            compatibleProfile.setChecked(true);
            minimalProfile.setChecked(false);
            updatePreview();
        });

        preview = text(context, "", 13, Theme.key_windowBackgroundWhiteGrayText, false);
        preview.setTypeface(android.graphics.Typeface.MONOSPACE);
        preview.setTextIsSelectable(true);
        GradientDrawable previewBackground = rounded(Theme.getColor(Theme.key_windowBackgroundGray), 12);
        preview.setBackground(previewBackground);
        preview.setPadding(AndroidUtilities.dp(14), AndroidUtilities.dp(12), AndroidUtilities.dp(14), AndroidUtilities.dp(12));
        profileCard.addView(preview, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT, 0, 10, 0, 0));

        LinearLayout securityCard = card(context);
        content.addView(securityCard, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT, 0, 0, 0, 12));
        securityCard.addView(sectionLabel(context, "ЛОКАЛЬНАЯ ЗАЩИТА"));
        pinField = input(context, activeContainer
                ? "Новый PIN — оставить пустым, чтобы не менять"
                : "PIN — минимум 6 символов (необязательно)");
        pinField.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
        securityCard.addView(pinField, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, 52));

        biometricSwitch = new Switch(context);
        biometricSwitch.setText("Разрешить биометрию после настройки PIN");
        biometricSwitch.setTextSize(15);
        biometricSwitch.setTextColor(Theme.getColor(Theme.key_windowBackgroundWhiteBlackText));
        biometricSwitch.setChecked(record.biometricEnabled);
        biometricSwitch.setPadding(0, AndroidUtilities.dp(8), 0, AndroidUtilities.dp(4));
        securityCard.addView(biometricSwitch, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, 52));

        securityCard.addView(sectionLabel(context, "УВЕДОМЛЕНИЯ"), LayoutHelper.createLinear(
                LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT, 0, 12, 0, 0));
        hiddenNotifications = radio(context, "Скрытые", "На экране блокировки виден только Agram и факт нового сообщения.");
        authorNotifications = radio(context, "Только автор", "Виден собеседник или чат, но не текст и не вложения.");
        fullNotifications = radio(context, "Полные", "Стандартное отображение Telegram с текстом и доступными превью.");
        securityCard.addView(hiddenNotifications);
        securityCard.addView(authorNotifications);
        securityCard.addView(fullNotifications);
        hiddenNotifications.setChecked(record.notificationPrivacy == AgramContainerManager.NOTIFICATION_HIDDEN);
        authorNotifications.setChecked(record.notificationPrivacy == AgramContainerManager.NOTIFICATION_AUTHOR);
        fullNotifications.setChecked(record.notificationPrivacy == AgramContainerManager.NOTIFICATION_FULL);
        hiddenNotifications.setOnClickListener(v -> selectNotificationPrivacy(AgramContainerManager.NOTIFICATION_HIDDEN));
        authorNotifications.setOnClickListener(v -> selectNotificationPrivacy(AgramContainerManager.NOTIFICATION_AUTHOR));
        fullNotifications.setOnClickListener(v -> selectNotificationPrivacy(AgramContainerManager.NOTIFICATION_FULL));

        TextView note = text(
                context,
                "Ключ контейнера создаётся в Android Keystore. Auth keys, база, кэш и настройки этого аккаунта не используются другими контейнерами.",
                13,
                Theme.key_windowBackgroundWhiteGrayText,
                false
        );
        note.setLineSpacing(AndroidUtilities.dp(2), 1f);
        securityCard.addView(note, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT, 0, 8, 0, 0));

        Space space = new Space(context);
        content.addView(space, LayoutHelper.createLinear(1, 8));

        TextView continueButton = text(
                context,
                activeContainer
                        ? "СОХРАНИТЬ НАСТРОЙКИ"
                        : (record.profileLocked ? "ПРОДОЛЖИТЬ ПОВТОРНЫЙ ВХОД" : "СОЗДАТЬ И ДОБАВИТЬ АККАУНТ"),
                14,
                Theme.key_featuredStickers_buttonText,
                true
        );
        continueButton.setGravity(Gravity.CENTER);
        continueButton.setBackground(rounded(Theme.getColor(Theme.key_featuredStickers_addButton), 12));
        continueButton.setOnClickListener(v -> continueToLogin());
        content.addView(continueButton, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, 52));

        if (record.profileLocked) {
            minimalProfile.setEnabled(false);
            compatibleProfile.setEnabled(false);
            if (!activeContainer) {
                nameField.setEnabled(false);
                pinField.setVisibility(View.GONE);
                biometricSwitch.setEnabled(false);
                hiddenNotifications.setEnabled(false);
                authorNotifications.setEnabled(false);
                fullNotifications.setEnabled(false);
            }
        }
        updatePreview();
        return fragmentView;
    }

    private void continueToLogin() {
        boolean activeContainer = UserConfig.getInstance(account).isClientActivated();
        String pin = pinField.getText().toString();
        if (!TextUtils.isEmpty(pin) && pin.length() < 6) {
            Toast.makeText(getParentActivity(), "PIN должен содержать не менее 6 символов", Toast.LENGTH_SHORT).show();
            return;
        }
        if (activeContainer) {
            if (biometricSwitch.isChecked() && TextUtils.isEmpty(pin) && !record.hasPin()) {
                Toast.makeText(getParentActivity(), "Сначала задайте PIN контейнера", Toast.LENGTH_SHORT).show();
                return;
            }
            AgramContainerManager.getInstance().updateContainerSecurity(
                    account,
                    nameField.getText().toString(),
                    pin,
                    biometricSwitch.isChecked(),
                    selectedNotificationPrivacy()
            );
            Toast.makeText(getParentActivity(), "Настройки контейнера сохранены", Toast.LENGTH_SHORT).show();
            finishFragment();
            return;
        }
        if (!record.profileLocked) {
            int mode = compatibleProfile.isChecked()
                    ? AgramContainerManager.PROFILE_COMPATIBLE
                    : AgramContainerManager.PROFILE_MINIMAL;
            AgramContainerManager.getInstance().updatePreLoginProfile(
                    account,
                    nameField.getText().toString(),
                    record.color,
                    mode,
                    Locale.getDefault().getLanguage(),
                    false,
                    TimeZone.getDefault().getOffset(System.currentTimeMillis()) / 1000,
                    pin,
                    biometricSwitch.isChecked() && !TextUtils.isEmpty(pin),
                    selectedNotificationPrivacy()
            );
        }
        presentFragment(new LoginActivity(account), true);
    }

    private void updatePreview() {
        if (preview == null) {
            return;
        }
        boolean minimal = minimalProfile.isChecked();
        String appVersion = "unknown";
        try {
            PackageInfo info = getParentActivity().getPackageManager().getPackageInfo(getParentActivity().getPackageName(), 0);
            appVersion = info.versionName + " (" + info.versionCode + ")";
        } catch (Exception ignore) {
        }
        String release = Build.VERSION.RELEASE;
        int dot = release == null ? -1 : release.indexOf('.');
        if (dot > 0) {
            release = release.substring(0, dot);
        }
        String model = minimal ? "Agram Android" : (Build.MANUFACTURER + Build.MODEL);
        String system = minimal ? "Android " + release : "SDK " + Build.VERSION.SDK_INT;
        String language = LocaleController.getSystemLocaleStringIso639().toLowerCase(Locale.US);
        int timezone = TimeZone.getDefault().getOffset(System.currentTimeMillis()) / 1000;
        preview.setText(
                "device_model: " + model +
                        "\nplatform: android" +
                        "\nsystem_version: " + system +
                        "\napp_version: " + appVersion +
                        "\nlang_code: " + language +
                        "\ntz_offset: " + timezone
        );
    }

    private void selectNotificationPrivacy(int privacy) {
        hiddenNotifications.setChecked(privacy == AgramContainerManager.NOTIFICATION_HIDDEN);
        authorNotifications.setChecked(privacy == AgramContainerManager.NOTIFICATION_AUTHOR);
        fullNotifications.setChecked(privacy == AgramContainerManager.NOTIFICATION_FULL);
    }

    private int selectedNotificationPrivacy() {
        if (fullNotifications.isChecked()) {
            return AgramContainerManager.NOTIFICATION_FULL;
        }
        if (authorNotifications.isChecked()) {
            return AgramContainerManager.NOTIFICATION_AUTHOR;
        }
        return AgramContainerManager.NOTIFICATION_HIDDEN;
    }

    private static LinearLayout card(Context context) {
        LinearLayout value = new LinearLayout(context);
        value.setOrientation(LinearLayout.VERTICAL);
        value.setPadding(AndroidUtilities.dp(16), AndroidUtilities.dp(14), AndroidUtilities.dp(16), AndroidUtilities.dp(16));
        value.setBackground(rounded(Theme.getColor(Theme.key_windowBackgroundWhite), 16));
        return value;
    }

    private static TextView sectionLabel(Context context, String value) {
        TextView view = text(context, value, 12, Theme.key_windowBackgroundWhiteBlueHeader, true);
        view.setLetterSpacing(.12f);
        view.setPadding(0, 0, 0, AndroidUtilities.dp(8));
        return view;
    }

    private static EditText input(Context context, String hint) {
        EditText view = new EditText(context);
        view.setSingleLine(true);
        view.setTextSize(16);
        view.setHint(hint);
        view.setTextColor(Theme.getColor(Theme.key_windowBackgroundWhiteBlackText));
        view.setHintTextColor(Theme.getColor(Theme.key_windowBackgroundWhiteHintText));
        view.setPadding(AndroidUtilities.dp(12), 0, AndroidUtilities.dp(12), 0);
        view.setBackground(rounded(Theme.getColor(Theme.key_windowBackgroundGray), 10));
        return view;
    }

    private static RadioButton radio(Context context, String title, String subtitle) {
        RadioButton view = new RadioButton(context);
        view.setText(title + "\n" + subtitle);
        view.setTextSize(15);
        view.setLineSpacing(AndroidUtilities.dp(2), 1f);
        view.setTextColor(Theme.getColor(Theme.key_windowBackgroundWhiteBlackText));
        int accent = Theme.getColor(Theme.key_featuredStickers_addButton);
        int secondary = Theme.getColor(Theme.key_windowBackgroundWhiteGrayText);
        view.setButtonTintList(new ColorStateList(
                new int[][]{new int[]{android.R.attr.state_checked}, new int[]{}},
                new int[]{accent, secondary}
        ));
        view.setPadding(0, AndroidUtilities.dp(4), 0, AndroidUtilities.dp(4));
        return view;
    }

    private static TextView text(Context context, String value, int size, int colorKey, boolean bold) {
        TextView view = new TextView(context);
        view.setText(value);
        view.setTextSize(size);
        view.setTextColor(Theme.getColor(colorKey));
        if (bold) {
            view.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        }
        return view;
    }

    private static GradientDrawable rounded(int color, int radiusDp) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(color);
        drawable.setCornerRadius(AndroidUtilities.dp(radiusDp));
        return drawable;
    }
}
