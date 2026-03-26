/*
 * macOS Native AUDIOLAB.wing.reaper.virtualsoundcheck Dialog Implementation
 * Provides consolidated dialog for all Wing operations
 */

#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#include "internal/wing_connector_dialog_macos.h"
#include "wingconnector/reaper_extension.h"
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

using namespace WingConnector;

// ===== CHANNEL SELECTION DIALOG =====

extern "C" {

bool ShowChannelSelectionDialog(std::vector<WingConnector::ChannelSelectionInfo>& channels,
                                const char* title,
                                const char* description,
                                bool& setup_soundcheck,
                                bool& overwrite_existing) {
    @autoreleasepool {
        NSAlert* alert = [[NSAlert alloc] init];
        [alert setMessageText:[NSString stringWithUTF8String:title]];
        [alert setInformativeText:[NSString stringWithUTF8String:description]];
        [alert setAlertStyle:NSAlertStyleInformational];
        
        // Calculate height needed for all channels
        int numChannels = (int)channels.size();
        int rowHeight = 24;
        int maxHeight = 400;
        int scrollHeight = std::min(numChannels * rowHeight + 20, maxHeight);
        
        // Create scrollable view for checkboxes
        NSScrollView* scrollView = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 500, scrollHeight)];
        [scrollView setHasVerticalScroller:YES];
        [scrollView setHasHorizontalScroller:NO];
        [scrollView setBorderType:NSBezelBorder];
        
        // Document view to hold all checkboxes
        NSView* documentView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 480, numChannels * rowHeight)];
        
        // Create checkbox array to track user selections
        NSMutableArray* checkboxes = [NSMutableArray arrayWithCapacity:numChannels];
        
        // Add checkbox for each channel
        int yPos = numChannels * rowHeight - rowHeight;
        for (int i = 0; i < numChannels; i++) {
            const auto& ch = channels[i];
            NSString* kindLabel = @"SRC";
            switch (ch.kind) {
                case SourceKind::Channel: kindLabel = @"CH"; break;
                case SourceKind::Bus: kindLabel = @"BUS"; break;
                case SourceKind::Matrix: kindLabel = @"MTX"; break;
            }
            const std::string display_name = ch.name.empty()
                ? (std::string([kindLabel UTF8String]) + " " + std::to_string(ch.source_number))
                : ch.name;
            
            // Create title showing channel info
            NSString* title = nil;
            if (ch.stereo_linked && !ch.partner_source_group.empty()) {
                title = [NSString stringWithFormat:@"%@%02d  %s  [%s%d / %s%d]%s",
                         kindLabel,
                         ch.source_number,
                         display_name.c_str(),
                         ch.source_group.c_str(),
                         ch.source_input,
                         ch.partner_source_group.c_str(),
                         ch.partner_source_input,
                         ch.soundcheck_capable ? "" : " [Record only]"];
            } else {
                title = [NSString stringWithFormat:@"%@%02d  %s  [%s%d]%s%s",
                         kindLabel,
                         ch.source_number,
                         display_name.c_str(),
                         ch.source_group.c_str(),
                         ch.source_input,
                         ch.stereo_linked ? " [Stereo]" : "",
                         ch.soundcheck_capable ? "" : " [Record only]"];
            }
            
            NSButton* checkbox = [[NSButton alloc] initWithFrame:NSMakeRect(10, yPos, 460, 20)];
            [checkbox setButtonType:NSButtonTypeSwitch];
            [checkbox setTitle:title];
            [checkbox setState:ch.selected ? NSControlStateValueOn : NSControlStateValueOff];
            
            [documentView addSubview:checkbox];
            [checkboxes addObject:checkbox];
            
            yPos -= rowHeight;
        }
        
        [scrollView setDocumentView:documentView];
        NSClipView* clipView = [scrollView contentView];
        NSRect clipBounds = [clipView bounds];
        NSRect documentBounds = [documentView bounds];
        CGFloat topOffset = std::max<CGFloat>(0.0, NSMaxY(documentBounds) - NSHeight(clipBounds));
        [clipView scrollToPoint:NSMakePoint(0, topOffset)];
        [scrollView reflectScrolledClipView:clipView];
        
        // Create container view for scroll view + options
        NSView* containerView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 500, scrollHeight + 70)];
        
        // Position scroll view at top of container
        [scrollView setFrameOrigin:NSMakePoint(0, 70)];
        [containerView addSubview:scrollView];
        
        // Add soundcheck mode checkbox at bottom
        NSButton* overwriteCheckbox = [[NSButton alloc] initWithFrame:NSMakeRect(10, 36, 480, 20)];
        [overwriteCheckbox setButtonType:NSButtonTypeSwitch];
        [overwriteCheckbox setTitle:@"Replace all existing REAPER tracks when applying this source selection"];
        [overwriteCheckbox setState:NSControlStateValueOn];
        [containerView addSubview:overwriteCheckbox];

        NSButton* soundcheckCheckbox = [[NSButton alloc] initWithFrame:NSMakeRect(10, 10, 480, 20)];
        [soundcheckCheckbox setButtonType:NSButtonTypeSwitch];
        [soundcheckCheckbox setTitle:@"Configure soundcheck mode for selected channels only (ALT + REAPER playback inputs)"];
        [soundcheckCheckbox setState:NSControlStateValueOn];  // Default to enabled
        [containerView addSubview:soundcheckCheckbox];
        
        [alert setAccessoryView:containerView];
        
        // Add buttons
        [alert addButtonWithTitle:@"OK"];
        [alert addButtonWithTitle:@"Cancel"];
        
        // Show dialog
        NSInteger result = [alert runModal];
        
        if (result == NSAlertFirstButtonReturn) {
            // OK clicked - update selection states
            for (int i = 0; i < numChannels; i++) {
                NSButton* checkbox = [checkboxes objectAtIndex:i];
                channels[i].selected = ([checkbox state] == NSControlStateValueOn);
            }
            // Update soundcheck mode option
            setup_soundcheck = ([soundcheckCheckbox state] == NSControlStateValueOn);
            overwrite_existing = ([overwriteCheckbox state] == NSControlStateValueOn);
            return true;
        }
        
        // Cancel clicked
        return false;
    }
}

} // extern "C"

// ===== MAIN WING CONNECTOR WINDOW =====

@interface WingConnectorFlippedView : NSView
@end

@implementation WingConnectorFlippedView
- (BOOL)isFlipped {
    return NO;
}
@end

@interface WingConnectorWindowController : NSWindowController <NSWindowDelegate>
{
    // UI Elements
    NSScrollView* mainScrollView;
    WingConnectorFlippedView* formContentView;
    NSWindow* debugLogWindow;
    NSPopUpButton* wingDropdown;
    NSTextField* manualIPField;
    NSButton* scanButton;
    NSMutableArray* discoveredIPs;
    NSTextField* statusLabel;
    NSButton* setupSoundcheckButton;
    NSButton* toggleSoundcheckButton;
    NSButton* connectButton;
    NSTextField* setupSoundcheckDescriptionLabel;
    NSTabView* settingsTabView;
    NSTextView* activityLogView;
    NSScrollView* logScrollView;
    NSButton* debugLogToggleButton;
    NSSegmentedControl* outputModeControl;
    NSSegmentedControl* midiActionsControl;
    NSSegmentedControl* autoRecordEnableControl;
    NSSegmentedControl* autoRecordModeControl;
    NSTextField* thresholdField;
    NSTextField* holdField;
    NSButton* applyAutomationButton;
    NSPopUpButton* monitorTrackDropdown;
    NSSegmentedControl* recorderTargetControl;
    NSPopUpButton* sdSourceDropdown;
    NSButton* sdRouteOnConnectCheckbox;
    NSButton* sdAutoRecordCheckbox;
    NSButton* oscOutEnableCheckbox;
    NSTextField* oscHostField;
    NSTextField* oscPortField;
    NSTextField* meterPreviewLabel;
    NSPopUpButton* ccLayerDropdown;
    NSTimer* meterPreviewTimer;
    
    BOOL isConnected;
    BOOL isWorking;  // Prevents re-entrant button clicks while an operation is in progress
    BOOL liveSetupValidated;  // True when Wing + REAPER routing validate as a complete live setup
    BOOL validationInProgress;  // Prevent overlapping auto-connect/validation runs
    CGFloat collapsedContentHeight;
    CGFloat expandedContentHeight;
    BOOL automationSettingsDirty;
}

- (instancetype)init;
- (void)setupUI;
- (void)updateConnectionStatus;
- (void)updateToggleSoundcheckButtonLabel;
- (void)updateSetupSoundcheckButtonLabel;
- (void)updateAutoTriggerControlsEnabled;
- (void)refreshLiveSetupValidation;
- (void)finalizeFormLayout;
- (void)adjustWindowHeightToFitContent;
- (void)updateFormLayoutForCurrentWindowSize;
- (void)appendToLog:(NSString*)message;
- (void)setWorkingState:(BOOL)working;
- (void)onDebugLogToggled:(id)sender;
- (void)windowDidResize:(NSNotification*)notification;
- (void)createDebugLogWindow;

- (void)startDiscoveryScan;
- (void)populateDropdownWithItems:(NSArray*)items ips:(NSArray*)ips;
- (void)onWingDropdownChanged:(id)sender;
- (void)onScanClicked:(id)sender;
- (void)onConnectClicked:(id)sender;
- (NSString*)selectedWingIP;
- (NSString*)selectedOrManualWingIP;
- (void)onManualIPChanged:(id)sender;

- (void)onSetupSoundcheckClicked:(id)sender;
- (void)onToggleSoundcheckClicked:(id)sender;
- (void)onOutputModeChanged:(id)sender;
- (void)onMidiActionsToggled:(id)sender;
- (void)onAutoRecordSettingsChanged:(id)sender;
- (void)onApplyAutomationSettingsClicked:(id)sender;
- (void)refreshMonitorTrackDropdown;
- (void)onMonitorTrackChanged:(id)sender;
- (void)persistConfigAndLog:(NSString*)message;
- (void)onMeterPreviewTimer:(NSTimer*)timer;
- (void)syncPendingAutomationSettingsFromUI;

- (void)runSetupSoundcheckFlow;
- (void)runToggleSoundcheckModeFlow;

@end

@implementation WingConnectorWindowController

- (instancetype)init {
    // Create the window with modern styling
    NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 700, 760)
                                                     styleMask:(NSWindowStyleMaskTitled |
                                                               NSWindowStyleMaskClosable |
                                                               NSWindowStyleMaskMiniaturizable |
                                                               NSWindowStyleMaskResizable)
                                                       backing:NSBackingStoreBuffered
                                                         defer:NO];
    [window setTitle:@"Behringer Wing"];
    [window setMinSize:NSMakeSize(700, 560)];
    [window center];
    
    self = [super initWithWindow:window];
    [window release];  // NSWindowController retains the window, release our creation reference
    if (!self) {
        return nil;
    }
    
    [window setDelegate:self];
    discoveredIPs = [[NSMutableArray alloc] init];  // Explicitly retain
    isConnected = NO;
    isWorking = NO;
    liveSetupValidated = NO;
    validationInProgress = NO;
    automationSettingsDirty = NO;
    meterPreviewTimer = nil;
    collapsedContentHeight = 760.0;
    expandedContentHeight = 760.0;
    
    // MUST call setupUI FIRST to initialize activityLogView!
    [self setupUI];
    [self updateConnectionStatus];
    
    // Set up log callback to capture C++ Log() calls
    auto log_lambda = [self](const std::string& msg) {
        NSString* nsMsg = [NSString stringWithUTF8String:msg.c_str()];
        dispatch_async(dispatch_get_main_queue(), ^{
            [self appendToLog:nsMsg];
        });
    };
    ReaperExtension::Instance().SetLogCallback(log_lambda);
    
    [self appendToLog:@"\nScanning network for Wing consoles...\n"];
    
    // Auto-scan for Wings on the network
    [self startDiscoveryScan];
    meterPreviewTimer = [[NSTimer scheduledTimerWithTimeInterval:0.5
                                                          target:self
                                                        selector:@selector(onMeterPreviewTimer:)
                                                        userInfo:nil
                                                         repeats:YES] retain];
    
    return self;
}

