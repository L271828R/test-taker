#import <WebKit/WebKit.h>
#include "inspector.h"

void EnableWebInspector(wxWebView* webView) {
    WKWebView* wk = (WKWebView*)webView->GetNativeBackend();
    if (!wk) return;
    [wk.configuration.preferences setValue:@YES forKey:@"developerExtrasEnabled"];
}
