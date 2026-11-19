#include "PrinterWebViewAuth.hpp"

#include <string>

#include <wx/webview.h>

#import <Foundation/Foundation.h>
#import <WebKit/WebKit.h>
#include <objc/runtime.h>

namespace {

// The address of this variable keys the hook object a WKWebView keeps alive.
char printer_auth_hook_key;

std::string to_string(NSString* text) { return text != nil ? std::string([text UTF8String]) : std::string(); }

NSString* to_nsstring(const std::string& text)
{
    NSString* result = [NSString stringWithUTF8String:text.c_str()];
    return result != nil ? result : @"";
}

} // namespace

// Answers the printer's HTTP digest challenge and forwards every other navigation delegate message
// to wx's own delegate, which it wraps. WebKit reads which messages a delegate handles when it is
// set, so respondsToSelector: reports the wrapped delegate's messages too.
@interface TinmanPrinterAuthDelegate : NSObject <WKNavigationDelegate>
{
@public
    Slic3r::GUI::PrinterWebViewAuthState state;
@private
    id<WKNavigationDelegate> _wrapped; // retained
}
- (instancetype)initWithDelegate:(id<WKNavigationDelegate>)delegate;
- (id<WKNavigationDelegate>)wrappedDelegate;
@end

@implementation TinmanPrinterAuthDelegate

- (instancetype)initWithDelegate:(id<WKNavigationDelegate>)delegate
{
    if ((self = [super init]))
        _wrapped = [delegate retain];
    return self;
}

- (void)dealloc
{
    [_wrapped release];
    [super dealloc];
}

- (id<WKNavigationDelegate>)wrappedDelegate
{
    return _wrapped;
}

- (BOOL)respondsToSelector:(SEL)selector
{
    return [super respondsToSelector:selector] || [_wrapped respondsToSelector:selector];
}

- (id)forwardingTargetForSelector:(SEL)selector
{
    return [_wrapped respondsToSelector:selector] ? _wrapped : [super forwardingTargetForSelector:selector];
}

- (void)webView:(WKWebView*)webView
    didReceiveAuthenticationChallenge:(NSURLAuthenticationChallenge*)challenge
                    completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition, NSURLCredential*))completionHandler
{
    NSURLProtectionSpace* space = challenge.protectionSpace;
    Slic3r::GUI::PrinterWebViewChallenge request;
    request.host   = to_string(space.host);
    request.port   = static_cast<int>(space.port);
    request.digest = [space.authenticationMethod isEqualToString:NSURLAuthenticationMethodHTTPDigest];
    if (challenge.previousFailureCount > 0) {
        // The credential that failed; without one, assume it was this hook's.
        NSURLCredential* proposed = challenge.proposedCredential;
        request.rejected_these_credentials = proposed == nil || state.is(to_string(proposed.user), to_string(proposed.password));
    }

    switch (state.answer(request)) {
    case Slic3r::GUI::PrinterWebViewAnswer::Credentials:
        completionHandler(NSURLSessionAuthChallengeUseCredential,
                          [NSURLCredential credentialWithUser:to_nsstring(state.credentials().user)
                                                     password:to_nsstring(state.credentials().password)
                                                  persistence:NSURLCredentialPersistenceNone]);
        return;
    case Slic3r::GUI::PrinterWebViewAnswer::Rejected:
        // Default handling would send the rejected credential again at once, as fast as the
        // printer replies.
        completionHandler(NSURLSessionAuthChallengeCancelAuthenticationChallenge, nil);
        return;
    case Slic3r::GUI::PrinterWebViewAnswer::None: break;
    }
    if ([_wrapped respondsToSelector:_cmd]) {
        [_wrapped webView:webView didReceiveAuthenticationChallenge:challenge completionHandler:completionHandler];
        return;
    }
    completionHandler(NSURLSessionAuthChallengePerformDefaultHandling, nil);
}

@end

namespace Slic3r {
namespace GUI {

void set_printer_webview_credentials(wxWebView* web_view, const PrinterWebViewCredentials& credentials)
{
    WKWebView* backend = web_view != nullptr ? (WKWebView*) web_view->GetNativeBackend() : nil;
    if (backend == nil)
        return;

    id<WKNavigationDelegate> current = backend.navigationDelegate;
    TinmanPrinterAuthDelegate* hook = [(id) current isKindOfClass:[TinmanPrinterAuthDelegate class]] ?
                                          (TinmanPrinterAuthDelegate*) current :
                                          nil;
    if (credentials.empty()) {
        if (hook != nil) {
            // wx's delegate first: clearing the association releases the hook.
            backend.navigationDelegate = [hook wrappedDelegate];
            objc_setAssociatedObject(backend, &printer_auth_hook_key, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        }
        return;
    }

    if (hook == nil) {
        // navigationDelegate is a weak reference, so the web view keeps the hook alive. wx clears
        // the delegate before releasing its own, so the hook never forwards to a destroyed wx view.
        hook = [[TinmanPrinterAuthDelegate alloc] initWithDelegate:current];
        objc_setAssociatedObject(backend, &printer_auth_hook_key, hook, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        [hook release];
        backend.navigationDelegate = hook;
    }
    hook->state.set(credentials);
}

} // namespace GUI
} // namespace Slic3r