- (void)dealloc {
    [discoveredIPs release];
    // Release UI elements that we retain in instance variables
    [wingDropdown release];
    [mainScrollView release];
    [formContentView release];
    [debugLogWindow release];
    [manualIPField release];
    [scanButton release];
    [statusLabel release];
    [setupSoundcheckButton release];
    [toggleSoundcheckButton release];
    [connectButton release];
    [setupSoundcheckDescriptionLabel release];
    [settingsTabView release];
    [activityLogView release];
    [logScrollView release];
    [debugLogToggleButton release];
    [outputModeControl release];
    [midiActionsControl release];
    [autoRecordEnableControl release];
    [autoRecordModeControl release];
    [thresholdField release];
    [holdField release];
    [applyAutomationButton release];
    [monitorTrackDropdown release];
    [recorderTargetControl release];
    [sdSourceDropdown release];
    [sdRouteOnConnectCheckbox release];
    [sdAutoRecordCheckbox release];
    [oscOutEnableCheckbox release];
    [oscHostField release];
    [oscPortField release];
    [meterPreviewLabel release];
    [ccLayerDropdown release];
    [meterPreviewTimer invalidate];
    [meterPreviewTimer release];
    [super dealloc];
}

- (void)setupUI {
    NSView* windowContentView = [[self window] contentView];
    mainScrollView = [[NSScrollView alloc] initWithFrame:[windowContentView bounds]];
    [mainScrollView setHasVerticalScroller:YES];
    [mainScrollView setHasHorizontalScroller:NO];
    [mainScrollView setBorderType:NSNoBorder];
    [mainScrollView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    [windowContentView addSubview:mainScrollView];

    formContentView = [[WingConnectorFlippedView alloc] initWithFrame:NSMakeRect(0, 0, NSWidth([windowContentView bounds]), expandedContentHeight)];
    [formContentView setAutoresizingMask:NSViewWidthSizable];
    [mainScrollView setDocumentView:formContentView];

    NSView* contentView = formContentView;
    int yPos = (int)expandedContentHeight - 80;
    
    // ===== HEADER WITH LOGO =====
    NSBox* headerBox = [[NSBox alloc] initWithFrame:NSMakeRect(0, yPos - 10, 700, 70)];
    [headerBox setBoxType:NSBoxCustom];
    [headerBox setFillColor:[NSColor colorWithWhite:0.95 alpha:1.0]];
    [headerBox setBorderWidth:0];
    [contentView addSubview:headerBox];
    
    // App Icon
    NSImageView* iconView = [[NSImageView alloc] initWithFrame:NSMakeRect(20, yPos, 40, 40)];
    NSImage* appIcon = [NSImage imageNamed:NSImageNameApplicationIcon];
    [iconView setImage:appIcon];
    [contentView addSubview:iconView];
    
    // Title
    NSTextField* titleLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(70, yPos + 20, 400, 24)];
    [titleLabel setStringValue:@"Behringer Wing"];
    [titleLabel setFont:[NSFont systemFontOfSize:18 weight:NSFontWeightMedium]];
    [titleLabel setBezeled:NO];
    [titleLabel setEditable:NO];
    [titleLabel setSelectable:NO];
    [titleLabel setBackgroundColor:[NSColor clearColor]];
    [titleLabel setTextColor:[NSColor labelColor]];
    [contentView addSubview:titleLabel];
    
    // Subtitle
    NSTextField* subtitleLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(70, yPos, 400, 18)];
    [subtitleLabel setStringValue:@"Configure virtual soundcheck / recording"];
    [subtitleLabel setFont:[NSFont systemFontOfSize:12]];
    [subtitleLabel setBezeled:NO];
    [subtitleLabel setEditable:NO];
    [subtitleLabel setSelectable:NO];
    [subtitleLabel setBackgroundColor:[NSColor clearColor]];
    [subtitleLabel setTextColor:[NSColor secondaryLabelColor]];
    [contentView addSubview:subtitleLabel];
    
    yPos -= 85;
    auto& cfg = ReaperExtension::Instance().GetConfig();
    auto& ext = ReaperExtension::Instance();

    auto addInfoLabel = ^(NSView* parent, NSRect frame, NSString* text, CGFloat fontSize, NSColor* color) {
        NSTextField* label = [[NSTextField alloc] initWithFrame:frame];
        [label setStringValue:text];
        [label setFont:[NSFont systemFontOfSize:fontSize]];
        [label setBezeled:NO];
        [label setEditable:NO];
        [label setSelectable:NO];
        [label setBackgroundColor:[NSColor clearColor]];
        [label setTextColor:color];
        [label setLineBreakMode:NSLineBreakByWordWrapping];
        [label setUsesSingleLineMode:NO];
        [parent addSubview:label];
    };

    settingsTabView = [[NSTabView alloc] initWithFrame:NSMakeRect(20, 20, 660, 620)];
    [settingsTabView setTabViewType:NSTopTabsBezelBorder];
    [settingsTabView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    [contentView addSubview:settingsTabView];

    auto makeScrollableTab = ^NSScrollView* (WingConnectorFlippedView** documentViewOut, CGFloat documentHeight) {
        NSScrollView* scrollView = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 630, 580)];
        [scrollView setHasVerticalScroller:YES];
        [scrollView setHasHorizontalScroller:NO];
        [scrollView setBorderType:NSNoBorder];
        [scrollView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];

        WingConnectorFlippedView* documentView = [[WingConnectorFlippedView alloc] initWithFrame:NSMakeRect(0, 0, 620, documentHeight)];
        [documentView setAutoresizingMask:NSViewWidthSizable];
        [scrollView setDocumentView:documentView];

        if (documentViewOut) {
            *documentViewOut = documentView;
        }
        return scrollView;
    };

    WingConnectorFlippedView* setupTabView = nil;
    WingConnectorFlippedView* automationTabView = nil;
    WingConnectorFlippedView* advancedTabView = nil;
    NSScrollView* setupTabScrollView = makeScrollableTab(&setupTabView, 700.0);
    NSScrollView* automationTabScrollView = makeScrollableTab(&automationTabView, 980.0);
    NSScrollView* advancedTabScrollView = makeScrollableTab(&advancedTabView, 600.0);

    NSTabViewItem* setupItem = [[NSTabViewItem alloc] initWithIdentifier:@"setup"];
    [setupItem setLabel:@"Setup"];
    [setupItem setView:setupTabScrollView];
    [settingsTabView addTabViewItem:setupItem];

    NSTabViewItem* automationItem = [[NSTabViewItem alloc] initWithIdentifier:@"automation"];
    [automationItem setLabel:@"Automation"];
    [automationItem setView:automationTabScrollView];
    [settingsTabView addTabViewItem:automationItem];

    NSTabViewItem* advancedItem = [[NSTabViewItem alloc] initWithIdentifier:@"advanced"];
    [advancedItem setLabel:@"Advanced"];
    [advancedItem setView:advancedTabScrollView];
    [settingsTabView addTabViewItem:advancedItem];

    const CGFloat labelX = 20;
    const CGFloat controlX = 220;
    const CGFloat labelW = 180;
    const CGFloat setupWidth = 610;

    CGFloat setupY = 520;
    addInfoLabel(setupTabView, NSMakeRect(20, setupY, 590, 34),
                 @"Connect to a Wing, choose where recording channels go, and get live or soundcheck playback ready without cable gymnastics.",
                 12, [NSColor secondaryLabelColor]);
    setupY -= 44;

    NSTextField* setupConnectionHeader = [[NSTextField alloc] initWithFrame:NSMakeRect(20, setupY, 300, 20)];
    [setupConnectionHeader setStringValue:@"🌐 Connection"];
    [setupConnectionHeader setFont:[NSFont systemFontOfSize:13 weight:NSFontWeightSemibold]];
    [setupConnectionHeader setBezeled:NO];
    [setupConnectionHeader setEditable:NO];
    [setupConnectionHeader setSelectable:NO];
    [setupConnectionHeader setBackgroundColor:[NSColor clearColor]];
    [setupConnectionHeader setTextColor:[NSColor labelColor]];
    [setupTabView addSubview:setupConnectionHeader];
    setupY -= 32;
    addInfoLabel(setupTabView, NSMakeRect(20, setupY, 590, 30),
                 @"Use Scan to find a console on the network, or enter its IP manually if discovery comes back empty-handed.",
                 11, [NSColor secondaryLabelColor]);
    setupY -= 44;

    NSTextField* consoleLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(labelX, setupY + 4, 110, 20)];
    [consoleLabel setStringValue:@"Wing Console:"];
    [consoleLabel setFont:[NSFont systemFontOfSize:12]];
    [consoleLabel setBezeled:NO];
    [consoleLabel setEditable:NO];
    [consoleLabel setSelectable:NO];
    [consoleLabel setBackgroundColor:[NSColor clearColor]];
    [consoleLabel setTextColor:[NSColor secondaryLabelColor]];
    [setupTabView addSubview:consoleLabel];
    wingDropdown = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(130, setupY, 340, 28) pullsDown:NO];
    [wingDropdown addItemWithTitle:@"Scanning..."];
    [[wingDropdown itemAtIndex:0] setEnabled:NO];
    [wingDropdown setEnabled:NO];
    [wingDropdown setTarget:self];
    [wingDropdown setAction:@selector(onWingDropdownChanged:)];
    [setupTabView addSubview:wingDropdown];
    scanButton = [[NSButton alloc] initWithFrame:NSMakeRect(490, setupY + 2, 120, 28)];
    [scanButton setTitle:@"Scan"];
    [scanButton setBezelStyle:NSBezelStyleRounded];
    [scanButton setTarget:self];
    [scanButton setAction:@selector(onScanClicked:)];
    [setupTabView addSubview:scanButton];
    setupY -= 30;
    addInfoLabel(setupTabView, NSMakeRect(130, setupY, 430, 16),
                 @"Pick a discovered Wing to fill the connection details automatically.",
                 10, [NSColor tertiaryLabelColor]);
    setupY -= 28;

    NSTextField* manualIPLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(labelX, setupY + 4, 110, 20)];
    [manualIPLabel setStringValue:@"Manual IP:"];
    [manualIPLabel setFont:[NSFont systemFontOfSize:12]];
    [manualIPLabel setBezeled:NO];
    [manualIPLabel setEditable:NO];
    [manualIPLabel setSelectable:NO];
    [manualIPLabel setBackgroundColor:[NSColor clearColor]];
    [manualIPLabel setTextColor:[NSColor secondaryLabelColor]];
    [setupTabView addSubview:manualIPLabel];
    manualIPField = [[NSTextField alloc] initWithFrame:NSMakeRect(130, setupY, 260, 24)];
    if (!cfg.wing_ip.empty()) {
        [manualIPField setStringValue:[NSString stringWithUTF8String:cfg.wing_ip.c_str()]];
    }
    [manualIPField setPlaceholderString:@"Use this if scan does not find your Wing"];
    [manualIPField setFont:[NSFont systemFontOfSize:11]];
    [manualIPField setTarget:self];
    [manualIPField setAction:@selector(onManualIPChanged:)];
    [setupTabView addSubview:manualIPField];
    setupY -= 28;
    addInfoLabel(setupTabView, NSMakeRect(130, setupY, 420, 28),
                 @"If you already know the console IP, skip the scan and connect directly.",
                 10, [NSColor tertiaryLabelColor]);
    setupY -= 42;

    statusLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(20, setupY, 300, 26)];
    [statusLabel setStringValue:@"⚪ Not Connected"];
    [statusLabel setFont:[NSFont systemFontOfSize:13 weight:NSFontWeightMedium]];
    [statusLabel setBezeled:NO];
    [statusLabel setEditable:NO];
    [statusLabel setSelectable:NO];
    [statusLabel setBackgroundColor:[NSColor clearColor]];
    [statusLabel setTextColor:[NSColor labelColor]];
    [setupTabView addSubview:statusLabel];
    connectButton = [[NSButton alloc] initWithFrame:NSMakeRect(490, setupY - 2, 120, 28)];
    [connectButton setBezelStyle:NSBezelStyleRounded];
    [connectButton setTitle:@"Connect"];
    [connectButton setTarget:self];
    [connectButton setAction:@selector(onConnectClicked:)];
    [setupTabView addSubview:connectButton];
    setupY -= 46;

    NSBox* setupSeparator = [[NSBox alloc] initWithFrame:NSMakeRect(20, setupY, setupWidth, 1)];
    [setupSeparator setBoxType:NSBoxSeparator];
    [setupTabView addSubview:setupSeparator];
    setupY -= 28;

    NSTextField* routingHeader = [[NSTextField alloc] initWithFrame:NSMakeRect(20, setupY, 300, 20)];
    [routingHeader setStringValue:@"🎚 Recording and Soundcheck"];
    [routingHeader setFont:[NSFont systemFontOfSize:13 weight:NSFontWeightSemibold]];
    [routingHeader setBezeled:NO];
    [routingHeader setEditable:NO];
    [routingHeader setSelectable:NO];
    [routingHeader setBackgroundColor:[NSColor clearColor]];
    [routingHeader setTextColor:[NSColor labelColor]];
    [setupTabView addSubview:routingHeader];
    setupY -= 32;
    addInfoLabel(setupTabView, NSMakeRect(20, setupY, 590, 30),
                 @"Setup Live Recording can replace the current REAPER track list, rebuild the selected recording paths, and keep soundcheck switching limited to prepared channels.",
                 11, [NSColor secondaryLabelColor]);
    setupY -= 44;

    NSTextField* outputModeLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(labelX, setupY + 8, labelW, 20)];
    [outputModeLabel setStringValue:@"Recording I/O Mode:"];
    [outputModeLabel setFont:[NSFont systemFontOfSize:11]];
    [outputModeLabel setBezeled:NO];
    [outputModeLabel setEditable:NO];
    [outputModeLabel setSelectable:NO];
    [outputModeLabel setBackgroundColor:[NSColor clearColor]];
    [setupTabView addSubview:outputModeLabel];
    outputModeControl = [[NSSegmentedControl alloc] initWithFrame:NSMakeRect(controlX, setupY + 4, 120, 24)];
    [outputModeControl setSegmentCount:2];
    [outputModeControl setLabel:@"USB" forSegment:0];
    [outputModeControl setLabel:@"CARD" forSegment:1];
    [outputModeControl setSelectedSegment:(cfg.soundcheck_output_mode == "CARD") ? 1 : 0];
    [outputModeControl setSegmentStyle:NSSegmentStyleRounded];
    [outputModeControl setTarget:self];
    [outputModeControl setAction:@selector(onOutputModeChanged:)];
    [setupTabView addSubview:outputModeControl];
    setupY -= 28;
    addInfoLabel(setupTabView, NSMakeRect(controlX, setupY, 360, 28),
                 @"Choose where the Wing sends the recording channels. USB is the usual direct-to-computer path; CARD uses the Wing audio card route.",
                 10, [NSColor tertiaryLabelColor]);
    setupY -= 40;

    setupSoundcheckButton = [[NSButton alloc] initWithFrame:NSMakeRect(controlX, setupY, 220, 32)];
    [setupSoundcheckButton setBezelStyle:NSBezelStyleRounded];
    [setupSoundcheckButton setTitle:@"Setup Live Recording"];
    [setupSoundcheckButton setTarget:self];
    [setupSoundcheckButton setAction:@selector(onSetupSoundcheckClicked:)];
    [setupTabView addSubview:setupSoundcheckButton];
    setupSoundcheckDescriptionLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(labelX, setupY + 8, 250, 20)];
    [setupSoundcheckDescriptionLabel setStringValue:@"Replace the current REAPER track setup"];
    [setupSoundcheckDescriptionLabel setFont:[NSFont systemFontOfSize:11]];
    [setupSoundcheckDescriptionLabel setBezeled:NO];
    [setupSoundcheckDescriptionLabel setEditable:NO];
    [setupSoundcheckDescriptionLabel setSelectable:NO];
    [setupSoundcheckDescriptionLabel setBackgroundColor:[NSColor clearColor]];
    [setupSoundcheckDescriptionLabel setTextColor:[NSColor labelColor]];
    [setupTabView addSubview:setupSoundcheckDescriptionLabel];
    setupY -= 42;
    addInfoLabel(setupTabView, NSMakeRect(controlX, setupY, 390, 28),
                 @"Opens the source picker, optionally replaces the current REAPER tracks, updates routing, and rebuilds the live recording setup.",
                 10, [NSColor tertiaryLabelColor]);
    setupY -= 36;

    toggleSoundcheckButton = [[NSButton alloc] initWithFrame:NSMakeRect(controlX, setupY, 220, 32)];
    [toggleSoundcheckButton setBezelStyle:NSBezelStyleRounded];
    [toggleSoundcheckButton setTitle:@"🎙️ Live Mode"];
    [toggleSoundcheckButton setTarget:self];
    [toggleSoundcheckButton setAction:@selector(onToggleSoundcheckClicked:)];
    [toggleSoundcheckButton setEnabled:NO];
    [setupTabView addSubview:toggleSoundcheckButton];
    NSTextField* toggleDesc = [[NSTextField alloc] initWithFrame:NSMakeRect(labelX, setupY + 8, labelW, 20)];
    [toggleDesc setStringValue:@"Switch live/soundcheck:"];
    [toggleDesc setFont:[NSFont systemFontOfSize:11]];
    [toggleDesc setBezeled:NO];
    [toggleDesc setEditable:NO];
    [toggleDesc setSelectable:NO];
    [toggleDesc setBackgroundColor:[NSColor clearColor]];
    [setupTabView addSubview:toggleDesc];
    setupY -= 42;
    addInfoLabel(setupTabView, NSMakeRect(controlX, setupY, 360, 28),
                 @"After setup is validated, this flips prepared channels between live inputs and REAPER playback. One button, less panic.",
                 10, [NSColor tertiaryLabelColor]);
    setupY -= 38;

    CGFloat autoY = 720;
    addInfoLabel(automationTabView, NSMakeRect(20, autoY, 590, 34),
                 @"Automation watches a REAPER track, reacts to signal level, and can optionally bring the Wing recorder along for the ride.",
                 12, [NSColor secondaryLabelColor]);
    autoY -= 44;

    NSTextField* triggerHeader = [[NSTextField alloc] initWithFrame:NSMakeRect(20, autoY, 240, 20)];
    [triggerHeader setStringValue:@"⚡ Auto Trigger"];
    [triggerHeader setFont:[NSFont systemFontOfSize:13 weight:NSFontWeightSemibold]];
    [triggerHeader setBezeled:NO];
    [triggerHeader setEditable:NO];
    [triggerHeader setSelectable:NO];
    [triggerHeader setBackgroundColor:[NSColor clearColor]];
    [triggerHeader setTextColor:[NSColor labelColor]];
    [automationTabView addSubview:triggerHeader];
    autoY -= 32;
    addInfoLabel(automationTabView, NSMakeRect(20, autoY, 590, 30),
                 @"Trigger controls wake up after live setup validates, because they depend on the prepared recording path.",
                 11, [NSColor secondaryLabelColor]);
    autoY -= 44;

    NSTextField* trackLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(labelX, autoY + 8, labelW, 20)];
    [trackLabel setStringValue:@"Monitor Track:"];
    [trackLabel setFont:[NSFont systemFontOfSize:11]];
    [trackLabel setBezeled:NO];
    [trackLabel setEditable:NO];
    [trackLabel setSelectable:NO];
    [trackLabel setBackgroundColor:[NSColor clearColor]];
    [automationTabView addSubview:trackLabel];
    monitorTrackDropdown = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(controlX, autoY + 4, 220, 24) pullsDown:NO];
    [monitorTrackDropdown setTarget:self];
    [monitorTrackDropdown setAction:@selector(onMonitorTrackChanged:)];
    [automationTabView addSubview:monitorTrackDropdown];
    [self refreshMonitorTrackDropdown];
    autoY -= 28;
    addInfoLabel(automationTabView, NSMakeRect(controlX, autoY, 360, 16),
                 @"Choose which REAPER track is watched for trigger level. Auto watches all armed and monitored tracks.",
                 10, [NSColor tertiaryLabelColor]);
    autoY -= 32;

    NSTextField* autoEnableLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(labelX, autoY + 8, labelW, 20)];
    [autoEnableLabel setStringValue:@"Enable Trigger:"];
    [autoEnableLabel setFont:[NSFont systemFontOfSize:11]];
    [autoEnableLabel setBezeled:NO];
    [autoEnableLabel setEditable:NO];
    [autoEnableLabel setSelectable:NO];
    [autoEnableLabel setBackgroundColor:[NSColor clearColor]];
    [automationTabView addSubview:autoEnableLabel];
    autoRecordEnableControl = [[NSSegmentedControl alloc] initWithFrame:NSMakeRect(controlX, autoY + 4, 120, 24)];
    [autoRecordEnableControl setSegmentCount:2];
    [autoRecordEnableControl setLabel:@"OFF" forSegment:0];
    [autoRecordEnableControl setLabel:@"ON" forSegment:1];
    [autoRecordEnableControl setSelectedSegment:cfg.auto_record_enabled ? 1 : 0];
    [autoRecordEnableControl setTarget:self];
    [autoRecordEnableControl setAction:@selector(onAutoRecordSettingsChanged:)];
    [automationTabView addSubview:autoRecordEnableControl];
    autoY -= 28;
    addInfoLabel(automationTabView, NSMakeRect(controlX, autoY, 350, 16),
                 @"Turns signal-based trigger monitoring on or off.",
                 10, [NSColor tertiaryLabelColor]);
    autoY -= 32;

    NSTextField* modeLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(labelX, autoY + 8, labelW, 20)];
    [modeLabel setStringValue:@"Trigger Mode:"];
    [modeLabel setFont:[NSFont systemFontOfSize:11]];
    [modeLabel setBezeled:NO];
    [modeLabel setEditable:NO];
    [modeLabel setSelectable:NO];
    [modeLabel setBackgroundColor:[NSColor clearColor]];
    [automationTabView addSubview:modeLabel];
    autoRecordModeControl = [[NSSegmentedControl alloc] initWithFrame:NSMakeRect(controlX, autoY + 4, 220, 24)];
    [autoRecordModeControl setSegmentCount:2];
    [autoRecordModeControl setLabel:@"WARNING" forSegment:0];
    [autoRecordModeControl setLabel:@"RECORD" forSegment:1];
    [autoRecordModeControl setSelectedSegment:cfg.auto_record_warning_only ? 0 : 1];
    [autoRecordModeControl setTarget:self];
    [autoRecordModeControl setAction:@selector(onAutoRecordSettingsChanged:)];
    [automationTabView addSubview:autoRecordModeControl];
    autoY -= 28;
    addInfoLabel(automationTabView, NSMakeRect(controlX, autoY, 360, 28),
                 @"Warning flashes Wing controls when the trigger fires. Record also starts and stops recording automatically when the moment arrives.",
                 10, [NSColor tertiaryLabelColor]);
    autoY -= 40;

    NSTextField* thresholdLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(labelX, autoY + 8, labelW, 20)];
    [thresholdLabel setStringValue:@"Trigger Threshold:"];
    [thresholdLabel setFont:[NSFont systemFontOfSize:11]];
    [thresholdLabel setBezeled:NO];
    [thresholdLabel setEditable:NO];
    [thresholdLabel setSelectable:NO];
    [thresholdLabel setBackgroundColor:[NSColor clearColor]];
    [automationTabView addSubview:thresholdLabel];
    thresholdField = [[NSTextField alloc] initWithFrame:NSMakeRect(controlX, autoY + 4, 80, 24)];
    [thresholdField setStringValue:[NSString stringWithFormat:@"%.1f", cfg.auto_record_threshold_db]];
    [thresholdField setTarget:self];
    [thresholdField setAction:@selector(onAutoRecordSettingsChanged:)];
    [automationTabView addSubview:thresholdField];
    addInfoLabel(automationTabView, NSMakeRect(controlX + 90, autoY + 8, 50, 16),
                 @"dBFS", 10, [NSColor tertiaryLabelColor]);
    autoY -= 28;
    addInfoLabel(automationTabView, NSMakeRect(controlX, autoY, 360, 28),
                 @"Recording starts when the monitored signal rises above this level. Raise it to ignore low background noise and stage rustling.",
                 10, [NSColor tertiaryLabelColor]);
    autoY -= 40;

    NSTextField* holdLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(labelX, autoY + 8, labelW, 20)];
    [holdLabel setStringValue:@"Hold Time:"];
    [holdLabel setFont:[NSFont systemFontOfSize:11]];
    [holdLabel setBezeled:NO];
    [holdLabel setEditable:NO];
    [holdLabel setSelectable:NO];
    [holdLabel setBackgroundColor:[NSColor clearColor]];
    [automationTabView addSubview:holdLabel];
    holdField = [[NSTextField alloc] initWithFrame:NSMakeRect(controlX, autoY + 4, 80, 24)];
    [holdField setStringValue:[NSString stringWithFormat:@"%.1f", cfg.auto_record_hold_ms / 1000.0]];
    [holdField setTarget:self];
    [holdField setAction:@selector(onAutoRecordSettingsChanged:)];
    [automationTabView addSubview:holdField];
    addInfoLabel(automationTabView, NSMakeRect(controlX + 90, autoY + 8, 50, 16),
                 @"s", 10, [NSColor tertiaryLabelColor]);
    autoY -= 28;
    addInfoLabel(automationTabView, NSMakeRect(controlX, autoY, 360, 28),
                 @"Controls how long the signal may stay quiet before the trigger stops or resets. Longer times avoid nervous stop/start behavior.",
                 10, [NSColor tertiaryLabelColor]);
    autoY -= 44;

    meterPreviewLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(20, autoY + 8, 320, 20)];
    [meterPreviewLabel setStringValue:@"Trigger level: -- dBFS"];
    [meterPreviewLabel setFont:[NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular]];
    [meterPreviewLabel setBezeled:NO];
    [meterPreviewLabel setEditable:NO];
    [meterPreviewLabel setSelectable:NO];
    [meterPreviewLabel setBackgroundColor:[NSColor clearColor]];
    [automationTabView addSubview:meterPreviewLabel];
    addInfoLabel(automationTabView, NSMakeRect(350, autoY + 8, 240, 20),
                 @"Live meter readout for the current trigger source.",
                 10, [NSColor tertiaryLabelColor]);
    autoY -= 34;

    applyAutomationButton = [[NSButton alloc] initWithFrame:NSMakeRect(controlX, autoY, 220, 32)];
    [applyAutomationButton setBezelStyle:NSBezelStyleRounded];
    [applyAutomationButton setTitle:@"Apply Automation Settings"];
    [applyAutomationButton setTarget:self];
    [applyAutomationButton setAction:@selector(onApplyAutomationSettingsClicked:)];
    [applyAutomationButton setEnabled:NO];
    [automationTabView addSubview:applyAutomationButton];
    addInfoLabel(automationTabView, NSMakeRect(controlX + 230, autoY + 8, 230, 20),
                 @"Pending changes stay parked until you apply them.",
                 10, [NSColor tertiaryLabelColor]);
    autoY -= 28;
    addInfoLabel(automationTabView, NSMakeRect(controlX, autoY, 360, 28),
                 @"Changing trigger settings pauses automation immediately. Recording cannot auto-start again until these values are applied.",
                 10, [NSColor secondaryLabelColor]);
    autoY -= 40;

    NSBox* automationSeparator = [[NSBox alloc] initWithFrame:NSMakeRect(20, autoY, setupWidth, 1)];
    [automationSeparator setBoxType:NSBoxSeparator];
    [automationTabView addSubview:automationSeparator];
    autoY -= 28;

    NSTextField* recorderHeader = [[NSTextField alloc] initWithFrame:NSMakeRect(20, autoY, 240, 20)];
    [recorderHeader setStringValue:@"📼 Recorder Coordination"];
    [recorderHeader setFont:[NSFont systemFontOfSize:13 weight:NSFontWeightSemibold]];
    [recorderHeader setBezeled:NO];
    [recorderHeader setEditable:NO];
    [recorderHeader setSelectable:NO];
    [recorderHeader setBackgroundColor:[NSColor clearColor]];
    [recorderHeader setTextColor:[NSColor labelColor]];
    [automationTabView addSubview:recorderHeader];
    autoY -= 32;
    addInfoLabel(automationTabView, NSMakeRect(20, autoY, 590, 30),
                 @"These options decide which Wing recorder is prepared and whether it follows the auto-trigger or REAPER transport.",
                 11, [NSColor secondaryLabelColor]);
    autoY -= 44;

    NSTextField* recorderTargetLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(20, autoY + 8, 180, 20)];
    [recorderTargetLabel setStringValue:@"Recorder Target:"];
    [recorderTargetLabel setFont:[NSFont systemFontOfSize:11]];
    [recorderTargetLabel setBezeled:NO];
    [recorderTargetLabel setEditable:NO];
    [recorderTargetLabel setSelectable:NO];
    [recorderTargetLabel setBackgroundColor:[NSColor clearColor]];
    [automationTabView addSubview:recorderTargetLabel];
    recorderTargetControl = [[NSSegmentedControl alloc] initWithFrame:NSMakeRect(controlX, autoY + 4, 300, 24)];
    [recorderTargetControl setSegmentCount:2];
    [recorderTargetControl setLabel:@"SD (WING-LIVE)" forSegment:0];
    [recorderTargetControl setLabel:@"USB Recorder" forSegment:1];
    [recorderTargetControl setSelectedSegment:(cfg.recorder_target == "USBREC") ? 1 : 0];
    [recorderTargetControl setTarget:self];
    [recorderTargetControl setAction:@selector(onAutoRecordSettingsChanged:)];
    [automationTabView addSubview:recorderTargetControl];
    autoY -= 28;
    addInfoLabel(automationTabView, NSMakeRect(controlX, autoY, 360, 28),
                 @"Choose which recorder gets the red-light treatment when recorder automation is enabled.",
                 10, [NSColor tertiaryLabelColor]);
    autoY -= 36;

    sdRouteOnConnectCheckbox = [[NSButton alloc] initWithFrame:NSMakeRect(20, autoY + 4, 450, 20)];
    [sdRouteOnConnectCheckbox setButtonType:NSButtonTypeSwitch];
    [sdRouteOnConnectCheckbox setTitle:@"Route Main LR to recorder inputs 1/2 when connected"];
    [sdRouteOnConnectCheckbox setState:cfg.sd_lr_route_enabled ? NSControlStateValueOn : NSControlStateValueOff];
    [sdRouteOnConnectCheckbox setTarget:self];
    [sdRouteOnConnectCheckbox setAction:@selector(onAutoRecordSettingsChanged:)];
    [automationTabView addSubview:sdRouteOnConnectCheckbox];
    autoY -= 24;
    addInfoLabel(automationTabView, NSMakeRect(40, autoY, 540, 28),
                 @"Prepares the selected recorder path as soon as the plugin connects, so the main mix is ready to capture without extra menu diving.",
                 10, [NSColor tertiaryLabelColor]);
    autoY -= 34;

    sdAutoRecordCheckbox = [[NSButton alloc] initWithFrame:NSMakeRect(20, autoY + 4, 500, 20)];
    [sdAutoRecordCheckbox setButtonType:NSButtonTypeSwitch];
    [sdAutoRecordCheckbox setTitle:@"Start or stop the selected recorder with auto-trigger recordings"];
    [sdAutoRecordCheckbox setState:cfg.sd_auto_record_with_reaper ? NSControlStateValueOn : NSControlStateValueOff];
    [sdAutoRecordCheckbox setTarget:self];
    [sdAutoRecordCheckbox setAction:@selector(onAutoRecordSettingsChanged:)];
    [automationTabView addSubview:sdAutoRecordCheckbox];
    autoY -= 24;
    addInfoLabel(automationTabView, NSMakeRect(40, autoY, 540, 28),
                 @"When enabled, the chosen Wing recorder follows the plugin-controlled recording session instead of freelancing on its own.",
                 10, [NSColor tertiaryLabelColor]);
    autoY -= 34;

    NSTextField* sdSourceLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(20, autoY + 8, 180, 20)];
    [sdSourceLabel setStringValue:@"Recorder Source Pair:"];
    [sdSourceLabel setFont:[NSFont systemFontOfSize:11]];
    [sdSourceLabel setBezeled:NO];
    [sdSourceLabel setEditable:NO];
    [sdSourceLabel setSelectable:NO];
    [sdSourceLabel setBackgroundColor:[NSColor clearColor]];
    [automationTabView addSubview:sdSourceLabel];
    sdSourceDropdown = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(controlX, autoY + 4, 220, 24) pullsDown:NO];
    for (int i = 1; i <= 7; i += 2) {
        NSString* title = [NSString stringWithFormat:@"MAIN %d/%d", i, i + 1];
        [sdSourceDropdown addItemWithTitle:title];
        [[sdSourceDropdown itemAtIndex:((i - 1) / 2)] setTag:i];
    }
    int selectedLeft = std::max(1, cfg.sd_lr_left_input);
    int selectedIndex = std::max(0, std::min(3, (selectedLeft - 1) / 2));
    [sdSourceDropdown selectItemAtIndex:selectedIndex];
    [sdSourceDropdown setTarget:self];
    [sdSourceDropdown setAction:@selector(onAutoRecordSettingsChanged:)];
    [automationTabView addSubview:sdSourceDropdown];
    autoY -= 28;
    addInfoLabel(automationTabView, NSMakeRect(controlX, autoY, 360, 28),
                 @"Select which MAIN stereo pair is sent to the recorder when route-on-connect is enabled.",
                 10, [NSColor tertiaryLabelColor]);

    CGFloat advY = 520;
    addInfoLabel(advancedTabView, NSMakeRect(20, advY, 590, 34),
                 @"Advanced options add a few extra levers for REAPER control and make troubleshooting less mysterious.",
                 12, [NSColor secondaryLabelColor]);
    advY -= 44;

    NSTextField* midiHeader = [[NSTextField alloc] initWithFrame:NSMakeRect(20, advY, 240, 20)];
    [midiHeader setStringValue:@"🎛 Wing Control Integration"];
    [midiHeader setFont:[NSFont systemFontOfSize:13 weight:NSFontWeightSemibold]];
    [midiHeader setBezeled:NO];
    [midiHeader setEditable:NO];
    [midiHeader setSelectable:NO];
    [midiHeader setBackgroundColor:[NSColor clearColor]];
    [midiHeader setTextColor:[NSColor labelColor]];
    [advancedTabView addSubview:midiHeader];
    advY -= 32;
    addInfoLabel(advancedTabView, NSMakeRect(20, advY, 590, 30),
                 @"These options map Wing user controls to REAPER actions and warning feedback for operators who want hands on the console.",
                 11, [NSColor secondaryLabelColor]);
    advY -= 44;

    NSTextField* midiActionsLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(labelX, advY + 8, labelW, 20)];
    [midiActionsLabel setStringValue:@"MIDI Shortcuts:"];
    [midiActionsLabel setFont:[NSFont systemFontOfSize:11]];
    [midiActionsLabel setBezeled:NO];
    [midiActionsLabel setEditable:NO];
    [midiActionsLabel setSelectable:NO];
    [midiActionsLabel setBackgroundColor:[NSColor clearColor]];
    [advancedTabView addSubview:midiActionsLabel];
    BOOL midiFullyEnabled = ext.IsMidiActionsEnabled();
    midiActionsControl = [[NSSegmentedControl alloc] initWithFrame:NSMakeRect(controlX, advY + 4, 120, 24)];
    [midiActionsControl setSegmentCount:2];
    [midiActionsControl setLabel:@"OFF" forSegment:0];
    [midiActionsControl setLabel:@"ON" forSegment:1];
    [midiActionsControl setSelectedSegment:midiFullyEnabled ? 1 : 0];
    [midiActionsControl setSegmentStyle:NSSegmentStyleRounded];
    [midiActionsControl setTarget:self];
    [midiActionsControl setAction:@selector(onMidiActionsToggled:)];
    [midiActionsControl setEnabled:NO];
    [advancedTabView addSubview:midiActionsControl];
    advY -= 28;
    addInfoLabel(advancedTabView, NSMakeRect(controlX, advY, 360, 28),
                 @"Allows configured Wing buttons or user controls to trigger REAPER transport and related actions after live setup is validated.",
                 10, [NSColor tertiaryLabelColor]);
    advY -= 36;

    NSTextField* ccLayerLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(labelX, advY + 8, labelW, 20)];
    [ccLayerLabel setStringValue:@"Warning CC Layer:"];
    [ccLayerLabel setFont:[NSFont systemFontOfSize:11]];
    [ccLayerLabel setBezeled:NO];
    [ccLayerLabel setEditable:NO];
    [ccLayerLabel setSelectable:NO];
    [ccLayerLabel setBackgroundColor:[NSColor clearColor]];
    [advancedTabView addSubview:ccLayerLabel];
    ccLayerDropdown = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(controlX, advY + 4, 140, 24) pullsDown:NO];
    for (int i = 1; i <= 16; ++i) {
        NSString* title = [NSString stringWithFormat:@"Layer %d", i];
        [ccLayerDropdown addItemWithTitle:title];
        [[ccLayerDropdown itemAtIndex:i - 1] setTag:i];
    }
    int selectedLayer = std::min(16, std::max(1, cfg.warning_flash_cc_layer));
    [ccLayerDropdown selectItemAtIndex:selectedLayer - 1];
    [ccLayerDropdown setTarget:self];
    [ccLayerDropdown setAction:@selector(onAutoRecordSettingsChanged:)];
    [advancedTabView addSubview:ccLayerDropdown];
    advY -= 28;
    addInfoLabel(advancedTabView, NSMakeRect(controlX, advY, 360, 28),
                 @"Select which Wing user-control layer should flash or react when the auto-trigger enters warning mode.",
                 10, [NSColor tertiaryLabelColor]);
    advY -= 40;

    NSBox* advancedSeparator = [[NSBox alloc] initWithFrame:NSMakeRect(20, advY, setupWidth, 1)];
    [advancedSeparator setBoxType:NSBoxSeparator];
    [advancedTabView addSubview:advancedSeparator];
    advY -= 28;

    NSTextField* supportHeader = [[NSTextField alloc] initWithFrame:NSMakeRect(20, advY, 240, 20)];
    [supportHeader setStringValue:@"🛠 Support and Diagnostics"];
    [supportHeader setFont:[NSFont systemFontOfSize:13 weight:NSFontWeightSemibold]];
    [supportHeader setBezeled:NO];
    [supportHeader setEditable:NO];
    [supportHeader setSelectable:NO];
    [supportHeader setBackgroundColor:[NSColor clearColor]];
    [supportHeader setTextColor:[NSColor labelColor]];
    [advancedTabView addSubview:supportHeader];
    advY -= 32;
    addInfoLabel(advancedTabView, NSMakeRect(20, advY, 590, 30),
                 @"Use the debug log when things get weird, or when you simply want receipts for discovery, routing, validation, and recorder activity.",
                 11, [NSColor secondaryLabelColor]);
    advY -= 44;

    debugLogToggleButton = [[NSButton alloc] initWithFrame:NSMakeRect(controlX, advY, 180, 28)];
    [debugLogToggleButton setButtonType:NSButtonTypeMomentaryPushIn];
    [debugLogToggleButton setBezelStyle:NSBezelStyleRounded];
    [debugLogToggleButton setTitle:@"Open Debug Log"];
    [debugLogToggleButton setTarget:self];
    [debugLogToggleButton setAction:@selector(onDebugLogToggled:)];
    [advancedTabView addSubview:debugLogToggleButton];
    addInfoLabel(advancedTabView, NSMakeRect(labelX, advY + 4, labelW, 20),
                 @"Inspect plugin activity:" , 11, [NSColor labelColor]);
    advY -= 32;
    addInfoLabel(advancedTabView, NSMakeRect(controlX, advY, 360, 28),
                 @"Opens a live activity window with connection, validation, OSC, and routing messages for troubleshooting.",
                 10, [NSColor tertiaryLabelColor]);
    
    const int logHeight = 320;
    logScrollView = [[NSScrollView alloc] initWithFrame:NSMakeRect(20, 20, 660, logHeight)];
    [logScrollView setHasVerticalScroller:YES];
    [logScrollView setHasHorizontalScroller:NO];
    [logScrollView setBorderType:NSBezelBorder];
    [logScrollView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    [logScrollView setBackgroundColor:[NSColor textBackgroundColor]];
    
    activityLogView = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 640, logHeight)];
    [activityLogView setFont:[NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular]];
    [activityLogView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    [activityLogView setTextColor:[NSColor labelColor]];
    [activityLogView setBackgroundColor:[NSColor textBackgroundColor]];

    [logScrollView setDocumentView:activityLogView];

    auto normalizeTabDocument = ^(WingConnectorFlippedView* documentView, CGFloat minBottomPadding) {
        if (!documentView) {
            return;
        }

        CGFloat minY = CGFLOAT_MAX;
        CGFloat maxY = 0.0;
        for (NSView* subview in [documentView subviews]) {
            if ([subview isHidden]) {
                continue;
            }
            const NSRect frame = [subview frame];
            minY = std::min(minY, NSMinY(frame));
            maxY = std::max(maxY, NSMaxY(frame));
        }

        if (minY == CGFLOAT_MAX) {
            return;
        }

        if (minY < minBottomPadding) {
            const CGFloat shift = minBottomPadding - minY;
            for (NSView* subview in [documentView subviews]) {
                NSRect frame = [subview frame];
                frame.origin.y += shift;
                [subview setFrame:frame];
            }
            maxY += shift;
        }

        NSRect documentFrame = [documentView frame];
        documentFrame.size.height = std::max(documentFrame.size.height, maxY + 24.0);
        [documentView setFrame:documentFrame];
    };
    normalizeTabDocument(setupTabView, 26.0);
    normalizeTabDocument(automationTabView, 26.0);
    normalizeTabDocument(advancedTabView, 26.0);

    auto scrollTabToTop = ^(NSScrollView* scrollView) {
        NSClipView* clipView = [scrollView contentView];
        NSView* documentView = [scrollView documentView];
        if (!clipView || !documentView) {
            return;
        }
        NSRect clipBounds = [clipView bounds];
        NSRect documentBounds = [documentView bounds];
        CGFloat topOffset = std::max<CGFloat>(0.0, NSMaxY(documentBounds) - NSHeight(clipBounds));
        [clipView scrollToPoint:NSMakePoint(0, topOffset)];
        [scrollView reflectScrolledClipView:clipView];
    };
    scrollTabToTop(setupTabScrollView);
    scrollTabToTop(automationTabScrollView);
    scrollTabToTop(advancedTabScrollView);

    [self createDebugLogWindow];
    [self finalizeFormLayout];
}

