#include <QSocketNotifier>
#include <QApplication>
#include <QMainWindow>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QUrl>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QIcon>
#include <QCloseEvent>
#include <QRegularExpression>
#include <QTimer>
#include <thread>
#include <QDir>
#include <QSvgRenderer>
#include <QPixmap>
#include <QPainter>
#include <QProcess>
#include <QDateTime>
#include <QObject>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>

// Custom window class to intercept close events and hide to tray natively
class LindoraWindow : public QMainWindow {
protected:
    void closeEvent(QCloseEvent *event) override {
        if (isVisible()) {
            event->ignore(); // Stop the window from actually destroying itself
            hide();          // Simply hide it from view
        }
    }
};

class MprisPlayer;

class MprisAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT

    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")

public:
    explicit MprisAdaptor(MprisPlayer *parent);

public slots:
    void Play();
    void Pause();
    void PlayPause();
    void Next();
    void Previous();

public:
    Q_PROPERTY(QString PlaybackStatus READ PlaybackStatus)
    Q_PROPERTY(QVariantMap Metadata READ Metadata)
    Q_PROPERTY(qlonglong Position READ Position)
    Q_PROPERTY(bool CanPlay READ CanPlay)
    Q_PROPERTY(bool CanPause READ CanPause)
    Q_PROPERTY(bool CanGoNext READ CanGoNext)
    Q_PROPERTY(bool CanGoPrevious READ CanGoPrevious)
    Q_PROPERTY(bool CanSeek READ CanSeek)
    Q_PROPERTY(bool CanControl READ CanControl)

    Q_PROPERTY(double Rate READ Rate)
    Q_PROPERTY(double MinimumRate READ MinimumRate)
    Q_PROPERTY(double MaximumRate READ MaximumRate)

    Q_PROPERTY(double Volume READ Volume)

bool CanPlay() const;
bool CanPause() const;
bool CanGoNext() const;
bool CanGoPrevious() const;
bool CanSeek() const;
bool CanControl() const;

double Rate() const;
double MinimumRate() const;
double MaximumRate() const;

double Volume() const;
    QString PlaybackStatus() const;
    QVariantMap Metadata() const;
    qlonglong Position() const;

private:
    MprisPlayer *m_player;
};

class MprisRootAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT

    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")

public:
    explicit MprisRootAdaptor(QObject *parent)
        : QDBusAbstractAdaptor(parent)
    {
    }

    Q_PROPERTY(bool CanQuit READ CanQuit)
    Q_PROPERTY(bool CanRaise READ CanRaise)
    Q_PROPERTY(QString Identity READ Identity)

    Q_PROPERTY(bool HasTrackList READ HasTrackList)
    Q_PROPERTY(QString DesktopEntry READ DesktopEntry)

    bool CanQuit() const { return false; }
    bool CanRaise() const { return true; }
    QString Identity() const { return "Lindora"; }

    bool HasTrackList() const
    {
        return false;
    }

    QString DesktopEntry() const
    {
        return "Lindora";
    }

public slots:
    void Raise() {}
};

class MprisPlayer : public QObject
{
    Q_OBJECT

public:
    explicit MprisPlayer(QWebEngineView *view)
        : m_view(view)
    {
        new MprisAdaptor(this);
        new MprisRootAdaptor(this);

        QDBusConnection::sessionBus().registerService(
            "org.mpris.MediaPlayer2.pandora");

        QDBusConnection::sessionBus().registerObject(
            "/org/mpris/MediaPlayer2",
            this,
            QDBusConnection::ExportAdaptors);

        auto *timer = new QTimer(this);

        connect(timer, &QTimer::timeout,
                this, &MprisPlayer::updateMetadata);

        timer->start(1000);
    }

    QString title() const { return m_title; }
    QString artist() const { return m_artist; }
    bool playing() const { return m_playing; }
    qlonglong position() const { return m_position; }

    void runJS(const QString &js)
    {
        m_view->page()->runJavaScript(js);
    }

public slots:
    void updateMetadata()
    {
        QString js = R"(
(() => {
    return {
        title:
            document.querySelector(
                '[data-qa="mini_track_title"]'
            )?.textContent?.trim() || "",

        artist:
            document.querySelector(
                '[data-qa="mini_track_artist_name"]'
            )?.textContent?.trim() || "",

        elapsed:
            document.querySelector(
                '[data-qa="elapsed_time"]'
            )?.textContent?.trim() || "",

        playing:
            document.querySelector(
                '[data-qa="pause_button"]'
            ) !== null
    };
})();
)";

