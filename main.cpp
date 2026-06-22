#include <QApplication>
#include <QCloseEvent>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDateTime>
#include <QDir>
#include <QIcon>
#include <QMainWindow>
#include <QMenu>
#include <QObject>
#include <QPainter>
#include <QPixmap>
#include <QProcess>
#include <QRegularExpression>
#include <QSocketNotifier>
#include <QSvgRenderer>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QUrl>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineSettings>
#include <QWebEngineView>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>
#include <thread>

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

class MprisAdaptor : public QDBusAbstractAdaptor {
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

class MprisRootAdaptor : public QDBusAbstractAdaptor {
  Q_OBJECT

  Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")

public:
  explicit MprisRootAdaptor(QObject *parent) : QDBusAbstractAdaptor(parent) {}

  Q_PROPERTY(bool CanQuit READ CanQuit)
  Q_PROPERTY(bool CanRaise READ CanRaise)
  Q_PROPERTY(QString Identity READ Identity)

  Q_PROPERTY(bool HasTrackList READ HasTrackList)
  Q_PROPERTY(QString DesktopEntry READ DesktopEntry)

  bool CanQuit() const { return false; }
  bool CanRaise() const { return true; }
  QString Identity() const { return "Lindora"; }

  bool HasTrackList() const { return false; }

  QString DesktopEntry() const { return "Lindora"; }

public slots:
  void Raise() {}
};

class MprisPlayer : public QObject {
  Q_OBJECT

public:
  explicit MprisPlayer(QWebEngineView *view) : m_view(view) {
    new MprisAdaptor(this);
    new MprisRootAdaptor(this);

    QDBusConnection::sessionBus().registerService(
        "org.mpris.MediaPlayer2.pandora");

    QDBusConnection::sessionBus().registerObject(
        "/org/mpris/MediaPlayer2", this, QDBusConnection::ExportAdaptors);

    auto *timer = new QTimer(this);

    connect(timer, &QTimer::timeout, this, &MprisPlayer::updateMetadata);

    timer->start(1000);
  }

  QString title() const { return m_title; }
  QString artist() const { return m_artist; }
  bool playing() const { return m_playing; }
  qlonglong position() const { return m_position; }