- (void)updateConnectionStatus {
    isConnected = ReaperExtension::Instance().IsConnected();
    [self refreshMonitorTrackDropdown];
    
    if (isConnected) {
        [statusLabel setStringValue:@"🟢 Connected"];
        [connectButton setTitle:@"Disconnect"];
    } else {
        [statusLabel setStringValue:@"⚪ Not Connected"];
        [connectButton setTitle:@"Connect"];
    }
    // Re-enable buttons only if no operation is currently running
    if (!isWorking) {
        [setupSoundcheckButton setEnabled:YES];
        [scanButton setEnabled:YES];
        [connectButton setEnabled:YES];
        // Toggle button handled in updateToggleSoundcheckButtonLabel
    }
    [self updateToggleSoundcheckButtonLabel];
    [self updateSetupSoundcheckButtonLabel];
    [self updateAutoTriggerControlsEnabled];
    [self refreshLiveSetupValidation];
}

- (void)refreshMonitorTrackDropdown {
    auto& config = ReaperExtension::Instance().GetConfig();
    [monitorTrackDropdown removeAllItems];

    [monitorTrackDropdown addItemWithTitle:@"Auto (Armed+Monitored)"];
    [[monitorTrackDropdown itemAtIndex:0] setTag:0];

    const int track_count = ReaperExtension::Instance().GetProjectTrackCount();
    for (int i = 1; i <= track_count; ++i) {
        NSString* title = [NSString stringWithFormat:@"Track %d", i];
        [monitorTrackDropdown addItemWithTitle:title];
        [[monitorTrackDropdown itemAtIndex:i] setTag:i];
    }

    int wanted = std::max(0, config.auto_record_monitor_track);
    if (wanted > track_count) {
        wanted = 0;
        config.auto_record_monitor_track = 0;
    }
    [monitorTrackDropdown selectItemAtIndex:wanted];
}

