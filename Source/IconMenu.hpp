//
//  IconMenu.hpp
//  Light Host
//
//  Created by Rolando Islas on 12/26/15.
//
//

#ifndef IconMenu_hpp
#define IconMenu_hpp

#include "LanguageManager.hpp"
class MainWindowContent;

// ==================== 全局函數宣告 ====================
/**
 * getAppProperties() 函數
 * 取得全局應用程式屬性實例
 * 用於存取持久化設置
 * 
 * @return ApplicationProperties 參考
 */
ApplicationProperties& getAppProperties();

class MixerCallBack : public AudioIODeviceCallback
{
public:
    std::unique_ptr<FileLogger> m_flogger;

    MixerCallBack() {
        m_flogger = std::unique_ptr<FileLogger>(FileLogger::createDefaultAppLogger("LightHost", "mylog.txt", "Welcome to mcb"));
    }
    ~MixerCallBack() {
        //if (m_flogger)
            m_flogger->logMessage("~MixerCallBack()");
    }

    void audioDeviceIOCallback(const float **inputChannelData, int numInputChannels, float **outputChannelData, int numOutputChannels, int numSamples) 
    {
        //if (m_flogger)
            m_flogger->logMessage("audioDeviceIOCallback()");

        for (int i=0;i<numOutputChannels;++i)
               {
                   for (int j=0;j<numSamples;++j)
                   {
                       outputChannelData[i][j] = 0.0f;
                   }
               }
    }
    
    void audioDeviceAboutToStart(juce::AudioIODevice *device) override  {}
    void audioDeviceStopped() override {}
};

class IconMenu : public SystemTrayIconComponent, private Timer, public ChangeListener
{
public:
    IconMenu();
    ~IconMenu();
    void mouseDown(const MouseEvent&);
    static void menuInvocationCallback(int id, IconMenu*);
    void changeListenerCallback(ChangeBroadcaster* changed);
	static String getKey(String type, PluginDescription plugin);
    
	const int INDEX_EDIT, INDEX_BYPASS, INDEX_DELETE, INDEX_MOVE_UP, INDEX_MOVE_DOWN;
private:
    void timerCallback();
    void reloadPlugins();
    void loadActivePlugins();
    void savePluginStates();
	void removePluginsLackingInputOutput();
	void setIcon();

    // ==================== 音頻處理成員 ====================
    
    AudioDeviceManager deviceManager;
    
    AudioPluginFormatManager formatManager;
    KnownPluginList knownPluginList;
    Array<PluginDescription> pluginMenuTypes;
    PopupMenu menu;
    std::unique_ptr<PluginDirectoryScanner> scanner;
    AudioProcessorGraph graph;
    AudioProcessorPlayer player;
    AudioProcessorGraph::Node *inputNode;
    AudioProcessorGraph::Node *outputNode;

	class PluginListWindow;
	std::unique_ptr<PluginListWindow> pluginListWindow;

	class MainWindow;
	std::unique_ptr<MainWindow> mainWindow;
	std::unique_ptr<MainWindowContent> mainContent;
};

#endif /* IconMenu_hpp */