  void runJS(const QString &js) { m_view->page()->runJavaScript(js); }

public slots:
  void updateMetadata() {
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

    m_view->page()->runJavaScript(js, [this](const QVariant &v) {
      QVariantMap map = v.toMap();

      QString newTitle = map["title"].toString();

      QString newArtist = map["artist"].toString();

      bool newPlaying = map["playing"].toBool();

      QString elapsed = map["elapsed"].toString();

      QStringList parts = elapsed.split(":");

      qlonglong pos = 0;

      if (parts.size() == 2) {
        pos = (parts[0].toLongLong() * 60 + parts[1].toLongLong()) * 1000000;
      }

      bool changed = newTitle != m_title || newArtist != m_artist ||
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

  void emitPropertiesChanged() {
    QVariantMap changed;

    changed["PlaybackStatus"] = m_playing ? "Playing" : "Paused";

    QVariantMap metadata;

    metadata["xesam:title"] = m_title;

    metadata["xesam:artist"] = QStringList{m_artist};

    changed["Metadata"] = QVariant::fromValue(metadata);

    changed["Position"] = m_position;

    QDBusMessage signal = QDBusMessage::createSignal(
        "/org/mpris/MediaPlayer2", "org.freedesktop.DBus.Properties",
        "PropertiesChanged");

    signal << "org.mpris.MediaPlayer2.Player" << changed << QStringList();

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
    : QDBusAbstractAdaptor(parent), m_player(parent) {}

QString MprisAdaptor::PlaybackStatus() const {
  return m_player->playing() ? "Playing" : "Paused";
}

qlonglong MprisAdaptor::Position() const { return m_player->position(); }

QVariantMap MprisAdaptor::Metadata() const {
  QVariantMap metadata;

  metadata["xesam:title"] = m_player->title();

  metadata["xesam:artist"] = QStringList{m_player->artist()};

  return metadata;
}

bool MprisAdaptor::CanPlay() const { return true; }

bool MprisAdaptor::CanPause() const { return true; }

bool MprisAdaptor::CanGoNext() const { return true; }

bool MprisAdaptor::CanGoPrevious() const { return true; }

bool MprisAdaptor::CanSeek() const { return false; }

bool MprisAdaptor::CanControl() const { return true; }

double MprisAdaptor::Rate() const { return 1.0; }

double MprisAdaptor::MinimumRate() const { return 1.0; }

double MprisAdaptor::MaximumRate() const { return 1.0; }

double MprisAdaptor::Volume() const { return 1.0; }

void MprisAdaptor::Play() {
  m_player->runJS(
      "document.querySelector('[data-qa=\"play_button\"]')?.click();");
}

void MprisAdaptor::Pause() {
  m_player->runJS(
      "document.querySelector('[data-qa=\"pause_button\"]')?.click();");
}

void MprisAdaptor::PlayPause() {
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

void MprisAdaptor::Next() {
  m_player->runJS(
      "document.querySelector('[data-qa=\"skip_button\"]')?.click();");
}

void MprisAdaptor::Previous() {
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
  profile->setPersistentCookiesPolicy(
      QWebEngineProfile::ForcePersistentCookies);

  auto getThemeColors = []() {
    QPalette p = qApp->palette();

    return QMap<QString, QString>{
        {"bg", p.color(QPalette::Window).name()},
        {"panel", p.color(QPalette::Base).name()},
        {"text", p.color(QPalette::WindowText).name()},
        {"accent", p.color(QPalette::Highlight).name()}};
  };

  auto colors = getThemeColors();

  QString bg = colors["bg"];
  QString panel = colors["panel"];
  QString text = colors["text"];
  QString accent = colors["accent"];

  QString customCss =

      /* 1. Universal "Catch-All" */
      QString("*:not(img):not(svg):not(canvas):not(video):"
              "not([class*='art']):not([class*='Art']):"
              "not([class*='thumb']):not([class*='Thumb']):"
              "not(.Image__img) { "
              "background-color: var(--bg-color) !important; "
              "background-image: none !important; "
              "} "

              /* 2. Root variables */
              ":root { "
              "--bg-color: %1; "
              "--panel-purple: %2; "
              "--text-color: %3; "
              "--accent-color: %4; "
              "} ")
          .arg(bg)
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
      + "h1, h2, h3, p, span, div, a{ "
        "color: var(--text-color) !important; "
        "} "

      /* Profile Input Fix */
      + ".UserProfile__input { "
        "    color: var(--text-color) !important; "
        "    -webkit-text-fill-color: var(--text-color) !important; "
        "    caret-color: var(--text-color) !important; "
        "    background-color: var(--panel-purple) !important; "
        "    border-bottom: 1px solid var(--accent-color) !important; "
        "} "

      /* Ensure placeholders in profile are also visible */
      + ".UserProfile__input::placeholder { "
        "    color: var(--text-color) !important; "
        "    opacity: 0.6; "
        "} "

      /* Active Nav Underline Fix */
      + ".NavHorizontal__item__link--active, "
        ".NavHorizontal__item__link:hover { "
        "    border-bottom: 2px solid var(--accent-color) !important; "
        "    color: var(--accent-color) !important; "
        "} "

      /* Also apply to the text inside to match */
      + ".NavHorizontal__item__link--active span, "
        ".NavHorizontal__item__link:hover span { "
        "    color: var(--accent-color) !important; "
        "    -webkit-text-fill-color: var(--accent-color) !important; "
        "} "

      /* 6. Scrollbar */
      + "::-webkit-scrollbar { width: 8px !important; } "
        "::-webkit-scrollbar-thumb { background: var(--accent-color) "
        "!important; } "
        "::-webkit-scrollbar-track { background: var(--bg-color) !important; } "

      /* 7. Pandora SVG background */
      + ".AppBg, .BlurredBackground, .BlurredBackground__svg { "
        "background: var(--bg-color) !important; "
        "} "
        ".BlurredBackground__svg rect { "
        "fill: var(--bg-color) !important; "
        "opacity: 1 !important; "
        "} "
        ".BlurredBackground__svg stop { "
        "stop-color: var(--panel-purple) !important; "
        "} "

      + ".BlurredBackground__svg rect { "
        "fill: var(--bg-color) !important; "
        "opacity: 1 !important; "
        "} "

      + ".BlurredBackground__svg path, "
        ".BlurredBackground__svg polygon, "
        ".BlurredBackground__svg circle { "
        "fill: var(--bg-color) !important; "
        "} "

      + ".BlurredBackground__svg stop { "
        "stop-color: var(--bg-color) !important; "
        "} "

      + ".BlurredBackground, "
        ".BlurredBackground__svg { "
        "background: var(--bg-color) !important; "
        "} "

      + ".EmptySearch__wrapper, "
        ".EmptySearch__content { "
        "background: var(--bg-color) !important; "
        "} "

      + ".BlurredBackground__svg foreignObject { "
        "background: var(--bg-color) !important; "
        "} "

      + ".BlurredBackground__svg { "
        "    background: var(--bg-color) !important; "
        "} "

      /* 8. Search input */
      + ".GlobalSearchInput, .GlobalSearchInput__inner { "
        "background: transparent !important; "
        "} "

      + ".GlobalSearchInput__input { "
        "background-color: var(--panel-purple) !important; "
        "color: var(--text-color) !important; "
        "caret-color: var(--text-color) !important; "
        "-webkit-text-fill-color: var(--text-color) !important; "
        "border: 1px solid var(--accent-color) !important; "
        "box-shadow: none !important; "
        "outline: none !important; "
        "} "

      + ".GlobalSearchInput__input::placeholder { "
        "color: rgba(255,255,255,0.6) !important; "
        "} "

      + ".GlobalSearchInput__iconSearch, "
        ".GlobalSearchInput__clear__icon { "
        "color: var(--text-color) !important; "
        "fill: var(--text-color) !important; "
        "} "

      + ".SearchModalT3, .SearchT3, .EmptySearch, .SearchModalT3--light, "
        ".SearchT3--light { "
        "background-color: var(--bg-color) !important; "
        "} "

      /* 9. Media icons */
      + ".Icon, .TunerControl__Icon, [class*='Icon'] { "
        "color: var(--text-color) !important; "
        "fill: var(--text-color) !important; "
        "stroke: var(--text-color) !important; "
        "} "

      + ".Icon path, .TunerControl__Icon path { "
        "fill: currentColor !important; "
        "stroke: none !important; "
        "} "

      + ".PlayButton, .ThumbUpButton, .ThumbDownButton, "
        ".SkipButton, .ReplayButton, .RepeatButton, .ShuffleButton { "
        "color: var(--text-color) !important; "
        "} "

      + "[aria-checked='true'] .Icon, [aria-checked='true'] svg { "
        "color: var(--text-color) !important; "
        "fill: var(--text-color) !important; "
        "} "

      /* 10. Force full dark-theme text consistency in forms */
      + ".Form, .FormItem, .FormItemGroup { "
        "color: var(--text-color) !important; "
        "} "

      + ".FormItem__title, .FormItem__label, "
        ".Form__radio__text, .Form__radio__text--nonBinary, "
        ".Form__radio__text--secondary { "
        "color: var(--text-color) !important; "
        "} "

      + ".Form__input { "
        "color: var(--text-color) !important; "
        "-webkit-text-fill-color: var(--text-color) !important; "
        "} "

      + ".Form__input::placeholder { "
        "color: rgba(255,255,255,0.45) !important; "
        "} "

      + ".AuthLayout__linkContainer, "
        ".AuthLayout__linkContainer span { "
        "color: var(--text-color) !important; "
        "} "

      + ".AuthLayout__link { "
        "color: var(--accent-color) !important; "
        "} "

      /* 11. Custom Radio Button - Active State Fix */
      + ".RadioButton__input { "
        "    appearance: none !important; "
        "    position: relative !important; "
        "    width: 18px !important; "
        "    height: 18px !important; "
        "    border: 2px solid var(--text-color) !important; "
        "    border-radius: 50% !important; "
        "    background-color: transparent !important; "
        "    cursor: pointer !important; "
        "} "

      + ".RadioButton__input:checked { "
        "    border-color: var(--accent-color) !important; "
        "} "

      /* The inner dot that appears when checked */
      + ".RadioButton__input::after { "
        "    content: '' !important; "
        "    position: absolute !important; "
        "    top: 50% !important; "
        "    left: 50% !important; "
        "    transform: translate(-50%, -50%) !important; "
        "    width: 10px !important; "
        "    height: 10px !important; "
        "    border-radius: 50% !important; "
        "    background-color: transparent !important; "
        "    transition: background-color 0.2s ease !important; "
        "} "

      + ".RadioButton__input:checked::after { "
        "    background-color: var(--accent-color) !important; "
        "} "

      /* 12. Unified override */
      + "*:not(.GlobalSearchInput__input) { "
        "background-color: var(--bg-color) !important; "
        "background-image: none !important; "
        "}"

      /* 13. Layouts */
      /* Cleaner Subscription Page - Main Containers Only */

      /* 1. Remove borders from internal items */
      + ".Subscription *, .InfoBlock * { "
        "    border: none !important; "
        "    box-shadow: none !important; "
        "} "

      /* 2. Only border the major 'Card' sections */
      + ".Subscription{ "
        "    border: 1px solid var(--accent-color) !important; "
        "    border-radius: 12px !important; "
        "    padding: 20px !important; "
        "    background-color: var(--panel-purple) !important; "
        "    margin-bottom: 24px !important; "
        "} "

      /* Account Page - Clean Card Layout */

      /* 1. Main Account Container Card */
      + ".Account, .AccountInfo--notEditable { "
        "    border: 1px solid var(--accent-color) !important; "
        "    border-radius: 12px !important; "
        "    padding: 24px !important; "
        "    background-color: var(--panel-purple) !important; "
        "    margin: 20px 0 !important; "
        "} "

      /* 2. Style the individual Info Rows */
      + ".AccountInfo__row { "
        "    border-bottom: 1px solid rgba(255, 255, 255, 0.1) !important; "
        "    padding: 12px 0 !important; "
        "    display: flex !important; "
        "    justify-content: space-between !important; "
        "    align-items: center !important; "
        "} "

      /* 3. Ensure the labels (Email, Password, etc.) stand out */
      + ".AccountInfo__sub-header { "
        "    color: var(--accent-color) !important; "
        "    font-weight: bold !important; "
        "    text-transform: uppercase !important; "
        "    font-size: 0.8rem !important; "
        "    letter-spacing: 1px !important; "
        "} "

      /* Remove border from the last row */
      + ".AccountInfo__row:last-child { "
        "    border-bottom: none !important; "
        "} "

      /* 13. UNIVERSAL PSEUDO-ELEMENT FIX (The 'White' Fix) */
      + "*::before, *::after { "
        "    background-color: var(--bg-color) !important; "
        "    background-image: none !important; "
        "    color: var(--text-color) !important; "
        "} "

      /* 13b. DROPDOWN EXCEPTION - Transparent so menu text is not hidden */
      + ".region-dropdown *::before, .region-dropdown *::after, "
        ".region-overlay *::before, .region-overlay *::after, "
        ".Dropdown *::before, .Dropdown *::after { "
        "    background-color: transparent !important; "
        "} "

      /* 13c. ALBUM ART EXCEPTION - Keep pseudo-elements transparent for
         images/hovers */
      + "[class*='art']::before, [class*='art']::after, "
        "[class*='Art']::before, [class*='Art']::after, "
        "[class*='thumb']::before, [class*='thumb']::after, "
        "[class*='Thumb']::before, [class*='Thumb']::after, "
        ".Image__img::before, .Image__img::after { "
        "    background-color: transparent !important; "
        "    background-image: none !important; "
        "} ";

  QString jsCode =
      QString(
          "(function() {"
          "  var style = document.getElementById('lindora-theme');"

          "  if (!style) {"
          "    style = document.createElement('style');"
          "    style.id = 'lindora-theme';"
          "    style.type = 'text/css';"
          "    document.head.appendChild(style);"
          "  }"

          "  style.textContent = `%1`;"

          "  if (!window.lindoraThemeObserver) {"
          "    window.lindoraThemeObserver = new MutationObserver(function() {"
          "      if (!document.head.contains(style)) {"
          "        document.head.appendChild(style);"
          "      }"
          "    });"

          "    window.lindoraThemeObserver.observe("
          "      document.documentElement,"
          "      { childList: true, subtree: true }"
          "    );"
          "  }"

          "})();")
          .arg(customCss);

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

  page->setBackgroundColor(QColor(bg));

  // Load the official site directly
  browser->setUrl(QUrl("https://www.pandora.com"));

  QTimer *themeWatcher = new QTimer(&window);

  QObject::connect(themeWatcher, &QTimer::timeout, [&]() {
    QPalette p = qApp->palette();

    QString bg = p.color(QPalette::Window).name();
    QString panel = p.color(QPalette::Base).name();
    QString text = p.color(QPalette::WindowText).name();
    QString accent = p.color(QPalette::Highlight).name();

    QWebEngineSettings *s = page->settings();
    s->setAttribute(QWebEngineSettings::ShowScrollBars, true);

    QString updateJs = QString(R"(
(function() {
    document.documentElement.style.setProperty('--bg-color', '%1');
    document.documentElement.style.setProperty('--panel-purple', '%2');
    document.documentElement.style.setProperty('--text-color', '%3');
    document.documentElement.style.setProperty('--accent-color', '%4');
})();
)")
                           .arg(bg)
                           .arg(panel)
                           .arg(text)
                           .arg(accent);

    browser->page()->runJavaScript(updateJs);
  });

  themeWatcher->start(1000);

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
  QObject::connect(quitAction, &QAction::triggered, [&app]() { app.quit(); });

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
        "    document.querySelector('[data-qa=\"mini_player\"] "
        "[data-qa*=\"artist\"]') || "
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
        "  var remaining = "
        "document.querySelector('[data-qa=\"remaining_time\"]');"

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
        "      document.querySelector('[data-qa=\"t3_skip_forward_button\"]') "
        "||"
        "      document.querySelector('[data-qa=\"skip_button\"]') ||"
        "      document.querySelector('.Tuner__Control__SkipForward__Button') "
        "||"
        "      document.querySelector('.Tuner__Control__Skip__Button') ||"
        "      document.querySelector('[aria-label=\"Skip forwards\"]') ||"
        "      document.querySelector('[aria-label=\"skip forwards\"]');"
        ""
        "  if (!btn) {"
        "      const candidates = Array.from("
        "          document.querySelectorAll("
        "              "
        "'button,[role=\"button\"],div[role=\"button\"],span[role=\"button\"]'"
        "          )"
        "      );"
        ""
        "      btn = candidates.find(el => {"
        "          const aria = (el.getAttribute('aria-label') || "
        "'').toLowerCase();"
        "          const dataqa = (el.getAttribute('data-qa') || "
        "'').toLowerCase();"
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
        "  if (btn.getAttribute('aria-disabled') === 'true') return "
        "'ARIA_DISABLED';"
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
        "    const candidates = Array.from(document.querySelectorAll('button, "
        "div[role=\"button\"], span[role=\"button\"]'));"
        "    return candidates.find(el => {"
        "      const a = (el.getAttribute('aria-label') || '').toLowerCase();"
        "      const keywords = ['next', 'skip', 'forward', 'skip forward'];"
        "      return keywords.some(k => a.includes(k));"
        "    });"
        "  }"
        ""
        "  let btn = findBtn();"
        "  return !!(btn && !btn.disabled && btn.getAttribute('aria-disabled') "
        "!== 'true');"
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
        "    return style && style.display !== 'none' && style.visibility !== "
        "'hidden' && el.offsetParent !== null;"
        "  }"
        ""
        "  function findBtn() {"
        "    const candidates = Array.from(document.querySelectorAll('button, "
        "div[role=\"button\"], span[role=\"button\"]'));"
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
        "    const all = document.querySelectorAll('button, "
        "div[role=\"button\"]');"
        "    for (let el of all) {"
        "      const a = (el.getAttribute && el.getAttribute('aria-label') || "
        "'').toLowerCase();"
        "      if ((a.includes('previous') || a.includes('back')) && "
        "el.offsetParent !== null) {"
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
        "          "
        "document.querySelectorAll('button,[role=\"button\"],div[role="
        "\"button\"],span[role=\"button\"]')"
        "      );"
        ""
        "      btn = candidates.find(el => {"
        "          const aria = (el.getAttribute('aria-label') || "
        "'').toLowerCase();"
        "          const dataqa = (el.getAttribute('data-qa') || "
        "'').toLowerCase();"
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

  QObject::connect(trayMenu, &QMenu::aboutToShow, [browser, replayAction]() {
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

    browser->page()->runJavaScript(replayCheckJs,
                                   [replayAction](const QVariant &res) {
                                     if (replayAction)
                                       replayAction->setVisible(res.toBool());
                                   });
  });

  QAction *repeatAction = trayMenu->addAction("Repeat");

  QObject::connect(repeatAction, &QAction::triggered, [browser]() {
    QString repeatJs =
        "(function() {"
        "  const btn = "
        "document.querySelector('[data-qa=\"tuner_repeat_button\"]');"
        "  if (!btn) return;"
        "  if (btn.disabled) return;"
        "  if (btn.getAttribute('aria-disabled') === 'true') return;"
        "  btn.click();"
        "})();";

    browser->page()->runJavaScript(repeatJs);
  });

  QObject::connect(trayMenu, &QMenu::aboutToShow, [browser, repeatAction]() {
    QString repeatStateJs =
        "(function() {"
        "  const btn = "
        "document.querySelector('[data-qa=\"tuner_repeat_button\"]');"
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
        repeatStateJs, [repeatAction](const QVariant &res) {
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
        });
  });

  // Add a Pause/Play option to the right-click menu
  QAction *pauseAction = trayMenu->addAction("Pause/Play");
  QObject::connect(pauseAction, &QAction::triggered, [browser]() {
    QString pauseJs =
        "(function() {"
        "  var btn = document.querySelector('[data-qa=\"play_pause_button\"]') "
        "|| "
        "            document.querySelector('[data-qa=\"play_button\"]') || "
        "            document.querySelector('[data-qa=\"pause_button\"]');"

        "  if (btn) {"
        "    const rect = btn.getBoundingClientRect();"
        "    const x = rect.left + rect.width / 2;"
        "    const y = rect.top + rect.height / 2;"
        "    const opts = { bubbles: true, cancelable: true, view: window, "
        "clientX: x, clientY: y };"
        "    btn.dispatchEvent(new PointerEvent('pointerdown', { ...opts, "
        "pointerId: 1, isPrimary: true }));"
        "    btn.dispatchEvent(new MouseEvent('mousedown', opts));"
        "    btn.dispatchEvent(new PointerEvent('pointerup', { ...opts, "
        "pointerId: 1, isPrimary: true }));"
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
  QObject::connect(trayIcon, &QSystemTrayIcon::activated,
                   [&window](QSystemTrayIcon::ActivationReason reason) {
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
        "    const candidates = Array.from(document.querySelectorAll('button, "
        "div[role=\"button\"], span[role=\"button\"]'));"
        ""
        "    return candidates.find(el => {"
        "      const a = (el.getAttribute('aria-label') || '').toLowerCase();"
        "      const t = (el.innerText || '').toLowerCase();"
        "      const c = (el.className || '').toLowerCase();"
        ""
        // Added explicit check: skip if it looks like a dislike button
        "      const isDislike = a.includes('dislike') || a.includes('thumb "
        "down') || t.includes('dislike');"
        ""
        "      return ("
        "        (a.includes('like') || a.includes('thumb up') || "
        "t.includes('like')) && !isDislike"
        "      );"
        "    });"
        "  }"
        ""
        "  let btn = findBtn();"
        ""
        "  if (!btn) {"
        "    const all = document.querySelectorAll('*');"
        "    for (let el of all) {"
        "      const a = (el.getAttribute && el.getAttribute('aria-label') || "
        "'').toLowerCase();"
        "      if ((a.includes('like') || a.includes('thumb up')) && "
        "!a.includes('dislike')) {"
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

  QObject::connect(trayMenu, &QMenu::aboutToShow, [browser, thumbUpAction]() {
    QString checkJs =
        "(function() {"
        "  const btn = "
        "document.querySelector('[data-qa=\"thumbs_up_button\"]');"
        "  if (!btn) return 'MISSING';"
        ""
        "  return btn.getAttribute('aria-checked') === 'true'"
        "      ? 'ON'"
        "      : 'OFF';"
        "})();";

    browser->page()->runJavaScript(checkJs,
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
        "    const candidates = Array.from(document.querySelectorAll('button, "
        "div[role=\"button\"], span[role=\"button\"]'));"
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
        "      const a = (el.getAttribute && el.getAttribute('aria-label') || "
        "'').toLowerCase();"
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
        "    const candidates = Array.from(document.querySelectorAll('button, "
        "div[role=\"button\"], span[role=\"button\"]'));"
        "    return candidates.find(el => {"
        "      const a = (el.getAttribute('aria-label') || '').toLowerCase();"
        "      const t = (el.innerText || '').toLowerCase();"
        "      const c = (el.className || '').toLowerCase();"
        "      return a.includes('thumb down') || a.includes('dislike') || "
        "t.includes('thumb down') || t.includes('dislike') || "
        "c.includes('dislike');"
        "    });"
        "  }"
        "  let btn = findBtn();"
        "  return !!(btn && !btn.disabled && btn.getAttribute('aria-disabled') "
        "!== 'true');"
        "})();";

    browser->page()->runJavaScript(checkJs,
                                   [thumbDownAction](const QVariant &res) {
                                     thumbDownAction->setVisible(res.toBool());
                                   });
  });

  QAction *shuffleAction = trayMenu->addAction("Shuffle");
  QObject::connect(shuffleAction, &QAction::triggered, [browser]() {
    QString shuffleJs =
        "(function() {"
        "  const candidates = Array.from(document.querySelectorAll('button, "
        "div[role=\"button\"], span[role=\"button\"]'));"
        "  const btn = candidates.find(el => {"
        "    const a = (el.getAttribute('aria-label') || '').toLowerCase();"
        "    const t = (el.innerText || '').toLowerCase();"
        "    return a.includes('shuffle') || t.includes('shuffle');"
        "  });"
        ""
        "  if (btn) {"
        "    btn.dispatchEvent(new MouseEvent('click', { bubbles: true, "
        "cancelable: true, view: window }));"
        "    btn.click();"
        "  }"
        "})();";
    browser->page()->runJavaScript(shuffleJs);
  });

  QObject::connect(trayMenu, &QMenu::aboutToShow, [browser, shuffleAction]() {
    QString checkJs =
        "(function() {"
        "  const btn = "
        "document.querySelector('[data-qa=\"tuner_shuffle_button\"]');"
        "  if (!btn || btn.disabled || btn.getAttribute('aria-disabled') === "
        "'true') {"
        "    return null;"
        "  }"
        "  return btn.getAttribute('aria-checked') === 'true';"
        "})();";

    browser->page()->runJavaScript(
        checkJs, [shuffleAction](const QVariant &res) {
          if (res.isNull()) {
            shuffleAction->setVisible(false);
            return;
          }

          bool enabled = res.toBool();

          shuffleAction->setVisible(true);
          shuffleAction->setText(enabled ? "Shuffle: On" : "Shuffle: Off");
        });
  });

  auto *mpris = new MprisPlayer(browser);

  Q_UNUSED(mpris);

  window.show();
  return app.exec();
}
#include "main.moc"