- (void)onMonitorTrackChanged:(id)sender {
    (void)sender;
    auto& config = ReaperExtension::Instance().GetConfig();
    NSMenuItem* item = [monitorTrackDropdown selectedItem];
    config.auto_record_monitor_track = item ? (int)[item tag] : 0;
    [self onAutoRecordSettingsChanged:sender];
}

- (void)updateToggleSoundcheckButtonLabel {
    auto& extension = ReaperExtension::Instance();
    bool enabled = extension.IsSoundcheckModeEnabled();
    if (enabled) {
        [toggleSoundcheckButton setTitle:@"🎧 Soundcheck Mode"];
    } else {
        [toggleSoundcheckButton setTitle:@"🎙️ Live Mode"];
    }
    
    // Only enable when live setup validates against Wing + REAPER state.
    if (liveSetupValidated && !isWorking) {
        [toggleSoundcheckButton setEnabled:YES];
    } else {
        [toggleSoundcheckButton setEnabled:NO];
    }
}

- (void)updateSetupSoundcheckButtonLabel {
    auto& extension = ReaperExtension::Instance();
    const bool existing_patching_detected = liveSetupValidated || extension.IsSoundcheckModeEnabled();
    if (existing_patching_detected) {
        [setupSoundcheckButton setTitle:@"Setup Live Recording"];
        [setupSoundcheckDescriptionLabel setStringValue:@"Can replace the current REAPER tracks and routing"];
        [setupSoundcheckDescriptionLabel setTextColor:[NSColor systemOrangeColor]];
    } else {
        [setupSoundcheckButton setTitle:@"Setup Live Recording"];
        [setupSoundcheckDescriptionLabel setStringValue:@"Build the REAPER track setup for the current selection"];
        [setupSoundcheckDescriptionLabel setTextColor:[NSColor secondaryLabelColor]];
    }
}

