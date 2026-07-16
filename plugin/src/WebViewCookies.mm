#include "EditorWebViewSetup.h"

#import <WebKit/WebKit.h>

namespace EditorWebViewSetup {

// JUCE's WebBrowserComponent on macOS is a WKWebView on the default (shared,
// persistent) WKWebsiteDataStore. juce::WebBrowserComponent::clearCookies()
// only touches NSHTTPCookieStorage, which WKWebView ignores — so the OAuth
// session survives it. This clears every kind of site data (cookies, local /
// session storage, IndexedDB, caches) but only for tone3000.com records, so
// the plugin UI's own origin (juce.backend) keeps its storage.
void clearAuthCookies() {
  WKWebsiteDataStore* store = [WKWebsiteDataStore defaultDataStore];
  NSSet<NSString*>* types = [WKWebsiteDataStore allWebsiteDataTypes];

  [store fetchDataRecordsOfTypes:types
               completionHandler:^(NSArray<WKWebsiteDataRecord*>* records) {
                 NSMutableArray<WKWebsiteDataRecord*>* matching = [NSMutableArray array];
                 for (WKWebsiteDataRecord* record in records) {
                   NSString* name = record.displayName;
                   if ([name isEqualToString:@"tone3000.com"] ||
                       [name hasSuffix:@".tone3000.com"]) {
                     [matching addObject:record];
                   }
                 }

                 if (matching.count == 0) {
                   juce::Logger::writeToLog("clearAuthCookies: no tone3000.com web data found");
                   return;
                 }

                 [store removeDataOfTypes:types
                           forDataRecords:matching
                        completionHandler:^{
                          juce::Logger::writeToLog(
                              "clearAuthCookies: cleared tone3000.com web session data");
                        }];
               }];
}

}  // namespace EditorWebViewSetup