        m_view->page()->runJavaScript(
            js,
            [this](const QVariant &v)
            {
                QVariantMap map = v.toMap();

                QString newTitle =
                    map["title"].toString();

                QString newArtist =
                    map["artist"].toString();

                bool newPlaying =
                    map["playing"].toBool();

                QString elapsed =
                    map["elapsed"].toString();

                QStringList parts =
                    elapsed.split(":");

                qlonglong pos = 0;

                if (parts.size() == 2)
                {
                    pos =
                        (parts[0].toLongLong() * 60 +
                         parts[1].toLongLong())
                        * 1000000;
                }

                bool changed =
                    newTitle != m_title ||
                    newArtist != m_artist ||
                    newPlaying != m_playing;
                    pos != m_position;

                m_title = newTitle;
                m_artist = newArtist;
                m_playing = newPlaying;
                m_position = pos;

                if (changed)
                    emitPropertiesChanged();
            });
    }

    void emitPropertiesChanged()
    {
        QVariantMap changed;

        changed["PlaybackStatus"] =
            m_playing ? "Playing" : "Paused";

        QVariantMap metadata;

        metadata["xesam:title"] =
            m_title;

        metadata["xesam:artist"] =
            QStringList{m_artist};

        changed["Metadata"] =
            QVariant::fromValue(metadata);

        changed["Position"] = m_position;

        QDBusMessage signal =
            QDBusMessage::createSignal(
                "/org/mpris/MediaPlayer2",
                "org.freedesktop.DBus.Properties",
                "PropertiesChanged");

        signal << "org.mpris.MediaPlayer2.Player"
               << changed
               << QStringList();

        QDBusConnection::sessionBus().send(signal);
    }

private:
    QWebEngineView *m_view;

    QString m_title;
    QString m_artist;

    bool m_playing = false;

    qlonglong m_position = 0;
};

MprisAdaptor::MprisAdaptor(MprisPlayer *parent)
    : QDBusAbstractAdaptor(parent),
      m_player(parent)
{
}

QString MprisAdaptor::PlaybackStatus() const
{
    return m_player->playing()
               ? "Playing"
               : "Paused";
}

qlonglong MprisAdaptor::Position() const
{
    return m_player->position();
}

QVariantMap MprisAdaptor::Metadata() const
{
    QVariantMap metadata;

    metadata["xesam:title"] =
        m_player->title();

    metadata["xesam:artist"] =
        QStringList{m_player->artist()};

    return metadata;
}

bool MprisAdaptor::CanPlay() const
{
    return true;
}

bool MprisAdaptor::CanPause() const
{
    return true;
}

bool MprisAdaptor::CanGoNext() const
{
    return true;
}

bool MprisAdaptor::CanGoPrevious() const
{
    return true;
}

bool MprisAdaptor::CanSeek() const
{
    return false;
}

bool MprisAdaptor::CanControl() const
{
    return true;
}

double MprisAdaptor::Rate() const
{
    return 1.0;
}

double MprisAdaptor::MinimumRate() const
{
    return 1.0;
}

double MprisAdaptor::MaximumRate() const
{
    return 1.0;
}

double MprisAdaptor::Volume() const
{
    return 1.0;
}

void MprisAdaptor::Play()
{
    m_player->runJS(
        "document.querySelector('[data-qa=\"play_button\"]')?.click();");
}

void MprisAdaptor::Pause()
{
    m_player->runJS(
        "document.querySelector('[data-qa=\"pause_button\"]')?.click();");
}

void MprisAdaptor::PlayPause()
{
    m_player->runJS(R"(
const pauseBtn =
    document.querySelector(
        '[data-qa="pause_button"]');

const playBtn =
    document.querySelector(
        '[data-qa="play_button"]');

if (pauseBtn)
    pauseBtn.click();
else if (playBtn)
    playBtn.click();
)");
}

void MprisAdaptor::Next()
{
    m_player->runJS(
        "document.querySelector('[data-qa=\"skip_button\"]')?.click();");
}

void MprisAdaptor::Previous()
{
    m_player->runJS(
        "document.querySelector('[data-qa=\"t3_skip_back_button\"]')?.click();");
}

int main(int argc, char *argv[]) {
    qputenv("QTWEBENGINE_REMOTE_DEBUGGING", "9222");
    qputenv("QTWEBENGINE_DISABLE_MEDIA_SESSION_API", "1");
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS",
        "--disable-features=MediaSessionService");
    

    QApplication app(argc, argv);

    // MUST be before any window is created/mapped
    app.setApplicationName("Lindora");
    app.setApplicationDisplayName("Lindora");
    app.setDesktopFileName("lindora");

    QIcon appIcon("/usr/share/icons/hicolor/scalable/apps/lindora.svg");
    app.setWindowIcon(appIcon);

    LindoraWindow window;
    window.setWindowIcon(appIcon);
    window.setWindowTitle("Lindora");
    window.setObjectName("lindora-native");
    window.resize(1100, 750);

    app.setQuitOnLastWindowClosed(false);

    // Maintain persistent profile so your login session remains saved
    QWebEngineProfile *profile = new QWebEngineProfile("lindora-native", &app);
    profile->setPersistentStoragePath(profile->persistentStoragePath());
    profile->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);


QPalette p = qApp->palette();

/* Qt native system colors */
QString bg = p.color(QPalette::Window).name();
QString panel = p.color(QPalette::Base).name();
QString text = p.color(QPalette::WindowText).name();
QString accent = p.color(QPalette::Highlight).name();

