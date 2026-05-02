#include "AudioDeviceSettings.h"
#include "LanguageManager.hpp"

//==============================================================================
// ScaleSettingsManager 實現
// 負責管理和持久化 UI 縮放設定
//==============================================================================

/// 取得全域單例實例
ScaleSettingsManager &ScaleSettingsManager::getInstance()
{
    static ScaleSettingsManager instance;
    return instance;
}

/// 取得當前縮放因子（1.0 = 100%）
float ScaleSettingsManager::getScaleFactor() const { return scaleFactor; }

/// 設定新的縮放因子並自動保存設定
void ScaleSettingsManager::setScaleFactor(float scale)
{
    scaleFactor = scale;
    saveSettings();
}

/// 從應用屬性中載入已保存的縮放設定
void ScaleSettingsManager::loadSettings()
{
    try
    {
        auto &props = getAppProperties();
        float v = props.getUserSettings()->getValue("uiScaleFactor", "1.0").getFloatValue();
        // 驗證縮放因子在有效範圍內（0.5 ~ 3.0）
        if (v >= 0.5f && v <= 3.0f)
            scaleFactor = v;
    }
    catch (...)
    {
    }
}

/// 將縮放設定保存到應用屬性
void ScaleSettingsManager::saveSettings()
{
    try
    {
        auto &props = getAppProperties();
        props.getUserSettings()->setValue("uiScaleFactor", String(scaleFactor));
        props.saveIfNeeded();
    }
    catch (...)
    {
    }
}

//==============================================================================
// DeviceSelectorDialog 實現
// 顯示 JUCE AudioDeviceSelectorComponent 並應用動態縮放
//==============================================================================

/// 建構函數：初始化對話框，創建音訊設備選擇器並應用縮放
DeviceSelectorDialog::DeviceSelectorDialog(AudioDeviceManager &dm, int maxIn, int maxOut)
    : mgr(dm), initialMaxIn(maxIn), initialMaxOut(maxOut)
{
    updateSelectorComponent();
}

/// 解構函數
DeviceSelectorDialog::~DeviceSelectorDialog()
{
    if (sel)
        sel->setLookAndFeel(nullptr);
}

void DeviceSelectorDialog::updateSelectorComponent()
{
    // 如果已經存在舊的 selector 元件，先從畫面上移除
    // 避免重複疊加或殘留舊狀態
    if (sel)
    {
        sel->setLookAndFeel(nullptr);
        removeChildComponent(sel.get());
    }
    
    // 重置與字體縮放相關的快取資料
    // naturalFontHeight：儲存原始字體高度（未縮放）
    naturalFontHeight.clear();
    
    // naturalItemHeight：AudioDeviceSelectorComponent 原始 item 高度
    naturalItemHeight = 0;
    
    // computedContentHeight：重建時歸零，由 async block 重新量測
    computedContentHeight = 0;
    
    // 建立新的 AudioDeviceSelectorComponent
    //
    // 參數說明：
    // mgr           : AudioDeviceManager
    // 0             : 最小輸入通道數
    // initialMaxIn  : 最大輸入通道數
    // 0             : 最小輸出通道數
    // initialMaxOut : 最大輸出通道數
    // false ×4     : 不顯示 MIDI、音訊設定、進階選項
    sel = std::make_unique<AudioDeviceSelectorComponent>(
                                                         mgr, 0, initialMaxIn, 0, initialMaxOut,
                                                         false, false, false, false);
    
    //auto& selectorLookAndFeel = getScaledSelectorLookAndFeel();
    //sel->setLookAndFeel(&selectorLookAndFeel);
    
    // 加入畫面並設為可見
    addAndMakeVisible(sel.get());
    
    sel->setSize(420, 2000);
    int maxBottom = 0;
    for (int i = 0; i < sel->getNumChildComponents(); ++i)
    {
        auto* c = sel->getChildComponent(i);
        if (c->isVisible())
            maxBottom = jmax(maxBottom, c->getBottom());
    }
    computedContentHeight = (maxBottom > 30) ? maxBottom : 300;
    
    if (onScaleChanged)
        onScaleChanged();
}

// ---- private helpers ----

void DeviceSelectorDialog::collectNaturalFonts(Component *comp)
{
    if (!comp)
        return;
    if (auto *lbl = dynamic_cast<Label *>(comp))
        naturalFontHeight[comp] = lbl->getFont().getHeight();
    for (int i = 0; i < comp->getNumChildComponents(); ++i)
        collectNaturalFonts(comp->getChildComponent(i));
}

String DeviceSelectorDialog::getCurrentDeviceName() const
{
    String name = LanguageManager::getInstance().getText("audioDevice");
    if (auto *d = mgr.getCurrentAudioDevice()) {
        name = d->getName();
    }
    return name;
}

//==============================================================================
// DeviceSelectorWindow 實現
// 可調整大小的視窗，包含 DeviceSelectorDialog
//==============================================================================

/// 建構函數：創建視窗並初始化對話框組件
DeviceSelectorWindow::DeviceSelectorWindow(const String &title, AudioDeviceManager &dm,
                                           int maxIn, int maxOut,
                                           std::function<void(const String &)> cb)
    : DocumentWindow(title, Colours::lightgrey,
                     DocumentWindow::closeButton | DocumentWindow::minimiseButton, true),
      onConfirmCallback(std::move(cb))
{
    // 創建並設定對話框組件
    auto *dlg = new DeviceSelectorDialog(dm, maxIn, maxOut);
    dlg->onScaleChanged = [this]
    { updateWindowSize(); };

    // 設定視窗風格
    setContentOwned(dlg, true);   // 視窗擁有對話框生命週期
    setUsingNativeTitleBar(true); // 使用 Windows 原生標題欄
    setResizable(false, false);   // 禁用調整大小（由計時器控制）
    setBackgroundColour(Colour::fromRGB(236, 236, 236));

    // 初始化視窗大小
    updateWindowSize();
    setTopLeftPosition(250, 150); // 預設視窗左上角座標
}

/// 關閉視窗並執行清理
void DeviceSelectorWindow::closeWindow()
{
    // 當用戶點擊窗口的X按鈕時，獲取當前選擇的設備並調用回調
    if (auto *dlg = dynamic_cast<DeviceSelectorDialog *>(getContentComponent()))
    {
        String name = dlg->getCurrentDeviceName();
        if (onConfirmCallback)
            onConfirmCallback(name);
    }

    removeFromDesktop();
    delete this; // 自我銷毀（由 addToDesktop 管理的視窗）
}

/// DocumentWindow 虛函數：當用戶點擊視窗關閉按鈕時調用
void DeviceSelectorWindow::closeButtonPressed()
{
    closeWindow();
}

void DeviceSelectorDialog::getPreferredSize(int &outWidth, int &outHeight) const
{
    outWidth = 420;

    // 優先使用動態量測、否則用估算値
    int contentH = (computedContentHeight > 30)
                       ? computedContentHeight
                       : 300;

    outHeight = contentH + 12;

    // 上限：不超過主螢幕可用區高度 − 50px
    auto &displays = Desktop::getInstance().getDisplays();
    if (auto *primary = displays.getPrimaryDisplay())
    {
        int screenH = primary->userArea.getHeight();
        outHeight = jmin(outHeight, screenH - 50);
    }
}

void DeviceSelectorWindow::updateWindowSize()
{
    if (auto *dlg = dynamic_cast<DeviceSelectorDialog *>(getContentComponent()))
    {
        int w = 0, h = 0;
        dlg->getPreferredSize(w, h);
        setSize(w, h);
    }
}
