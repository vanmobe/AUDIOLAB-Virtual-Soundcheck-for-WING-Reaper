/*
 * macOS Native Settings Dialog Implementation
 * Provides native Cocoa dialogs for editing AUDIOLAB.wing.reaper.virtualsoundcheck settings
 */

#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#include <string>
#include <cstring>
#include <cstdlib>

// Returns true if user confirmed, false if cancelled
// On success, ip_out contains the new value
extern "C" {

bool ShowSettingsDialog(const char* current_ip,
                       char* ip_out, int ip_out_size) {
    @autoreleasepool {
        // Create alert dialog
        NSAlert* alert = [[NSAlert alloc] init];
        [alert setMessageText:@"WINGuard Settings"];
        [alert setInformativeText:@"Guard every take. Faster setup, safer record(w)ing!\n\nEdit your WING console settings.\nRemember to enable OSC on the WING (Setup > Network > OSC)."];
        [alert setAlertStyle:NSAlertStyleInformational];
        
        // Create view for input fields
        NSView* accessoryView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 300, 50)];
        
        // IP Address label and text field
        NSTextField* ipLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 20, 80, 20)];
        [ipLabel setStringValue:@"Wing IP:"];
        [ipLabel setEditable:NO];
        [ipLabel setDrawsBackground:NO];
        [ipLabel setBordered:NO];
        [accessoryView addSubview:ipLabel];
        
        NSTextField* ipField = [[NSTextField alloc] initWithFrame:NSMakeRect(100, 16, 200, 24)];
        [ipField setStringValue:[NSString stringWithUTF8String:current_ip]];
        [accessoryView addSubview:ipField];
        
        [alert setAccessoryView:accessoryView];
        
        // Add buttons
        [alert addButtonWithTitle:@"OK"];
        [alert addButtonWithTitle:@"Cancel"];
        
        // Show dialog
        NSInteger result = [alert runModal];
        
        // Process result
        if (result == NSAlertFirstButtonReturn) {
            // User clicked OK
            const char* ip_str = [[ipField stringValue] UTF8String];
            
            if (ip_str) {
                strncpy(ip_out, ip_str, ip_out_size - 1);
                ip_out[ip_out_size - 1] = '\0';
                return true;
            }
        }
        
        return false;
    }
}

} // extern "C"

#endif