QString customCss =
    /* 1. Universal "Catch-All": Qt system background */
    QString("*:not(img):not(svg):not(canvas):not(video):not([class*='art']):not([class*='Art']):not([class*='thumb']):not([class*='Thumb']):not(.Image__img) { "
            "background-color: %1 !important; "
            "background-image: none !important; "
            "} ")
    .arg(bg)

    /* 2. Root variables */
    + QString(":root { "
              "--panel-purple: %1; "
              "--text-color: %2; "
              "--accent-color: %3; "
              "} ")
    .arg(panel)
    .arg(text)
    .arg(accent)

    /* 3. Main UI layers */
    + ".nowPlayingTopInfo, .sidebar, .root-container { "
      "background-color: var(--panel-purple) !important; "
      "} "

    /* 4. Buttons */
    + "button, [role='button'] { "
      "background-color: inherit !important; "
      "border: 1px solid var(--accent-color) !important; "
      "color: var(--text-color) !important; "
      "} "

    /* 5. Text */
    + "h1, h2, h3, p, span, div, a { "
      "color: var(--text-color) !important; "
      "} "

    /* 6. Scrollbar */
    + QString("::-webkit-scrollbar { width: 8px !important; } "
              "::-webkit-scrollbar-thumb { background: %1 !important; } "
              "::-webkit-scrollbar-track { background: %2 !important; }")
    .arg(accent)
    .arg(bg)

    /* =========================== */
    /* 7. PANDORA SVG BACKGROUND FIX (THIS WAS THE MISSING PIECE) */
    /* =========================== */
    + QString(".AppBg, .BlurredBackground, .BlurredBackground__svg { "
              "background: %1 !important; "
              "} "
              ".BlurredBackground__svg rect { "
              "fill: %1 !important; "
              "opacity: 1 !important; "
              "} "
              ".BlurredBackground__svg stop { "
              "stop-color: %2 !important; "
              "} ")
    .arg(bg)
    .arg(panel)

    
    /* =========================== */
    /* 8. SEARCH INPUT FIX */
    /* =========================== */
    + QString(".GlobalSearchInput__input { "
              "background-color: #2b2b2b !important; "
              "color: %1 !important; "
              "border: 1px solid #444444 !important; "
              "box-shadow: none !important; "
              "outline: none !important; "
              "-webkit-appearance: none !important; "
              "appearance: none !important; "
              "} "

              ".GlobalSearchInput__input::placeholder { "
              "color: #888888 !important; "
              "} "

              ".GlobalSearchInput__input:focus { "
              "background-color: #333333 !important; "
              "border: 1px solid #666666 !important; "
              "} "

              ".GlobalSearchInput__input::-webkit-search-decoration,"
              ".GlobalSearchInput__input::-webkit-search-cancel-button,"
              ".GlobalSearchInput__input::-webkit-search-results-button,"
              ".GlobalSearchInput__input::-webkit-search-results-decoration { "
              "display: none !important; "
              "} ")
    .arg(text)

    + QString(
/* =========================== */
/* 9. PANDORA MEDIA ICON FIX   */
/* =========================== */

/* FORCE ICON BASE COLOR (THIS IS THE KEY) */
".Icon, .TunerControl__Icon, [class*='Icon'] { "
"  color: %1 !important; "
"  fill: %1 !important; "
"  stroke: %1 !important; "
"} "

/* DIRECT SVG PATH CONTROL (CRITICAL) */
".Icon path, .TunerControl__Icon path { "
"  fill: currentColor !important; "
"  stroke: none !important; "
"} "

/* BUTTON WRAPPER FIX (prevents white override in light mode) */
".PlayButton, .ThumbUpButton, .ThumbDownButton, "
".SkipButton, .ReplayButton, .RepeatButton, .ShuffleButton { "
"  color: %1 !important; "
"} "

/* ACTIVE STATE FIX (Pandora toggles aria-checked) */
"[aria-checked='true'] .Icon, "
"[aria-checked='true'] svg { "
"  color: %1 !important; "
"  fill: %1 !important; "
"} "
)
.arg(text)
.arg(accent)

    + QString(
        /* =========================== */
        /* 10. UNIFIED QT PRIMARY BACKGROUND OVERRIDE */
        /* =========================== */

        /* EVERYTHING defaults to Qt "Window" (PRIMARY system background) */
        "* { "
        "  background-color: %1 !important; "
        "  background-image: none !important; "
        "} "
    ).arg(bg);

QString jsCode = QString(
    "(function() {"
    "  var style = document.createElement('style');"
    "  style.type = 'text/css';"
    "  style.appendChild(document.createTextNode(`%1`));"
    "  document.head.appendChild(style);"

    "  var observer = new MutationObserver(function() {"
    "    if (!document.head.contains(style)) {"
    "      document.head.appendChild(style);"
    "    }"
    "  });"

    "  observer.observe(document.documentElement, {childList: true, subtree: true});"
    "})();"
).arg(customCss);

QWebEngineScript script;
script.setName("PlumCanvasTheme");
script.setSourceCode(jsCode);
script.setInjectionPoint(QWebEngineScript::DocumentReady);
script.setRunsOnSubFrames(true);

profile->scripts()->insert(script);

    QWebEnginePage *page = new QWebEnginePage(profile, &window);
    QWebEngineView *browser = new QWebEngineView(&window);
    browser->setPage(page);
    window.setCentralWidget(browser);

    // Load the official site directly
    browser->setUrl(QUrl("https://www.pandora.com"));

QString iconPath = "/usr/share/icons/hicolor/scalable/apps/lindora.svg";

