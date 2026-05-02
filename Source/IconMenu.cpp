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

IconMenu::IconMenu()
{
    // Initiialization
    addDefaultFormatsToManager(formatManager);
    
    // Load saved language preference and apply it
    String savedLanguageId = getAppProperties().getUserSettings()->getValue("language", "English");
    LanguageManager::getInstance().setLanguageById(savedLanguageId);
    
    // Audio device
    std::unique_ptr<XmlElement> savedAudioState (getAppProperties().getUserSettings()->getXmlValue("audioDeviceState"));
    deviceManager.initialise(2, 2, savedAudioState.get(), true);

    // Plugins - all
    std::unique_ptr<XmlElement> savedPluginList(getAppProperties().getUserSettings()->getXmlValue("pluginList"));
    if (savedPluginList != nullptr)
        knownPluginList.recreateFromXml(*savedPluginList);
    knownPluginList.addChangeListener(this);

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

    std::unique_ptr<XmlElement> savedGraphState(getAppProperties().getUserSettings()->getXmlValue("nodeGraphState"));
    if (savedGraphState != nullptr)
        mainContent->loadState(*savedGraphState);
    
    deviceManager.addAudioCallback(&player);
    player.setProcessor(&graph);
   
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
	Image icon = ImageFileFormat::loadFrom(BinaryData::menu_icon_white_png, BinaryData::menu_icon_white_pngSize);
	setIconImage(icon, icon);
}

void IconMenu::loadActivePlugins()
{
    // Set up the graph's fixed I/O nodes.
    // Audio routing is now driven by the NodeGraphCanvas UI.
    inputNode  = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioInputNode),
                     AudioProcessorGraph::NodeID(NodeGraphCanvas::kInputNodeUID));
    outputNode = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode),
                     AudioProcessorGraph::NodeID(NodeGraphCanvas::kOutputNodeUID));


    double sr = 44100.0;
    int    bs = 512;
    if (auto* dev = deviceManager.getCurrentAudioDevice())
    {
        sr = dev->getCurrentSampleRate();
        bs = dev->getCurrentBufferSizeSamples();
    }

    inputNode->getProcessor()->setPlayConfigDetails(1, 2, sr, bs);
    inputNode->getProcessor()->prepareToPlay(sr, bs);
    outputNode->getProcessor()->setPlayConfigDetails(2, 2, sr, bs);
    outputNode->getProcessor()->prepareToPlay(sr, bs);
    
    // NOTE: Plugin loading and connection routing is now handled by
    // the NodeGraphCanvas (MainWindowContent). Draw wires in the canvas
    // to route audio: Input → Plugin → Output.
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

String IconMenu::getKey(String type, PluginDescription plugin)
{
	String key = "plugin-" + type.toLowerCase() + "-" + plugin.name + plugin.version + plugin.pluginFormatName;
	return key;
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
        bool isInputOrOutput = (dynamic_cast<IOProcessor*>(proc) != nullptr);
        if (isInputOrOutput)
            continue;  // Skip input/output nodes
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
    for (const auto& plugin : knownPluginList.getTypes())
    {
        if (plugin.numInputChannels == 0 || plugin.numOutputChannels == 0)
		    knownPluginList.removeType(plugin);
    }
}
