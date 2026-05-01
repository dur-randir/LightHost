//
//  IconMenu.cpp
//  Light Host
//
//  Created by Rolando Islas on 12/26/15.
//
//

#include "JuceHeader.h"
#include "IconMenu.hpp"
#include "LanguageManager.hpp"
#include "PluginWindow.h"
#include "MainWindowContent.h"
#include <ctime>
#include <limits.h>

// ==================== Windows 平台特定實現 ====================

class IconMenu::PluginListWindow : public DocumentWindow
{
public:
	PluginListWindow(IconMenu& owner_, AudioPluginFormatManager& pluginFormatManager)
		: DocumentWindow(LanguageManager::getInstance().getText("availablePlugins"), Colours::white,
			DocumentWindow::minimiseButton | DocumentWindow::closeButton),
		owner(owner_)
	{
		const File deadMansPedalFile(getAppProperties().getUserSettings()
			->getFile().getSiblingFile("RecentlyCrashedPluginsList"));

		setContentOwned(new PluginListComponent(pluginFormatManager,
			owner.knownPluginList,
			deadMansPedalFile,
			getAppProperties().getUserSettings()), true);

		setUsingNativeTitleBar(true);
		setResizable(true, false);
		setResizeLimits(300, 400, 800, 1500);
		setTopLeftPosition(60, 60);

		restoreWindowStateFromString(getAppProperties().getUserSettings()->getValue("listWindowPos"));
		setVisible(true);
	}

	~PluginListWindow()
	{
		getAppProperties().getUserSettings()->setValue("listWindowPos", getWindowStateAsString());

		clearContentComponent();
	}

	void closeButtonPressed()
	{
        owner.removePluginsLackingInputOutput();
		owner.pluginListWindow = nullptr;
	}

private:
	IconMenu& owner;
};

// ==================== Main Window ====================

class IconMenu::MainWindow : public DocumentWindow
{
public:
    MainWindow(IconMenu& owner_)
        : DocumentWindow(LanguageManager::getInstance().getText("appName"),
                         Colour::fromRGB(26, 26, 26),
                         DocumentWindow::minimiseButton | DocumentWindow::closeButton),
          owner(owner_)
    {
        // Don't create a new one, use the persistent one owned by IconMenu
        auto* content = owner.mainContent.get();
        content->setSize(900, 560);
        
        // Pass ownership flag false so DocumentWindow doesn't delete it
        setContentNonOwned(content, true);

        setUsingNativeTitleBar(true);
        setResizable(true, false);
        setResizeLimits(600, 400, 4096, 4096);
        centreWithSize(900, 560);
        setVisible(true);
        setTopLeftPosition(250, 150); // 預設視窗左上角座標
    }