// Rasterize the SVG to a 64x64 pixmap for the tray
QPixmap pixmap(64, 64);
pixmap.fill(Qt::transparent);
QSvgRenderer renderer(iconPath);
QPainter painter(&pixmap);
renderer.render(&painter);
painter.end();

QIcon trayIconImage(pixmap);
QSystemTrayIcon *trayIcon = new QSystemTrayIcon(trayIconImage, &window);
    QMenu *trayMenu = new QMenu(&window);
    
    // Add a "Show" option to the right-click menu
    QAction *restoreAction = trayMenu->addAction("Show Lindora");
    QObject::connect(restoreAction, &QAction::triggered, [&window]() {
        window.show();
        window.raise();
        window.activateWindow();
    });

    // Add an "Exit" option to completely kill the background process
    QAction *quitAction = trayMenu->addAction("Exit");
    QObject::connect(quitAction, &QAction::triggered, [&app]() {
        app.quit(); 
    });

    QAction *restartAction = trayMenu->addAction("Restart App");
QObject::connect(restartAction, &QAction::triggered, []() {
    QString program = QApplication::applicationFilePath();
    QStringList arguments = QApplication::arguments();
    
    // Remove the first argument, which is the executable path itself
    arguments.removeFirst(); 

    // Start a new instance
    QProcess::startDetached(program, arguments);
    
    // Quit the current instance
    QApplication::quit();
});

    trayIcon->setIcon(trayIconImage);

    trayIcon->setContextMenu(trayMenu);
// ... inside main() after trayIcon initialization ...

QTimer *timer = new QTimer(&window);

QObject::connect(timer, &QTimer::timeout, [browser, trayIcon]() {

    QString scrapeJs =
    "(function() {"

    "  function text(el) {"
    "    return el ? el.innerText.trim() : '';"
    "  }"

    "  var track = "
    "    document.querySelector('[data-qa=\"mini_track_title\"]') || "
    "    document.querySelector('[data-qa=\"track_name\"]') || "
    "    document.querySelector('.Marquee__wrapper__content');"

    "  var artist = "
    "    document.querySelector('[data-qa=\"mini_track_artist\"]') || "
    "    document.querySelector('[data-qa=\"artist_name\"]') || "
    "    document.querySelector('[data-qa=\"artistName\"]') || "
    "    document.querySelector('[data-qa=\"now_playing_artist\"]') || "
    "    document.querySelector('[data-qa=\"mini_player\"] [data-qa*=\"artist\"]') || "
    "    document.querySelector('[class*=\"artist\"]') || "
    "    document.querySelector('[class*=\"Artist\"]') || "
    "    document.querySelector('.nowPlayingTopInfo__current__artist') || "
    "    document.querySelector('.Marquee__wrapper__content') || "
    "    document.querySelector('.Marquee__wrapper__content + div') || "
    "    document.querySelector('a[href*=\"/artist/\"]') || "
    "    document.querySelector('span[aria-label*=\"artist\"]') || "
    "    document.querySelector('div[aria-label*=\"artist\"]') || "
    "    document.querySelector('[role=\"link\"][href*=\"artist\"]') || "
    "    document.querySelector('div[data-automation*=\"artist\"]') || "
    "    document.querySelector('span[data-automation*=\"artist\"]') || "
    "    document.querySelector('div[data-test*=\"artist\"]') || "
    "    document.querySelector('span[data-test*=\"artist\"]') || "
    "    document.querySelector('meta[property=\"music:musician\"]') || "
    "    null;"

    // ⏱ TIMESTAMP ELEMENTS
    "  var elapsed = document.querySelector('[data-qa=\"elapsed_time\"]');"
    "  var remaining = document.querySelector('[data-qa=\"remaining_time\"]');"

    "  var song = text(track);"
    "  var artistName = text(artist);"

    "  var elapsedTime = elapsed ? elapsed.innerText.trim() : '';"
    "  var remainingTime = remaining ? remaining.innerText.trim() : '';"

    "  var result = '';"

    "  if (song && artistName) {"
    "    result = song + ' - ' + artistName;"
    "  }"

    "  if (!song && artistName) {"
    "    result = artistName;"
    "  }"

    "  if (song && !artistName) {"
    "    result = song;"
    "  }"

    "  if (elapsedTime || remainingTime) {"
    "    result += ' | ' + elapsedTime + ' / ' + remainingTime;"
    "  }"

    "  return result;"
    "})();";

    browser->page()->runJavaScript(scrapeJs, [trayIcon](const QVariant &v) {
        QString info = v.toString();
        if (!info.isEmpty()) {
            trayIcon->setToolTip(info);
        }
    });

});

timer->start(500);

// 1. Declare the action once
QAction *skipAction = trayMenu->addAction("Skip Song");