- (void)updateAutoTriggerControlsEnabled {
    const BOOL liveSetupControlsEnabled = (liveSetupValidated && !isWorking) ? YES : NO;
    const BOOL sdControlsEnabled = isWorking ? NO : YES;
    [midiActionsControl setEnabled:liveSetupControlsEnabled];
    [monitorTrackDropdown setEnabled:liveSetupControlsEnabled];
    [autoRecordEnableControl setEnabled:liveSetupControlsEnabled];
    [autoRecordModeControl setEnabled:liveSetupControlsEnabled];
    [thresholdField setEnabled:liveSetupControlsEnabled];
    [holdField setEnabled:liveSetupControlsEnabled];
    [ccLayerDropdown setEnabled:liveSetupControlsEnabled];
    [applyAutomationButton setEnabled:isWorking ? NO : YES];
    [recorderTargetControl setEnabled:sdControlsEnabled];
    [sdRouteOnConnectCheckbox setEnabled:sdControlsEnabled];
    [sdAutoRecordCheckbox setEnabled:sdControlsEnabled];
    [sdSourceDropdown setEnabled:sdControlsEnabled];
}

- (void)refreshLiveSetupValidation {
    // Never block the UI thread with network/OSC queries.
    if (validationInProgress || isWorking) {
        return;
    }

    auto& extension = ReaperExtension::Instance();
    auto& config = extension.GetConfig();

    std::string candidate_ip;
    NSString* selectedIP = [self selectedOrManualWingIP];
    if (selectedIP && [selectedIP length] > 0) {
        candidate_ip = std::string([selectedIP UTF8String]);
    } else if (!config.wing_ip.empty()) {
        candidate_ip = config.wing_ip;
    }

    if (!extension.IsConnected() && candidate_ip.empty()) {
        liveSetupValidated = NO;
        [self updateToggleSoundcheckButtonLabel];
        [self updateSetupSoundcheckButtonLabel];
        [self updateAutoTriggerControlsEnabled];
        [self appendToLog:@"Live setup validation: NOT READY — no Wing IP available to validate against.\n"];
        return;
    }

    validationInProgress = YES;
    WingConnectorWindowController* blockSelf = self;
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        bool connected_now = extension.IsConnected();
        std::string details;

        // Only validate against Wing when already connected. Explicit connect/disconnect
        // is controlled by the UI button to keep behavior predictable.
        if (!connected_now) {
            config.wing_ip = candidate_ip;
            details = "Not connected to Wing. Connect first to validate setup.";
            dispatch_async(dispatch_get_main_queue(), ^{
                blockSelf->validationInProgress = NO;
                blockSelf->isConnected = NO;
                [blockSelf->statusLabel setStringValue:@"⚪ Not Connected"];
                    blockSelf->liveSetupValidated = NO;
                    [blockSelf updateToggleSoundcheckButtonLabel];
                    [blockSelf updateSetupSoundcheckButtonLabel];
                    [blockSelf updateAutoTriggerControlsEnabled];
                    [blockSelf appendToLog:[NSString stringWithFormat:@"Live setup validation: NOT READY — %s\n",
                                            details.c_str()]];
                });
            return;
        }

        bool valid = extension.ValidateLiveRecordingSetup(details);
        dispatch_async(dispatch_get_main_queue(), ^{
            blockSelf->validationInProgress = NO;
            // Avoid changing state while another operation is in progress.
            if (blockSelf->isWorking) {
                return;
            }
            blockSelf->isConnected = extension.IsConnected() ? YES : NO;
            [blockSelf->statusLabel setStringValue:blockSelf->isConnected ? @"🟢 Connected" : @"⚪ Not Connected"];
            blockSelf->liveSetupValidated = valid ? YES : NO;
            [blockSelf updateToggleSoundcheckButtonLabel];
            [blockSelf updateSetupSoundcheckButtonLabel];
            [blockSelf updateAutoTriggerControlsEnabled];
            [blockSelf appendToLog:[NSString stringWithFormat:@"Live setup validation: %s — %s\n",
                                    valid ? "READY" : "NOT READY",
                                    details.c_str()]];
        });
    });
}

