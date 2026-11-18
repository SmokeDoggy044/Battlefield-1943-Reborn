// The news window's embedded YouTube player: see news_video.h.

#include "news_video.h"

#include <rex/logging.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <WebView2.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <string>
#endif

namespace bf1943 {
std::filesystem::path ExecutableDir();  // shader_prep.cpp
}

namespace bf1943::news {

#if defined(_WIN32)
namespace {

constexpr const wchar_t* kHost = L"news.bf1943reborn.example";
constexpr const wchar_t* kPageBase = L"https://news.bf1943reborn.example/";

// The player page: YouTube's IFrame Player API (the official embed), privacy enhanced host, autoplay with the sound
// asked for; bfMute(on) from the game. Its address gives the embed its referrer (YouTube's error 153 without one).
constexpr const char* kPlayerPage = R"page(<!doctype html>
<html><head><meta charset="utf-8"><meta name="referrer" content="strict-origin-when-cross-origin">
<style>html,body{margin:0;width:100%;height:100%;background:#000;overflow:hidden}#p,iframe{border:0;width:100%;height:100%;display:block}</style>
</head><body><div id="p"></div>
<script>
var q = new URLSearchParams(location.search), id = q.get('v') || '', muted = q.get('mute') === '1', player = null;
function onYouTubeIframeAPIReady() {
  if (!/^[A-Za-z0-9_-]{11}$/.test(id)) return;
  player = new YT.Player('p', {
    width: '100%', height: '100%', videoId: id, host: 'https://www.youtube-nocookie.com',
    playerVars: {autoplay: 1, mute: muted ? 1 : 0, playsinline: 1, rel: 0, origin: location.origin},
    events: {
      onReady: function (e) { if (muted) e.target.mute(); else e.target.unMute(); e.target.playVideo(); say('ready'); },
      onStateChange: function (e) { say('state ' + e.data); },
      onError: function (e) { say('error ' + e.data); }
    }
  });
}
// The player's state for the game's log (1 playing, 2 paused, 0 ended, 3 buffering; errors as YouTube numbers them).
function say(what) {
  try { window.chrome.webview.postMessage(what + (player && player.isMuted ? (player.isMuted() ? ' muted' : ' sound on') : '')); } catch (e) {}
}
function bfMute(on) { muted = on; if (player && player.mute) { if (on) player.mute(); else player.unMute(); } }
// No internet, or YouTube out of reach: the player's API never comes (the game shows its own message instead).
var started = false;
setTimeout(function () { if (!started) say('error offline'); }, 15000);
</script>
<script src="https://www.youtube.com/iframe_api" onload="started = true" onerror="say('error offline')"></script>
</body></html>
)page";

std::wstring Wide(const std::string& s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
  std::wstring w(size_t(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
  return w;
}
std::string Narrow(const wchar_t* w) {
  if (!w || !*w) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  std::string s(size_t(std::max(0, n - 1)), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
  return s;
}
LPWSTR Dup(const std::wstring& s) {
  auto* p = static_cast<LPWSTR>(CoTaskMemAlloc((s.size() + 1) * sizeof(wchar_t)));
  if (p) std::memcpy(p, s.c_str(), (s.size() + 1) * sizeof(wchar_t));
  return p;
}

// A COM callback from a function (the SDK's own helper needs WRL).
template <class I, class... A>
class Handler final : public I {
 public:
  explicit Handler(std::function<HRESULT(A...)> fn) : fn_(std::move(fn)) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
    if (riid == __uuidof(IUnknown) || riid == __uuidof(I)) {
      *out = static_cast<I*>(this);
      AddRef();
      return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG r = --ref_;
    if (!r) delete this;
    return r;
  }
  HRESULT STDMETHODCALLTYPE Invoke(A... a) override { return fn_ ? fn_(a...) : S_OK; }

 private:
  std::function<HRESULT(A...)> fn_;
  std::atomic<ULONG> ref_{1};
};

// The environment's options: the autoplay rule and the runtime this header was written for.
class Options final : public ICoreWebView2EnvironmentOptions {
 public:
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
    if (riid == __uuidof(IUnknown) || riid == __uuidof(ICoreWebView2EnvironmentOptions)) {
      *out = static_cast<ICoreWebView2EnvironmentOptions*>(this);
      AddRef();
      return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG r = --ref_;
    if (!r) delete this;
    return r;
  }
  HRESULT STDMETHODCALLTYPE get_AdditionalBrowserArguments(LPWSTR* v) override { return Out(v, args_); }
  HRESULT STDMETHODCALLTYPE put_AdditionalBrowserArguments(LPCWSTR v) override { return In(args_, v); }
  HRESULT STDMETHODCALLTYPE get_Language(LPWSTR* v) override { return Out(v, language_); }
  HRESULT STDMETHODCALLTYPE put_Language(LPCWSTR v) override { return In(language_, v); }
  HRESULT STDMETHODCALLTYPE get_TargetCompatibleBrowserVersion(LPWSTR* v) override { return Out(v, target_); }
  HRESULT STDMETHODCALLTYPE put_TargetCompatibleBrowserVersion(LPCWSTR v) override { return In(target_, v); }
  HRESULT STDMETHODCALLTYPE get_AllowSingleSignOnUsingOSPrimaryAccount(BOOL* v) override {
    if (!v) return E_POINTER;
    *v = FALSE;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE put_AllowSingleSignOnUsingOSPrimaryAccount(BOOL) override { return S_OK; }

 private:
  static HRESULT Out(LPWSTR* v, const std::wstring& s) {
    if (!v) return E_POINTER;
    *v = Dup(s);
    return *v ? S_OK : E_OUTOFMEMORY;
  }
  static HRESULT In(std::wstring& s, LPCWSTR v) {
    s = v ? v : L"";
    return S_OK;
  }
  std::atomic<ULONG> ref_{1};
  std::wstring args_ = L"--autoplay-policy=no-user-gesture-required";
  std::wstring language_;
  std::wstring target_ = L"135.0.3179.45";  // the SDK's (1.0.3179.45) CORE_WEBVIEW_TARGET_PRODUCT_VERSION
};

using CreateEnvFn = HRESULT(STDAPICALLTYPE*)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*,
                                             ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
using VersionFn = HRESULT(STDAPICALLTYPE*)(PCWSTR, LPWSTR*);

struct State {
  bool checked = false, failed = false;
  HMODULE loader = nullptr;
  CreateEnvFn create_env = nullptr;
  ICoreWebView2Environment* env = nullptr;
  bool env_pending = false;
  ICoreWebView2Controller* ctl = nullptr;
  ICoreWebView2* view = nullptr;
  bool ctl_pending = false;
  HWND parent = nullptr;
  bool want = false;           // the menu wants the player this frame (VideoShow since the last VideoHide)
  std::string id, shown_id;    // wanted, navigated
  RECT rect{}, shown_rect{};
  bool muted = false, shown_muted = false;
  bool visible = false;
  bool ended = false;          // the player said it ended, paused or failed (the menu music may play again)
  bool broken = false;         // the player said it failed (no internet, or YouTube refused the video)
  bool escape = false, refocus = false;
  std::filesystem::path page_dir;
  std::filesystem::path data_dir;
};
State g;

void Fail(const char* what, HRESULT hr) {
  REXLOG_WARN("BF news video: {} (0x{:08X}): the thumbnail instead for this run", what, unsigned(hr));
  g.failed = true;
}

void OpenOutside(const wchar_t* uri) {
  const std::string u = Narrow(uri);
  if (u.rfind("https://", 0) != 0) return;
  REXLOG_INFO("BF news video: {} opens in the browser", u);
  ShellExecuteW(nullptr, L"open", uri, nullptr, nullptr, SW_SHOWNORMAL);
}

void ReleaseView() {
  if (g.ctl) {
    g.ctl->Close();
    g.ctl->Release();
  }
  if (g.view) g.view->Release();
  g.ctl = nullptr, g.view = nullptr, g.visible = false, g.broken = false, g.shown_id.clear(), g.shown_rect = {};
}

void Navigate() {
  if (!g.view) return;
  const std::wstring url = std::wstring(kPageBase) + L"player.html?v=" + Wide(g.id) + L"&mute=" + (g.muted ? L"1" : L"0");
  const HRESULT hr = g.view->Navigate(url.c_str());
  g.shown_id = g.id, g.shown_muted = g.muted, g.ended = false, g.broken = false;
  REXLOG_INFO("BF news video: playing {} ({}{})", g.id, g.muted ? "muted" : "with sound", SUCCEEDED(hr) ? "" : ", the navigation failed");
}

void SetupView() {
  ICoreWebView2Settings* s = nullptr;
  if (SUCCEEDED(g.view->get_Settings(&s)) && s) {
    s->put_AreDevToolsEnabled(FALSE);
    s->put_AreDefaultContextMenusEnabled(FALSE);
    s->put_IsStatusBarEnabled(FALSE);
    s->put_IsZoomControlEnabled(FALSE);
    s->put_AreDefaultScriptDialogsEnabled(FALSE);
    s->Release();
  }
  ICoreWebView2_3* v3 = nullptr;
  if (SUCCEEDED(g.view->QueryInterface(__uuidof(ICoreWebView2_3), reinterpret_cast<void**>(&v3))) && v3) {
    v3->SetVirtualHostNameToFolderMapping(kHost, g.page_dir.wstring().c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
    v3->Release();
  }
  if (ICoreWebView2Controller2* c2 = nullptr;
      SUCCEEDED(g.ctl->QueryInterface(__uuidof(ICoreWebView2Controller2), reinterpret_cast<void**>(&c2))) && c2) {
    c2->put_DefaultBackgroundColor(COREWEBVIEW2_COLOR{255, 0, 0, 0});
    c2->Release();
  }
  EventRegistrationToken token{};
  // Links out of the player (its YouTube logo, its title): the player's browser, never inside the view.
  auto* nw = new Handler<ICoreWebView2NewWindowRequestedEventHandler, ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs*>(
      [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
        LPWSTR uri = nullptr;
        if (SUCCEEDED(args->get_Uri(&uri)) && uri) OpenOutside(uri), CoTaskMemFree(uri);
        args->put_Handled(TRUE);
        return S_OK;
      });
  g.view->add_NewWindowRequested(nw, &token);
  nw->Release();
  auto* nav = new Handler<ICoreWebView2NavigationStartingEventHandler, ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*>(
      [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
        LPWSTR uri = nullptr;
        if (SUCCEEDED(args->get_Uri(&uri)) && uri) {
          if (std::wstring(uri).rfind(kPageBase, 0) != 0) {
            args->put_Cancel(TRUE);
            OpenOutside(uri);
          }
          CoTaskMemFree(uri);
        }
        return S_OK;
      });
  g.view->add_NavigationStarting(nav, &token);
  nav->Release();
  // Esc while the view has the keyboard is the window's Close; and the keyboard goes back to the game after a click
  // (the menu and the controller read the game's window).
  auto* key = new Handler<ICoreWebView2AcceleratorKeyPressedEventHandler, ICoreWebView2Controller*,
                          ICoreWebView2AcceleratorKeyPressedEventArgs*>(
      [](ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT {
        COREWEBVIEW2_KEY_EVENT_KIND kind{};
        UINT vk = 0;
        args->get_KeyEventKind(&kind);
        args->get_VirtualKey(&vk);
        if (vk == VK_ESCAPE && kind == COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN) {
          g.escape = true;
          args->put_Handled(TRUE);
        }
        return S_OK;
      });
  g.ctl->add_AcceleratorKeyPressed(key, &token);
  key->Release();
  auto* focus = new Handler<ICoreWebView2FocusChangedEventHandler, ICoreWebView2Controller*, IUnknown*>(
      [](ICoreWebView2Controller*, IUnknown*) -> HRESULT {
        g.refocus = true;
        return S_OK;
      });
  g.ctl->add_GotFocus(focus, &token);
  focus->Release();
  // The player's own word on what it does (the evidence that a video plays, and with its sound or not).
  auto* msg = new Handler<ICoreWebView2WebMessageReceivedEventHandler, ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*>(
      [](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
        LPWSTR text = nullptr;
        if (SUCCEEDED(args->TryGetWebMessageAsString(&text)) && text) {
          const std::string said = Narrow(text);
          REXLOG_INFO("BF news video: the player says: {}", said);
          CoTaskMemFree(text);
          // 0 ended, 2 paused, an error: no longer playing; 1 playing, 3 buffering: playing again.
          if (said.rfind("state 0", 0) == 0 || said.rfind("state 2", 0) == 0 || said.rfind("error", 0) == 0) g.ended = true;
          if (said.rfind("error", 0) == 0) g.broken = true;
          else if (said.rfind("state 1", 0) == 0 || said.rfind("state 3", 0) == 0) g.ended = false;
        }
        return S_OK;
      });
  g.view->add_WebMessageReceived(msg, &token);
  msg->Release();
  // Tests (BF_NEWS_SILENT=1): the view's sound off at its output, the page still playing with its sound on, so a run
  // proves autoplay with sound without a sound on the PC.
  if (const char* silent = std::getenv("BF_NEWS_SILENT"); silent && *silent == '1') {
    ICoreWebView2_8* v8 = nullptr;
    if (SUCCEEDED(g.view->QueryInterface(__uuidof(ICoreWebView2_8), reinterpret_cast<void**>(&v8))) && v8) {
      v8->put_IsMuted(TRUE);
      v8->Release();
      REXLOG_INFO("BF news video: BF_NEWS_SILENT=1: the view's output is silent");
    }
  }
}

void CreateController() {
  g.ctl_pending = true;
  auto* done = new Handler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>(
      [](HRESULT hr, ICoreWebView2Controller* ctl) -> HRESULT {
        g.ctl_pending = false;
        if (FAILED(hr) || !ctl) {
          Fail("the view could not be made", hr);
          return S_OK;
        }
        if (!g.want) {  // closed meanwhile
          ctl->Close();
          return S_OK;
        }
        g.ctl = ctl;
        g.ctl->AddRef();
        g.ctl->get_CoreWebView2(&g.view);
        if (!g.view) {
          ReleaseView();
          Fail("the view has no browser", E_FAIL);
          return S_OK;
        }
        SetupView();
        REXLOG_INFO("BF news video: the view is ready");
        return S_OK;
      });
  const HRESULT hr = g.env->CreateCoreWebView2Controller(g.parent, done);
  done->Release();
  if (FAILED(hr)) g.ctl_pending = false, Fail("CreateCoreWebView2Controller", hr);
}

void CreateEnvironment(const std::filesystem::path& cache_dir) {
  // WebView2 needs a single threaded apartment on the window's thread.
  const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (co == RPC_E_CHANGED_MODE) return Fail("the window's thread is not a single threaded apartment", co);
  g.page_dir = cache_dir / "news" / "web";
  g.data_dir = cache_dir / "news" / "webview2";
  std::error_code ec;
  std::filesystem::create_directories(g.page_dir, ec);
  std::filesystem::create_directories(g.data_dir, ec);
  std::ofstream(g.page_dir / "player.html", std::ios::binary | std::ios::trunc) << kPlayerPage;
  auto* options = new Options();
  g.env_pending = true;
  auto* done = new Handler<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>(
      [](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
        g.env_pending = false;
        if (FAILED(hr) || !env) {
          Fail("the WebView2 runtime did not start", hr);
          return S_OK;
        }
        g.env = env;
        g.env->AddRef();
        REXLOG_INFO("BF news video: WebView2 started");
        return S_OK;
      });
  const HRESULT hr = g.create_env(nullptr, g.data_dir.wstring().c_str(), options, done);
  done->Release();
  options->Release();
  if (FAILED(hr)) g.env_pending = false, Fail("CreateCoreWebView2EnvironmentWithOptions", hr);
}

}  // namespace

bool VideoAvailable() {
  if (g.failed) return false;
  if (g.checked) return g.create_env != nullptr;
  g.checked = true;
  if (const char* v = std::getenv("BF_NEWS_NO_VIDEO"); v && *v == '1') {
    REXLOG_INFO("BF news video: BF_NEWS_NO_VIDEO=1: the thumbnail instead");
    return false;
  }
  // From the game's own folder only (never the search path).
  const std::filesystem::path dll = ExecutableDir() / "WebView2Loader.dll";
  g.loader = LoadLibraryExW(dll.wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (!g.loader) {
    REXLOG_INFO("BF news video: no {}: the thumbnail instead", dll.string());
    return false;
  }
  auto version = reinterpret_cast<VersionFn>(GetProcAddress(g.loader, "GetAvailableCoreWebView2BrowserVersionString"));
  auto create = reinterpret_cast<CreateEnvFn>(GetProcAddress(g.loader, "CreateCoreWebView2EnvironmentWithOptions"));
  LPWSTR v = nullptr;
  if (!version || !create || FAILED(version(nullptr, &v)) || !v) {
    REXLOG_INFO("BF news video: no WebView2 runtime on this PC: the thumbnail instead");
    if (v) CoTaskMemFree(v);
    return false;
  }
  REXLOG_INFO("BF news video: WebView2 runtime {}", Narrow(v));
  CoTaskMemFree(v);
  g.create_env = create;
  return true;
}

void VideoShow(void* parent, const std::filesystem::path& cache_dir, const std::string& id, float x, float y, float w,
               float h, float display_w, float display_h, bool muted) {
  if (!VideoAvailable() || !parent) return;
  // The menu's coordinates to the client area's pixels (the same unless the window is scaled).
  RECT client{};
  GetClientRect(static_cast<HWND>(parent), &client);
  const float sx = display_w > 0 ? float(client.right - client.left) / display_w : 1.0f;
  const float sy = display_h > 0 ? float(client.bottom - client.top) / display_h : 1.0f;
  g.want = true, g.parent = static_cast<HWND>(parent), g.id = id;
  g.rect = RECT{LONG(x * sx + 0.5f), LONG(y * sy + 0.5f), LONG((x + w) * sx + 0.5f), LONG((y + h) * sy + 0.5f)};
  if (!g.env) {
    if (!g.env_pending) CreateEnvironment(cache_dir);
    return;
  }
  if (!g.ctl) {
    if (!g.ctl_pending) {
      g.muted = muted;
      CreateController();
    }
    return;
  }
  if (std::memcmp(&g.rect, &g.shown_rect, sizeof(RECT)) != 0) g.ctl->put_Bounds(g.rect), g.shown_rect = g.rect;
  if (g.shown_id != g.id) g.muted = muted, Navigate();
  if (!g.visible) g.ctl->put_IsVisible(TRUE), g.visible = true;
  if (g.refocus) {  // the keyboard back to the game after a click in the player
    g.refocus = false;
    SetFocus(g.parent);
  }
}

void VideoHide() {
  g.want = false;
  if (g.ctl) {
    ReleaseView();
    REXLOG_INFO("BF news video: stopped");
    if (g.parent) SetFocus(g.parent);
  }
}

void VideoSetMuted(bool muted) {
  g.muted = muted;
  if (!g.view || g.shown_muted == muted) return;
  g.shown_muted = muted;
  auto* done = new Handler<ICoreWebView2ExecuteScriptCompletedHandler, HRESULT, LPCWSTR>([](HRESULT, LPCWSTR) { return S_OK; });
  g.view->ExecuteScript(muted ? L"bfMute(true)" : L"bfMute(false)", done);
  done->Release();
}

bool VideoShowing() { return g.ctl && g.visible; }

bool VideoPlaying() { return VideoShowing() && !g.ended; }

bool VideoFailed() { return g.broken; }

bool VideoTakeEscape() {
  const bool e = g.escape;
  g.escape = false;
  return e;
}

#else  // Linux and the Steam Deck: the thumbnail, the video in the browser.

bool VideoAvailable() { return false; }
void VideoShow(void*, const std::filesystem::path&, const std::string&, float, float, float, float, float, float, bool) {}
void VideoHide() {}
void VideoSetMuted(bool) {}
bool VideoShowing() { return false; }
bool VideoPlaying() { return false; }
bool VideoFailed() { return false; }
bool VideoTakeEscape() { return false; }

#endif

}  // namespace bf1943::news