// 2. Define the trigger logic
QObject::connect(skipAction, &QAction::triggered, [browser]() {
    QString skipJs =
    "(function() {"
    ""
    "  let btn ="
    "      document.querySelector('[data-qa=\"t3_skip_forward_button\"]') ||"
    "      document.querySelector('[data-qa=\"skip_button\"]') ||"
    "      document.querySelector('.Tuner__Control__SkipForward__Button') ||"
    "      document.querySelector('.Tuner__Control__Skip__Button') ||"
    "      document.querySelector('[aria-label=\"Skip forwards\"]') ||"
    "      document.querySelector('[aria-label=\"skip forwards\"]');"
    ""
    "  if (!btn) {"
    "      const candidates = Array.from("
    "          document.querySelectorAll("
    "              'button,[role=\"button\"],div[role=\"button\"],span[role=\"button\"]'"
    "          )"
    "      );"
    ""
    "      btn = candidates.find(el => {"
    "          const aria = (el.getAttribute('aria-label') || '').toLowerCase();"
    "          const dataqa = (el.getAttribute('data-qa') || '').toLowerCase();"
    "          const cls = (el.className || '').toLowerCase();"
    ""
    "          return ("
    "              dataqa === 't3_skip_forward_button' ||"
    "              dataqa === 'skip_button' ||"
    "              aria === 'skip forwards' ||"
    "              aria === 'skip_forwards' ||"
    "              cls.includes('tuner__control__skipforward__button') ||"
    "              cls.includes('tuner__control__skip__button')"
    "          );"
    "      });"
    "  }"
    ""
    "  if (!btn) return 'NOT_FOUND';"
    "  if (btn.disabled) return 'DISABLED';"
    "  if (btn.getAttribute('aria-disabled') === 'true') return 'ARIA_DISABLED';"
    ""
    "  btn.focus();"
    "  btn.click();"
    ""
    "  btn.dispatchEvent(new MouseEvent('click', {"
    "      bubbles: true,"
    "      cancelable: true,"
    "      view: window"
    "  }));"
    ""
    "  return 'CLICKED';"
    ""
    "})();";

    browser->page()->runJavaScript(skipJs);
});

// 3. Single Visibility Connection
QObject::connect(trayMenu, &QMenu::aboutToShow, [browser, skipAction]() {
    QString checkJs =
    "(function() {"
    "  function findBtn() {"
    "    const candidates = Array.from(document.querySelectorAll('button, div[role=\"button\"], span[role=\"button\"]'));"
    "    return candidates.find(el => {"
    "      const a = (el.getAttribute('aria-label') || '').toLowerCase();"
    "      const keywords = ['next', 'skip', 'forward', 'skip forward'];"
    "      return keywords.some(k => a.includes(k));"
    "    });"
    "  }"
    ""
    "  let btn = findBtn();"
    "  return !!(btn && !btn.disabled && btn.getAttribute('aria-disabled') !== 'true');"
    "})();";

    browser->page()->runJavaScript(checkJs, [skipAction](const QVariant &res) {
        if (skipAction)
            skipAction->setVisible(res.toBool());
    });
});


QAction *prevAction = trayMenu->addAction("Previous Song");

QObject::connect(prevAction, &QAction::triggered, [browser]() {
    QString prevJs =
"(function() {"

"  const selectors = ["
"    '[data-qa=\"t3_skip_back_button\"]',"
"    '[aria-label=\"Skip backwards\"]',"
"    '[aria-label=\"skip_backwards\"]',"
"    '.Tuner__Control__SkipBack__Button'"
"  ];"

"  let btn = null;"

"  for (const s of selectors) {"
"    btn = document.querySelector(s);"
"    if (btn) break;"
"  }"

"  if (!btn) {"
"    const candidates = Array.from("
"      document.querySelectorAll('button,[role=\"button\"]')"
"    );"

"    btn = candidates.find(el => {"
"      const a = (el.getAttribute('aria-label') || '').toLowerCase();"
"      const d = (el.getAttribute('data-qa') || '').toLowerCase();"
"      const c = (el.className || '').toLowerCase();"

"      return ("
"        d.includes('skip_back_button') ||"
"        d.includes('skip_back') ||"
"        a.includes('skip_backwards') ||"
"        a.includes('skip backwards') ||"
"        c.includes('skipbackbutton') ||"
"        c.includes('skipback')"
"      );"
"    });"
"  }"

"  if (!btn)"
"    return 'NOT_FOUND';"

"  if (btn.disabled)"
"    return 'DISABLED';"

"  if (btn.getAttribute('aria-disabled') === 'true')"
"    return 'ARIA_DISABLED';"

"  btn.click();"

"  return 'CLICKED';"

"})();";

    browser->page()->runJavaScript(prevJs);
});