- (void)setWorkingState:(BOOL)working {
    isWorking = working;
    [setupSoundcheckButton setEnabled:!working];
    [scanButton setEnabled:!working];
    [connectButton setEnabled:!working];
    // Toggle button state depends on both working state and setup completion
    [self updateToggleSoundcheckButtonLabel];
    [self updateSetupSoundcheckButtonLabel];
    [self updateAutoTriggerControlsEnabled];
}

- (void)appendToLog:(NSString*)message {
    if (!message) {
        return;
    }
    NSString* cleaned = [message stringByReplacingOccurrencesOfString:@"AUDIOLAB.wing.reaper.virtualsoundcheck: "
                                                           withString:@""];
    cleaned = [cleaned stringByReplacingOccurrencesOfString:@"AUDIOLAB.wing.reaper.virtualsoundcheck:"
                                                 withString:@""];
    NSString* currentText = [activityLogView string];
    NSString* newText = [currentText stringByAppendingString:cleaned];
    [activityLogView setString:newText];
    
    // Scroll to bottom
    NSRange range = NSMakeRange([[activityLogView string] length], 0);
    [activityLogView scrollRangeToVisible:range];
}

// ===== WING DISCOVERY =====

- (void)startDiscoveryScan {
    [wingDropdown removeAllItems];
    [wingDropdown addItemWithTitle:@"Scanning..."];
    [[wingDropdown itemAtIndex:0] setEnabled:NO];
    [wingDropdown setEnabled:NO];
    [scanButton setTitle:@"Scanning..."];
    [scanButton setEnabled:NO];
    
    WingConnectorWindowController* blockSelf = self;
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        auto wings = ReaperExtension::Instance().DiscoverWings(1500);
        NSMutableArray* items = [NSMutableArray array];
        NSMutableArray* ips   = [NSMutableArray array];
        for (const auto& w : wings) {
            NSString* label;
            if (!w.name.empty() && !w.model.empty()) {
                label = [NSString stringWithFormat:@"%s \u2013 %s  (%s)",
                         w.name.c_str(), w.model.c_str(), w.console_ip.c_str()];
            } else if (!w.name.empty()) {
                label = [NSString stringWithFormat:@"%s  (%s)",
                         w.name.c_str(), w.console_ip.c_str()];
            } else if (!w.model.empty()) {
                label = [NSString stringWithFormat:@"%s  (%s)",
                         w.model.c_str(), w.console_ip.c_str()];
            } else {
                label = [NSString stringWithUTF8String:w.console_ip.c_str()];
            }
            [items addObject:label];
            [ips   addObject:[NSString stringWithUTF8String:w.console_ip.c_str()]];
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            [blockSelf populateDropdownWithItems:items ips:ips];
            // Only re-enable scan button if no other operation is running
            if (!blockSelf->isWorking) {
                [blockSelf->scanButton setTitle:@"Scan"];
                [blockSelf->scanButton setEnabled:YES];
            }
        });
    });
}

- (void)populateDropdownWithItems:(NSArray*)items ips:(NSArray*)ips {
    [discoveredIPs release];
    discoveredIPs = [[NSMutableArray arrayWithArray:ips] retain];
    [wingDropdown removeAllItems];
    if ([items count] == 0) {
        [wingDropdown addItemWithTitle:@"No Wings found — press Scan"];
        [[wingDropdown itemAtIndex:0] setEnabled:NO];
        [wingDropdown setEnabled:NO];
        [self appendToLog:@"\u2717 No Wing consoles found on the network. Is the Wing powered on and reachable?\n"];
    } else {
        [wingDropdown setEnabled:YES];
        for (NSString* title in items) {
            [wingDropdown addItemWithTitle:title];
        }
        [wingDropdown selectItemAtIndex:0];
        // Immediately apply the first found Wing IP to config
        auto& config = ReaperExtension::Instance().GetConfig();
        config.wing_ip = std::string([[ips objectAtIndex:0] UTF8String]);
        [manualIPField setStringValue:[ips objectAtIndex:0]];
        [self appendToLog:[NSString stringWithFormat:@"Found %d Wing console(s):\n", (int)[items count]]];
        for (NSString* title in items) {
            [self appendToLog:[NSString stringWithFormat:@"  \u2022 %@\n", title]];
        }
        [self refreshLiveSetupValidation];
    }
}

