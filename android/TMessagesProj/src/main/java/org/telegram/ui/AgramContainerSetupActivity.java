/*
 * This file is part of Agram and is licensed under GNU GPL v2 or later.
 */
package org.telegram.ui;

import android.content.Context;
import android.content.pm.PackageInfo;
import android.content.res.ColorStateList;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.text.Editable;
import android.text.InputFilter;
import android.text.InputType;
import android.text.TextUtils;
import android.text.TextWatcher;
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
    private RadioButton customProfile;
    private LinearLayout customProfileFields;
    private EditText deviceModelField;
    private EditText systemVersionField;
    private EditText appVersionField;
    private TextView detectProfileButton;
    private Switch biometricSwitch;
    private Switch ghostModeSwitch;
    private Switch ghostReadSwitch;
    private Switch ghostStoriesSwitch;
    private Switch ghostTypingSwitch;
    private Switch ghostOnlineSwitch;
    private Switch ghostReadOnInteractionSwitch;
    private Switch ghostWarnSwitch;
    private final EditText[] decoyPinFields = new EditText[3];
    private final EditText[] decoyTargetFields = new EditText[3];
    private Switch clearDecoyCodesSwitch;
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
        compatibleProfile = radio(context, "Авто", "Реальные модель устройства, Android/SDK и версия установленной ά‑Gram.");
        customProfile = radio(context, "Вручную", "Своя постоянная подпись устройства для этой сессии; задаётся до входа.");
        profileCard.addView(minimalProfile);
        profileCard.addView(compatibleProfile);
        profileCard.addView(customProfile);
        minimalProfile.setOnClickListener(v -> selectProfileMode(AgramContainerManager.PROFILE_MINIMAL));
        compatibleProfile.setOnClickListener(v -> selectProfileMode(AgramContainerManager.PROFILE_COMPATIBLE));
        customProfile.setOnClickListener(v -> selectProfileMode(AgramContainerManager.PROFILE_CUSTOM));

        customProfileFields = new LinearLayout(context);
        customProfileFields.setOrientation(LinearLayout.VERTICAL);
        deviceModelField = profileInput(context, "Модель устройства", valueOrDetected(record.deviceModel, detectedDeviceModel()));
        systemVersionField = profileInput(context, "Версия системы", valueOrDetected(record.systemVersion, detectedSystemVersion()));
        appVersionField = profileInput(context, "Версия клиента", valueOrDetected(record.appVersion, detectedAppVersion()));
        customProfileFields.addView(deviceModelField, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, 48, 0, 6, 0, 0));
        customProfileFields.addView(systemVersionField, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, 48, 0, 6, 0, 0));
        customProfileFields.addView(appVersionField, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, 48, 0, 6, 0, 0));

        detectProfileButton = text(context, "ЗАПОЛНИТЬ С ЭТОГО УСТРОЙСТВА", 13, Theme.key_windowBackgroundWhiteBlueText, true);
        detectProfileButton.setGravity(Gravity.CENTER);
        detectProfileButton.setBackground(rounded(Theme.getColor(Theme.key_windowBackgroundGray), 10));
        detectProfileButton.setOnClickListener(v -> fillDetectedProfile());
        customProfileFields.addView(detectProfileButton, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, 44, 0, 8, 0, 0));

        TextView profileNote = text(
                context,
                "Допустимо 1–64 печатных символа. Значения не меняют реальную модель телефона и не обходят ограничения Telegram.",
                12,
                Theme.key_windowBackgroundWhiteGrayText,
                false
        );
        profileNote.setLineSpacing(AndroidUtilities.dp(2), 1f);
        customProfileFields.addView(profileNote, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT, 0, 8, 0, 0));
        profileCard.addView(customProfileFields, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT, 0, 4, 0, 0));

        TextWatcher profileWatcher = new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence s, int start, int count, int after) {
            }

            @Override
            public void onTextChanged(CharSequence s, int start, int before, int count) {
                updatePreview();
            }

            @Override
            public void afterTextChanged(Editable s) {
            }
        };
        deviceModelField.addTextChangedListener(profileWatcher);
        systemVersionField.addTextChangedListener(profileWatcher);
        appVersionField.addTextChangedListener(profileWatcher);

        preview = text(context, "", 13, Theme.key_windowBackgroundWhiteGrayText, false);
        preview.setTypeface(android.graphics.Typeface.MONOSPACE);
        preview.setTextIsSelectable(true);
        GradientDrawable previewBackground = rounded(Theme.getColor(Theme.key_windowBackgroundGray), 12);
        preview.setBackground(previewBackground);
        preview.setPadding(AndroidUtilities.dp(14), AndroidUtilities.dp(12), AndroidUtilities.dp(14), AndroidUtilities.dp(12));
        profileCard.addView(preview, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT, 0, 10, 0, 0));

        LinearLayout ghostCard = card(context);
        content.addView(ghostCard, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT, 0, 0, 0, 12));
        ghostCard.addView(sectionLabel(context, "GHOST MODE · BEST EFFORT"));
        ghostModeSwitch = settingSwitch(context, "Включить Ghost Mode", record.ghostModeEnabled);
        ghostReadSwitch = settingSwitch(context, "Не отправлять отметку о прочтении", record.ghostSuppressReadReceipts);
        ghostStoriesSwitch = settingSwitch(context, "Не показывать просмотр историй", record.ghostSuppressStoryViews);
        ghostTypingSwitch = settingSwitch(context, "Не отправлять typing / recording", record.ghostSuppressTyping);
        ghostOnlineSwitch = settingSwitch(context, "Минимизировать online", record.ghostMinimizeOnline);
        ghostReadOnInteractionSwitch = settingSwitch(context, "Прочитать при взаимодействии", record.ghostReadOnInteraction);
        ghostWarnSwitch = settingSwitch(context, "Предупреждать перед реакцией или ответом", record.ghostWarnBeforeInteraction);
        ghostCard.addView(ghostModeSwitch);
        ghostCard.addView(ghostReadSwitch);
        ghostCard.addView(ghostStoriesSwitch);
        ghostCard.addView(ghostTypingSwitch);
        ghostCard.addView(ghostOnlineSwitch);
        ghostCard.addView(ghostReadOnInteractionSwitch);
        ghostCard.addView(ghostWarnSwitch);
        ghostModeSwitch.setOnCheckedChangeListener((buttonView, isChecked) -> setGhostControlsEnabled(isChecked));
        ghostReadSwitch.setOnCheckedChangeListener((buttonView, isChecked) -> {
            ghostReadOnInteractionSwitch.setEnabled(ghostModeSwitch.isChecked() && isChecked);
            ghostReadOnInteractionSwitch.setAlpha(ghostReadOnInteractionSwitch.isEnabled() ? 1f : .5f);
        });
        setGhostControlsEnabled(ghostModeSwitch.isChecked());

        TextView ghostNote = text(
                context,
                "Best effort: клиент подавляет известные запросы активности, но отправка сообщения, реакция, звонок и другие серверные действия всё равно могут раскрыть присутствие.",
                12,
                Theme.key_windowBackgroundWhiteGrayText,
                false
        );
        ghostNote.setLineSpacing(AndroidUtilities.dp(2), 1f);
        ghostCard.addView(ghostNote, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT, 0, 8, 0, 0));

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

        if (activeContainer) {
            LinearLayout legendCard = card(context);
            content.addView(legendCard, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT, 0, 0, 0, 12));
            legendCard.addView(sectionLabel(context, "ЛОЖНЫЕ КОДЫ · ЛЕГЕНДА"));
            for (int i = 0; i < decoyPinFields.length; i++) {
                decoyPinFields[i] = input(context, "Ложный код " + (i + 1) + " — минимум 6 символов");
                decoyPinFields[i].setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
                decoyTargetFields[i] = input(context, "Номер аккаунта-легенды (1–" + UserConfig.MAX_ACCOUNT_COUNT + ")");
                decoyTargetFields[i].setInputType(InputType.TYPE_CLASS_NUMBER);
                if (i < record.decoyCodes.size()) {
                    decoyTargetFields[i].setText(String.valueOf(record.decoyCodes.get(i).targetAccount + 1));
                }
                legendCard.addView(decoyPinFields[i], LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, 48, 0, i == 0 ? 2 : 12, 0, 0));
                legendCard.addView(decoyTargetFields[i], LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, 48, 0, 6, 0, 0));
            }
            clearDecoyCodesSwitch = settingSwitch(context, "Удалить сохранённые ложные коды", false);
            legendCard.addView(clearDecoyCodesSwitch, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, 52, 0, 8, 0, 0));
            TextView legendNote = text(
                    context,
                    "Ложный код открывает выбранный локальный аккаунт-легенду. Пустые поля сохраняют текущие коды. Автоматическое удаление, выход из сеансов и скрытые SOS-действия не выполняются.",
                    12,
                    Theme.key_windowBackgroundWhiteGrayText,
                    false
            );
            legendNote.setLineSpacing(AndroidUtilities.dp(2), 1f);
            legendCard.addView(legendNote, LayoutHelper.createLinear(LayoutHelper.MATCH_PARENT, LayoutHelper.WRAP_CONTENT, 0, 8, 0, 0));
        }

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
            customProfile.setEnabled(false);
            deviceModelField.setEnabled(false);
            systemVersionField.setEnabled(false);
            appVersionField.setEnabled(false);
            detectProfileButton.setEnabled(false);
            detectProfileButton.setAlpha(.5f);
            if (!activeContainer) {
                nameField.setEnabled(false);
                pinField.setVisibility(View.GONE);
                biometricSwitch.setEnabled(false);
                hiddenNotifications.setEnabled(false);
                authorNotifications.setEnabled(false);
                fullNotifications.setEnabled(false);
            }
        }
        selectProfileMode(record.profileMode);
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
            if (!saveDecoyCodes()) {
                return;
            }
            saveGhostMode();
            Toast.makeText(getParentActivity(), "Настройки контейнера сохранены", Toast.LENGTH_SHORT).show();
            finishFragment();
            return;
        }
        if (!record.profileLocked) {
            int mode = selectedProfileMode();
            if (mode == AgramContainerManager.PROFILE_CUSTOM
                    && (TextUtils.isEmpty(deviceModelField.getText().toString().trim())
                    || TextUtils.isEmpty(systemVersionField.getText().toString().trim())
                    || TextUtils.isEmpty(appVersionField.getText().toString().trim()))) {
                Toast.makeText(getParentActivity(), "Заполните все три поля профиля устройства", Toast.LENGTH_SHORT).show();
                return;
            }
            AgramContainerManager.getInstance().updatePreLoginProfile(
                    account,
                    nameField.getText().toString(),
                    record.color,
                    mode,
                    deviceModelField.getText().toString(),
                    systemVersionField.getText().toString(),
                    appVersionField.getText().toString(),
                    Locale.getDefault().getLanguage(),
                    false,
                    TimeZone.getDefault().getOffset(System.currentTimeMillis()) / 1000,
                    pin,
                    biometricSwitch.isChecked() && !TextUtils.isEmpty(pin),
                    selectedNotificationPrivacy()
            );
        }
        saveGhostMode();
        presentFragment(new LoginActivity(account), true);
    }

    private void updatePreview() {
        if (preview == null) {
            return;
        }
        int mode = selectedProfileMode();
        String appVersion = mode == AgramContainerManager.PROFILE_CUSTOM
                ? profileText(appVersionField, detectedAppVersion())
                : detectedAppVersion();
        String release = Build.VERSION.RELEASE;
        int dot = release == null ? -1 : release.indexOf('.');
        if (dot > 0) {
            release = release.substring(0, dot);
        }
        String model;
        String system;
        if (mode == AgramContainerManager.PROFILE_MINIMAL) {
            model = "Agram Android";
            system = "Android " + release;
        } else if (mode == AgramContainerManager.PROFILE_CUSTOM) {
            model = profileText(deviceModelField, detectedDeviceModel());
            system = profileText(systemVersionField, detectedSystemVersion());
        } else {
            model = detectedDeviceModel();
            system = detectedSystemVersion();
        }
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

    private void selectProfileMode(int mode) {
        int normalized = mode == AgramContainerManager.PROFILE_COMPATIBLE
                || mode == AgramContainerManager.PROFILE_CUSTOM
                ? mode
                : AgramContainerManager.PROFILE_MINIMAL;
        minimalProfile.setChecked(normalized == AgramContainerManager.PROFILE_MINIMAL);
        compatibleProfile.setChecked(normalized == AgramContainerManager.PROFILE_COMPATIBLE);
        customProfile.setChecked(normalized == AgramContainerManager.PROFILE_CUSTOM);
        customProfileFields.setVisibility(normalized == AgramContainerManager.PROFILE_CUSTOM ? View.VISIBLE : View.GONE);
        updatePreview();
    }

    private int selectedProfileMode() {
        if (customProfile != null && customProfile.isChecked()) {
            return AgramContainerManager.PROFILE_CUSTOM;
        }
        if (compatibleProfile != null && compatibleProfile.isChecked()) {
            return AgramContainerManager.PROFILE_COMPATIBLE;
        }
        return AgramContainerManager.PROFILE_MINIMAL;
    }

    private void fillDetectedProfile() {
        deviceModelField.setText(detectedDeviceModel());
        systemVersionField.setText(detectedSystemVersion());
        appVersionField.setText(detectedAppVersion());
        deviceModelField.setSelection(deviceModelField.length());
        updatePreview();
    }

    private String detectedAppVersion() {
        try {
            PackageInfo info = getParentActivity().getPackageManager().getPackageInfo(getParentActivity().getPackageName(), 0);
            return info.versionName + " (" + info.versionCode + ")";
        } catch (Exception ignore) {
            return BuildVars.BUILD_VERSION_STRING;
        }
    }

    private static String detectedDeviceModel() {
        String manufacturer = TextUtils.isEmpty(Build.MANUFACTURER) ? "Android" : Build.MANUFACTURER.trim();
        String model = TextUtils.isEmpty(Build.MODEL) ? "device" : Build.MODEL.trim();
        if (model.toLowerCase(Locale.US).startsWith(manufacturer.toLowerCase(Locale.US))) {
            return model;
        }
        return manufacturer + " " + model;
    }

    private static String detectedSystemVersion() {
        String release = TextUtils.isEmpty(Build.VERSION.RELEASE) ? "unknown" : Build.VERSION.RELEASE;
        return "Android " + release + " (SDK " + Build.VERSION.SDK_INT + ")";
    }

    private static String valueOrDetected(String value, String detected) {
        return TextUtils.isEmpty(value) ? detected : value;
    }

    private static String profileText(EditText field, String fallback) {
        if (field == null || TextUtils.isEmpty(field.getText().toString().trim())) {
            return fallback;
        }
        return field.getText().toString().trim();
    }

    private static EditText profileInput(Context context, String hint, String value) {
        EditText view = input(context, hint);
        view.setFilters(new InputFilter[]{new InputFilter.LengthFilter(64)});
        view.setText(value);
        view.setSelection(view.length());
        return view;
    }

    private void selectNotificationPrivacy(int privacy) {
        hiddenNotifications.setChecked(privacy == AgramContainerManager.NOTIFICATION_HIDDEN);
        authorNotifications.setChecked(privacy == AgramContainerManager.NOTIFICATION_AUTHOR);
        fullNotifications.setChecked(privacy == AgramContainerManager.NOTIFICATION_FULL);
    }

    private void saveGhostMode() {
        AgramContainerManager.getInstance().updateGhostMode(
                account,
                ghostModeSwitch.isChecked(),
                ghostReadSwitch.isChecked(),
                ghostStoriesSwitch.isChecked(),
                ghostTypingSwitch.isChecked(),
                ghostOnlineSwitch.isChecked(),
                ghostReadOnInteractionSwitch.isChecked(),
                ghostWarnSwitch.isChecked()
        );
    }

    private boolean saveDecoyCodes() {
        if (clearDecoyCodesSwitch == null) {
            return true;
        }
        int count = 0;
        for (EditText field : decoyPinFields) {
            if (field != null && !TextUtils.isEmpty(field.getText().toString())) {
                count++;
            }
        }
        if (count == 0 && !clearDecoyCodesSwitch.isChecked()) {
            return true;
        }
        String[] pins = new String[count];
        int[] targets = new int[count];
        int index = 0;
        for (int i = 0; i < decoyPinFields.length; i++) {
            String pin = decoyPinFields[i].getText().toString();
            if (TextUtils.isEmpty(pin)) {
                continue;
            }
            String targetText = decoyTargetFields[i].getText().toString().trim();
            if (pin.length() < 6 || TextUtils.isEmpty(targetText)) {
                Toast.makeText(getParentActivity(), "Для каждого ложного кода укажите минимум 6 символов и аккаунт-легенду", Toast.LENGTH_LONG).show();
                return false;
            }
            try {
                pins[index] = pin;
                targets[index] = Integer.parseInt(targetText) - 1;
            } catch (NumberFormatException e) {
                Toast.makeText(getParentActivity(), "Номер аккаунта-легенды указан неверно", Toast.LENGTH_LONG).show();
                return false;
            }
            index++;
        }
        try {
            AgramContainerManager.getInstance().updateDecoyCodes(
                    account,
                    pins,
                    targets,
                    clearDecoyCodesSwitch.isChecked()
            );
            return true;
        } catch (IllegalArgumentException e) {
            Toast.makeText(getParentActivity(), "Ложный код должен отличаться от настоящего, а легендой должен быть другой активный аккаунт", Toast.LENGTH_LONG).show();
            return false;
        }
    }

    private void setGhostControlsEnabled(boolean enabled) {
        Switch[] controls = {
                ghostReadSwitch,
                ghostStoriesSwitch,
                ghostTypingSwitch,
                ghostOnlineSwitch,
                ghostReadOnInteractionSwitch,
                ghostWarnSwitch
        };
        for (Switch control : controls) {
            boolean controlEnabled = enabled && (control != ghostReadOnInteractionSwitch || ghostReadSwitch.isChecked());
            control.setEnabled(controlEnabled);
            control.setAlpha(controlEnabled ? 1f : .5f);
        }
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

    private static Switch settingSwitch(Context context, String title, boolean checked) {
        Switch view = new Switch(context);
        view.setText(title);
        view.setTextSize(15);
        view.setTextColor(Theme.getColor(Theme.key_windowBackgroundWhiteBlackText));
        view.setChecked(checked);
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