QObject::connect(trayMenu, &QMenu::aboutToShow, [browser, prevAction]() {

    QString checkJs =
    "(function() {"
    "  function isVisible(el) {"
    "    if (!el) return false;"
    "    const style = window.getComputedStyle(el);"
    "    return style && style.display !== 'none' && style.visibility !== 'hidden' && el.offsetParent !== null;"
    "  }"
    ""
    "  function findBtn() {"
    "    const candidates = Array.from(document.querySelectorAll('button, div[role=\"button\"], span[role=\"button\"]'));"
    ""
    "    return candidates.find(el => {"
    "      const a = (el.getAttribute('aria-label') || '').toLowerCase();"
    "      const t = (el.innerText || '').toLowerCase();"
    "      const c = (el.className || '').toLowerCase();"
    ""
    "      const match = ("
    "        a.includes('previous') ||"
    "        a.includes('back') ||"
    "        a.includes('rewind') ||"
    "        t.includes('previous') ||"
    "        t.includes('back') ||"
    "        c.includes('previous') ||"
    "        c.includes('back')"
    "      );"
    ""
    "      return match && isVisible(el);"
    "    });"
    "  }"
    ""
    "  let btn = findBtn();"
    ""
    "  if (!btn) {"
    "    const all = document.querySelectorAll('button, div[role=\"button\"]');"
    "    for (let el of all) {"
    "      const a = (el.getAttribute && el.getAttribute('aria-label') || '').toLowerCase();"
    "      if ((a.includes('previous') || a.includes('back')) && el.offsetParent !== null) {"
    "        btn = el;"
    "        break;"
    "      }"
    "    }"
    "  }"
    ""
    "  return !!btn;"
    "})();";

    browser->page()->runJavaScript(checkJs, [prevAction](const QVariant &res) {
        prevAction->setVisible(res.toBool());
    });
});

QAction *replayAction = trayMenu->addAction("Replay");

QObject::connect(replayAction, &QAction::triggered, [browser]() {
    QString replayJs =
    "(function() {"
    "  let btn ="
    "      document.querySelector('[data-qa=\"replay_button\"]') ||"
    "      document.querySelector('.Tuner__Control__Replay__Button') ||"
    "      document.querySelector('[aria-label=\"Replay\"]');"
    ""
    "  if (!btn) {"
    "      const candidates = Array.from("
    "          document.querySelectorAll('button,[role=\"button\"],div[role=\"button\"],span[role=\"button\"]')"
    "      );"
    ""
    "      btn = candidates.find(el => {"
    "          const aria = (el.getAttribute('aria-label') || '').toLowerCase();"
    "          const dataqa = (el.getAttribute('data-qa') || '').toLowerCase();"
    "          const cls = (el.className || '').toLowerCase();"
    ""
    "          return ("
    "              dataqa === 'replay_button' ||"
    "              aria === 'replay' ||"
    "              cls.includes('replaybutton') ||"
    "              cls.includes('tuner__control__replay__button')"
    "          );"
    "      });"
    "  }"
    ""
    "  if (!btn) return;"
    "  if (btn.disabled) return;"
    "  if (btn.getAttribute('aria-disabled') === 'true') return;"
    ""
    "  btn.focus();"
    "  btn.click();"
    ""
    "  btn.dispatchEvent(new MouseEvent('click', {"
    "      bubbles: true,"
    "      cancelable: true,"
    "      view: window"
    "  }));"
    ""
    "})();";

    browser->page()->runJavaScript(replayJs);
});

QObject::connect(trayMenu, &QMenu::aboutToShow,
                 [browser, replayAction]() {

    QString replayCheckJs =
    "(function() {"
    "  const btn = document.querySelector('[data-qa=\"replay_button\"]');"
    ""
    "  return !!("
    "      btn &&"
    "      !btn.disabled &&"
    "      btn.getAttribute('aria-disabled') !== 'true'"
    "  );"
    "})();";

    browser->page()->runJavaScript(
        replayCheckJs,
        [replayAction](const QVariant &res) {
            if (replayAction)
                replayAction->setVisible(res.toBool());
        }
    );
});

QAction *repeatAction = trayMenu->addAction("Repeat");

QObject::connect(repeatAction, &QAction::triggered, [browser]() {
    QString repeatJs =
    "(function() {"
    "  const btn = document.querySelector('[data-qa=\"tuner_repeat_button\"]');"
    "  if (!btn) return;"
    "  if (btn.disabled) return;"
    "  if (btn.getAttribute('aria-disabled') === 'true') return;"
    "  btn.click();"
    "})();";

    browser->page()->runJavaScript(repeatJs);
});

QObject::connect(trayMenu, &QMenu::aboutToShow,
                 [browser, repeatAction]() {

    QString repeatStateJs =
    "(function() {"
    "  const btn = document.querySelector('[data-qa=\"tuner_repeat_button\"]');"
    ""
    "  if (!btn)"
    "    return { exists: false };"
    ""
    "  return {"
    "    exists: true,"
    "    checked: btn.getAttribute('aria-checked') || ''"
    "  };"
    "})();";

    browser->page()->runJavaScript(
        repeatStateJs,
        [repeatAction](const QVariant &res) {

            QVariantMap info = res.toMap();

            bool exists = info.value("exists").toBool();

            repeatAction->setVisible(exists);

            if (!exists)
                return;

            QString checked = info.value("checked").toString();

            if (checked == "false")
                repeatAction->setText("Repeat: Off");
            else if (checked == "true")
                repeatAction->setText("Repeat: Playlist");
            else if (checked == "mixed")
                repeatAction->setText("Repeat: Song");
            else
                repeatAction->setText("Repeat");
        }
    );
});