- (void)onWingDropdownChanged:(id)sender {
    NSInteger idx = [wingDropdown indexOfSelectedItem];
    if (discoveredIPs && idx >= 0 && idx < (NSInteger)[discoveredIPs count]) {
        auto& config = ReaperExtension::Instance().GetConfig();
        config.wing_ip = std::string([[discoveredIPs objectAtIndex:idx] UTF8String]);
        [manualIPField setStringValue:[discoveredIPs objectAtIndex:idx]];
        [self appendToLog:[NSString stringWithFormat:@"Selected Wing: %@\n",
                          [wingDropdown titleOfSelectedItem]]];
    }
}

- (void)onScanClicked:(id)sender {
    [self appendToLog:@"\n=== Re-scanning for Wing consoles ===\n"];
    [self startDiscoveryScan];
}

- (void)onConnectClicked:(id)sender {
    if (isWorking || validationInProgress) return;

    WingConnectorWindowController* blockSelf = self;
    [self setWorkingState:YES];

    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        auto& extension = ReaperExtension::Instance();
        auto& config = extension.GetConfig();

        if (extension.IsConnected()) {
            extension.DisconnectFromWing();
            dispatch_async(dispatch_get_main_queue(), ^{
                blockSelf->liveSetupValidated = NO;
                [blockSelf appendToLog:@"Disconnected from Wing.\n"];
                [blockSelf setWorkingState:NO];
                [blockSelf updateConnectionStatus];
            });
            return;
        }

        NSString* wingIP = [blockSelf selectedOrManualWingIP];
        if (wingIP && [wingIP length] > 0) {
            config.wing_ip = std::string([wingIP UTF8String]);
        }

        if (config.wing_ip.empty()) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [blockSelf appendToLog:@"✗ No Wing selected. Press Scan and choose a console first.\n"];
                [blockSelf setWorkingState:NO];
                [blockSelf updateConnectionStatus];
            });
            return;
        }

        if (!extension.ConnectToWing()) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [blockSelf appendToLog:[NSString stringWithFormat:@"✗ Connection failed to %s.\n",
                                        config.wing_ip.c_str()]];
                [blockSelf setWorkingState:NO];
                [blockSelf updateConnectionStatus];
            });
            return;
        }

        dispatch_async(dispatch_get_main_queue(), ^{
            [blockSelf appendToLog:[NSString stringWithFormat:@"✓ Connected to %s\n",
                                    config.wing_ip.c_str()]];
            [blockSelf setWorkingState:NO];
            [blockSelf updateConnectionStatus];
        });
    });
}

- (NSString*)selectedWingIP {
    if (!wingDropdown || !discoveredIPs) {
        return nil;
    }
    NSInteger idx = [wingDropdown indexOfSelectedItem];
    if (idx >= 0 && idx < (NSInteger)[discoveredIPs count]) {
        return [discoveredIPs objectAtIndex:idx];
    }
    return nil;
}

- (NSString*)selectedOrManualWingIP {
    NSString* selected = [self selectedWingIP];
    if (selected && [selected length] > 0) {
        return selected;
    }
    if (manualIPField) {
        NSString* typed = [[manualIPField stringValue]
            stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
        if ([typed length] > 0) {
            return typed;
        }
    }
    return nil;
}

- (void)onManualIPChanged:(id)sender {
    (void)sender;
    if (!manualIPField) {
        return;
    }
    NSString* typed = [[manualIPField stringValue]
        stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    auto& config = ReaperExtension::Instance().GetConfig();
    config.wing_ip = ([typed length] > 0) ? std::string([typed UTF8String]) : std::string();
    if ([typed length] > 0) {
        [self appendToLog:[NSString stringWithFormat:@"Manual Wing IP set to %@\n", typed]];
    }
    [self persistConfigAndLog:nil];
    [self refreshLiveSetupValidation];
}

- (void)onSetupSoundcheckClicked:(id)sender {
    if (isWorking) return;  // Prevent re-entrant clicks
    auto& extension = ReaperExtension::Instance();

    // Update output mode from UI
    auto& config = extension.GetConfig();
    config.soundcheck_output_mode = ([outputModeControl selectedSegment] == 0) ? "USB" : "CARD";
    
    [self appendToLog:[NSString stringWithFormat:@"\n=== Setting up Live Recording (%s mode, replacing prior REAPER tracks if requested) ===\n",
                      config.soundcheck_output_mode.c_str()]];
    [self setWorkingState:YES];
    [self runSetupSoundcheckFlow];
}

- (void)onToggleSoundcheckClicked:(id)sender {
    if (isWorking) return;  // Prevent re-entrant clicks
    [self appendToLog:@"\n=== Toggling Soundcheck Mode ===\n"];
    [self setWorkingState:YES];
    [self runToggleSoundcheckModeFlow];
}

- (void)onOutputModeChanged:(id)sender {
    auto& extension = ReaperExtension::Instance();
    auto& config = extension.GetConfig();
    config.soundcheck_output_mode = ([outputModeControl selectedSegment] == 0) ? "USB" : "CARD";

    std::string details;
    bool fullyAvailable = extension.CheckOutputModeAvailability(config.soundcheck_output_mode, details);
    [self appendToLog:[NSString stringWithFormat:@"Output mode selected: %s\n", config.soundcheck_output_mode.c_str()]];
    [self appendToLog:[NSString stringWithFormat:@"%s\n", details.c_str()]];

    if (!fullyAvailable) {
        NSAlert* alert = [[NSAlert alloc] init];
        [alert setMessageText:@"Selected mode may not be fully available"];
        [alert setInformativeText:[NSString stringWithUTF8String:details.c_str()]];
        [alert addButtonWithTitle:@"OK"];
        [alert runModal];
    }

    [self persistConfigAndLog:@"Saved output mode setting.\n"];
}

- (void)onMidiActionsToggled:(id)sender {
    if (!liveSetupValidated) {
        [midiActionsControl setSelectedSegment:0];
        [self appendToLog:@"Configure live setup first before enabling MIDI shortcuts.\n"];
        return;
    }

    auto& extension = ReaperExtension::Instance();
    BOOL enabled = ([midiActionsControl selectedSegment] == 1);
    
    if (enabled) {
        extension.EnableMidiActions(true);
        [self appendToLog:@"✓ MIDI actions enabled - Wing buttons now control REAPER\n"];
    } else {
        extension.EnableMidiActions(false);
        [self appendToLog:@"MIDI actions disabled\n"];
    }
    [self persistConfigAndLog:@"Saved MIDI action setting.\n"];
}

- (void)persistConfigAndLog:(NSString*)message {
    auto& config = ReaperExtension::Instance().GetConfig();
    const std::string path = WingConfig::GetConfigPath();
    if (config.SaveToFile(path)) {
        if (message) {
            [self appendToLog:message];
        }
    } else {
        [self appendToLog:@"⚠ Failed to save config.json\n"];
    }
}

- (void)onDebugLogToggled:(id)sender {
    (void)sender;
    if (!debugLogWindow) {
        [self createDebugLogWindow];
    }
    [debugLogWindow makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (void)windowDidResize:(NSNotification*)notification {
    (void)notification;
    [self updateFormLayoutForCurrentWindowSize];
}

- (void)finalizeFormLayout {
    if (!formContentView) {
        return;
    }

    CGFloat minY = CGFLOAT_MAX;
    CGFloat maxY = 0.0;
    for (NSView* subview in [formContentView subviews]) {
        if ([subview isHidden]) {
            continue;
        }
        const NSRect frame = [subview frame];
        minY = std::min(minY, NSMinY(frame));
        maxY = std::max(maxY, NSMaxY(frame));
    }

    if (minY == CGFLOAT_MAX) {
        expandedContentHeight = collapsedContentHeight;
        [self updateFormLayoutForCurrentWindowSize];
        return;
    }

    const CGFloat desiredBottomPadding = 24.0;
    if (minY < desiredBottomPadding) {
        const CGFloat shift = desiredBottomPadding - minY;
        for (NSView* subview in [formContentView subviews]) {
            NSRect frame = [subview frame];
            frame.origin.y += shift;
            [subview setFrame:frame];
        }
        maxY += shift;
    }

    const CGFloat desiredTopPadding = 20.0;
    expandedContentHeight = std::max(collapsedContentHeight, maxY + desiredTopPadding);
    [self adjustWindowHeightToFitContent];
    [self updateFormLayoutForCurrentWindowSize];
}

- (void)adjustWindowHeightToFitContent {
    NSWindow* window = [self window];
    if (!window) {
        return;
    }

    NSRect currentFrame = [window frame];
    NSRect currentContentRect = [window contentRectForFrameRect:currentFrame];
    const CGFloat desiredContentHeight = expandedContentHeight;
    if (currentContentRect.size.height >= desiredContentHeight) {
        return;
    }

    NSScreen* screen = [window screen];
    if (!screen) {
        screen = [NSScreen mainScreen];
    }
    if (!screen) {
        return;
    }

    const NSRect visibleFrame = [screen visibleFrame];
    NSRect maxContentRect = [window contentRectForFrameRect:visibleFrame];
    const CGFloat maxContentHeight = maxContentRect.size.height;
    const CGFloat targetContentHeight = std::min(desiredContentHeight, maxContentHeight);
    if (targetContentHeight <= currentContentRect.size.height) {
        return;
    }

    const CGFloat delta = targetContentHeight - currentContentRect.size.height;
    currentFrame.origin.y -= delta;
    currentFrame.size.height += delta;
    [window setFrame:currentFrame display:NO];
}

- (void)updateFormLayoutForCurrentWindowSize {
    if (!mainScrollView || !formContentView) {
        return;
    }
    NSSize clipSize = [[mainScrollView contentView] bounds].size;
    const CGFloat targetHeight = std::max(expandedContentHeight, clipSize.height);
    [formContentView setFrame:NSMakeRect(0, 0, std::max(clipSize.width, (CGFloat)700.0), targetHeight)];
    NSClipView* clipView = [mainScrollView contentView];
    CGFloat topOriginY = std::max(0.0, targetHeight - NSHeight([clipView bounds]));
    [clipView scrollToPoint:NSMakePoint(0, topOriginY)];
    [mainScrollView reflectScrolledClipView:clipView];
}

- (void)createDebugLogWindow {
    if (debugLogWindow) {
        return;
    }
    debugLogWindow = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 700, 360)
                                                 styleMask:(NSWindowStyleMaskTitled |
                                                           NSWindowStyleMaskClosable |
                                                           NSWindowStyleMaskMiniaturizable |
                                                           NSWindowStyleMaskResizable)
                                                   backing:NSBackingStoreBuffered
                                                     defer:NO];
    [debugLogWindow setTitle:@"Behringer Wing Debug Log"];
    [debugLogWindow setMinSize:NSMakeSize(520, 220)];

    NSView* logContentView = [debugLogWindow contentView];
    [logScrollView setFrame:[logContentView bounds]];
    [logScrollView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    [logContentView addSubview:logScrollView];
}

- (void)onAutoRecordSettingsChanged:(id)sender {
    auto& extension = ReaperExtension::Instance();
    auto& config = extension.GetConfig();
    (void)sender;
    [self syncPendingAutomationSettingsFromUI];
    extension.PauseAutoRecordForSetup();
    automationSettingsDirty = YES;
    [self updateAutoTriggerControlsEnabled];
    NSString* recorderLabel = ([recorderTargetControl selectedSegment] == 1)
        ? @"USB recorder"
        : @"SD card (WING-LIVE)";
    [self appendToLog:[NSString stringWithFormat:@"Automation settings pending: %s, mode=%s, source=REAPER, recorder=%@, threshold=%.1f dBFS, hold=%.1fs, track=%d, ccLayer=%d\n",
                       config.auto_record_enabled ? "ON" : "OFF",
                       config.auto_record_warning_only ? "WARNING" : "RECORD",
                       recorderLabel,
                       config.auto_record_threshold_db,
                       config.auto_record_hold_ms / 1000.0,
                       config.auto_record_monitor_track,
                       config.warning_flash_cc_layer]];
    [self appendToLog:@"Auto-trigger paused. Apply the automation settings to make them active.\n"];
}

- (void)onApplyAutomationSettingsClicked:(id)sender {
    (void)sender;
    auto& extension = ReaperExtension::Instance();
    auto& config = extension.GetConfig();
    [self syncPendingAutomationSettingsFromUI];
    extension.ApplyAutoRecordSettings();
    extension.SyncMidiActionsToWing();

    NSString* recorderLabel = ([recorderTargetControl selectedSegment] == 1)
        ? @"USB recorder"
        : @"SD card (WING-LIVE)";
    if (extension.IsConnected()) {
        if (config.sd_lr_route_enabled) {
            extension.ApplyRecorderRoutingNoDialog();
            [self appendToLog:[NSString stringWithFormat:@"Requested Main LR routing to %@ 1/2 (verify on WING).\n",
                               recorderLabel]];
        } else {
            [self appendToLog:[NSString stringWithFormat:@"%@ route-on-connect disabled. Existing WING routing was not restored automatically.\n",
                               recorderLabel]];
        }
    }

    automationSettingsDirty = NO;
    [self updateAutoTriggerControlsEnabled];
    [self appendToLog:[NSString stringWithFormat:@"Applied automation settings: %s, mode=%s, source=REAPER, recorder=%@, threshold=%.1f dBFS, hold=%.1fs, track=%d, ccLayer=%d\n",
                       config.auto_record_enabled ? "ON" : "OFF",
                       config.auto_record_warning_only ? "WARNING" : "RECORD",
                       recorderLabel,
                       config.auto_record_threshold_db,
                       config.auto_record_hold_ms / 1000.0,
                       config.auto_record_monitor_track,
                       config.warning_flash_cc_layer]];
    [self persistConfigAndLog:@"Saved and applied automation settings.\n"];
}

- (void)syncPendingAutomationSettingsFromUI {
    auto& extension = ReaperExtension::Instance();
    auto& config = extension.GetConfig();
    config.auto_record_enabled = ([autoRecordEnableControl selectedSegment] == 1);
    config.auto_record_warning_only = ([autoRecordModeControl selectedSegment] == 0);
    config.auto_record_threshold_db = [[thresholdField stringValue] doubleValue];
    const double hold_seconds = std::max(0.0, [[holdField stringValue] doubleValue]);
    config.auto_record_hold_ms = (int)std::lround(hold_seconds * 1000.0);
    NSMenuItem* selectedTrackItem = [monitorTrackDropdown selectedItem];
    config.auto_record_monitor_track = selectedTrackItem ? (int)[selectedTrackItem tag] : 0;
    NSMenuItem* selectedLayerItem = [ccLayerDropdown selectedItem];
    config.warning_flash_cc_layer = selectedLayerItem ? (int)[selectedLayerItem tag] : 1;
    config.sd_lr_route_enabled = ([sdRouteOnConnectCheckbox state] == NSControlStateValueOn);
    config.sd_auto_record_with_reaper = ([sdAutoRecordCheckbox state] == NSControlStateValueOn);
    config.recorder_target = ([recorderTargetControl selectedSegment] == 1) ? "USBREC" : "WLIVE";
    NSMenuItem* sdItem = [sdSourceDropdown selectedItem];
    int sdLeft = sdItem ? (int)[sdItem tag] : 1;
    config.sd_lr_group = "MAIN";
    config.sd_lr_left_input = sdLeft;
    config.sd_lr_right_input = sdLeft + 1;
}

- (void)onMeterPreviewTimer:(NSTimer*)timer {
    (void)timer;
    auto& extension = ReaperExtension::Instance();
    double lin = extension.ReadCurrentTriggerLevel();
    if (lin <= 0.0000001) {
        [meterPreviewLabel setStringValue:@"Trigger level: -inf dBFS"];
        return;
    }
    double db = 20.0 * std::log10(lin);
    [meterPreviewLabel setStringValue:[NSString stringWithFormat:@"Trigger level: %.1f dBFS", db]];
}

- (void)runSetupSoundcheckFlow {
    // Capture UI values on the main thread before dispatching
    NSString* wingIP = [self selectedOrManualWingIP];
    WingConnectorWindowController* blockSelf = self;

    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        auto& extension = ReaperExtension::Instance();

        // Prevent auto-record from starting transport while setup is being configured.
        extension.PauseAutoRecordForSetup();
        dispatch_async(dispatch_get_main_queue(), ^{
            [blockSelf appendToLog:@"Auto-trigger paused during setup configuration.\n"];
        });

        if (!extension.IsConnected()) {
            dispatch_async(dispatch_get_main_queue(), ^{ [blockSelf appendToLog:@"Not connected — attempting to connect automatically...\n"]; });
            if (!wingIP) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    [blockSelf appendToLog:@"✗ No Wing selected. Press Scan or enter a manual IP.\n"];
                    [blockSelf setWorkingState:NO];
                    [blockSelf updateConnectionStatus];
                });
                return;
            }
            auto& config = extension.GetConfig();
            config.wing_ip = std::string([wingIP UTF8String]);
            if (!extension.ConnectToWing()) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    [blockSelf appendToLog:@"✗ Auto-connect failed. Check that the Wing is reachable.\n"];
                    [blockSelf setWorkingState:NO];
                    [blockSelf updateConnectionStatus];
                });
                return;
            }
            dispatch_async(dispatch_get_main_queue(), ^{
                [blockSelf appendToLog:@"✓ Auto-connected to Wing\n"];
                [blockSelf updateConnectionStatus];
            });
        }

        dispatch_async(dispatch_get_main_queue(), ^{ [blockSelf appendToLog:@"Getting Wing sources for live recording setup...\n"]; });
        auto channels = extension.GetAvailableSources();
        
        if (channels.empty()) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [blockSelf appendToLog:@"✗ No selectable sources found.\n"];
                [blockSelf setWorkingState:NO];
            });
            return;
        }

        __block auto blockChannels = channels;
        __block bool confirmed = false;
        __block bool setup_soundcheck = true;  // Will be set by dialog
        __block bool overwrite_existing = true;
        dispatch_sync(dispatch_get_main_queue(), ^{
            [blockSelf appendToLog:[NSString stringWithFormat:@"Found %d selectable sources\n", (int)blockChannels.size()]];
            confirmed = ShowChannelSelectionDialog(
                blockChannels,
                "Select Sources for Recording Setup",
                "Choose which channels, buses, or matrices to configure. Soundcheck applies only to channels.",
                setup_soundcheck,
                overwrite_existing
            );
        });

        if (!confirmed) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [blockSelf appendToLog:@"Cancelled by user\n"];
                [blockSelf setWorkingState:NO];
            });
            return;
        }

        int selectedCount = 0;
        for (const auto& ch : blockChannels) {
            if (ch.selected) selectedCount++;
        }

        if (selectedCount == 0) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [blockSelf appendToLog:@"✗ No sources selected\n"];
                [blockSelf setWorkingState:NO];
            });
            return;
        }

        dispatch_sync(dispatch_get_main_queue(), ^{
            [blockSelf appendToLog:[NSString stringWithFormat:@"Setting up live recording for %d sources (%s existing REAPER tracks)...\n",
                                   selectedCount,
                                   overwrite_existing ? "replacing" : "appending to"]];
            extension.SetupSoundcheckFromSelection(blockChannels, setup_soundcheck, overwrite_existing);
            [blockSelf appendToLog:@"✓ Live recording setup complete\n"];
            [blockSelf refreshMonitorTrackDropdown];
            [blockSelf setWorkingState:NO];
            [blockSelf refreshLiveSetupValidation];
        });
    });
}