    void closeButtonPressed() override
    {
        owner.mainWindow = nullptr;
    }

private:
    IconMenu& owner;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

IconMenu::IconMenu() : INDEX_EDIT(1000000), INDEX_BYPASS(2000000), INDEX_DELETE(3000000), INDEX_MOVE_UP(4000000), INDEX_MOVE_DOWN(5000000)
{
    // Initiialization
    addDefaultFormatsToManager(formatManager);
    
    // Load saved language preference and apply it
    String savedLanguageId = getAppProperties().getUserSettings()->getValue("language", "English");
    LanguageManager::getInstance().setLanguageById(savedLanguageId);
    
    // Audio device
    std::unique_ptr<XmlElement> savedAudioState (getAppProperties().getUserSettings()->getXmlValue("audioDeviceState"));
    deviceManager.initialise(256, 256, savedAudioState.get(), true);
    player.setProcessor(&graph);
    deviceManager.addAudioCallback(&player);
    // Plugins - all
    std::unique_ptr<XmlElement> savedPluginList(getAppProperties().getUserSettings()->getXmlValue("pluginList"));
    if (savedPluginList != nullptr)
        knownPluginList.recreateFromXml(*savedPluginList);
    pluginSortMethod = KnownPluginList::sortByManufacturer;
    knownPluginList.addChangeListener(this);
    // Plugins - active
    std::unique_ptr<XmlElement> savedPluginListActive(getAppProperties().getUserSettings()->getXmlValue("pluginListActive"));
    if (savedPluginListActive != nullptr)
        activePluginList.recreateFromXml(*savedPluginListActive);
    // Setup the main content and bind the graph change callback for saving
    mainContent = std::make_unique<MainWindowContent>(
        deviceManager,
        knownPluginList,
        formatManager,
        graph);
    
    mainContent->onManagePlugins = [this] { reloadPlugins(); };
    mainContent->onGraphChanged  = [this]
    {
        if (auto xml = mainContent->saveState())
        {
            getAppProperties().getUserSettings()->setValue("nodeGraphState", xml.get());
            getAppProperties().saveIfNeeded();
        }
    };
    mainContent->onScaleChanged = [this]
    {
        if (mainWindow != nullptr)
        {
            const float scale = ScaleSettingsManager::getInstance().getScaleFactor();
            int w = static_cast<int>(900 * scale);
            int h = static_cast<int>(560 * scale);
            if (auto* disp = Desktop::getInstance().getDisplays().getPrimaryDisplay())
            {
                w = jmin(w, disp->userArea.getWidth());
                h = jmin(h, disp->userArea.getHeight() - 50);
            }
            mainWindow->setSize(w, h);
        }
    };

    // Load saved graph state after setting up fixed I/O nodes
    // The loadActivePlugins() call now just setups the I/O nodes in AudioProcessorGraph
    loadActivePlugins();
    activePluginList.addChangeListener(this);

    std::unique_ptr<XmlElement> savedGraphState(getAppProperties().getUserSettings()->getXmlValue("nodeGraphState"));
    if (savedGraphState != nullptr)
        mainContent->loadState(*savedGraphState);
    
    // After loading graph, also trigger a save to ensure all plugin states are captured
    mainContent->onGraphChanged();

	setIcon();
	setIconTooltip(LanguageManager::getInstance().getText("appName"));
}

IconMenu::~IconMenu()
{
	savePluginStates();
    // clear window before tearing down device manager & graph
    mainWindow.reset();
    mainContent.reset(); 
}

void IconMenu::setIcon()
{
	// Set menu icon - Windows only
	Image icon;
	String defaultColor = "white";
	
	if (!getAppProperties().getUserSettings()->containsKey("icon"))
		getAppProperties().getUserSettings()->setValue("icon", defaultColor);
	
	String color = getAppProperties().getUserSettings()->getValue("icon");
	if (color.equalsIgnoreCase("white"))
		icon = ImageFileFormat::loadFrom(BinaryData::menu_icon_white_png, BinaryData::menu_icon_white_pngSize);
	else if (color.equalsIgnoreCase("black"))
		icon = ImageFileFormat::loadFrom(BinaryData::menu_icon_png, BinaryData::menu_icon_pngSize);
	setIconImage(icon, icon);
}

void IconMenu::loadActivePlugins()
{
    const int INPUT  = 1000000;
    const int OUTPUT = INPUT + 1;

    PluginWindow::closeAllCurrentlyOpenWindows();
    graph.clear();

    // Set up the graph's fixed I/O nodes.
    // Audio routing is now driven by the NodeGraphCanvas UI.
    inputNode  = graph.addNode(std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor>(
                     AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode),
                     AudioProcessorGraph::NodeID(INPUT));
    outputNode = graph.addNode(std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor>(
                     AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode),
                     AudioProcessorGraph::NodeID(OUTPUT));

    // NOTE: Plugin loading and connection routing is now handled by
    // the NodeGraphCanvas (MainWindowContent). Draw wires in the canvas
    // to route audio: Input → Plugin → Output.
}

PluginDescription IconMenu::getNextPluginOlderThanTime(int &time)
{
	int timeStatic = time;
	PluginDescription closest;
	int diff = INT_MAX;
	for (const auto& plugin : activePluginList.getTypes())
	{
		String key = getKey("order", plugin);
		String pluginTimeString = getAppProperties().getUserSettings()->getValue(key);
		int pluginTime = atoi(pluginTimeString.toStdString().c_str());
		if (pluginTime > timeStatic && abs(timeStatic - pluginTime) < diff)
		{
			diff = abs(timeStatic - pluginTime);
			closest = plugin;
			time = pluginTime;
		}
	}
	return closest;
}

void IconMenu::changeListenerCallback(ChangeBroadcaster* changed)
{
    if (changed == &knownPluginList)
    {
        std::unique_ptr<XmlElement> savedPluginList (knownPluginList.createXml());
        if (savedPluginList != nullptr)
        {
            getAppProperties().getUserSettings()->setValue ("pluginList", savedPluginList.get());
            getAppProperties().saveIfNeeded();
        }
    }
    else if (changed == &activePluginList)
    {
        std::unique_ptr<XmlElement> savedPluginList (activePluginList.createXml());
        if (savedPluginList != nullptr)
        {
            getAppProperties().getUserSettings()->setValue ("pluginListActive", savedPluginList.get());
            getAppProperties().saveIfNeeded();
        }
    }
}


void IconMenu::timerCallback()
{
    stopTimer();
    menu.clear();
    menu.addSectionHeader(LanguageManager::getInstance().getText("appName"));
    
    // Edit Plugins - simple menu item
    menu.addItem(2, LanguageManager::getInstance().getText("editPlugins"));
    menu.addItem(3, LanguageManager::getInstance().getText("showMain"));

    menu.addSeparator();
    
    // Quit
    menu.addItem(1, LanguageManager::getInstance().getText("quit"));
    
	menu.showMenuAsync(PopupMenu::Options().withMousePosition(), ModalCallbackFunction::forComponent(menuInvocationCallback, this));
}

void IconMenu::mouseDown(const MouseEvent& e)
{
    // Only show menu on right-click
    if (e.mods.isRightButtonDown())
    {
        Process::makeForegroundProcess();
        startTimer(50);
    }
}

void IconMenu::menuInvocationCallback(int id, IconMenu* im)
{
    // ID 1: Quit
    if (id == 1)
    {
        im->savePluginStates();
        return JUCEApplication::getInstance()->quit();
    }
    
    // ID 2: Edit Plugins (reload plugins)
    if (id == 2)
    {
        return im->reloadPlugins();
    }

    if (id == 3)
    {
        if (im->mainWindow == nullptr) {
            im->mainWindow = std::make_unique<MainWindow>(*im);
        } else {
            im->mainWindow->toFront(true);
        }
    }
}

std::vector<PluginDescription> IconMenu::getTimeSortedList()
{
	int time = 0;
	std::vector<PluginDescription> list;
	for (int i = 0; i < activePluginList.getNumTypes(); i++)
		list.push_back(getNextPluginOlderThanTime(time));
	return list;
		
}

String IconMenu::getKey(String type, PluginDescription plugin)
{
	String key = "plugin-" + type.toLowerCase() + "-" + plugin.name + plugin.version + plugin.pluginFormatName;
	return key;
}

void IconMenu::deletePluginStates()
{
	std::vector<PluginDescription> list = getTimeSortedList();
    for (int i = 0; i < activePluginList.getNumTypes(); i++)
    {
		String pluginUid = getKey("state", list[i]);
        getAppProperties().getUserSettings()->removeValue(pluginUid);
        getAppProperties().saveIfNeeded();
    }
}

void IconMenu::savePluginStates()
{
    // The graph now saves via nodeGraphState XML which includes plugin state info
    // This method iterates actual graph nodes and creates a plugin state backup
    // in case the node graph XML gets corrupted
    
    for (const auto* node : graph.getNodes())
    {
        if (node == nullptr || node->getProcessor() == nullptr)
            continue;
            
        auto* proc = node->getProcessor();
        bool isInputOrOutput = (dynamic_cast<AudioProcessorGraph::AudioGraphIOProcessor*>(proc) != nullptr);
        if (isInputOrOutput)
            continue;  // Skip input/output nodes
            
        // Try to find this processor in our known plugin list to get description
        for (const auto& desc : activePluginList.getTypes())
        {
            if (auto* pi = dynamic_cast<AudioPluginInstance*>(proc))
            {
                PluginDescription procDesc;
                pi->fillInPluginDescription(procDesc);
                if (procDesc.name == desc.name && procDesc.pluginFormatName == desc.pluginFormatName)
                {
                    String pluginUid = getKey("state", desc);
                    MemoryBlock savedStateBinary;
                    proc->getStateInformation(savedStateBinary);
                    getAppProperties().getUserSettings()->setValue(pluginUid, savedStateBinary.toBase64Encoding());
                    break;
                }
            }
        }
    }
    
    getAppProperties().saveIfNeeded();
}

void IconMenu::reloadPlugins()
{
	if (pluginListWindow == nullptr)
		pluginListWindow.reset (new PluginListWindow(*this, formatManager));
	pluginListWindow->toFront(true);
}

void IconMenu::removePluginsLackingInputOutput()
{
	// TODO needs sanity check
    for (const auto& plugin : knownPluginList.getTypes())
    {
        if (plugin.numInputChannels < 2 || plugin.numOutputChannels < 2)
		    knownPluginList.removeType(plugin);
    }
}