// Add a Pause/Play option to the right-click menu
QAction *pauseAction = trayMenu->addAction("Pause/Play");
QObject::connect(pauseAction, &QAction::triggered, [browser]() {
    QString pauseJs =
    "(function() {"
"  var btn = document.querySelector('[data-qa=\"play_pause_button\"]') || "
"            document.querySelector('[data-qa=\"play_button\"]') || "
"            document.querySelector('[data-qa=\"pause_button\"]');"

    "  if (btn) {"
    "    const rect = btn.getBoundingClientRect();"
    "    const x = rect.left + rect.width / 2;"
    "    const y = rect.top + rect.height / 2;"
    "    const opts = { bubbles: true, cancelable: true, view: window, clientX: x, clientY: y };"
    "    btn.dispatchEvent(new PointerEvent('pointerdown', { ...opts, pointerId: 1, isPrimary: true }));"
    "    btn.dispatchEvent(new MouseEvent('mousedown', opts));"
    "    btn.dispatchEvent(new PointerEvent('pointerup', { ...opts, pointerId: 1, isPrimary: true }));"
    "    btn.dispatchEvent(new MouseEvent('mouseup', opts));"
    "    btn.dispatchEvent(new MouseEvent('click', opts));"
    "    if (typeof btn.click === 'function') btn.click();"
    "  }"
    "})();";
    browser->page()->runJavaScript(pauseJs);
});

QObject::connect(trayMenu, &QMenu::aboutToShow, [browser, pauseAction]() {
    QString checkJs =
    "(function() {"
    "  if (document.querySelector('[data-qa=\"pause_button\"]'))"
    "    return 'PAUSE';"
    ""
    "  if (document.querySelector('[data-qa=\"play_button\"]'))"
    "    return 'PLAY';"
    ""
    "  return 'UNKNOWN';"
    "})();";

    browser->page()->runJavaScript(checkJs, [pauseAction](const QVariant &res) {
        QString state = res.toString();

        if (state == "PAUSE")
            pauseAction->setText("Pause");
        else if (state == "PLAY")
            pauseAction->setText("Play");
        else
            pauseAction->setText("Play/Pause");
    });
});

    trayIcon->show();

    // Toggle window visibility when clicking the system tray icon directly
    QObject::connect(trayIcon, &QSystemTrayIcon::activated, [&window](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            if (window.isVisible()) {
                window.hide();
            } else {
                window.show();
                window.raise();
                window.activateWindow();
            }
        }
    });

QAction *thumbUpAction = trayMenu->addAction("Thumbs Up");
QObject::connect(thumbUpAction, &QAction::triggered, [browser]() {

    QString thumbUpJs =
    "(function() {"
    "  function findBtn() {"
    "    const candidates = Array.from(document.querySelectorAll('button, div[role=\"button\"], span[role=\"button\"]'));"
    ""
    "    return candidates.find(el => {"
    "      const a = (el.getAttribute('aria-label') || '').toLowerCase();"
    "      const t = (el.innerText || '').toLowerCase();"
    "      const c = (el.className || '').toLowerCase();"
    ""
    // Added explicit check: skip if it looks like a dislike button
    "      const isDislike = a.includes('dislike') || a.includes('thumb down') || t.includes('dislike');"
    ""
    "      return ("
    "        (a.includes('like') || a.includes('thumb up') || t.includes('like')) && !isDislike"
    "      );"
    "    });"
    "  }"
    ""
    "  let btn = findBtn();"
    ""
    "  if (!btn) {"
    "    const all = document.querySelectorAll('*');"
    "    for (let el of all) {"
    "      const a = (el.getAttribute && el.getAttribute('aria-label') || '').toLowerCase();"
    "      if ((a.includes('like') || a.includes('thumb up')) && !a.includes('dislike')) {"
    "        btn = el;"
    "        break;"
    "      }"
    "    }"
    "  }"
    ""
    "  if (btn) {"
    "    btn.dispatchEvent(new MouseEvent('click', {"
    "      bubbles: true,"
    "      cancelable: true,"
    "      view: window"
    "    }));"
    "    btn.click();"
    "  }"
    ""
    "})();";

    browser->page()->runJavaScript(thumbUpJs);
});

QObject::connect(trayMenu, &QMenu::aboutToShow,
                 [browser, thumbUpAction]() {

    QString checkJs =
    "(function() {"
    "  const btn = document.querySelector('[data-qa=\"thumbs_up_button\"]');"
    "  if (!btn) return 'MISSING';"
    ""
    "  return btn.getAttribute('aria-checked') === 'true'"
    "      ? 'ON'"
    "      : 'OFF';"
    "})();";

    browser->page()->runJavaScript(
        checkJs,
        [thumbUpAction](const QVariant &res) {

            QString state = res.toString();

            if (state == "MISSING") {
                thumbUpAction->setVisible(false);
                return;
            }

            thumbUpAction->setVisible(true);

            if (state == "ON")
                thumbUpAction->setText("Thumbs Up: On");
            else
                thumbUpAction->setText("Thumbs Up: Off");
        });
});

