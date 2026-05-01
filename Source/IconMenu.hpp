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