- (void)runToggleSoundcheckModeFlow {
    // Capture UI values on the main thread before dispatching
    NSString* wingIP = [self selectedOrManualWingIP];
    WingConnectorWindowController* blockSelf = self;

    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        auto& extension = ReaperExtension::Instance();

        if (!extension.IsConnected()) {
            dispatch_async(dispatch_get_main_queue(), ^{ [blockSelf appendToLog:@"Not connected — attempting to connect automatically...\n"]; });
            if (!wingIP) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    [blockSelf appendToLog:@"✗ No Wing selected. Press Scan or enter a manual IP.\n"];
                    [blockSelf setWorkingState:NO];
                    [blockSelf updateConnectionStatus];
                });
                return;
            }
            auto& config = extension.GetConfig();
            config.wing_ip = std::string([wingIP UTF8String]);
            if (!extension.ConnectToWing()) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    [blockSelf appendToLog:@"✗ Auto-connect failed. Check that the Wing is reachable.\n"];
                    [blockSelf setWorkingState:NO];
                    [blockSelf updateConnectionStatus];
                });
                return;
            }
            dispatch_async(dispatch_get_main_queue(), ^{
                [blockSelf appendToLog:@"✓ Auto-connected to Wing\n"];
                [blockSelf updateConnectionStatus];
            });
        }

        // CRITICAL: ToggleSoundcheckMode() shows message boxes, which MUST run on main thread
        dispatch_async(dispatch_get_main_queue(), ^{
            [blockSelf appendToLog:@"Toggling soundcheck mode...\n"];
            
            extension.ToggleSoundcheckMode();
            bool enabled = extension.IsSoundcheckModeEnabled();
            
            if (enabled) {
                [blockSelf appendToLog:@"✓ Soundcheck mode ENABLED (using playback inputs)\n"];
            } else {
                [blockSelf appendToLog:@"✓ Soundcheck mode DISABLED (using live inputs)\n"];
            }
            [blockSelf updateToggleSoundcheckButtonLabel];
            [blockSelf setWorkingState:NO];
            [blockSelf refreshLiveSetupValidation];
        });
    });
}

@end

extern "C" {

void ShowWingConnectorDialog() {
    // Static to keep controller alive in MRC - window doesn't retain it by default
    static WingConnectorWindowController* controller = nil;
    
    // Must run UI operations on main thread
    dispatch_async(dispatch_get_main_queue(), ^{
        // NO @autoreleasepool here - it would drain objects that need to live longer
        // If window already exists and is visible, just bring it to front
        if (controller && [[controller window] isVisible]) {
            [[controller window] makeKeyAndOrderFront:nil];
            return;
        }
        
        // Create new controller (retained by static variable)
        if (controller) {
            [controller release];
        }
        controller = [[WingConnectorWindowController alloc] init];

        [[controller window] makeKeyAndOrderFront:nil];
    });
}

} // extern "C"

#endif // __APPLE__