QAction *thumbDownAction = trayMenu->addAction("Thumbs Down");
QObject::connect(thumbDownAction, &QAction::triggered, [browser]() {

    QString thumbDownJs =
    "(function() {"
    "  function findBtn() {"
    "    const candidates = Array.from(document.querySelectorAll('button, div[role=\"button\"], span[role=\"button\"]'));"
    ""
    "    return candidates.find(el => {"
    "      const a = (el.getAttribute('aria-label') || '').toLowerCase();"
    "      const t = (el.innerText || '').toLowerCase();"
    "      const c = (el.className || '').toLowerCase();"
    ""
    "      return ("
    "        a.includes('thumb down') ||"
    "        a.includes('dislike') ||"
    "        t.includes('thumb down') ||"
    "        t.includes('dislike') ||"
    "        c.includes('dislike')"
    "      );"
    "    });"
    "  }"
    ""
    "  let btn = findBtn();"
    ""
    "  if (!btn) {"
    "    const all = document.querySelectorAll('*');"
    "    for (let el of all) {"
    "      const a = (el.getAttribute && el.getAttribute('aria-label') || '').toLowerCase();"
    "      if (a.includes('dislike') || a.includes('thumb down')) {"
    "        btn = el;"
    "        break;"
    "      }"
    "    }"
    "  }"
    ""
    "  if (btn) {"
    "    btn.dispatchEvent(new MouseEvent('click', {"
    "      bubbles: true,"
    "      cancelable: true,"
    "      view: window"
    "    }));"
    "    btn.click();"
    "  }"
    ""
    "})();";

    browser->page()->runJavaScript(thumbDownJs);
});

QObject::connect(trayMenu, &QMenu::aboutToShow, [browser, thumbDownAction]() {
    QString checkJs =
    "(function() {"
    "  function findBtn() {"
    "    const candidates = Array.from(document.querySelectorAll('button, div[role=\"button\"], span[role=\"button\"]'));"
    "    return candidates.find(el => {"
    "      const a = (el.getAttribute('aria-label') || '').toLowerCase();"
    "      const t = (el.innerText || '').toLowerCase();"
    "      const c = (el.className || '').toLowerCase();"
    "      return a.includes('thumb down') || a.includes('dislike') || t.includes('thumb down') || t.includes('dislike') || c.includes('dislike');"
    "    });"
    "  }"
    "  let btn = findBtn();"
    "  return !!(btn && !btn.disabled && btn.getAttribute('aria-disabled') !== 'true');"
    "})();";

    browser->page()->runJavaScript(checkJs, [thumbDownAction](const QVariant &res) {
        thumbDownAction->setVisible(res.toBool());
    });
});

QAction *shuffleAction = trayMenu->addAction("Shuffle");
QObject::connect(shuffleAction, &QAction::triggered, [browser]() {
    QString shuffleJs =
    "(function() {"
    "  const candidates = Array.from(document.querySelectorAll('button, div[role=\"button\"], span[role=\"button\"]'));"
    "  const btn = candidates.find(el => {"
    "    const a = (el.getAttribute('aria-label') || '').toLowerCase();"
    "    const t = (el.innerText || '').toLowerCase();"
    "    return a.includes('shuffle') || t.includes('shuffle');"
    "  });"
    ""
    "  if (btn) {"
    "    btn.dispatchEvent(new MouseEvent('click', { bubbles: true, cancelable: true, view: window }));"
    "    btn.click();"
    "  }"
    "})();";
    browser->page()->runJavaScript(shuffleJs);
});

QObject::connect(trayMenu, &QMenu::aboutToShow, [browser, shuffleAction]() {
    QString checkJs =
        "(function() {"
        "  const btn = document.querySelector('[data-qa=\"tuner_shuffle_button\"]');"
        "  if (!btn || btn.disabled || btn.getAttribute('aria-disabled') === 'true') {"
        "    return null;"
        "  }"
        "  return btn.getAttribute('aria-checked') === 'true';"
        "})();";

    browser->page()->runJavaScript(checkJs, [shuffleAction](const QVariant &res) {
        if (res.isNull()) {
            shuffleAction->setVisible(false);
            return;
        }

        bool enabled = res.toBool();

        shuffleAction->setVisible(true);
        shuffleAction->setText(
            enabled ? "Shuffle: On" : "Shuffle: Off"
        );
    });
});

auto *mpris =
    new MprisPlayer(browser);

Q_UNUSED(mpris);

    window.show();
    return app.exec();
}
#include "main.moc"
EOF

set -e

ICON_URL="https://raw.githubusercontent.com/Logawinner/Lindora/main/Lindora.svg"

TMP_DIR=$(mktemp -d)
SVG_PATH="$TMP_DIR/lindora.svg"

# 1. Download from GitHub
wget -O "$SVG_PATH" "$ICON_URL"

# 2. Install SVG into system icon theme
sudo install -Dm644 "$SVG_PATH" /usr/share/icons/hicolor/scalable/apps/lindora.svg

# 3. Generate PNG fallback
sudo mkdir -p /usr/share/icons/hicolor/128x128/apps

sudo rsvg-convert -w 128 -h 128 \
  /usr/share/icons/hicolor/scalable/apps/lindora.svg \
  -o /usr/share/icons/hicolor/128x128/apps/lindora.png

  cat << 'EOF' > CMakeLists.txt
cmake_minimum_required(VERSION 3.20)
project(lindora-native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets WebEngineWidgets Svg DBus)

add_executable(lindora-native main.cpp)

target_link_libraries(lindora-native PRIVATE
    Qt6::Widgets
    Qt6::WebEngineWidgets
    Qt6::Svg
    Qt6::DBus
)
